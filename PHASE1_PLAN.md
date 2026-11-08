# Blink Phase 1 Plan

当前执行状态（2026-11-08）：位置跟随与高级搜索已完成：此电脑搜所有本地盘，目录内默认搜所在整盘；支持指定多盘、当前/指定目录及子目录开关，设置按标签独立、按搜索/回车提交。Debug/Release各1416 PASS/0 FAIL/4权限SKIP，新增34项与4项Phase05检查；359资源/220输入一致，真实C/D范围和目录深度、19项自有窗口检查通过，默认Release已更新。人工体验仍暂缓，完成本任务后停止，项目其余功能待续。

基线：Phase 0.5 已在 `e20ba2bd6bac22db7baa73952609b9205a36bd0c` / `phase-0.5-accepted` 完成并接受。本文定义 Phase 1 的范围、顺序和验收标准；2026-10-14 用户要求继续推进当前冻结范围内的功能开发；Slice 4 与下述 Slice 5 不重新执行 elevated acceptance。

## 搜索性能专项 — 2026-11-08

用户在本轮明确授权多子代理解决明显慢于Everything的搜索问题，并要求半小时节点汇报。本专项允许修改此前延后的FastIndex查询实现及服务多卷驻留；边界为减少重复加载、名称预处理、有效limit和实机测量，不扩展产品功能或本地防护机制，不更改BLK5磁盘格式和IPC协议。主模型统一单并发构建验证，真实库保留。性能结果必须区分服务IPC、UI首屏、首次加载和重复查询，不能以功能测试数量替代性能证据。

## Phase 1B Stabilization Gate — accepted 2026-08-18

- Status: **ACCEPTED / COMPLETE**.
- The production Tab/Tree lifetime blocker (`0xC0000374`) is fixed and the
  real-Blink AddButton/Ctrl+T lifetime regression passed in Debug/Release
  coverage, including bounded repeated tab creation and TabView/tree switching.
- Debug and Release `Blink.Phase1.Tests` pass. The 50k initial `CommitBrowse`
  gate is measured independently at approximately 24–29 ms in Release and
  219–245 ms in Debug.
- The former allocation failure is classified as **test-order / allocator-state
  sensitive**. The test harness now isolates the allocation-sensitive benchmark
  from unrelated preceding large tests. No lower-level allocator mechanism is
  claimed by this closure.
- No production resource leak was found. No IndexService acceptance, elevated
  acceptance, Phase 0.5 acceptance, or Slice 4 work is authorized by this
  closure.

## 1. Goal

把现有 Phase 0 原型变成普通用户可以日常使用的 Windows 本地文件管理器，完成以下产品闭环：

`Blink.exe` 启动 → This PC → 浏览本地磁盘/目录 → 地址栏/前进后退/上级 → 多标签与多种视图 → 标准选择和键盘操作 → 打开目录/文件 → 本地索引搜索 → 定位/打开结果 → 新建、重命名、复制、移动、删除。

产品交互原则：

- 操作逻辑尽量符合 Windows 文件资源管理器的用户预期；视觉布局可以保持 Blink 自己的现代、紧凑风格。
- 熟悉的打开、选择、导航、复制、移动、删除、拖放、快捷键和标签语义优先于新颖交互。
- Blink 是自己的文件系统/Application Model，不以 Windows Shell Namespace 作为主文件系统模型。
- 任何系统 API、文件关联或回收站适配都必须保持普通用户权限、可取消、可解释失败和 UI 故障隔离。

全程保持以下行为：

- UI 线程不等待磁盘、IndexService、Provider 或第三方处理器。
- IndexService 只提供已验收的高权限卷索引、状态和只读搜索；不增加文件操作后门。
- 文件系统是文件操作的事实来源；索引是反应式搜索视图。
- `FileIdentity` 使用卷身份和 File ID；路径是可变化的定位信息，不是永久身份。
- IndexService 停止、重启、重建或不可用时，本地浏览和用户权限下的文件操作仍可用。

## 2. Non-goals

本阶段不把下列内容作为交付目标：

- SFTP、SMB、WebDAV、Cloud、Archive、MTP、Provider 产品化、AgentBridge、MCP、ExtensionHost。
- 完整 MetadataHost、预览、缩略图、Shell Namespace、控制面板、打印机和 Win10 兼容路径。
- Explorer 第三方 Shell Extension、第三方 context-menu handler、Shell Namespace 浏览和 Shell 外观复制。
- BLK5 最终数据库 ABI、B+Tree/LSM、完整倒排/trigram 搜索重构、初始 MFT 大规模性能重构。
- `SERVICE_SID_TYPE_RESTRICTED`、虚拟服务账户实验、插件生态、安装/签名/发布体系的完整产品化。
- 高级主题、动画和大范围视觉润色。

这些事项保留在 Phase 1 末尾的技术债清单中，不得为满足 Phase 1 UI 需求而扩大 IndexService、FastIndex 或 IPC 协议。

### 2.1 Extension-readiness constraint（D-0025）

Phase 1 只保留未来扩展能力的架构空间，不实现扩展系统：

> Phase 1 shall avoid architectural decisions that prevent future out-of-process third-party extensions. Public plugin SDK, ExtensionHost, capability permission system, extension distribution, marketplace, compatibility guarantees, and third-party extension loading are deferred to a later phase.

具体约束：

- `Extension readiness != Extension implementation`；不创建 `ExtensionHost.exe`、公开 Plugin SDK、manifest/parser、plugin loader、capability manager 或 demo plugin。
- 第三方代码未来默认在 ordinary-user 的进程外 Host 中运行，不以第三方 DLL 注入 `Blink.exe`；插件故障不得冻结或崩溃 UI。
- Phase 1 的 `WindowState`、`TabState`、`NavigationModel`、Folder controller、IndexService coordinator 和 File operation coordinator 保持 app-private，不成为第三方 ABI；WinUI control、raw pointer 和内部 STL ABI 不进入未来公共 contract。
- Provider 继续复用现有 Provider abstraction；不建立第二套文件系统模型。IndexService 仍只提供已定义的特权索引职责，不因插件需求新增任意 privileged filesystem command。
- 只有当前已经存在多个同类实现并确实需要统一注册时，才建立应用内 registry；“未来可能有插件”本身不足以创建空 registry。
- Plugin System 与 Custom Frontend System、AgentBridge 保持独立；本阶段不实现 frontend protocol 或 capability/version negotiation。

### 2.2 Localization foundation（Phase 1A）

Phase 1A 的固定用户可见文本使用 MRT Core `.resw` 资源，当前提供
`src/Blink/Strings/en-US/Resources.resw` 与
`src/Blink/Strings/zh-CN/Resources.resw`。`Blink.exe` 通过 app-private 的
`blink::app::Localization` 薄封装调用 `ResourceLoader`；不建立独立本地化框架，
不把资源键或 WinUI 控件暴露为公共 ABI。

- 语言选择遵循 Windows 当前语言/资源回退规则，当前不提供运行时语言切换 UI。
- 应用标题、导航、工具栏、列头、状态、文件类型、大小单位和浏览错误属于资源化范围。
- 路径、文件名、identity、Win32 数字码、IPC/operation stage 和诊断字段保持数据语义，不翻译。
- 缺失 PRI 或普通测试环境使用安全的英文 fallback；正常发布构建仍以生成的 PRI 为资源来源。
- 运行时语言切换、翻译管理工具、插件/Provider 文案 ABI 和 Phase 1B 产品化不在本刀范围。

## 3. User-visible acceptance scenario

普通用户启动 Blink 后应能完成以下场景：

