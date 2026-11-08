# Blink Phase 0 架构（已由运行探针验证）

## 边界

Blink.exe 只负责 WinUI 3、用户操作和任务调度。磁盘枚举、索引、属性读取和 CopyFile2 都在后台任务执行；这些模块通过静态库边界保持可测试。网络/第三方 Provider 不加载进 Blink.exe，而由 Blink.ProviderHost.exe 承载。

## Phase 0.5 boundary

`Blink.IndexService` owns the privileged volume handle and runs MFT enumeration plus USN catch-up. `Blink.exe` is still a normal-user process and talks to the service through `Blink.Index.Contracts`. The service protocol exposes status, indexing, progress, cancellation, and read-only search only; it is not a general file-operation backdoor.

The fast store keeps Volume ID + File ID + Parent ID/name links and resolves paths lazily. A directory rename therefore changes resolved paths through the parent tree instead of rewriting every descendant. Shell properties remain lazy metadata enrichment and are outside the privileged service.

~~~mermaid
flowchart LR
    UI["Blink.exe\nWinUI 3 + C++/WinRT"] --> Core["Blink.Core\n路径、句柄、元数据、身份"]
    UI --> Index["Blink.Index\n内存索引 + USN + 目录观察"]
    UI --> Props["Blink.Properties\nProperty System + ACL + streams"]
    UI --> Transfer["Blink.Transfer\nCopyFile2"]
    UI --> Contracts["Provider.Contracts\nnamed-pipe client"]
    Contracts <-->|"message-mode pipe"| Host["Blink.ProviderHost.exe"]
    Host --> Fake["Provider.Fake\nnormal/delay/hang/error/disconnect/crash"]
~~~

## 核心数据路径

1. NTFS Fast Path 使用 `FSCTL_ENUM_USN_DATA` 完成全卷 MFT/USN enumeration，记录 FileEntry、Volume ID 和 128-bit File ID；历史 Phase 0 prototype 在不支持 Fast Index 或 IndexService 不可用时可使用目录 DFS fallback，但该 fallback 仅限 prototype/local-directory browsing 语义，不构成 Phase 1 product full-volume Search fallback。Phase 1 中 IndexService unavailable 时 local folder browsing 仍可用，product full-volume indexed Search 必须显示 `Unavailable`/degraded，禁止静默使用全卷 DFS Search fallback。Search 只在内存索引中执行，不重新枚举磁盘。
2. 全卷增量路线使用 USN Journal；目录级实时观察使用 ReadDirectoryChangesExW。USN 不可用时必须显式报告并按策略重新扫描。
3. 文件属性读取通过 IPropertyStore、GetFileInformationByHandleEx、GetNamedSecurityInfoW 和流/压缩信息 API 组合完成；UI 只接收已完成的后台结果。
4. CopyFile2 在后台线程运行，进度回调更新状态，CopyControl 通过 CopyFile2 的取消指针发出取消。
5. ProviderClient 启动独立 ProviderHost，使用消息模式命名管道传输 LIST 请求；请求线程可通过 CancelSynchronousIo 取消阻塞读取，主机可被终止。

## 身份模型

FileIdentity = VolumeSerialNumber + 16-byte FileId。重命名探针验证了同一对象的身份保持不变；路径只作为当前访问定位，不作为唯一身份。

## Phase 0 已知边界

- 当前本机非管理员查询全卷 USN 返回 ERROR_ACCESS_DENIED；代码和报告保留该失败，不使用旧 API 静默替代。
- IPropertyStore 仍可能调用第三方 Handler；正式产品需要独立 MetadataHost。
- Provider IPC 已验证隔离和故障响应，生产协议仍需更强的身份校验、版本协商和资源配额。
- 索引持久化是原型二进制格式，尚未承诺生产数据库兼容性。

## Data Storage & Per-Volume Index Persistence Architecture (正式存储与恢复架构)

1. **安装卷跟随策略 (`Install-Volume Following DataRoot`)**:
   - 定义：`InstallVolume = VolumeOf(InstallRoot)`。Blink 大容量数据（Index/Cache/Logs/Dumps/Temp）默认跟随安装卷。当安装在非系统卷时，禁止因为 Windows 默认目录惯例而静默写入系统卷；安装在系统卷时按同卷原则正常放置。
   - 绝不静默 Fallback：空间不足时提示用户（清理缓存/迁移位置），严禁静默迁移数据往系统盘。
2. **按卷隔离持久化与恢复 (`Per-Volume Persistent Index`)**:
   - 物理存储按卷独立隔离：`DataRoot\Index\Volumes\Volume-{GUID-or-stable-id}\`。
   - 盘符解耦：Drive Letter 仅为当前 Mount Point，不是持久身份。Volume GUID / Volume Serial / Root Identity 用于持久 Volume Identity matching，并在每次恢复时重新验证。
   - Windows 重装故障恢复：重装系统盘后，未格式化的数据卷只要卷身份匹配且 USN Journal 连续，恢复后直接载入 Checkpoint 并 CatchUp 恢复为 `Healthy`；仅重建格式化/损坏的卷。
3. **自索引环路隔离 (`InternalExcludedRoots`)**:
   - 当 DataRoot 与被索引卷同卷时，Blink 自身文件产生的 USN Record 仍必须被读取、消费并正确推进 journal checkpoint；Internal Exclusion 只发生在 index mutation / user-visible indexing / persistent dirty propagation 层，禁止通过直接跳过这些 USN Record 来实现排除。
4. **一致性校验与防幽灵条目 (`Anti-Stale Index & Scrubbing`)**:
   - USN 增量实时消费 `Delete` 记录。在 Volume Index 状态为 Healthy / 已完成 CatchUp 时，不允许已知已删除对象继续作为有效搜索结果存在（CatchingUp 允许极短最终一致性窗口）。
   - 低频后台 Consistency Scrub 机制对比 Persistent Index 与 MFT 真实身份集，修复少量微小差异；超越安全阈值时触发单卷重建 (`RebuildRequired`)。

## Future Extension Boundary（D-0025）

Blink 在架构上允许未来扩展，但 Phase 1 不实现公开 Plugin SDK、ExtensionHost、插件加载、capability 权限系统或插件分发。第三方扩展默认应通过版本化、capability-based contract 在普通用户权限的进程外 Host 中运行：

```text
Blink Application Model
        │ versioned contract
        ├── Default WinUI frontend
        ├── AgentBridge（future, separate boundary）
        └── ExtensionHost（future）
                └── third-party extensions