1. 默认进入 Blink-native 的 This PC；This PC 不是伪造的文件系统路径，地址栏显示本地化的 `This PC/此电脑`。
2. This PC 显示常用位置和当前可访问的本地固定/可移动卷；进入卷或目录后可用地址栏、Up、Back、Forward、Refresh 导航。
3. 主窗口先显示可交互框架，随后异步显示内容；窗口不会因目录枚举、服务连接、文件操作或外部 watcher 而卡住。
4. 每个标签保持自己的 Location、历史、选择、视图和列表；Back/Forward 不串台，关闭或切换标签不会留下 stale worker 回调。
5. Details、List、Grid/Icons 视图都保留当前目录、FileIdentity、选择真相和历史；Details 至少显示 Name、Type、Size、Modified，并支持正反向排序和目录优先规则。
6. Single/Ctrl/Shift 选择、Ctrl+A、双击目录、Enter、F2、Delete、Shift+Delete、F5、Ctrl+L/T/W 和 Ctrl+C/X/V 具有冻结的 Windows-style 语义。
7. 双击目录进入目录；双击普通文件或 Enter 使用当前用户的系统文件关联打开，打开失败可见，不通过 Shell Namespace 枚举或服务提权。
8. Blink-owned context menu 提供 Open、Cut、Copy、Rename、Delete、Properties 策略，以及空白处的 Paste、New Folder、Refresh、View、Sort。
9. 搜索按 This PC/所有已索引本地卷或 Current Folder 选择范围运行；结果包含 Name、Path、Type、identity 和 source volume，并支持 Locate/Open。
10. 选中文件/文件夹后完成 New Folder、Rename、File/Directory Copy、Same-volume/Cross-volume Move、系统回收站 Delete 和确认后的永久删除；冲突、进度、取消、权限错误和部分失败可见。
11. Ctrl+C/X/V 与 Explorer/Desktop 使用 Windows 系统文件剪贴板互操作；Blink 内部操作仍由普通用户进程的 FileOperationCoordinator 执行。
12. Drag & Drop 的 ownership 按方向区分：External → Blink 的实际 operation 由 `FileOperationCoordinator` 拥有；Blink → Blink 的实际 operation 由 `FileOperationCoordinator` 拥有；Blink → External 时 Blink 只发布标准 Windows filesystem transfer data，实际 operation 由 external target 拥有。对于 external Cut，在 external target 明确执行 operation 之前，Blink 不得自行删除 source。
13. 当前目录的外部新建、删除、重命名、移入/移出最终收敛；可移动卷插拔、索引服务故障和权限失败不会崩溃、冻结或静默丢数据。

## 4. Architecture changes

### 4.1 UI/application layer

当前 `src/Blink/src/main.cpp` 是一个集中式、代码构建的单窗口 StackPanel 原型，包含路径输入、ListView、原型本地 FileIndex、IndexService 按钮、属性、Copy 和 Provider 按钮。Phase 1A 先建立可测试的 UI 状态/控制器边界，再逐步替换这些原型按钮；不要求引入新的 UI 框架或迁移到 Shell Namespace。

建议的最小边界：

- `Location`/`NavigationModel`：app-private 的 `ThisPC` 或规范化 `Folder`、历史、上级和标签状态，不执行 I/O；This PC 不伪造路径。
- `FolderViewController`：在后台调用 `blink::core::EnumerateDirectory`，以请求代数/取消令牌防止过期结果覆盖新目录。
- `IndexServiceCoordinator`：在后台复用 `Blink.Index.Contracts::Client`，统一连接、状态、进度、搜索、重连和错误映射；不改现有协议。
- `FileOperationCoordinator`：在用户进程中调度 Core 的创建/重命名/删除和 Transfer 的 CopyFile2，统一进度、取消、错误和刷新。
- `WindowState`/`TabState`/`OperationState`：只保存可观察状态，所有 UI 更新通过 DispatcherQueue 返回 UI 线程。

这些模块可以先作为 `src/Blink` 的 app-private 源文件，只有纯状态模型确实需要跨测试复用时才放进 `Blink.Core`；不因计划而新建多个没有独立边界的静态库。

### 4.2 Async and lifetime model

Phase 0 prototype 的旧搜索/服务入口仍作为 deferred/diagnostic debt 记录；active Phase 1 browse path 使用可追踪、可取消、可在窗口关闭时收束的后台任务集合，不得继续增加 detached 线程。

每个异步请求必须带有：

- 请求/代数 ID；只接受当前标签和当前请求的结果。
- `stop_token` 或等价取消标记。
- 明确的完成、取消、失败路径。
- DispatcherQueue 发布前的窗口/控制器生命周期检查。

服务请求、目录枚举和文件操作均属于后台工作；事件处理函数只提交任务并立即返回。

### 4.3 Reuse boundaries

- `Blink.Core`：复用 `FileEntry`、`DirectoryListing`、`FileIdentity`、路径规范化、`EnumerateDirectory`、`CreateDirectoryModern`、`RenameFileModern`、`DeleteFileModern` 和 `RemoveDirectoryModern`。
- `Blink.Transfer`：复用 `CopyFileAsync`/CopyFile2、取消和进度回调；补一个 app 层 operation wrapper，不改变 CopyFile2 语义。
- `Blink.Index.Contracts`：复用现有版本化 Named Pipe client 和 `QueryVolumeStatus`、`StartInitialIndex`、`QueryIndexProgress`、`Search`、`StopOperation`；不增加协议命令。
- `Blink.Properties`：Phase 1 不把完整属性/元数据做成主流程；需要时只作为后续详情入口。
- `Blink.Index`/`Blink.IndexService`：保持 Phase 0.5 已验收的核心、权限、Pipe ACL、BLK5 和状态语义不变。
- Provider 项目：不接入 Phase 1 主导航和主搜索。

## 5. Project/module changes

计划中的改动位置如下；本文件创建阶段不修改它们。

| 模块 | Phase 1 用途 | 明确不做 |
| --- | --- | --- |
| `src/Blink` | WinUI 主窗口、视图状态、导航、标签、IndexService coordinator、文件操作 coordinator | 不继续把业务逻辑塞回单个 `main.cpp`；不增加特权操作 |
| `src/Blink.Core` | 复用并必要时补纯导航/操作辅助类型 | 不改变 FileIdentity 语义，不替换文件系统事实来源 |
| `src/Blink.Transfer` | CopyFile2 异步任务、进度和取消适配 | 不把 Copy 变成 Service 命令 |
| `src/Blink.Index.Contracts` | 作为现有 IPC adapter 的唯一协议入口 | 不改版本、消息 ABI 或服务安全策略 |
| `src/Blink.Index` / `Blink.IndexService` | 仅作为既有已验收服务使用 | 不优化 MFT、FastIndex、Search 或 BLK5 |
| `tests/Blink.Phase0.Tests` | 保持 Phase 0 回归 | 不把 UI 验收塞入旧 Phase 0 入口 |
| `tests/Blink.Phase05.Tests` | 保持 Index/IPC/安全回归 | 不为 Phase 1 UI 需求重跑 elevated 流程 |
| `tests` 新增普通测试入口 | 导航、异步状态、操作 coordinator、Search response mapping | 不依赖管理员、LocalSystem 或真实全卷 MFT |

工程仍使用现有 `Blink.sln`、C++20、WinUI/Windows App SDK 2.3.1、Windows SDK 10.0.26100；构建输出和 NuGet 缓存继续遵循现有项目目录布局。

## 6. UI state model

### 6.1 Window state

窗口级状态至少包括：

- `active_tab_id` 和标签列表。
- IndexService 连接状态、索引状态、最近 operation ID、进度和最近错误。
- 当前全局文件操作列表及其进度/取消状态。
- UI 通知和错误队列。
- 当前全局 hidden/system items policy、可移动卷可用性和当前 view mode。

### 6.2 Tab state

每个标签独立保存：

- `Location`：`ThisPC` 或带规范化路径的 `Folder`；This PC 不使用伪造的 path。
- Back/Forward history 和当前历史位置。
- committed folder page state（`Idle`/`Ready`/`Empty`）和独立的 browse activity 必须分离；失败请求不得把仍有效的 committed page 伪装成 `Loading`/`Error`。
- 当前 `DirectoryListing`、排序规则、排序方向、选择集合和焦点项。
- 当前 view mode：Details、List 或 Grid/Icons。
- 搜索模式、查询文本、搜索状态和结果集合；搜索结果不覆盖本地目录列表，除非用户明确选择定位。
- 当前请求代数，防止旧枚举、旧搜索或旧定位结果覆盖新状态。

This PC、Folder、Search result 和 unavailable volume 都是不同的 Location/状态，不能把它们压扁成一个路径字符串。卷移除时，受影响标签保留可解释的 unavailable/error 状态，历史仍可退回其它有效 Location。

### 6.3 Item and operation state