```

第三方 DLL 不得作为公开机制直接注入 `Blink.exe`。插件与 Custom Frontend System、AgentBridge 分离；未来 frontend 也应通过独立进程和独立 contract 工作。`ProviderHost.exe` 已有的进程隔离方向继续保留，但本阶段不统一重构各类 Host。

未来扩展只能访问明确暴露的稳定 contract，不依赖 WinUI control、`WindowState`/`TabState`、内部 C++ 地址、raw pointer、内部 STL ABI 或 path 作为永久 identity。Provider 扩展继续复用 Provider abstraction；Search/Index Enricher 不得修改 IndexService 的 authoritative filesystem facts、USN、delete 或 checkpoint correctness。IndexService 不因插件需求增加任意特权文件操作。

本阶段只记录扩展点，不预建空 registry；只有已有多个同类实现需要统一注册时才引入 registry。Capability、protocol version negotiation、稳定 `FileRef` ABI、SDK、安装/签名/市场/更新和 sandbox 均为后续工作。详见 [`DECISIONS.md`](DECISIONS.md) 的 D-0025。

## Localization boundary（Phase 1A）

Blink 的用户可见固定文案由 MRT Core `.resw` 资源提供，当前资源集为
`src/Blink/Strings/en-US/Resources.resw` 和
`src/Blink/Strings/zh-CN/Resources.resw`。`blink::app::Localization` 是
`src/Blink` 内部的薄应用边界，负责 `ResourceLoader` 查找、少量占位符格式化和
资源缺失时的安全 fallback；业务状态、路径、FileIdentity、Win32 错误码和 IPC
协议字段仍由原有模型保存，不依赖资源文本作为事实来源。

应用使用系统当前语言及 MRT fallback 规则；本阶段没有运行时语言切换、翻译管理
工具或面向插件的本地化 ABI。UI 仍可替换，资源键不把具体 WinUI control、列表
布局或前端协议固定下来。

## Phase 1 Product Surface Boundary（D-0027）

Phase 1 的产品目标是普通用户可日常使用的本地 Windows 文件管理器。操作语义尽量
符合 Explorer 用户预期，但 Blink 保持自己的 `Location`、文件系统事实来源和
Application Model；Shell Namespace 不是主导航模型，第三方 Shell Extension 和
context-menu handler 不加载进 Blink.exe。

- 默认 Location 是 Blink-native `ThisPC`，不是 Current Working Directory、exe 目录或
  Desktop。`ThisPC` 不是 filesystem path；Folder、ThisPC、Search result 和 unavailable
  volume 必须由状态模型区分。
- Phase 1 的交互边界包含 This PC、按需 navigation tree、多标签、Details/List/Grid、
  标准选择/键盘、文件关联打开、Blink-owned context menu、system clipboard、Drag & Drop、
  当前目录外部变化收敛和可移动卷生命周期；具体阶段及 Deferred 清单见 `PHASE1_PLAN.md`
  与 D-0027。
- 所有 Blink-owned File/Directory mutation 都在 Blink.exe 普通用户进程中，复用同一
  `FileOperationCoordinator`。External→Blink 与 Blink→Blink 的 Clipboard/Drag & Drop
  operation 由该 coordinator 拥有；Blink→External 时 Blink 只发布标准 Windows filesystem
  transfer data，由 external target 拥有实际 operation。Cut 在 external target 明确执行前，
  Blink 不得自行删除 source。IndexService 不增加 Copy/Delete/Rename/Open 等特权命令。
- 普通 Delete 使用 Windows 系统 Recycle Bin；Blink 不拥有自定义删除存储。跨卷 Directory
  Move 只有在递归复制完整成功后才允许删除源树；junction、symlink 和其它 reparse point
  默认绝不 follow，不能可靠复制 reparse object 本身时必须明确 Unsupported。
- Conflict policy 按 object/operation type 区分：File Copy/Move/Paste 是 Replace/Skip/
  Keep Both/Cancel；Directory-to-existing-directory 是 Merge/Skip/Keep Both/Cancel，Merge
  递归处理子项且不得先删除目标目录；Rename target-exists 至少是 Rename Again/Cancel。
- Search 至少区分 All indexed local volumes 与 Current Folder；IndexService unavailable
  时 local folder browsing 仍可用，但 product full-volume indexed Search 必须为
  `Unavailable`/degraded，不能静默 DFS fallback。Search rows 同样使用 app-private
  row/link identity 区分不同 Parent+Name links。
- `FileIdentity` 保持 Volume identity + File ID；同一 File ID 的多个 hard-link 名称使用
  app-private row/link identity 区分 UI 行和 Search result/action target，不降级 authoritative identity。
- Show hidden items 只定义 hidden-items policy；protected operating-system items 默认继续
   隐藏，完整 protected-system-items policy 保持 Deferred。

## Phase 1B Slice 1 implementation boundary

**Status: IMPLEMENTED + ordinary validation PASS + manual UI PASS**。本 Slice 是 D-0027 已冻结 Location/This PC 语义的最小实现，不扩大 Product Surface；最终用户手工验收已完成。

- `blink::app::Location` 是 app-private 的值模型，只区分 Blink-native `ThisPC` 与规范化
  `Folder(path)`；`ThisPC` 不映射为伪造 filesystem path。
- `NavigationModel` 是唯一的导航/history 真相源，`TabState` 保存 committed Location、
  committed folder/This PC content、selection 和 presentation state；Back/Forward/Up/Refresh
  不建立第二条导航路径。
- This PC content 只包含 app-private Places 与当前可访问的固定/可移动本地卷。其异步构建
  通过受管理的 task、generation 和 lifetime gate 发布；不能因 This PC 加载失败而破坏已有
  committed folder page。
- Address Bar、sidebar、This PC item activation、目录激活和现有 Folder browse/render pipeline
  都汇入同一 NavigationModel/BrowseController 边界。WinUI controls 只是 projection，不是
  Location 或业务事实来源。
- navigation tree、views、file activation、watcher、Search coordinator、
  FileOperationCoordinator、clipboard/drag-drop 和 mutation 不属于本 Slice。真实多标签
  ownership 在下一个 Slice 单独实现。

## Phase 1B Slice 2 implementation boundary

**Status: IMPLEMENTED + ordinary validation PASS + manual UI PASS**。本 Slice 在已完成的 Location/This PC
边界上加入真实多标签和 per-tab state isolation，不改变 D-0027 Product Surface。

- `TabWindowState` 是窗口级 app-private authoritative owner，持有有序 `TabRecord` 集合和
  稳定 `TabId`。TabId 独立于 WinUI `TabView` 的 selected index；`TabView` 只是 projection
  和用户输入事件来源。
- 每个 `TabRecord` 独立拥有 `TabState`、`BrowseState` 和 `BrowseController`；每个标签的
  Location/history、committed listing/This PC、selection、SortDescriptor 和请求生命周期不
  共享。窗口级 UI 只维护 per-tab projection cache，不把 WinUI control 作为业务事实来源。
- 新标签从 This PC 开始并复用既有异步导航入口。关闭活动标签选择右邻居，否则左邻居；
  最后一个标签请求关闭窗口。关闭标签先取消其 UI presentation/This PC work，再关闭其
  controller/state 生命周期。
- 异步 Folder/This PC 结果必须同时通过 TabId、标签本地 presentation generation、
  TabState/BrowseState request gate 和 window lifetime gate；非活动标签的准备结果可以保留，
  已关闭标签的回调不得重新进入 UI 或提交状态。
- 标签切换只执行 active presentation sync，恢复该标签已提交的 path/sidebar/content/status/
  button/selection/sort；切换不触发全盘或目录重新枚举，也不建立第二套 navigation pipeline。

本 Slice 不实现 Search UI、FileOperationCoordinator、mutation、clipboard/drag-drop、
navigation tree、watcher、Provider 或 Shell Namespace；Slice 2 的 manual acceptance 已完成。

## Phase 1E Code Hygiene Closure

Phase 1E 是 review/audit slice，不是功能重写：产品路径必须保持唯一且可追踪，后台 worker/task 必须有明确 ownership 和关闭/取消语义，WinUI controls 不得成为 authoritative business state；被替代的 Phase 0 prototype 必须删除或隔离为 diagnostic/non-product path。不得保留重复 Navigation/Search/Copy 路径、silent fallback 或被吞掉的 correctness/security error；TODO/FIXME 只能归类为 Phase 1 blocker 或 explicit deferred debt。该 closure 不扩大 D-0027 已冻结的 Product Surface。

## Speculative Folder Prefetch — Future Performance Direction

> **Status: NOT IMPLEMENTED; Future / Phase 2 performance optimization**

预测式目录预加载只记录为未来的可选性能方向：目标是降低用户感知的目录进入延迟，不是修复当前正确性问题。当前 Phase 1A 的大目录浏览已经具备可接受的普通本地目录表现（约 10k：首行约 91 ms、完整呈现约 168 ms；约 50k：首行约 156 ms、完整呈现约 493 ms；最长观测 UI block 约 5.6/21.2 ms），因此本条不重新打开 Phase 1A 性能优化。

未来实现必须复用同一条 authoritative folder-preparation pipeline：

```text
normal navigation ───────┐
                         v
                 Folder Preparation Pipeline
                         ^