列表项模型至少保留 `FileIdentity`、当前路径、名称、目录标志、类型、大小、修改时间和基础图标类别。显示路径可以更新，identity 不可由路径字符串替代。

UI row identity 不总是等于 `FileIdentity`：同一 File ID 的多个 hard-link 名称必须能单独选择、重命名和删除。可以在 app-private 层引入 `LinkIdentity`/`RowIdentity`（FileIdentity + Parent ID + Name/link discriminator），但不修改 authoritative FileIdentity 或公共 ABI。该规则同样适用于 Search result rows：两个不同 Parent ID + Name link 即使 FileIdentity 相同，也必须作为独立结果和独立 action target。

文件操作状态统一为 `Queued`、`Running`、`Cancelling`、`Completed`、`Cancelled`、`Failed`，包含源/目标、已传输量、总量（若可知）、Win32/HRESULT 错误和刷新目标目录。

冲突策略按 operation/object type 区分：File Copy/Move 使用 `Replace`、`Skip`、`Keep Both` 或 `Cancel`；Directory-to-existing-directory 使用 `Merge`、`Skip`、`Keep Both` 或 `Cancel`。`Merge` 不得先删除整个目标目录，子项冲突必须继续递归使用对应 conflict policy。Rename 遇到 target-exists 至少提供 `Rename Again` 或 `Cancel`，不得机械套用 Copy 的四选项；其它替换语义只有经过单独安全设计后才能加入。批量操作可另加“应用到剩余冲突”选项；Keep Both 名称生成必须确定性、Unicode-safe、保留 extension 且不得覆盖已有对象。

所有状态变更必须在 UI 线程发布；后台线程不能直接操作 WinUI 对象。

## 7. Navigation model

导航规则：

- `ThisPC` 是虚拟 Blink-native Location；路径输入仅用于 Folder Location。地址栏在 This PC 时显示本地化名称，不伪造 `C:\This PC`。
- 输入路径先规范化并验证为用户可访问路径；空路径、非法路径和不可访问路径显示错误，不改变当前有效页面。
- 进入新目录后清除该标签的 Forward history；同一目录的 Refresh 不新增历史。
- Back/Forward 只移动当前标签的 history；每次移动都启动新的后台枚举并使旧请求失效。
- Up 对普通目录使用规范化父路径；对卷根目录返回 Blink-native `ThisPC`，不产生错误。Back 可从该 ThisPC 历史位置返回卷根目录。
- 每个标签有独立 Location/history/selection/listing；切换标签不触发另一个标签的历史变化。
- 双击目录和 Enter 目录使用导航模型；文件使用当前用户关联打开，打开失败可见。
- Search result 的 Locate 先使用 result path 作为候选，再在后台用 FileIdentity 验证；路径失效时显示“结果已变化/无法定位”，不得把同名文件当作同一对象。
- 浏览目录不依赖 IndexService；IndexService 不可用时仍可直接枚举当前用户可访问目录。
- This PC 的卷/常用位置列表使用 Blink 自己的 Location 模型；不通过 Shell Namespace 作为主枚举模型。
- 导航树按需异步展开，不预先递归扫描整棵树；盘符/挂载点变化只更新受影响 Location。

## 8. Search integration

### 8.1 Product path

`IndexServiceCoordinator` 在后台完成 Connect/Send/Disconnect，并把现有 `Response` 映射为 UI 状态。UI 不直接调用同步 IPC client；同步调用只能出现在 coordinator 的 worker 内。

搜索使用已有字段和语义：exact/prefix/substring/extension、可选 `path_prefix`、limit 和 operation ID。Phase 1 不改变 Search IPC deadline，不把“返回了响应”当作 correctness；known-hit、known-miss 和 path/identity mapping 继续由普通回归覆盖。

### 8.2 Visible status

产品 Search scope 固定为：

- `This PC` / All indexed local volumes：各卷独立显示 Healthy、CatchingUp、Rebuilding、Unavailable 等状态；一个卷重建不能冻结其它卷的搜索。
- `Current Folder`：以当前 Folder Location 为范围，不能把同名但范围外的结果混入。

IndexService 不可用时不得偷偷对全卷做 DFS fallback；当前目录本地浏览仍可用，但全卷搜索明确显示不可用或 degraded。结果必须携带 source volume，并支持 Open 与 Locate 的 identity 验证。

UI 至少显示：

- `Healthy`：允许搜索。
- `Loading`/`Rebuilding`：显示真实 operation/progress；不把旧结果标成 Healthy。
- `CatchingUp`：显示 USN catch-up 进度，允许按产品策略排队搜索或提示稍后重试。
- `RebuildRequired`/`Corrupted`：显示索引不可用于正常搜索的原因，不静默使用可能过期结果。
- `Unavailable`/`Disconnected`：显示服务不可用；提供异步重连/刷新，不阻塞浏览。

初始索引启动若仍需要 UI 操作，复用 `StartInitialIndex` 并显示 `operation_id` 和 `QueryIndexProgress`；不新增后台重建策略。

### 8.3 Result actions

每个结果至少可执行：显示完整路径、定位到父目录、打开文件/目录。Search result row 必须携带可区分的 link/row identity；不同 Parent ID + Name links 即使共享 FileIdentity，也必须保持独立结果和独立 action target。定位和打开前可做轻量 identity/link 检查；删除、重命名或移动后的旧结果不能被伪装为当前 Healthy 结果。

`Blink.Core::FileIndex::Build/Save` 仅保留为原型/普通测试工具，不作为 Phase 1 产品搜索后备路径；IndexService 不可用时应明确显示不可用，而不是未经用户同意对全卷做 DFS 搜索。

Search result 只是一段可变化的定位信息，不是授权凭据；Locate/Open 前重新验证 identity/path，失败时显示“结果已变化/无法定位”。

## 9. File operation integration

所有 mutation 在 `Blink.exe` 普通用户进程中执行：

- New Folder：`CreateDirectoryModern`。
- Rename：`RenameFileModern`，提交前保留 identity，完成后刷新并验证新路径。
- Delete：普通 Delete 请求 Windows 系统 Recycle Bin；不能回收时明确失败并保留源文件。Shift+Delete 才能进入永久删除，并必须有明确确认。Blink 不建立自有回收站、垃圾目录或删除数据库。
- Same-volume Move：优先使用现代 rename 路径，保留 identity 语义。
- File Copy：后台执行，显示进度、取消、错误和部分文件结果。Phase0的`CopyFileAsync`/CopyFile2原型保留为诊断路径；1D Slice3首刀使用identity-checked handle读取、CREATE_NEW原子防覆盖及owned handle清理，特殊流/存储明确Unsupported，完整metadata兼容和冲突策略继续独立推进，依据见D-0007补充和Slice3计划。
- Cross-volume file Move：先完成 Copy 并确认成功，再执行源删除；任一步失败都不得静默丢失源文件。
- Directory Copy：Phase 1D 必须支持递归、可取消、进度、部分失败、Unicode/long path，并保留源数据。对 junction、symlink 和其它 reparse point 默认绝不 follow；只有能够可靠复制 reparse object 本身时才复制，否则明确返回 `Unsupported`。禁止为了“成功”而跟随 reparse target contents；实现前重新完成 API/循环安全审计。
- Directory Move：同卷使用 rename；跨卷先完成递归 Directory Copy，确认所有 required items 成功后再删除 source tree；任何部分失败都保留源树并明确报告。
- Conflict：File Copy/Move/Paste 必须让用户选择 Replace、Skip、Keep Both 或 Cancel；Directory-to-existing-directory 必须使用 Merge、Skip、Keep Both 或 Cancel，Merge 递归处理子项且不得先删除目标目录；Rename target-exists 至少使用 Rename Again 或 Cancel。禁止随机、静默改名或静默替换。

操作完成后刷新受影响目录，并由索引服务通过既有 USN 路径最终收敛；不直接写 BLK5，不通过 IPC 请求服务执行 Copy/Delete/Rename/Open。

交互映射：F2=Rename、Delete=系统回收站 Delete、Shift+Delete=确认后的永久 Delete、F5=Refresh、Ctrl+L=地址栏、Ctrl+T=新标签、Ctrl+W=关闭标签、Ctrl+C/X/V=系统剪贴板 Copy/Cut/Paste、Ctrl+Shift+N=New Folder；Alt+Left/Right/Up 在不与 WinUI 输入模型冲突时分别映射 Back/Forward/Up；双击/Enter 按前述目录/文件规则处理。

Clipboard/Drag & Drop 的 ownership 按方向区分：External→Blink 与 Blink→Blink 由 `FileOperationCoordinator` 拥有实际 operation；Blink→External 时 Blink 只发布标准 Windows filesystem transfer data，实际 operation 由 external target 拥有。Cut 在 external target 明确执行并确认 operation 前，Blink 不得自行删除 source。Blink 内部拖放仍复用同一 operation model；通常同卷为 Move、跨卷为 Copy，modifier key 遵循 Windows 预期。

Blink context menu 是自己的 command surface：文件/目录提供 Open、Cut、Copy、Rename、Delete、Properties（若 Properties 尚未产品化则显示明确策略）；空白处提供 Paste、New Folder、Refresh、View、Sort。禁止直接加载第三方 Explorer Shell Extension handler。

Hidden items 默认不显示；Phase 1 的 Blink-owned Show hidden items 只定义 hidden-items policy，不自动改变 protected operating-system items 的可见性。Protected operating-system items 默认继续隐藏，完整 protected-system-items policy 保持 Deferred；不依赖 Explorer 全局设置作为 correctness 条件。

## 10. Failure/degraded modes

| 场景 | UI 行为 | 禁止行为 |
| --- | --- | --- |
| Service 尚未启动 | 本地浏览立即可用；索引区显示连接中/不可用 | UI 线程等待服务启动 |
| Pipe 断开/服务停止 | 搜索请求失败并显示 `Unavailable`，后台可重连 | 无限重试、无限等待、冻结窗口 |
| `CatchingUp`/`Rebuilding` | 显示 operation/progress，保留可用的本地浏览 | 把中间状态显示成 Healthy |
| `RebuildRequired`/`Corrupted` | 禁止正常搜索或明确标为不可用，提供状态信息 | 静默搜索旧 BLK5 |
| 目录枚举失败 | 保留旧页面或显示错误页，可重试 | 用过期后台结果覆盖当前路径 |
| 文件被外部删除/重命名 | 操作/打开失败可见，刷新当前目录 | 依据旧路径误判 identity |
| Copy/Move/Delete 权限或占用失败 | 保留源数据，显示 Win32/HRESULT，允许重试 | 静默吞错或告诉用户已完成 |
| 窗口/标签关闭时仍有任务 | 取消或安全收束后台任务 | detached 线程继续访问已销毁 UI |

## 11. Performance requirements

Phase 1 的性能门槛首先是响应性和隔离，而不是重新优化 Phase 0.5 的 MFT/Search 核心：

- 主窗口激活、地址栏和基本控件可交互，不等待首次目录枚举、服务连接或属性读取。
- UI 线程不得调用同步 `EnumerateDirectory`、同步 IPC `Connect/Send`、CopyFile2 等可能阻塞的 API。
- 大目录列表使用 WinUI 虚拟化/增量提交；后台结果批量发布，避免每个文件触发一次 UI 重排。
- 搜索、进度和重连均在 worker 运行；一次请求必须可取消或由新请求取代。
- 文件操作进度按可观察节奏更新，避免每个字节/回调直接触发 UI 更新。
- 建立普通测试可测的 UI-thread blocking guard、旧请求丢弃 guard 和 service disconnect bounded-return guard。
- 不通过提高 IPC timeout、降低 correctness 要求或重新扫描全盘来掩盖性能问题。

## 12. Security boundaries

- 当前 Phase 0.5 安全基线保持：LocalSystem、`SERVICE_SID_TYPE_UNRESTRICTED`、`NT SERVICE\Blink.IndexService`、`SeImpersonatePrivilege`、已接受的精确 Pipe DACL、`PIPE_REJECT_REMOTE_CLIENTS`。
- Phase 1 不修改 service account、Service SID、RequiredPrivileges、Pipe ACL、impersonation、协议版本或服务 token 验证。
- IndexService 继续只处理状态、索引、进度、搜索和取消等已定义命令；不增加任意路径打开、Copy、Delete、Rename、Shell、Network 命令。
- Blink.exe 的文件 mutation 使用当前交互用户 token 和 Windows 文件权限；服务不会替用户绕过 ACL。
- 所有用户输入路径都先规范化/验证；UI 显示名称和路径使用 UTF-16/显式 UTF-8 日志规则，不把日志字符串当安全校验。
- FileIdentity/结果映射不以同名作为授权或对象身份依据。

只有触及上述已验收边界时，才需要重新进行针对性的 elevated security/IndexService re-evaluation；普通 UI 集成不重新证明 Phase 0.5 全套场景。

## 13. Implementation stages

### 1A — Window shell, state model, navigation and local folder view

**Input**：当前 `BlinkApp`、`FileTypes`/`EnumerateDirectory`、WinUI3 工程和 Phase 0 普通测试。

**Scope**：建立窗口布局（导航区、地址栏、列表、状态栏、标签占位）、`WindowState`/`TabState`/`NavigationModel`、后台目录枚举、名称/类型/大小/修改时间/基础图标、选择、排序、刷新、上级和直接路径导航；移除对 detached worker 的新增依赖。

1A 还冻结并验收本地化文案、Details baseline、目录双击、hard-link row identity、中文/长路径/空目录、错误页保留旧 committed page、外部关闭期间的任务收束和大目录响应性；不在本阶段引入 This PC、真实多标签或完整 mutation。

**Forbidden**：不接入新的服务协议，不调用全卷 MFT，不实现 Provider/AgentBridge，不实现完整文件 mutation，不改 FastIndex/BLK5/安全策略。

**Ordinary tests**：导航 history 分支、每标签独立状态、根目录/非法路径、请求代数丢弃旧结果、目录枚举取消/错误、列表字段映射和 UI-thread blocking guard。

**Manual UI acceptance**：普通用户启动后窗口可交互；在几个本地目录间直接输入、Up、Back、Forward、Refresh；大目录枚举期间拖动窗口/切换控件仍响应；停止 IndexService 后上述浏览仍正常。

**Done**：1A 的本地浏览闭环可用，后台任务有生命周期管理，后续标签/搜索/操作不需要再绕过状态模型。

**Final acceptance**：普通用户手工验收已完成；Back/Forward history 与分支截断、50k
大目录拖动/resize/scroll、残余延迟分类，以及大目录呈现期间关闭窗口均已记录并通过。
50k 仍保留短暂交互停顿这一明确的 accepted performance debt；不表示零延迟或完全平滑。

### 1B — Tabs, open and Explorer-like interactions

**Input**：1A 的窗口和导航状态。

**Status**：**IMPLEMENTED + ordinary validation PASS + manual UI PASS**。本节记录已接受的 Location/This PC/default-startup foundation；其余 Phase 1B 功能不因 Slice 1 提前开放。

**Slice 1 implemented**：`Location` 区分 `ThisPC` 与 `Folder`；`NavigationModel` 和 `TabState` 共享统一 Location/history；默认启动异步加载 Blink-native This PC；This PC 展示 Places 与当前可访问的固定/可移动卷；Address Bar、sidebar、Back/Forward/Up/Refresh 和 This PC item activation 汇入同一导航入口；This PC work 使用受管理的后台任务和 generation/lifetime gate，并复用现有 Folder browse/render pipeline 的 UI surface。

**Scope**：This PC/Folder Location 模型、默认启动 This PC、常用位置与本地卷、按需导航树、多标签与标签级历史/选择/列表/view、Details/List/Grid、Ctrl+T/W/L、双击/Enter、文件关联打开、标准选择和键盘快捷键（Ctrl+A、F2、F5、Delete、Shift+Delete、Ctrl+C/X/V、Ctrl+Shift+N、可行时 Alt+Left/Right/Up）、Blink-owned context menu、hidden/system items policy、folder watcher 的基础产品路径。1B 负责建立命令路由和明确反馈，实际 mutation 由 1D 的 coordinator 执行。

**Forbidden**：不实现 Shell Namespace、不引入第三方 Shell Extension handler、Preview/Thumbnail/MetadataHost、不把文件操作移到服务端；1A 已完成的单标签导航不因本次补充返工。