speculative trigger ─────┘
```

当前 readiness audit：

- `BrowseController` 已将 `EnumerateDirectory` 与 WinUI controls 分离：枚举在受管理的 ThreadPool work item 中执行，通过 `BrowseResult` 发布普通数据；`TabState`/`NavigationModel` 仍负责 committed page 和 navigation truth。
- `TabState::SortEntries(std::vector<FileEntry>, SortDescriptor)` 是不依赖 WinUI 的纯排序入口；大目录当前已在既有受管理 presentation worker 中排序一次。
- `FolderRowViewData`、`FolderRowProjectionBuilder` 和 projection 函数是 app-private、轻量数据投影，不包含 WinUI control；当前 progressive batches 也已复用同一个 projection/localization context。
- `presentation_generation_`、presentation cancellation 和 stale-result gate 与 `TabState`/`NavigationModel` 的 committed truth 分开；因此准备好的数据理论上可以在 NavigationModel commit 之前存在，但不能因此直接成为 committed page。
- 未来若证据证明值得实现，预期主要增加“触发器 + 有界、短生命周期 cache + 校验/复用层”，不应替换 Phase 1A 的导航、提交或呈现架构，也不得建立第二套枚举/排序/projection 实现。

当前需要保留的边界和唯一架构阻碍：`BrowseController::Browse()` 目前同时推进当前 browse generation、取消旧 work，并通过其 callback 进入真实导航提交；hover/selection 预取不能直接调用它，否则可能取消或污染真实导航。未来应在同一准备逻辑上提供不提交、不改变当前 browse generation 的受管理 preparation operation，再由正常导航和 speculative trigger 共同消费；这不是本阶段授权的抽象或重构。

未来策略仅作设计指导，不冻结实现细节：

- SSD/NVMe 上主要成本是 I/O 带宽、CPU、RAM、功耗和 cache pollution，metadata/row preparation 可能有价值；HDD 上错误预测可能增加 seek，应默认更保守，甚至关闭 hover 预取。当前不检测存储介质。
- 网络/远程 Provider 的延迟、带宽、认证和服务端成本不同，不能自动套用本地磁盘策略；远程 speculative prefetch 默认关闭，直到 Provider 语义另行设计。当前不增加 Provider capability/negotiation。
- 纯 hover 不代表意图；短暂 dwell、selection/focus、双击序列的第一次点击可以是不同置信度的信号。当前不冻结阈值或 magic number。
- speculative work 必须有界、低优先级、可取消；真实导航立即优先；cache 必须短生命周期、有限内存并使用 LRU 或等价淘汰。只准备目录 metadata/row data，不预读大文件内容，不递归预加载整棵树。
- 预取结果不能直接视为 authoritative truth。未来必须结合显式 validation、expiry 或 filesystem/index evidence；Phase 1B 的 `ReadDirectoryChangesExW` watcher、debounce/reconcile 和 stale-callback protection 建立后，才评估其作为 invalidation 输入。本节不实现 TTL、cache validation 或预取与 watcher 的联动。

重新评估该方向的 gate 是：正常 tabs/navigation ownership 稳定、folder watcher/change invalidation 存在、多视图呈现行为明确，并且有真实用户延迟数据证明收益值得复杂度。未来 telemetry 至少应能区分 speculative request、hit/miss、cancel、expiry、memory cost 及实际 perceived-latency saving；若命中率或收益不足，设计应可移除。

本节明确不属于 Phase 1A blocker、Phase 1B/1C/1D required feature 或 D-0027 Product Surface expansion；Phase 1 Product Surface 保持 Frozen。本节不添加 hover handler、预取 timer、cache class、prefetch manager、scheduler、storage detection、Provider API 或任何预建空抽象。本地目录 watcher 在独立的 Phase 1B Slice 8 中实现。
## Phase 1B Slice 3: Navigation tree ownership

The navigation tree is a Blink-native presentation of an app-private
`NavigationTreeModel`. It owns node IDs, `Location` targets, node kind,
direct-child state, load state, errors, and reparse-point policy. WinUI
`TreeViewNode` instances are disposable presentation objects and are never
the source of navigation truth.

The initial model is cheap: `ThisPC` plus Places and currently accessible
fixed/removable volume roots. Folder children are enumerated only for an
explicit expansion or a bounded ancestor reveal needed to represent the
active tab's committed location. Enumeration runs through the existing
background directory-browse mechanism; UI publication is generation-checked.
The window owns expansion/reveal lifetime, so closing the window cancels or
drops work. A single shared tree keeps expansion state across tabs, while
the active tab controls selection and reveal. A newer navigation invalidates
only stale reveal work; it does not discard valid shared expansion state.

The tree filters hidden/system entries by the existing default policy and
does not expand reparse points. A reparse-point directory may be shown as a
non-expandable node, but Blink never follows its target to populate the tree.
Volume facts are refreshed through `EnumerateThisPcContent()` and its existing
logical-drive source; no drive letter is hard-coded.

All tree navigation actions route through the same `Location` /
`NavigationModel` / `BrowseController` path as the address bar, main-list
directory activation, Back, Forward, Up, Refresh, and This PC. Tree expansion
is not navigation, and selection is not inferred from a display label.

The navigation pane has two distinct UI semantics over the same This PC content:
the upper quick/common-location presentation is a non-hierarchical projection of
`ThisPcItemKind::Place`, while the lower `NavigationTreeModel` contains ThisPC,
volume, and folder nodes only. Quick items do not enumerate or reveal children.
An exact quick Location selects the quick projection; a descendant or ordinary
filesystem Location uses the bounded filesystem-tree reveal. Both activations still
enter the single `NavigateToLocation()`/`NavigationModel` route, and active-tab
committed state synchronizes both projections.

Deferred: filesystem watchers, recursive prefetch, removable-volume change
notifications, Shell Namespace integration, and any Phase 1B Slice 4 work.

### Phase 1B Stabilization closure — 2026-08-18

The Phase 1B stabilization gate is accepted. The production navigation tree
keeps model truth separate from disposable WinUI `TreeViewNode` presentation
objects, clears selected nodes before destructive presentation teardown, and
uses one active-tab ThisPC presentation path. The external production-Blink
AddButton/Ctrl+T lifetime regression passed in Debug and Release coverage.

The 50k `CommitBrowse` gate passed in isolated measurements. The prior full-test
allocation failure is recorded only as **test-order / allocator-state
sensitive**; no lower-level allocator mechanism is asserted and no production
resource leak was found. Slice 4 remains deferred and not started.

### Navigation tree loaded-state invariant

Collapsing a navigation-tree node is a presentation change, not an unload or
invalidation. After a successful direct-child enumeration, the authoritative
`NavigationTreeModel` remains `Loaded` and its child IDs remain cached. A
normal re-expansion reuses those children and does not enumerate the filesystem,
replace the child model, or rebuild the realized subtree. Re-enumeration is
reserved for a future explicit invalidation path (for example a watcher or an
explicit refresh); no watcher is implemented in the current slice.

The navigation pane's outer `ScrollViewer` owns the single visible vertical
scroll surface for both quick locations and the hierarchical tree. The
`TreeView` remains the lazy hierarchical presentation and does not become a
second business-state or scrolling authority.

Navigation wheel messages are handled at the desktop window boundary on the UI thread. Only coordinates inside the navigation viewport are consumed; screen/client coordinates use the current XamlRoot rasterization scale. The handler performs immediate ChangeView, observes the Windows wheel-lines setting and accumulates pending offsets. Inner template viewers have both axes disabled. Subclasses are removed before window state shutdown and at HWND destruction; outside-pane, Ctrl and horizontal input keep their normal route. This adapter does not enumerate, navigate, rebuild tree nodes or change selection.

## Phase 1B Slice 4 — File activation ownership

Current status: **IMPLEMENTED / NATIVE PASS / MANUAL UI PENDING (2026-10-13)**. Debug/Release builds, ordinary regressions and the existing production UI lifetime gate pass. New Slice 4 keyboard and associated-app manual acceptance remains pending.

Each TabRecord owns a FileActivationController and its generation/stage/path/error snapshot. Double-tap and list-scoped Enter resolve the action from the committed row/link model. Directory/This PC activation uses the existing NavigationModel/BrowseController route; file activation runs metadata validation on an owned ThreadPool task, then creates an ordinary-user `Blink.FileActivationHost.exe` with an absolute module-relative executable path and quoted private path/full-FileIdentity arguments.

The helper revalidates the object, queries the current user's association ProgID with Unknown fallback disabled, and performs the system `open` verb in STA COM. ReadEntryMetadata updates metadata only, so the helper explicitly retains its normalized path. ShellExecuteEx can call third-party Shell/COM handlers, so that code lives in the helper rather than Blink.exe. The controller starts the helper suspended, registers its process handle under the request gate, resumes only an accepted request, and uses a bounded 30-second worker wait. Cancel/close terminates only the owned helper; target applications are neither waited for nor placed in a kill-on-close job. A completed dispatch means the request was accepted, not that the target app is ready. A timeout is an uncertain launch outcome and is not automatically retried.

Navigation, tab close and window close invalidate the request. Worker publication and UI dispatch both check the generation and tab/window lifetime. Metadata I/O cancellation remains logical; pre-dispatch identity checks do not claim an atomic filesystem-to-association transaction. RowIdentity distinguishes hard-link paths, and missing/replaced/inaccessible files remain explicit failures.

Ctrl+A uses the committed valid row identities and keeps per-tab selection/history/sort ownership. Ctrl+A/Enter are focus-scoped to the file list; Ctrl+L, F5 and Alt navigation reuse existing application entry points. Views, watcher, product search and mutation/clipboard/context-menu flows remain later work under D-0027.


## Phase 1B Slice 5 — View presentation ownership

FolderViewMode（Details/List/Grid）由每标签 TabState 持有，并进入完整和轻量快照。FolderViewPresentation 是 UI 模块，只管理缓存的 DataTemplate、容器样式和虚拟化面板；ListView/GridView 经同一 ListViewBase 输入路径消费原来的 row_items_source。控件替换不会枚举、清空源或启动第二套投影，不改变 RowIdentity、选择、Location、历史或地址草稿。所有源解绑/选择恢复受 suppression 保护；异步 final batch 将选择恢复到当前控件，tab/source 重新呈现和窗口关闭使旧焦点恢复失效。

## Shared hidden-items policy (Phase 1B Slice 6)

TabWindowState owns one app-private ItemVisibilityPolicy and propagates it to existing and new TabState instances. The default excludes Hidden and System attributes; Show hidden items enables Hidden only, while all System-marked entries remain excluded. Explorer registry settings and protected-system configuration are not consulted or changed. This is a window-session preference.

TabState retains the complete committed DirectoryListing. Its visible snapshot and cached visible count supply the UI; a RowIdentity-to-attributes lookup also gates selection and activation. Refiltering does not navigate or perform filesystem I/O, preserves surviving hard-link selections and drops excluded selection/focus. A later browse commit uses the current policy. Projection cancellation/generation gates prevent old rows from being republished after a toggle. Tree workers capture the same policy, and a toggle cancels them, resets the lazy tree cache and reveals the active location through the existing lifetime-checked path. Manual tree expansion state may reset; current Location/history and address drafts are preserved.

Type 排序为当次排序准备显示类型 key，按规范化 extension 缓存标签，comparator 不查资源；排序仍保留目录优先和稳定名称 tie。排序改变投影视图，文件系统事实、身份和 committed listing 保持原样。网络 Provider、Search 和 mutation 未因视图功能引入。

## Window titlebar presentation (2026-10-14 user request)

Blink hosts the existing TabView in the built-in WinUI TitleBar.Content, with the browse root below it. The separate browse-root tab row is removed. The titlebar and native caption buttons share a 48-DIP row (PreferredHeightOption Tall); the TitleBarContentHorizontalAlignment resource aligns the tab strip to the left at its desired width. Its interactive bounds end after the occupied tabs and add button, leaving unused caption space draggable. AutoRefreshDragRegions keeps dynamic tab controls interactive while the built-in minimum drag region reserves blank space next to the system buttons. Window-wide keyboard accelerators belong to the common outer frame so they also run when a titlebar tab has focus. Client content extends into the caption area and Window.SystemBackdrop supplies MicaBackdrop. The framework owns drag regions, caption insets, DPI adjustments and system caption buttons. The app only synchronizes the caption palette with XAML ActualTheme and makes normal/inactive button backgrounds transparent. Loaded/theme callbacks capture the app weakly and stop after window close. This changes window presentation without changing tab, navigation, selection or worker ownership.


## Folder context commands (Phase 1B Slice 7)

FolderCommandPolicy derives availability from the committed location, browse/projection readiness, authoritative selection count and resolved target. It introduces no second filesystem or selection model. The window creates one transient MenuFlyout with weak app callbacks; an unselected requested row replaces selection, an already selected row preserves multi-selection, and background commands preserve it. Empty views remain keyboard-focusable.

A menu context captures stable TabId, committed Location and presentation/view generations plus a projection index valid only for that version. Dispatch rechecks the page and target selection, so a retained menu cannot operate on another tab or a refreshed/replaced projection. Real commands reuse existing activation, refresh and view/sort methods. Phase 1B reports unavailable clipboard/mutation/properties intents without publishing clipboard data or changing files. Context cancellation and window close hide the menu. File-list scoped shortcuts preserve text editor behavior.

## Folder changes (Phase 1B Slice 8)

Each TabRecord owns a FolderWatcher and a UI-thread FolderChangeState. A folder subscription opens the directory on a worker and arms a non-recursive overlapped ReadDirectoryChangesExW request. A coroutine awaits its event, so an idle tab does not hold a blocked worker. Notifications are invalidation hints: they never edit rows, selection or the index. Ready triggers an initial reconcile to cover the first-enumeration/subscription gap; Changed and Overflow trigger the same fresh listing path.

The window queues at most one pending hint per subscription and preserves errors across coalescing. A per-tab dispatcher timer runs only while changes remain, with a 200 ms quiet period and a one-second burst bound. Browse/projection work and committed-location mismatches defer consumption. New changes during a refresh form the next batch. Reconcile uses the existing TabState.Refresh/BrowseController commit gates and preserves address drafts, history, view/sort and surviving RowIdentity selection. Inactive tabs update their own page; active UI status writes are gated by TabId.

Navigation changes the subscription token; callbacks validate TabId, path and generation on the UI turn, including A→B→A and close races. Failed navigation restores the committed directory's subscription. This PC has none. Tab/window close stops timers, invalidates tokens and signals the native session without waiting on the UI. The worker requests CancelIoEx and drains actual completion before releasing/reusing its OVERLAPPED, buffer and handles. It rechecks cancellation after arming I/O because arming can reset the event.

Unavailable subscriptions report a localized Win32 error and retain manual Refresh, which can retry the subscription. There is no polling or older-API fallback. This slice updates folder contents; navigation-tree cache reconcile, recursive subscriptions, relocation of the watched directory itself, Provider monitoring and speculative prefetch remain deferred. Production UI/manual acceptance is independently pending.


## Phase 1C Slice 1 — Index service status

窗口独立 IndexServiceState、Coordinator 与 Presentation；IPC 仅在 owned worker，关闭使 generation 失效并在后台完成 bounded 请求。协议全局操作无卷根，不映射到所有卷；每轮只查选定卷，避免单活动 FastIndex 被状态轮询切换。服务故障不阻断浏览。


## Phase 1C Slice 2 — Search backend

SearchState 与 SearchCoordinator 独立于 TabState/BrowseState；每查询 generation 管理逐卷 SearchVolumeResult、source volume 与完整 link identity，取消不释放仍在执行 IPC 的 shared worker。同一次查询 retry 只补失败卷，显式新 query 丢弃所有旧结果。后续 presentation 独立 surface 与窗口级调度见 Slice 3 计划，当前尚未接线。

## Phase 1C Slice 3/4 — Search presentation and exact mode

每个TabRecord拥有搜索模型、controller、草稿、已提交快照、选择与动作generation；目录快照和历史保持独立。窗口SearchRequestQueue串行分配ticket，保留每标签最新pending请求，并在标签关闭后继续持有在途native action直到返回；状态轮询与搜索错开。SearchPresentation只处理虚拟列表和分批投影，不执行IPC或元数据读取。

CurrentFolder范围先验证目录身份与reparse祖先，再按真实祖先/Parent ID过滤，防止大小写敏感sibling混入。Open/Locate在worker重验当前对象及父目录身份，导航枚举后再验证目标目录；过期token不能定位新页面，丢失目标清除旧选择并显式报错。

Exact复用冻结prefix协议，wire候选上限1000，完整名称筛选与显示上限分开。达到候选上限使逐卷query_incomplete进入Partial，即使筛选后为0项；只重试未成功卷。没有新增IPC字段或文件系统搜索后备。Slice3原生验证通过，Slice4完整构建/Release验证待补，人工和真实服务验收暂缓，详见PROGRESS。

## Phase 1D Slice 1 — Window-owned New Folder

FileOperationCoordinator拥有单个用户权限worker、合作取消标记与实际提交结果。开始新任务前原native action必须结束；关闭只使UI发布失效，worker仍保留shared state。完成回调携带immutable snapshot，使后来请求不改写旧事件的刷新目录；已提交创建不因晚到取消而标为回滚。

DirectoryListing新增同一枚举句柄读取的directory_identity，TabState提供无I/O getter。FileOperations在worker验证leaf名称、目录链类型与identity/reparse，并短暂持有FILE_LIST_DIRECTORY且不共享DELETE的链句柄。CreateDirectory2W保留DISALLOW_PATH_REDIRECTS、禁止覆盖；输入对话框等待时不持有文件系统句柄。fallback枚举缺少可信身份时不开放写入。

FolderNameDialog仅呈现目标/名称/错误与确认/取消；窗口快捷键在对话框打开时受门禁。窗口footer独立保存操作状态、取消及失败后的修改名称入口。操作完成按committed directory identity向各标签发出刷新提示，已有变化timer等待browse/projection结束，显式导航丢弃旧提示；即使watcher不可用，Blink自身提交的变化仍可对账。搜索surface和目录历史不由操作结果直接替换。

完整native构建/普通检查与人工门禁状态以PROGRESS为准，人工UI与真实服务仍暂缓；不存在服务mutation、提权、剪贴板或删除功能接线。

## Phase 1D Slice 2 — Rename in the window operation pipeline

FileOperationKind区分NewFolder/Rename，FileOperationResult的committed表示实际提交而非创建专属标记。Snapshot保留目标父目录/新leaf及可选immutable rename source，单个Submit worker复用取消、关闭与完成发布。error=0且没有提交的Rename是成功no-op，不强行变成失败或假提交。

RenameOperation与DirectoryLease负责全部filesystem I/O与source-handle核验；main只捕获committed selection/context、调用coordinator并按parent identity对账。共享FolderNameDialog根据动作显示创建/重命名标题与控件名称；F2/菜单只允许一个有效目标，源或页面变化后不作用于其它row。目录/文件/硬链接/case-only/long-path与冲突的真实fixture检查独立于产品UI。

重命名后旧row identity失效，沿用已有选择规则清除，避免同ID硬链接误选；本刀不实现自动聚焦新名称、被改名目录的其它已打开标签自动路径重定位、Undo、跨目录Move或递归Copy。人工验收暂缓，完整native验证状态以PROGRESS为准。

## Phase1D Slice3 — copy worker and clipboard ownership

The only product copy execution path is window FileOperationCoordinator → FileCopyOperation::CopyFileSafely. ClipboardFileTransfer owns bounded Unicode CF_HDROP encoding/decoding and worker-only native clipboard/source identity capture; it does not own a separate file-operation engine. Internal source snapshots remain identity-bound while the clipboard sequence/path match. External snapshots are captured on the worker, then revalidated by the same copy worker. Destination origin is gated again on the UI before submission; late callbacks cannot write after navigation/close.

Copy follows the existing source/destination directory leases and validates the opened source handle. Exclusive CREATE_NEW destination ownership provides atomic no-overwrite and exact-handle cleanup. Progress is an immutable snapshot, coalesced at 100ms; actual completion outranks a late cancel request. Closing invalidates UI publication immediately without waiting for synchronous native I/O. Completed or partial writes invalidate only tabs committed to the destination parent's identity.

Only ordinary default-stream files are supported in this slice. Special metadata/storage, directory recursion, Replace/Skip/KeepBoth, Cut/Move and drag/drop remain explicit later work. Blink→external only publishes COPY data; external targets own their writes. The Phase0 CopyFile2/future prototype remains diagnostic and is never called by this product path; the Phase0 sections above describe historical probes. API choice and metadata limitations are recorded in D-0007 and the Slice3 plan.

## Phase1D Slice4 — one copy engine, explicit decisions and streams

FileCopyOperation continues as the only product copy engine. It copies the default and named $DATA streams using identity-checked owned handles and bounded enumeration; it does not add a second path through the Phase0 CopyFile2/future prototype. Source guards, destination directory leases, CREATE_NEW atomic ownership, cleanup and actual commit truth remain the native boundary. All stream bytes count toward progress; actual success follows final stream inventory/basic metadata verification.

The default target's DELETE ownership requires DELETE sharing on its ADS writers. This does not prohibit a competitor from marking an ADS for deletion. Final validation therefore checks every held target stream's identity, type, size and delete-pending state in addition to comparing inventories. A detected pending deletion aborts and removes the owned copy; it never publishes success for a stream that will disappear on close. External mutation after the commit boundary and a full atomic metadata snapshot remain outside this copy contract.

FileCopyConflictDialog owns one CopyConflictModel decision and one async dialog lifetime. Skip is a terminal no-effect choice, distinct from Copied and Cancelled; KeepBoth generates deterministic numbered candidates with CREATE_NEW. Replace remains explicitly unavailable because the tested path APIs cannot meet expected-target identity protection. The UI observes terminal snapshots, then awaits the native IAsyncAction’s real end before opening conflict choices; close/navigation invalidates the continuation and suppresses a late choice. Manual name retries remain available after failure or cancellation, with the destination path rebound only when the same directory identity is currently committed.

This slice retains local single-file scope; special storage types, full backup metadata, directory recursion, multi-file queues, Replace, Cut/Move, drag/drop and Delete remain independent work. Frozen IPC and IndexService semantics remain unchanged.

## Phase1D Slice5 — bounded copy queue ownership

The window still owns one FileOperationCoordinator and at most one native action. Copy(single) and CopyBatch both enter its authoritative Submit path; the worker executes existing CopyFileSafely calls serially. FileCopyBatchSnapshot retains item requests/stages/results/history and aggregate counts independently of WinUI controls. A failed current item stops the worker; Resume creates a new operation generation after actual worker completion, preserving earlier outcomes and untouched tail items. No worker blocks waiting for a dialog and no second copy engine is introduced.

ClipboardFileTransfer handles bounded multi-path encoding/decoding and whole-selection metadata capture. The window captures selection through one bounded TabState scan, and compares the actual published payload/sequence before retaining original source identities. Preparation has cooperative cancellation and a second UI-side cancellation check before submission. The conflict dialog only chooses the current item; manual name retry resumes the queue, while navigation invalidates the dialog continuation and cannot redirect an executing operation. A failed queue with pending work remains owned until a decision or explicit cancellation instead of being silently replaced by another mutation.

The status panel derives queue counts/current-file progress from snapshots. Earlier partial-file paths remain inspectable after a successful retry. Refresh requests use actual commit/partial counts and the destination parent's identity. Multi-file UI and external clipboard interoperability remain on the deferred manual checklist until exercised.

## Phase1D Slice6 — bounded directory plan and owned partial tree

CopyDirectorySafely is an app-private worker backend, currently unconnected to the window coordinator, clipboard or UI. It holds one source-parent chain, one destination-parent chain, each planned source-directory handle and each created destination-directory guard until the call ends. Its explicit plan is bounded by descendants, directories, depth and UTF-16 path storage. Directory enumeration is performed against the held handle; browser enumeration fallbacks are not mutation inputs.

After a complete preflight, the worker atomically creates a new root and its directories. A separate metadata handle is identity-bound while the creation guard remains held. Leaf files are copied serially by CopyFileSafely. Results distinguish committed files, created directories, failure path and partial_tree_left; failure after even an empty root creation retains a real partial tree. The worker does not merge, replace, remove source objects or recursively roll back paths. Final inventory/target checks and postorder metadata precede copied=true. Observer errors after that commit preserve success.

The existing multi-file UI publication now places scalar refresh facts and an optional current conflict request on the dispatcher, rather than retaining a whole FileCopyBatchSnapshot per queued notification. The authoritative coordinator still owns every item/result/history. Before opening a delayed conflict, the UI also checks the current stage and resumability so a cancelled operation cannot be reopened by its old failed notification. Ordinary compilation/check evidence and manual UI acceptance remain separate.

## Phase1D Slice7 — one coordinator action with distinct directory facts

The new CopyDirectory entry integrates the Slice6 worker into the existing coordinator without exposing directory UI or clipboard commands. DirectoryCopy is a separate operation kind with independent request/progress/result. Submit still admits exactly one native action; an unfinished file-batch decision also blocks this entry. Constructor compatibility is maintained through a third optional directory executor used by deterministic lifecycle tests.

The state mutex protects progress/results and publication occurs after unlocking. Per-file bytes are distinct from cumulative object counts, and the returned directory result is stored before best-effort generic projection. A completed progress observation does not release the action. Terminal state can be observed while the owned action is still Started; new mutations wait for actual return. Closed state suppresses late publication while the action continues to own state and cooperative cancellation until the engine ends. Directory partial facts stay independent from file partial/history, and a new operation clears previous kind-specific context.

## Phase1D Slice8 — mutually exclusive captured source modes

ClipboardCopySources is an app-private variant of monostate, an ordered file-request vector or one directory request. The new worker metadata entry clears the previous output before validation and publishes a complete fresh mode only on success. It preserves captured source/parent identities and leaves destination fields empty. Failure, mixed/multiple directories, cancellation and allocation failure never expose a valid prefix. Old files-only clipboard callers remain on their original API and do not gain directory commands through this change.

Capture describes the root object's metadata; the file/directory engine later determines whether content, streams, descendants and current identities are supported. This keeps clipboard preparation from doing synchronous tree traversal or becoming a second copy coordinator. Directory UI, partial-tree presentation/refresh and explicit new-name retry still need the separate product integration plan.

## Phase1D Slice9 — directory product entry and permanent origin invalidation

The window uses typed worker capture for ordinary file batches or one ordinary directory. Its clipboard cache retains the mode, exact published ordered paths and sequence, and the initial source/parent identities. Matching internal payloads cannot accept replacement objects by falling back to external capture. Destination binding publishes a complete fresh mode only after the UI cancellation/origin gate.

FileOperationOrigin now includes TabRecord's file_operation_generation. User navigation, search transitions, tab departure and shutdown invalidate the previous intent permanently, including failed navigation and A-B-A returns. Automatic same-identity reconcile preserves intent. The gate is shared by new-folder, rename, file copy, directory copy and their dialog continuations; an executing operation still owns its original destination.

DirectoryCopy uses the existing native action and independent tree facts. UI refresh reads directory copied/partial_tree_left, including an empty partial root, rather than generic file partial. Terminal publication is followed by asynchronous action completion before name decisions; departure suppresses old dialogs while completion can still refresh the status. Retrying by a new root name never creates an empty folder through the generic retry branch or reuses a partial tree. Presentation history retains up to32 paths/65536 characters with a truthful total count across the current explicit retry lineage; a new copy starts a new history without deleting disk objects. Merge, root KeepBoth, mixed/multiple-directory queues and Replace remain separate work. Native verification does not constitute manual clipboard or UI acceptance.

## 2026-10-27 Phase1D Slice10 — directory root conflicts

Directory root conflicts share the existing CopyConflictModel and unique coordinator action. Skip/Cancel are explicit no-I/O choices. KeepBoth prepares all destination path replacements within the old+new peak path budget, then atomically claims a free root with the same held source plan. Root creation permanently ends name search; later failure/cancellation preserves truthful tree partial facts. Directory Merge/Replace remain disabled. Dialog decisions require the completed action, current operation ID and current navigation intent.

## 2026-10-27 Phase1D Slice11 — mixed copy queue

CopyItems extends the existing serial batch and single native action with explicitly typed directory entries. Common fields use a file-shaped internal projection plus optional directory limits; source type/attributes must agree before any executor starts. Directory progress requires both operation ID and item index, and bytes reset when the next item begins. Each directory result and bounded partial history stay separate from file partial facts. Resume preserves completed prefix and updates only current/pending paths under the same destination identity.

## 2026-10-28 Phase1D Slice12 — same-volume move task ownership

MoveEntry now shares the existing Submit/owned IAsyncAction and ordinary-user coordinator. Its request/progress/result remain separate from file and directory copy contexts. Worker result decides moved/no-op/skipped/error; completed progress is only a hint, and terminal publication does not release action admission. A real moved fact survives late cancellation and an independent postcommit warning. Old-ID/kind/closed/terminal or regressing/oversize progress is rejected. Existing pending COPY queue admission is reused. No Cut/Move UI command, new worker architecture, service command or IPC contract is introduced by this backend slice.