**Ordinary tests**：标签切换状态隔离、关闭活动标签选择规则、键盘命令映射、文件夹/文件打开分流、关闭窗口时任务取消和 stale callback 防护。

**Manual UI acceptance**：This PC→卷→目录、导航树按需展开；两个标签分别浏览不同目录，Back/Forward/selection/view 不串台；Details/List/Grid 切换不改变 Location/identity/history；双击目录、双击文件、Enter、Ctrl+A、F2、F5、Delete/Shift+Delete、Ctrl+L/T/W、剪贴板和 Blink context menu 有清晰反馈；在 1B 尚未接入 1D 时，mutation 命令必须显示未就绪/不可用，而不是伪造成功；IndexService 未运行时本地浏览仍可用。

**Done**：窗口行为足够承载后续 Search 和操作流程，且 Slice 1 的普通代码路径没有引入 UI 生命周期错误；Slice 1 manual acceptance 已完成并接受。

### 1B Slice 2 — Real multi-tabs and per-tab state isolation

**Status**：**IMPLEMENTED + ordinary validation PASS + manual UI PASS**。本 Slice 将真实多标签加入现有单一导航/呈现路径，不扩大 D-0027 Product Surface。

**Slice 2 implemented**：窗口级 app-private `TabWindowState` 持有有序 `TabRecord` 集合；每个标签拥有稳定、与 WinUI selected index 无关的 `TabId`，以及独立的 `TabState`、`BrowseState`、`BrowseController` 和 UI projection state。WinUI `TabView` 只负责呈现标签和发出新增/切换/关闭意图，不作为业务状态真相源。

- 启动创建一个 This PC 标签；Ctrl+T 或 TabView add button 创建并激活新的 This PC 标签，走已有异步导航入口。
- Ctrl+W 或标签关闭请求关闭被请求的标签；关闭活动标签优先选择右邻居，否则左邻居；最后一个标签关闭窗口。关闭标签先取消其 presentation/This PC work，并关闭其 BrowseController/TabState 生命周期。
- active presentation 统一从当前 `TabId` 读取路径、侧栏、内容、状态、按钮、selection 和 sort；切换标签只重新投影该标签已提交状态，不重新枚举文件系统。
- Folder/This PC 异步回调同时通过 `TabId`、标签本地 generation、TabState/BrowseState 请求门和 window lifetime gate；非活动标签可以继续准备数据，已关闭标签的结果必须丢弃。
- 每个标签的 Location/history、committed listing、selection、SortDescriptor、This PC rows 和 progressive presentation generation 相互隔离；不新增第二套 Browse/Navigation pipeline。

**Ordinary tests**：覆盖初始/新增标签、稳定 TabId、激活与状态保留、Location/history/selection/sort 隔离、活动标签关闭邻居规则、最后标签窗口关闭请求、每标签 execution state 隔离和关闭标签的异步状态失效；既有 Phase 1A/Phase 1B Slice 1 回归保持通过。

**Manual UI acceptance**：已完成 Ctrl+T、标签切换、双标签分别导航、Back/Forward/selection/sort 隔离、关闭活动/非活动标签和最后标签关闭窗口的人工验收；Slice 2 已接受。Phase 1B 仍不会因此提前完成。

**Forbidden**：本 Slice 不实现 Search UI/productization、FileOperationCoordinator、mutation、clipboard/drag-drop、navigation tree、watcher、Provider、Shell Namespace 或 Phase 1C/1D。

### 1C — Product IndexService and local search integration

**Input**：1B、现有 `Blink.Index.Contracts`、已存在有效 BLK5/Service acceptance baseline。

**Scope**：实现后台 `IndexServiceCoordinator`、连接/重连、按卷状态和进度面板、Search query/result mapping、`ThisPC`/All indexed local volumes 与 `Current Folder` scope、known-hit/miss correctness、结果 identity/path、Locate/Open；一个卷的 Rebuilding/Unavailable 不得冻结其它卷；将旧的本地 FileIndex Build/Search 原型按钮从主产品流程中移除或改为明确的非产品诊断入口。

**Forbidden**：不改 IPC ABI、Named Pipe ACL、Search timeout、FastIndex、BLK5；不重新执行完整 MFT acceptance；不新增 DFS 全卷搜索 fallback。

**Ordinary tests**：用可控 fake response 覆盖 Healthy/Loading/CatchingUp/Rebuilding/Corrupted/Unavailable；验证 request/operation ID、结果 identity 映射、服务断开 bounded return、known-hit/miss 和 stale result rejection。

**Manual UI acceptance**：使用已验收服务/有效 store 看到真实 Healthy，并分别验证 This PC/All indexed local volumes 与 Current Folder；停止服务后本地浏览仍可用且状态变为 Unavailable；单卷 Rebuilding 不冻结其它卷；服务恢复后可重连；Rebuilding/CatchingUp 不被显示成 Healthy；结果的 Open/Locate 重新验证 identity/path。

**Done**：IndexService 成为产品搜索唯一正常路径，UI 从连接失败到恢复没有冻结或静默降级。

### 1D — Basic user-permission file operations

**Input**：1B 的选择/命令状态和 1C 的 service-independent UI。

**Scope**：New Folder、Rename、系统回收站 Delete、确认后的 Shift+Delete、File/Directory Copy、同卷 Move、跨卷 File/Directory Move 的安全 copy-then-delete、冲突策略、系统剪贴板互操作、Drag & Drop、进度/取消/错误和操作后刷新。

**Forbidden**：不增加 service mutation 命令，不把回收站替换成 Blink 自有存储，绝不 follow junction/symlink/reparse point；不能可靠复制 reparse object 本身时必须明确 `Unsupported`，不实现未经 API 审计的跨卷目录递归 Move，不引入 Shell Namespace、第三方 handler 或任意提权。

**Ordinary tests**：临时目录中的 create/rename/delete/copy/move、identity 稳定性、copy cancel/partial result、权限/占用失败、源数据保留、操作完成后的列表刷新和索引最终一致性 oracle（不依赖 elevated service）。

**Manual UI acceptance**：在普通用户可写 fixture 中通过快捷键、context menu、剪贴板和拖放操作文件/目录；覆盖系统回收站与永久删除确认、File Copy/Move 的 Replace/Skip/Keep Both/Cancel、Directory-to-existing-directory 的 Merge/Skip/Keep Both/Cancel、Rename target-exists 的 Rename Again/Cancel、递归目录复制、同/跨卷移动、进度/取消/错误；reparse object 能可靠保留时才复制，否则明确 Unsupported，绝不 follow target contents；External → Blink 与 Blink → Blink 的实际 operation 必须由 `FileOperationCoordinator` 拥有，Blink → External 只发布标准 Windows filesystem transfer data，external target 拥有实际 operation，external Cut 未被 external target 明确执行前不得删除 source；中途停止 IndexService 仍能完成本地权限允许的操作；重新浏览后结果正确。

**Done**：基本操作不冻结、不静默丢数据，错误可解释，操作实现与 service 安全边界一致。

### 1E — Degraded-mode and Phase 1 final acceptance

**Input**：1A–1D 全部通过。

**Scope**：组合回归、服务 Stop/Kill/Restart 下的 UI 行为、目录操作与搜索状态并发、窗口关闭/重连、日志和最终用户可见错误；形成 Phase 1 report。

1E 还覆盖可移动卷插入/移除和 mount-point 变化、当前目录外部 mutation convergence、operations/search 并发、Recycle Bin unavailable、权限/占用/长路径/Unicode、无 UI freeze、无 silent destructive fallback，并至少准备一个安全 removable-volume case（环境可用时）。

#### Phase 1E Code Hygiene Closure

这是 review/audit slice，不是新增产品功能，也不是重写。Phase 1 完成前必须完成最小整理和 ownership closure，并证明：

- no newly introduced detached workers；active product flow 中不存在未管理生命周期的 detached worker。
- navigation、search、file-operation 各自只有一个 authoritative product path。
- address bar、double click、navigation tree、Search Locate 最终都汇入统一 `NavigationModel`。
- shortcut、context menu、clipboard、Drag & Drop 最终都汇入统一 command / `FileOperationCoordinator` 边界。
- WinUI controls 不作为 authoritative business state；状态模型和业务事实不依赖具体控件。
- 被正式产品实现替代的 Phase 0 prototype path 必须删除，或明确隔离为 diagnostic/non-product path。
- 不保留重复 Copy/Search/Navigation 实现；no silent fallback；no swallowed correctness/security errors。
- resource/thread/task ownership 明确，关闭、取消、断连和失败路径均有可验证的收束语义。
- TODO/FIXME 必须分类为 `Phase 1 blocker` 或 `explicit deferred debt`。
- 不因未来可能需要而预建空 Registry/Factory/Manager/SDK，不进行无关大重构。

该 closure 不扩大已冻结的 Product Surface；任何新增功能族都必须另行提出并经过新的范围决策。

**Forbidden**：不借机优化 Initial MFT、Search algorithm 或 BLK5，不扩展 Provider/AgentBridge，不重复 Phase 0.5 全套高权限验收。

**Ordinary tests**：所有新增普通测试、Phase 0/Phase05 普通回归、UI state/operation tests、disconnect/reconnect bounded wait、UTF-8 path/name smoke。

**Manual UI acceptance**：按第 3 节完整走一遍 Healthy、Unavailable、CatchingUp/Rebuilding 和普通文件操作场景；服务恢复后搜索恢复；没有 UI freeze、假 Healthy 或错误的 identity 复用。

**Done**：每日使用闭环满足 Definition of Done；Phase 0.5 边界无回归；剩余事项均是已列明的非 Phase 1 blocker。

## 14. Ordinary regression tests

新增测试必须优先保持普通权限、可控目录和可重复性：

- `NavigationModel`：规范化路径、history、branch、Up、Refresh、per-tab isolation。
- `FolderViewController`：后台枚举、取消、旧请求结果丢弃、目录消失、中文路径/文件名。
- `IndexServiceCoordinator`：fake IPC response、状态映射、operation ID、bounded disconnect、known-hit/miss、identity/path mapping。
- `FileOperationCoordinator`：Create/Rename/Delete/Copy/Move/New Folder、取消、部分失败、源文件保留和刷新。
- UI responsiveness：故意延迟目录/服务/Copy worker，验证 UI dispatcher 不同步等待。
- `ThisPC`/drive lifecycle：卷插拔、不可用 Location、历史返回和 per-volume index state isolation。
- `FileSystemWatcher`/ReadDirectoryChangesExW 产品语义：外部 create/delete/rename/move 的 debounce、reconcile、selection 保留和 stale callback 防护。
- `FileOperationCoordinator`：system clipboard format、conflict policy、reparse point、system recycle/unavailable、directory copy/move、drag/drop input mapping。
- Existing regression：继续运行现有 `Blink.Phase0.Tests`、`Blink.Phase05.Tests` 普通入口和 `Blink.Transfer.Tests`；不把 elevated acceptance 作为每次 Phase 1 UI 迭代的默认测试。

测试 fixture 必须放在项目已约定的 `.blink-data`/临时测试目录规则内，显式 UTF-8 写日志/JSON/CSV；不得把真实用户目录当作 mutation fixture。

## 15. Manual acceptance

Phase 1 的人工验收分两层：

### 15.1 普通用户 UI acceptance

在不提权的 Blink.exe 中执行第 3 节场景，至少覆盖：

- 本地导航和每标签历史。
- This PC、常用位置、驱动器生命周期、按需导航树和多种 view mode。
- 中文路径/名称、长名称、空目录、不可访问目录和文件已被外部删除。
- Healthy 搜索、结果定位/打开、known-hit/miss。
- Service stop/kill/restart 时搜索状态与本地浏览隔离。
- 用户权限下的基本文件操作及取消/错误。

### 15.2 Phase 0.5 boundary re-evaluation rule

若 Phase 1 只修改 Blink.exe UI/普通权限 Core/Transfer adapter，则不重跑 full elevated acceptance。只有修改 IndexService security/SCM/pipe ACL/privileged USN/BLK5 semantics，或普通回归发现协议边界破坏，才开针对性 elevated re-evaluation，并明确记录触发原因。

## 16. Definition of Done

Phase 1 只有同时满足以下条件才可标记完成：

- 默认启动 This PC；本地磁盘和目录正常浏览；导航树、标签、Details/List/Grid 和标准选择/键盘行为完成，且普通用户可连续使用。
- 本地浏览、地址导航、Up/Back/Forward/Refresh 不依赖 IndexService。
- 当前目录外部变化能安全收敛；搜索通过现有 IndexService IPC，支持 This PC/All indexed local volumes 与 Current Folder scope，真实 per-volume 状态可见，结果可定位/打开，known-hit/miss 通过。
- New Folder、Rename、File/Directory Copy/Move、system Recycle Bin Delete、Shift+Delete confirmation 在用户权限下安全完成；冲突、剪贴板、Drag & Drop、context menu、进度/取消/错误可见且不冻结 UI。
- 服务断开、重启、重建、损坏/不可用状态不会冻结或假装 Healthy；本地浏览仍可用。
- UI、目录枚举、IPC、文件操作没有同步阻塞 UI 线程；后台任务可取消、可收束、不会访问已销毁窗口。
- Phase 1E Code Hygiene Closure 已通过：没有新引入或未管理的 detached worker；navigation/search/file-operation 各有唯一 authoritative product path；所有入口汇入统一 NavigationModel 或 `FileOperationCoordinator` 边界；WinUI controls 不承载 authoritative business state；Phase 0 prototype path 已删除或隔离；没有重复实现、silent fallback 或被吞掉的 correctness/security error；resource/thread/task ownership 和 TODO/FIXME 分类明确。
- Debug/Release build、普通单元/集成/回归测试通过；UTF-8 中文路径 smoke 通过。
- 没有为 Phase 1 引入新的特权文件操作、IPC 命令、Pipe 权限或未经批准的自动 fallback。
- 文档中的 Phase 1 状态、测试证据和剩余技术债已更新。

## 17. Explicit technical debt deferred after Phase 1

以下事项明确不阻塞 Phase 1，但必须保留为后续工作项：

- Provider/SFTP/SMB/WebDAV/Cloud/Archive/MTP、AgentBridge、MCP、插件生态。
- MetadataHost 隔离、完整文件元数据、预览、缩略图和 Shell Namespace 兼容层。
- Search 的统一 inverted/trigram architecture；当前 extension derived index 和既有 FastIndex 继续作为已验收实现。
- BLK5 从 versioned prototype checkpoint format 演进为最终数据库/磁盘 ABI。
- Initial MFT 和百万级查询的更深层性能/并发重构。
- LocalSystem 到 virtual service account 或 restricted Service SID 的后续实验；当前已接受的安全基线不变。
- 安装器、签名、发布、自动升级、恢复和正式生产数据迁移策略。
- 高级主题、动画、无障碍深度优化和完整视觉设计系统。
- 真实 Journal reset/USN wraparound 的专门长期测试，以及低频一致性 scrub 的产品可视化。
- Recycle Bin 浏览/恢复/清空 UI、Open With 完整 UI、Favorites/Pinned 自定义、Undo history UI、完整系统保护项编辑策略。
- Archive browsing、SMB/SFTP/WebDAV/Cloud/MTP、Provider 产品化、Shell Namespace 和第三方 Shell context-menu handlers。
- 公开 Plugin SDK、ExtensionHost、AgentBridge/MCP、安装/签名/市场/分发、multi-window、session restore（若后续仍未纳入）。
- Preview pane、Thumbnail pipeline、完整 Metadata/Property Handler、深度 accessibility polish、advanced themes/animations。

## Recommended execution order

推荐从 1A 开始。原因是当前 UI 仍是单个 `main.cpp` 中的最小代码构建原型，缺少 per-tab state、可取消后台任务和稳定的 UI/controller 边界；没有这个基础，Search、File Operation 和 Service failure 场景容易重新引入 UI freeze 或 stale result。

第一阶段只依赖已经存在且已普通权限验证的 `FileTypes`/`EnumerateDirectory` 和 WinUI 控件，不触碰 Phase 0.5 的高权限核心。1A 完成后再顺序进入 1B、1C、1D，最后用 1E 做组合验收。

当前不存在阻止“制定或开始 1A”的 Phase 0.5 blocker。需要在 1A 开始时优先处理的工程风险是：将现有 detached worker 收束为有生命周期管理的后台任务，并把 `main.cpp` 的直接控件操作迁移到可测试状态边界；这是 Phase 1A 的实施前置，不是对已接受 Phase 0.5 核心的否定。
## Phase 1B Slice 3 — Navigation Tree Productization

Slice 3 implements the first Blink-native navigation tree as a window-owned,
app-private model. The initial tree contains an explicit `ThisPC` node, the
enumerated Places, and accessible local volume roots. Direct folder children
are loaded lazily and asynchronously on expansion; no recursive initial scan,
Shell Namespace model, or silent full-volume fallback is introduced.

Tree node identity is an app-private stable node ID associated with a
`Location`, not a display name or a WinUI `TreeViewNode`. The model filters
files and hidden/system entries, represents reparse-point directories as
non-expandable nodes, and never follows reparse targets. Expansion and
ancestor reveal work are generation-gated, owned by the window, and cancelled
or dropped on close/newer navigation. A shared window tree keeps expansion
state across tabs; selection and bounded ancestor reveal follow the active
tab's committed `Location`.

Tree item invocation, address-bar navigation, Back, Forward, Up, Refresh,
double-click directory activation, and This PC all enter the existing
`NavigationModel`/`BrowseController` route. Tree expansion is deliberately
separate from navigation. Watchers, recursive prefetch, and removable-volume
notification remain deferred.

### Slice 3 navigation-pane semantic correction

The left navigation pane is intentionally two projections of the same committed
This PC facts, not one homogeneous hierarchy:

- The upper quick/common-location section presents Home, Desktop, Downloads, and
  Documents as non-hierarchical actions. They have no tree children or expansion
  semantics and route to the existing `NavigateToLocation()` path.
- The lower filesystem section presents ThisPC/local volume roots and lazily loaded
  direct folder children through `NavigationTreeModel`. It owns no duplicate volume
  enumeration and excludes quick places from tree expansion/reveal.

Exact known-folder locations select the quick item without forcing a drive-tree
reveal. Descendants of a known folder and ordinary filesystem locations clear the
quick selection and use the existing bounded ancestor reveal. Both projections are
resynchronized from the active tab's committed `Location`; neither WinUI selection
is authoritative.

## Phase 1B Slice 4 — File activation and basic keyboard commands

**Scope**：双击/Enter 激活目录或单个文件；Ctrl+L、列表内 Ctrl+A、F5、Alt+Left/Right/Up。目录及 This PC 激活复用既有导航路径；文件激活从 committed RowIdentity 获取 FileEntry，后台重新检查路径对应的 FileIdentity，再交给当前用户的 Windows 文件关联。

**Ownership**：每个 TabRecord 拥有 FileActivationController。关联派发由普通用户 `Blink.FileActivationHost.exe` 执行，该进程在派发前再次验证完整 FileIdentity；Shell 扩展不会进入 Blink 主进程。控制器以 suspended 启动 helper、确认请求仍有效后恢复执行，取消/关闭时只终止拥有的 helper，派发等待有 30 秒上限。导航和关闭使旧结果失效；已经交给 Windows 的目标应用不由 Blink 终止。Ctrl+A/Enter 以文件列表为作用域，地址编辑器保留自身的文本选择和 Enter 导航语义；Enter 激活已选择的焦点行或首个选中行，批量打开延期。

**Current status (2026-10-13)**：D:\DEV\C++ 工具链已按用户授权恢复；Debug/Release 构建和普通回归通过，包含真实 helper 的未关联错误返回。既有 Release UI 生命周期门禁通过，共 64 个动作。下方 Slice 4 新动作及实际关联应用打开的人工验收仍待完成。**不得将该状态写为 ACCEPTED / COMPLETE。**

**Ordinary validation**：真实可控 Unicode fixture 的 FileIdentity、同名替换、删除和独立 hard-link path；Windows helper argv 的空格、Unicode、引号和末尾反斜杠 round-trip；真实控制器/helper 以专用未关联扩展名验证完整路径与 `ERROR_NO_ASSOCIATION` 返回；假的关联启动函数验证后台执行、失败反馈、逻辑取消、关闭不 join worker、过期结果丢弃；全选保留独立 RowIdentity 和导航状态。窗口操作、helper 关闭时清理和实际关联应用派发仍须由对应 UI 验收证明。

**Manual gate**：[Slice 4 UI 清单](docs/PHASE1B_SLICE4_MANUAL_UI_CHECKLIST.md)。Phase 1B views 在下述 Slice 5 推进；watcher、hidden policy、context-menu/未就绪 mutation 命令路由等在后续 Slice 单独推进；Phase 1C/1D 保持未开始。


## Phase 1B Slice 5 — Folder views and shared sort commands

**Scope**：Details、紧凑 List、基础图标 Grid 三种视图；每标签独立 ViewMode；所有视图共享 Name/Type/Size/Modified 和升降序入口，Details 的四列均支持排序。目录在两个方向中始终优先，Type 按当前显示的本地化类型标签排序，相同类型再按名称。

**Ownership**：ViewMode 属于 app-private TabState，控件不是事实来源。三种视图共享同一 tab-owned row_items_source、RowIdentity、选择和浏览/导航 pipeline。FolderViewPresentation 只缓存 UI 模板与虚拟化面板；Details/List 使用 ItemsStackPanel，Grid 使用 ItemsWrapGrid。切换只替换控件并恢复选择，不重启枚举/投影，不恢复导航 UI 或覆盖地址草稿。渐进呈现期间保留 selection suppression，final batch 恢复到当前控件；延迟焦点恢复受 view generation/tab/window gate 约束。

**Current status (2026-10-14)**：IMPLEMENTED / NATIVE PASS / PRODUCTION VIEW UI PASS / MANUAL UI PENDING。Debug/Release 构建、各 330 项普通检查、各 35 项生产 UI 检查通过，两个测试窗口正常退出且未强制终止；证据见 `.build/phase1b-slice5/native-validation.json`。普通回归覆盖视图默认值、pending/history/selection/hardlinks 保持、标签隔离，以及 Type 正反向和稳定 tie；生产 UI 自动化与人工检查各自记录实际证据，不互相替代。

**Manual gate**：[Slice 5 UI 清单](docs/PHASE1B_SLICE5_MANUAL_UI_CHECKLIST.md)。Slice 4 实际关联应用人工门禁保持待验收；Phase 1B hidden policy、watcher、context-menu/未就绪 mutation 路由仍待后续 slice，Phase 1C/1D 尚未开始。

## Phase 1B Slice 6 — Shared hidden-items policy

**Scope**：全窗口“显示隐藏项目”开关，作用于所有已有/新增标签及导航树。默认隐藏 Hidden 与 System 属性项目；开启后显示 Hidden，但所有 System 属性项目继续隐藏。本 Slice 采用保守的 System 排除策略，完整 protected-system-items 设置保持 Deferred，不读写 Explorer 全局设置。开关为本次窗口会话状态，不加入持久化设置。

**Ownership**：ItemVisibilityPolicy 为 app-private 纯属性筛选规则，由 TabWindowState 拥有，TabState 保存已应用策略。Committed DirectoryListing 保留原始枚举事实；可见快照、条目计数、空目录状态和选择/激活入口共同遵守策略。切换不修改 Location/history、pending browse、view/sort 或地址草稿，不重新枚举文件区；保留可见选择并移除被隐藏项及其焦点。已在途枚举以提交时的当前策略呈现。每标签旧投影被取消并受 generation gate 约束；旧树任务取消，树按新策略重建惰性缓存并重新定位当前目录，其它手动展开状态可以重置。

**Current status (2026-10-14)**：IMPLEMENTED / NATIVE PASS / PRODUCTION VISIBILITY UI PASS / MANUAL UI PENDING。Debug/Release 构建、各 349 项普通检查和各 26 项生产隐藏项目 UI 检查通过；Release 原视图 35 项、窗口消息滚轮 15 项通过，测试窗口正常退出 0 且无强制终止。普通回归覆盖选择、硬链接、raw/visible 分离、真实系统属性、空目录、在途浏览和全窗口策略传播；UI 脚本 `scripts/run-blink-visibility-smoke.ps1` 使用 609 条目受控真实属性 fixture。证据见 PROGRESS 与 `.build/phase1b-slice6/native-validation.json`。

**Manual gate**：[Slice 6 UI 清单](docs/PHASE1B_SLICE6_MANUAL_UI_CHECKLIST.md)。Slice 4/5 的原人工门禁保持独立。Context-menu/未就绪 mutation 命令路由在下述 Slice 7 推进，folder watcher 留待后续 1B slice；Phase 1C/1D 保持未开始。

## Phase 1B Slice 7 — Context menus and unavailable command routing

**Scope**：Blink 自有文件区和空白处 MenuFlyout；Open、Refresh、View、Sort 复用既有路径。Cut/Copy/Rename/Delete/Properties、背景 Paste/New Folder 及 F2、Delete/Shift+Delete、Ctrl+C/X/V、Ctrl+Shift+N 汇入同一命令策略，明确报告暂未开放。Menu 键与 Shift+F10 支持键盘打开菜单；空目录保留焦点入口。This PC 禁用文件系统专属命令及排序。

**Ownership**：菜单只是 UI 意图，FolderCommandPolicy 读取 committed Location、browse/projection readiness 和有效选择；不建立文件系统或选择的第二真相源。右键未选中行替换选择，已选中行保留多选；背景保留选择。菜单捕获稳定 TabId、Location、presentation/view generation，点击时重新校验，拒绝过期页面与不再选中的打开目标。窗口关闭及 ContextCanceled 清理菜单；菜单回调使用弱 app 引用，文本编辑快捷键不被文件区命令接管。

**Boundary**：不发布系统剪贴板数据、不实现 mutation 或 Properties 产品化、不加载第三方 Shell handler，不推进 Phase 1C/1D。Folder watcher 在下述 Slice 8 推进。

**Validation**：普通策略回归新增 22 项，生产菜单脚本为 `scripts/run-blink-context-menu-smoke.ps1`，通过受控窗口按键消息与 UI Automation 检查菜单、未开放反馈、选择、地址草稿、视图排序、刷新及 stale invocation。当前原生构建、检查与源码摘要以 PROGRESS 和 `.build/phase1b-slice7/native-validation.json` 为准。

**Manual gate**：[Slice 7 UI 清单](docs/PHASE1B_SLICE7_MANUAL_UI_CHECKLIST.md)。鼠标右键、触摸、系统快捷键及标题栏空白实物拖动独立验收；原 Slice 4/5/6 门禁保留，不写为 ACCEPTED / COMPLETE。

## Phase 1B Slice 8 — Local folder change notifications

**Scope**：每个目录标签独立监听当前目录的直接子项，外部新增、重命名、删除、属性/大小/修改时间变化合并后重新枚举。200 ms 静默期与 1 s 连续变化上限；browse/projection 忙时保留 dirty，新变化留待下一轮。初次 Ready 再对账一次，覆盖监听建立与初次枚举之间的间隙。This PC 不监听。

**Ownership**：TabRecord 拥有 FolderWatcher/FolderChangeState，UI 回调检查稳定 TabId、路径和 generation；A→B→A 不复用旧订阅。每订阅最多一个待派发提示，timer 仅在 dirty 时运行；空闲 coroutine 事件等待不占用常驻 worker。复用 TabState.Refresh/BrowseController，自动刷新保留地址草稿、历史、视图/排序与仍有效的 RowIdentity 选择，非活动标签不改写当前状态栏。关闭标签/窗口使 token 失效并发取消信号，后台 CancelIoEx 后等待真实完成再释放存储；导航失败恢复原 committed 目录监听。

**Boundary**：不根据通知直接编辑行/索引，不新增 privileged API、轮询或旧 API fallback。监听不可用时明确提示并保留手动 Refresh/重试。目录树缓存的变化对账、递归监听、当前目录自身重定位、Provider、Search 与 mutation 不属于本刀。

**Validation**：开发计划见 [Slice 8 计划](docs/PHASE1B_SLICE8_DEVELOPMENT_PLAN.md)，当前构建、普通检查及源码/产物摘要见 PROGRESS 和 `.build/phase1b-slice8/native-validation.json`。覆盖合并、忙时延迟、generation、真实 Unicode 目录通知、取消、非递归边界、标签/窗口关闭；不以普通测试宣称生产 UI 体验通过。

**Manual gate**：按用户要求暂缓。后续独立检查三视图自动更新、地址草稿/历史/选择、快速导航、多标签、监听错误、大目录连续变化和关闭体验；原 Slice 4/5/6/7 门禁保留，不标为 ACCEPTED / COMPLETE。1C 已按后续 slice 推进开发，1D 尚未开始。


## Phase 1C Slice 1 — Service status

独立状态、后台 coordinator 和 presentation 接入选定本地卷状态、全局进度、异步重试与窗口生命周期。协议进度未携带卷根，Busy 保持未确认；每轮只查选定卷，避免服务单活动索引反复加载。Debug/Release 构建及各 447 项普通检查通过，人工验收暂缓，详见 PROGRESS 和 [开发计划](docs/PHASE1C_SLICE1_DEVELOPMENT_PLAN.md)。Search/Locate/Open 留待后续 1C slice，1C 不标为完成。


## Phase 1C Slice 2 — Search backend

SearchState / SearchCoordinator 实现独立 query/generation、只读后台请求、范围和 link identity 映射、逐卷错误及仅重试同一次查询的未完成卷。Debug/Release 构建及各 487 项普通检查通过；UI 和 Open/Locate 尚未接线，Phase 1C 不标完成。见 [Slice 2 计划](docs/PHASE1C_SLICE2_DEVELOPMENT_PLAN.md) 和 [后续 Slice 3 计划](docs/PHASE1C_SLICE3_DEVELOPMENT_PLAN.md)。已有 IPC 不提供 exact 开关，exact 产品语义仍需在冻结 ABI 范围内解决，不能用截断结果伪装完整查询。


## Phase1C Slice3 — Search presentation and result actions

按[计划](docs/PHASE1C_SLICE3_DEVELOPMENT_PLAN.md)实现每标签搜索、窗口请求队列、独立虚拟化/分批presentation、CurrentFolder真实scope身份、Open/Locate重验与精确行选择；最终Debug/Release各526项普通检查通过。人工UI/真实服务清单仍待执行，不能据此升级Phase1C验收。


## Phase1D Slice21 — Directory move merge (2026-11-08)

同盘和跨盘目录Move Merge已复用目录计划、原文件操作与唯一队列接入生产冲突菜单。内部文件支持Replace/Skip/KeepBoth/Cancel；跳过项保留来源，只移除实际移空目录，取消/失败保留前缀，已有变更的树只Skip/Cancel续尾。目录标签只按实际移除目录及真实目标跟随。此前各slice的未开始/待做描述保留为历史，不代表当前状态。

Debug/Release完整构建及各1516 PASS / 0 FAIL / 4权限SKIP，新增59项，235输入/368资源一致；8项自有生产窗口检查通过并发布默认Release。标签跟随本轮无GUI组合验收，人工体验暂缓，UI外观等用户提供提示词。见[Slice21记录](docs/PHASE1D_SLICE21_HANDOFF.md)。clipboard外部反馈/剪切外观、搜索Busy/首屏和组合场景仍待续，再推进SFTP/远端；整项目与Phase1不标完成。


## Explorer UI compatibility — Stage A and one P0 slice (2026-11-08)

用户任务书见[原文](docs/EXPLORER_UI_MASTER_PROMPT.md)。先以e497600单独提交阶段A的UI树、210条菜单台账、差距与交互矩阵，再仅实施顶部查看/显示：已有Grid/List/Details和隐藏项复用原命令与状态，排序暂保留。完整命令栏、面包屑、F2选区、其他右键/焦点场景分别后续推进，不把P1/P2全部并进一次提交。

235输入/369资源，Debug和Release完整构建及各1516 PASS/0 FAIL/4权限SKIP通过，15项自有窗口检查全部通过，默认Release已发布并打开。发布、清理和具体结果见[本轮交接](docs/EXPLORER_VIEW_MENU_HANDOFF.md)。当前主机25H2；Blink报告DPI96，Explorer参考DPI120，均只证明限定动作/观察。24H2和完整主题/DPI、人工验收仍NOT RUN。不能凭本切片升级整个P0、Phase1或项目完成状态。
