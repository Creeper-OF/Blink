# Blink Architecture Decisions

## D-0032：位置跟随默认范围与显式高级范围

2026-11-08，按用户请求，Tab默认FollowLocation：ThisPC解析全盘，文件夹解析所在整个本地盘。高级选定范围优先，按标签独立，切回FollowLocation恢复；只在明确提交时查询，重试保持已提交条件，提交同步读最新控件值。未解析的位置意图和无效显式范围明确失败，不暗中扩大为全盘。

指定目录关闭子目录时使用只读SearchDirectChildren=7，复用原帧字段及version1，原命令1–6保持。服务沿用原SearchRequest/FastIndex，在结果cap前匹配直接父目录；不扩展BLK5，也不引入第二套查询或本地防护平台。产品UI、原生和真实服务验证分别记录，不互相冒充。

## D-0031：搜索按卷驻留并单独验证交互性能

2026-11-08，用户明确授权本轮搜索性能专项，覆盖此前延后查询算法的计划限制。已初始化卷的FastIndex按卷根驻留，单个Load/Build/CatchUp操作槽保持，scheduler轮转各驻留卷；查询固定对应索引指针，别卷操作不阻塞已加载卷。名称casefold仅作内存派生结构，BLK5与只读IPC不变；有限limit沿用既有候选顺序，过滤后足够结果即停止路径准备，不引入新的排序Top-K承诺。

功能回归和性能是独立门槛。采用同一基准程序对照服务IPC；UI首屏、受控冷启动与Everything同机比较需要各自证据。常驻卷与名称缓存的内存代价必须记录。同卷增量/保存时Busy仍属于后续性能债，不用增加等待预算冒充提速。本轮无额外本地防护机制。

## D-0030：目录复制合并在原任务内逐项解决冲突

2026-11-06：目录Copy的Merge保留目标根、已有子目录及其独有条目，双方均为目录时递归合并，不先清空目标。既有目录的基础元数据不被来源覆盖，系统因新增子项产生的自然时间变化不回退。

- 内部文件同名时，既有操作worker等待Replace、Skip、KeepBoth或Cancel；UI只提交当前冲突的选择，任务继续当前项，已完成的前缀不重做。目录/文件类型不匹配只允许Skip、KeepBoth或Cancel。
- 取消、关闭窗口和界面发布失败都结束等待；保留已经复制的文件，并报告复制/新建/跳过数量与实际失败路径。部分失败后仅允许跳过当前树继续外层队列或取消，禁止整树自动重试。
- 复用原目录计划、文件复制、Replace和队列。目录Move的Merge仍待完成，不把本次Copy实现描述为移动合并已完成；不新增本地防护平台或事务机制。

## D-0029：普通文件覆盖采用显式目标名称替换

2026-11-06：推进本地文件管理的Replace，不把早期Slice4对原子expected-target-FileID协议的研究条件扩大为普通文件覆盖的永久门槛。

- Replace必须由用户显式选择，授权替换所选目标父目录中的同名文件。来源和目标父目录继续按既有FileIdentity绑定；检测到目标类型、只读/占用或已观察到的身份变化时失败，不静默删除再复制。
- Copy先在目标目录建立独占临时兄弟文件，沿用现有复制实现完成默认/命名数据流及基础元数据，再用一次普通replace-if-exists相对句柄rename提交。提交前取消或失败保留旧目标，清理本次临时文件；清理失败报告真实残留路径。
- 同卷Move复用现有相对句柄rename并只在显式Replace时设替换标志，保留来源身份。跨盘文件Move仍为完整复制提交后移除来源，来源移除失败保留新副本并报告部分完成；不承诺整个Move为事务。
- Windows rename不接收预期目标FileID，无法将“提交前检查”描述成对任意并发外部名称变更的原子比较交换。当前产品采用上述名称替换语义；不引入TxF、oplock、POSIX覆盖、只读覆盖标志或旧目标先删除的回退。
- 目录Replace不开放，目录合并在后续独立完成。普通文件Replace的同名对话框已接线，默认仍为取消；实际WinUI/Explorer交互继续按用户要求暂缓验收。


## D-0028：普通用户文件关联派发的进程隔离

> **Status: IMPLEMENTED / NATIVE PASS / MANUAL UI PENDING (2026-10-13)**

该决定落实 D-0027 已冻结的文件关联打开与第三方代码隔离要求。微软说明 `ShellExecuteExW` 可以调用第三方 Shell/COM verb handler；仅移到 Blink 的后台线程不能隔离 handler 的进程崩溃。因此，系统 `open` 派发在普通用户 `Blink.FileActivationHost.exe` 中执行，UI 进程保持纯任务协调和状态发布。

FileActivationController 按标签拥有请求、代数和 helper 生命周期；主进程后台检查完整 FileIdentity，helper 在派发前再次检查 path/identity。helper 使用 STA COM、`SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI`，保留 Windows 安全提示，返回派发结果。父进程只等待 helper 的有界结果，不等待目标应用就绪或退出；取消/关闭只终止自己的 helper，已经启动的应用继续运行。超时按不确定结果处理，不自动重发。

该 app-private 边界不增加 IndexService 权限或协议命令，不提供第三方插件加载、公开 ABI、Shell Namespace 导航、任意提权或安装服务。WinRT Launcher 的 ASTA 调用要求与可执行文件限制不能覆盖本阶段的一般桌面关联打开语义。

工具链已恢复；Debug/Release 构建和普通回归通过，真实控制器/helper 的未关联 fixture 返回 `ERROR_NO_ASSOCIATION`。helper 使用当前用户 ProgID 查询并禁用 Unknown 回退，避免将未关联错误变成等待打开方式选择的请求。既有生产 UI 生命周期门禁通过；实际关联应用打开和新增键盘行为的人工验收仍待完成。元数据检查与系统派发之间不声明原子文件系统事务。

依据：[ShellExecuteExW 的扩展委托说明](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw)、[SHELLEXECUTEINFO 标志](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfoa)、[Launcher 限制](https://learn.microsoft.com/en-us/uwp/api/windows.system.launcher.launchfileasync?view=winrt-26100)。

## D-0015：Phase 0.5 真实性验收需要独立的高权限执行条件

2026-08-10 的 Codex 本地验收尝试确认当前桌面进程是 Medium integrity、非管理员令牌。`install-index-service.ps1` 在 `#requires -RunAsAdministrator` 处停止；直接 SCM 创建返回 `OpenSCManager FAILED 5 (Access is denied)`，该 Codex 进程没有注册服务。LocalService 与 LocalSystem 的卷访问、USN Query、MFT Enum、USN Read 不能从这次非管理员尝试推断。随后单独的人工管理员运行记录在 D-0020。

在具备明确管理员执行条件前，Phase 0.5 的最终状态为 `NOT READY`，Phase 1 停止。下一次验收必须先完成 LocalService，再显式完成 LocalSystem fallback，并记录每个 API 的真实错误或成功结果。

## D-0016：开发阶段与正式产品的数据位置 [SUPERSEDED]

> **Status: SUPERSEDED by D-0021 (Install-volume-following DataRoot & Per-Volume Index Architecture)**

旧决策提议正式产品 Service/shared index 使用 `%ProgramData%\Blink\`，用户配置使用 `%LocalAppData%\Blink\`。该旧路线已由 **D-0021** 废弃。按 **D-0021** 规定，大容量数据默认跟随安装卷；只有当 Blink 安装在非系统卷时，才禁止因 Windows 默认目录惯例静默将大容量数据写入系统卷。如果 Blink 本身安装在系统卷，则 DataRoot 按“安装卷跟随”原则正常位于该卷。本条目仅保留历史决策说明，不再重复可能与 **D-0021** 冲突的规则。开发阶段继续保留在 `D:\Arduino\src\Blink_文件管理器\.blink-data`。

## D-0017：Authenticated Users 管道 ACL 是 Phase 1 前的硬化条件 [SUPERSEDED by D-0023]

本条记录的是安全闭合前的历史状态：当时服务使用 `PIPE_REJECT_REMOTE_CLIENTS`、客户端 PID/token 检查和只读索引命令边界，但 DACL 仍允许 `Authenticated Users` 读写。它不能直接执行 Copy/Delete/Rename/Arbitrary Open/Network/Shell 操作，但可扩大本地资源消耗面；当时分类为 `Phase 1 Blocker / Release Hardening`。该状态已由 D-0023 的单用户精确 DACL 和 Run `20260811-233345` 的真实运行证据取代。

## D-0018：BLK5 的定位保持不变

BLK5 继续定义为 `Versioned Prototype Checkpoint Format`，本轮只验证 Save、Load、Validation、Recovery、Performance。没有因真实性验收未执行而引入 B+Tree、LSM、自定义数据库引擎或最终磁盘 ABI。

## D-0019：高权限验收采用人工管理员执行 + Codex 结构化解析

当前 Codex 本地执行环境不能获得 High Integrity Administrator Token，因此不再尝试自提权或重复普通权限验收。`scripts\run-phase05-elevated-acceptance.ps1` 是唯一统一入口：它在任何构建、服务或测试动作前检查 Administrator/High Integrity，不满足即退出；满足后由人工管理员 PowerShell 完成 LocalService-first、显式 LocalSystem 选择、真实 D: 验收和清理，并把结果写入 `.blink-data\acceptance\phase05-results.json`、CSV、日志和失败日志。

服务 telemetry 只在安装命令带有 acceptance telemetry root 时写入验收指标，不改变 Blink IPC 的生产协议或索引数据模型。Windows 重启仍由人工步骤文件单独执行，脚本不会自动重启系统。

## D-0020：管理员验收 IPC 握手失败必须单独诊断

2026-08-10 的第一次人工管理员运行曾在 IPC 身份握手阶段遇到 `ImpersonateNamedPipeClient`/`OpenProcess` 的 Win32 error 5；补充 Named Pipe impersonation QoS 后，管理员运行已经成功越过 IPC。LocalService 随后真实打开了卷句柄，但在 `FSCTL_QUERY_USN_JOURNAL` 返回 Win32 error 5（`ERROR_ACCESS_DENIED`）。用户明确输入 `LOCAL_SYSTEM` 后，历史运行由客户端捕获了 `error=0` 的真实初始 MFT/USN 操作：`961220` 条 MFT 记录、`113` 条 USN 记录和 `108283017` 字节 BLK5。该卷权限结果不能被旧的 IPC 错误混淆；这份初始索引证据属于旧输入结构实现，仍不能把未执行的搜索、后续增量、恢复和故障场景当作完整 Phase 0.5 通过。

验收边界因此使用同步消息管道客户端、每次读写有界等待，并让服务优先通过 Named Pipe client-token impersonation 检查客户端 Token，保留进程 Token fallback；客户端身份拒绝写入 acceptance telemetry 的 API/PID/Win32 诊断。PowerShell 客户端显式请求 `TokenImpersonationLevel.Impersonation`，原生 `Blink.Index.Contracts` 客户端同步设置 `SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION`。验收脚本现对进度 IPC 做有界重试，并在初始启动响应超时后只通过 `QueryIndexProgress(operation_id=0)` 找回 operation，绝不重复提交初始扫描。由于有效 BLK5 在新服务进程中的首次加载可能超过 10 秒，脚本在搜索 benchmark 前及各服务重启后增加 120 秒有界 Load/Healthy warm-up；搜索和断连探测本身仍保持短超时。下一步仍必须取得搜索、后续 USN、持久化、故障和 RebuildRequired 证据后，才能更新 Phase 0.5 结论。

2026-08-11 验收脚本正确性补充：

- `FSCTL_ENUM_USN_DATA` / `FSCTL_READ_USN_JOURNAL` 的输入结构按文件系统选择：NTFS 使用 V0，ReFS 使用 V1 并显式限制 USN record major version；不再把 V1 作为 NTFS/ReFS 的统一输入。
- 目标卷与 `%TEMP%\BlinkAcceptance\<RunId>` live harness 必须先完成卷隔离检查，之后才允许创建日志、telemetry、fixture 或构建输出；验收服务 DataRoot 则必须与目标卷同卷，以覆盖 D-0021 自索引隔离语义。
- 等待逻辑使用绝对阶段 deadline；BLK5 load 的零 CatchUp 字段不单独触发 inactivity watchdog。连续写入 writer 有界等待并校验 Completed/错误，固定目标 USN 的 `catchup_batch_count` 通过 service telemetry 记录；artifact export 失败单独写入 `artifact_export=FAIL` 并返回非零退出码。
- 本补充只完成静态/普通权限脚本验证，没有重新执行高权限全卷验收；Phase 0.5 结论仍保持 `NOT READY`，Phase 1 不启动。

2026-08-11 人工验收结果补充：

- 管理员 High Integrity 运行 `20260811-012750` 真实验证了修正后的 NTFS `MFT_ENUM_DATA_V0`（24 bytes）与 `READ_USN_JOURNAL_DATA_V0`（40 bytes）：D: 直接探针分别得到 `1,009,877` MFT records 与 3 条 USN records。
- LocalService 仍在 `FSCTL_QUERY_USN_JOURNAL` 返回 Win32 `5 / ERROR_ACCESS_DENIED`；LocalSystem 安装和初始请求接受成功，但进度请求在旧的 30 秒 transport grace 内全部超时。源码路径表明 BuildInitial 持有 FastIndex mutex，而进度统计读取同一 mutex；该次运行不足以判定 LocalSystem MFT 失败。
- 验收 runner 已将 initial/store-load/reconnect 阶段的 IPC transport grace 调整为有界 120 秒，仍受绝对阶段 deadline 限制；失败运行的 per-run service-data 也不再在导出前删除，以保留 BLK5/telemetry 诊断。下一步仍需人工重新运行，Phase 0.5 保持 `NOT READY`。

2026-08-11 Search 阶段根因与最小修复补充：

- 人工运行 `20260811-031602` 的 Search request 493 已通过 FastIndex：FastIndex mutex 等待约 `0.002 ms`，实际搜索约 `3176.364 ms`，序列化约 `14.264 ms`，管道写入约 `0.009 ms`。request 494 没有进入 Search，而是在 `EnsureLoaded` 的 running-operation 快速路径返回 `Busy/CatchingUp/ERROR_IO_PENDING(997)`；因此该失败不是 Named Pipe 写阻塞，也不是 Search mutex 等待超时。
- 根因链为：scheduler 对任意 `snapshot.last_usn < journal.next_usn` 启动 CatchUp；同卷 DataRoot/telemetry 产生的内部 USN 又被服务自己的保存/telemetry 写入放大。`FastIndexStats.user_mutations_applied` 原先跨 CatchUp 累积，使内部批次继续被判断为 dirty 并触发 BLK5 Save。
- 最小修复为：每次 `FastIndex::CatchUp` 开始清零本次 dirty delta；纯内部、无错误的 CatchUp 仍消费 USN 并推进内存 checkpoint，但不再向被索引卷写 operation telemetry；失败操作和真实用户 mutation 仍保留 telemetry/Save。验收 Search 客户端只对明确 `Busy/997` 做有界重试，不改变真正 Search 的 IPC timeout 或其他错误处理。
- Debug/Release 编译与普通 Phase 0/0.5/Transfer 测试通过；该修复尚未由新的人工 elevated acceptance 运行确认，Phase 0.5 仍为 `NOT READY`，Phase 1 不启动。

2026-08-11 同卷 DataRoot 自索引环路复核：

- 人工运行 `20260811-054222` 的 LocalSystem 初始 MFT 索引真实完成（`909605` files、`101627` directories、`1011286` records seen、`56.6801 s`、BLK5 `112958985` bytes），但 BLK5 load 后的 operation 2–19 均把内部 USN 批次计为 dirty，最终在阶段 deadline 停止；Search 及后续验收均未执行，结论仍为 `NOT READY`。
- 上一版“每次 CatchUp 清零 dirty counter + 纯内部 CatchUp 不写 telemetry”修复本身生效，但 `IsInternalExcludedPathUnlocked()` 的路径解析在加载索引后的父链/规范 link 场景不稳定。最小修复是配置排除根时一次性读取其 `FileIdentity`，CatchUp 内只沿已有 parent identity 链做内存判定，失败后保留原路径 fallback；不在持锁路径做每条记录的 filesystem IO，不跳过 USN/checkpoint。
- 该修复只改变 internal dirty classification，不改变 Search、Hard Link、BLK5 格式或 scheduler 架构。Debug/Release 编译与普通测试通过，必须由下一次人工 elevated run 验证 `user_mutations_applied=0` 且 load recovery 不再自触发。

## 2026-08-11 人工运行 20260811-112040：Search 超时与卷序列号表示修正

- 该次人工 Administrator/High Integrity 运行在真实 D: NTFS 卷完成 LocalSystem 初始 MFT 索引：`909952` files、`101670` directories、`1011679` records seen；MFT enumeration `76.7394 s`、parent-tree `72.6032 s`、total `83.6026 s`、peak working set `641998848` bytes、BLK5 `113003643` bytes。LocalService 仍明确失败于 `FSCTL_QUERY_USN_JOURNAL`，Win32 `5 / ERROR_ACCESS_DENIED`。
- BLK5 load/journal catch-up 在客户端观察到 `Healthy`，但 operation 2–73 仍将每批同卷 DataRoot USN 记录计为 dirty。根因不是 ProgressSnapshot，也不是本次 Search 的锁等待：`FastIndex::Search` 的 69 个完成请求中，lock wait 平均/最大为 `0.00152/0.009 ms`，实际 Search 平均/最大为 `2615.797/5948.365 ms`，serialization duration 平均/最大为 `0.01887/0.030 ms`，pipe write duration 平均/最大为 `0.00839/0.015 ms`。因此本次 5 秒超时主要属于实际 Search 算法耗时，内部 CatchUp/Save 活动是放大因素。`Healthy` 不是对后续 Search 的锁预约：`RefreshIndexState` 释放 `state_mutex_` 后，scheduler 仍可在 `FastIndex::Search` 取锁前启动 CatchUp；本次实测的 lock wait 很小，所以这是次要竞态风险而非本次主因。
- 身份比较的明确根因是表示不一致：USN link identity 使用 `GetVolumeInformationW` 返回的 DWORD volume serial；`ReadFileIdentity` 通过 `FILE_ID_INFO` 取得 64-bit `VolumeSerialNumber`。此前排除根保存了后者，导致相同 File Reference Number 也无法与 USN identity 比较相等。
- 最小修复是在 `FastIndex::SetInternalExcludedRoots` 读取排除根身份后，用同一卷的 `GetVolumeInformationW` 将 `volume_serial` 规范化为 USN 使用的 DWORD；仍然先沿内存 parent identity chain 分类，不在 CatchUp 持锁路径做 filesystem IO。该修复未跳过 USN、未改变 BLK5 格式、Search 数据结构或 scheduler。
- Search 暂不通过“增大 IPC timeout”掩盖问题，也暂不进行 B+Tree/倒排索引等架构扩展。先由下一次人工 elevated run 验证内部 dirty delta 归零、load 稳定和 fixture 搜索结果正确；稳定基线之后再决定是否需要最小的查询性能优化。
- 本次 Search 的 exact/prefix/substring 行虽完成请求，但均为 `result_count=0`，只记为 transport/performance sample，不记为搜索正确性通过。extension 超时前未完成，directory-scoped、USN mutation、client restart、service stop/kill、synthetic invalid checkpoint 均未执行。Phase 0.5 仍 `NOT READY`，Phase 1 不启动。

### 2026-08-11 ordinary pre-acceptance confirmation: volume identity and Search correctness

- The ordinary-permission probe read the same D: volume through `D:\`, the project-local DataRoot, and a DataRoot child. Every target reported `GetVolumeInformationW DWORD = 0xC8B2C41B`, `FILE_ID_INFO 64 = 0x6AC8B2FAC8B2C41B`, `FILE_ID_INFO low32 = 0xC8B2C41B`, `FILE_ID_INFO high32 = 0x6AC8B2FA`, and `BY_HANDLE_FILE_INFORMATION DWORD = 0xC8B2C41B`. The mapping is therefore PASS for this target volume; it is evidence for a canonical 32-bit USN/InternalExcludedRoot comparison key, not evidence that the truncated value is the complete FILE_ID_INFO identity.
- `FileIdentity` continues to retain the complete FILE_ID_INFO volume serial and 128-bit FileId. `InternalExcludedRootIdentity` stores a separate `canonical_volume_serial`; the FastIndex root/path graph uses an equivalent boundary comparison for USN identities without overwriting the full root identity. This is the minimal repair for the observed zero-result path-resolution failure.
- The retained `20260811-112040` BLK5 was loaded by an ordinary offline test without starting the service. Known exact, prefix, substring, extension, directory-scoped, and known-miss assertions all passed with result counts `1`, `1`, `12`, `10`, `1`, and `0`. The original zero-result benchmark was therefore a correctness failure caused by the root identity representation mismatch, not a valid miss.
- The same offline query set was sampled five times per known-hit case. Exact max was `3149.53 ms`, prefix `2984.62 ms`, substring `2893.69 ms`, directory-scoped `2796.19 ms`, but extension was `min=6011.97 ms`, `median=6183.51 ms`, `p95=6282.32 ms`, `max=6289.93 ms`. This is a stable over-5-second query blocker; the IPC timeout must not be raised as a substitute for an algorithmic decision. Phase 0.5 remains `NOT READY`.
- The elevated harness now keeps performance rows separate from `search_correctness` and creates an out-of-scope duplicate for the directory-scoped sentinel. The next manual elevated run must still execute the full acceptance; this ordinary run did not start a service or elevation.

### 2026-08-11 derived extension index precondition

- The retained million-record BLK5 was benchmarked offline with the same known-hit query families in a forced full-scan reference mode and in the production derived-extension mode. Exact/prefix/substring/directory-scoped remained below the 5-second budget; extension was the isolated blocker at reference median/max `5336.71/5433.58 ms`, and directory+extension was `5341.03/5455.67 ms`.
- The minimal fix adds `extension -> FileIdentity set` as a derived in-memory structure. Search looks up extension candidates first, then applies name/path predicates and resolves paths only for those candidates. `links_` remains authoritative; the map is rebuilt after BLK5 Load and updated by Add/Remove link paths. No BLK5 version, checkpoint, or IPC ABI changed.
- Clean after results were correctness-checked against a full-scan oracle and measured extension median/max `240.31/245.18 ms`, directory+extension `216.38/216.90 ms`. No timeout increase was used. The ordinary test also covers hard-link multi-name retention, bucket mutation, BLK5 rebuild, and InternalExcludedRoots filtering.
- The retained acceptance fixture's hard link was created before Initial MFT, so its persisted index cannot prove a post-initial hard-link USN mutation. The elevated harness now creates the hard link in the mutation stage after Initial Index; this is a fixture correction, not a claim of runtime elevated acceptance.
- This resolves the Search-performance precondition only. Full Phase 0.5 remains `NOT READY`; the next permitted action is one manual elevated acceptance, and Phase 1 remains stopped. Exact/prefix/substring inverted-index work remains later Search technical debt because the current Debug samples are below the deadline.

## 2026-08-11 人工运行 20260811-145851：真实 Search/USN 证据与验收前置修正

- 管理员 High Integrity 运行在真实 D: NTFS 卷完成了 direct volume probe、Debug/Release 构建、LocalService-first SCM 流程和显式 LocalSystem fallback。LocalService 能打开卷句柄，但 `FSCTL_QUERY_USN_JOURNAL` 明确返回 Win32 `5 / ERROR_ACCESS_DENIED`；LocalSystem 安装、启动和初始 MFT 操作成功。
- LocalSystem 初始索引记录为 `910421` files、`101718` directories、`1012222` records seen；telemetry 为 `1012139` MFT links、83 USN records、MFT enumeration `153.055 s`、parent-tree `148.085 s`、total `160.289 s`、CPU `157.09375 s`、peak working set `755957760` bytes、Save `6.95427 s`、BLK5 `113058621` bytes。BLK5 load/journal validation 到达 `Healthy`，Journal ID 为 `134011753522823542`。
- 真实服务 Search benchmark 已完成且非零命中：exact/prefix/substring/extension/directory-scoped 结果数为 `1/1/13/11/1`，平均耗时约 `2684.77/2781.04/2729.05/244.16/2465.48 ms`，P95 约 `3463.71/3307.52/2898.66/276.76/2700.55 ms`。本次没有通过增大 IPC timeout 掩盖问题；derived extension index 的实时性能前置条件得到实际运行支持。
- Search correctness 五条 known-hit 断言失败的直接原因是 PowerShell 5.1 将未加后缀的 `0xFFFFFFFF` 解析为 `Int32 -1`，随后转换为 `UInt64` 抛错；对应服务返回结果数和范围/身份数据非零且有效，不能把该 harness 转换异常记为 Search miss。Harness 已改用正的 UInt64 mask，并防御空/畸形 hit 对象。
- USN mutation 的 File ID、Parent ID、lazy path、delete、hard-link、Unicode 和目录操作断言均通过。选中的非初始 CatchUp 返回 `error=0` 且 `records_seen>0`，但没有 telemetry 文件；服务对纯 internal CatchUp 有意抑制 telemetry 写入，以避免同卷 DataRoot/telemetry 形成自索引环路。Harness 已把“成功完成但 telemetry 被抑制”与真实 API 失败分开记录，不能仅凭缺少该文件判定 `FSCTL_READ_USN_JOURNAL` 失败。
- Continuous-write stress 尚未形成验收结果：旧 Harness 在 StrictMode 下因 `Find-Hit` 访问缺少 `name` 属性的返回对象而中止。`Find-Hit` 已改为忽略 null/畸形对象并安全检查属性，使下一次运行能够记录真实断言失败而不是异常退出。客户端重启、Service stop/kill/reconnect、invalid journal、corruption 和 Windows restart 阶段因此尚未执行。
- 该次运行之后只完成 Harness/文档侧修正，没有重新启动服务或重跑 elevated acceptance。UTF-8 native child stdout/stderr smoke test、PowerShell parse、Debug/Release ordinary Search regression 均通过；当前 Phase 0.5 仍为 `NOT READY`，Phase 1 不启动。下一步仍只允许一次人工管理员验收。

## 2026-08-12 人工运行 20260811-162754：核心场景证据与连续写入 sentinel 保留项

- 管理员 High Integrity 运行在真实 D: NTFS 卷完成 direct V0 probe、Debug/Release build、LocalService-first 和显式 LocalSystem fallback。LocalService 的卷句柄成功，但 `FSCTL_QUERY_USN_JOURNAL` 为 Win32 `5 / ERROR_ACCESS_DENIED`；LocalSystem 初始索引成功：`911110` files、`101750` directories、`1012889` records seen，MFT records `1012860`、USN records `29`、MFT enumeration `138.039 s`、parent-tree `133.662 s`、total `144.940 s`、CPU `144.46875 s`、peak `755896320` bytes、Save `6.67079 s`、BLK5 `113131795` bytes。
- BLK5 load/Journal validation 到达 `Healthy`。真实服务 Search benchmark 的结果数为 `1/1/13/11/1`，平均耗时为 `2268.52/2189.24/2413.42/245.67/2278.33 ms`，P95 为 `2411.72/2340.84/2508.02/284.89/2428.64 ms`；五条 known-hit 与 known-miss correctness 均通过，没有增大 IPC timeout。
- USN mutation 的 File ID、Parent ID、lazy path、delete、hard-link、Unicode、目录操作和 reparse 断言通过；选中的非初始 CatchUp operation 107 返回 `error=0`、`records_seen=29859`、`usn_records=2`、`user_mutations_applied=2`。重启恢复、无完整 MFT rebuild、Service stop/kill/reconnect 和 corruption 均通过。
- Continuous-write stress 的 writer、18 个 fixed-target CatchUp batches、单调 USN 和 IPC 延迟均通过，但 Search-level sentinel create/delete convergence 未被接受。保留为唯一未解决运行时一致性项；Harness 已加入最后候选记录和规范化 scope matching，下一次人工运行只需重新验证这一点，不得静默改判。
- Synthetic checksum-valid Journal ID mismatch 的服务终态为 `RebuildRequired` + Win32 `1181 / ERROR_JOURNAL_ENTRY_DELETED`。该次 JSON 的 `rebuild_required=false` 是 Harness 仍期待 `1178 / ERROR_JOURNAL_DELETE_IN_PROGRESS` 的旧断言，不是服务错误；runner 已修正为期待 `1181`。Windows Restart Acceptance 仍 `NOT EXECUTED`。
- 本次未修改索引核心 C++，未增大 timeout，未重跑 elevated acceptance。Phase 0.5 仍 `NOT READY`；LocalSystem 仅为明确选择的 prototype fallback，生产账户仍 `UNRESOLVED`，Phase 1 不启动。

## 2026-08-12 Targeted Continuous-write Search sentinel acceptance：20260811-201439

- **Decision:** Continuous-write Search sentinel convergence is formally **ACCEPTED** for the targeted acceptance slice. The run used the existing Blink.IndexService, BLK5 Load, IPC Search, and USN CatchUp paths while a bounded continuous writer was active; create and delete converged with exact path and FileIdentity checks.
- Targeted mode copied the historical valid BLK5 into a new RunId-specific `service-data-targeted` root and never modified the source store. Initial MFT was intentionally not executed.
- The runner's top-level `final_result=NOT READY` is by design because targeted mode does not recalculate the complete Phase 0.5 acceptance. This targeted PASS must not be written or interpreted as a complete Phase 0.5 PASS or a Phase 1 approval.
- At the time of this targeted run, the full Phase 0.5 status remained `NOT READY` pending the separate Windows Restart Acceptance; production service-account selection remained `UNRESOLVED` and the broad pipe ACL remained a hardening condition. No C++ change, Search-timeout change, Initial Build optimization, or AgentBridge implementation was authorized by this result.
- The next acceptance slice identified by that run was the persistent two-phase `PrepareWindowsRestartAcceptance` / `CompleteWindowsRestartAcceptance -RunId <id>` flow. That slice is now recorded as accepted below.

## 2026-08-12 Windows Restart Acceptance：20260811-204329

- **Decision:** The two-phase Windows Restart Acceptance is formally **ACCEPTED**. The same persisted state and copied BLK5 survived a real OS reboot; Journal ID continuity, non-initial USN CatchUp, and RebootSentinel path/FileIdentity correctness all passed.
- Evidence: boot time changed from `2026-08-11T20:39:54.5000000Z` to `2026-08-11T20:51:29.5000000Z`; recovery operation `10` reported `initial=false`, `mft_records=0`, and `catchup_completed_usn=3664878520`; Search returned one exact sentinel hit with path and identity match. The service registration and fixture were removed only after PASS.
- The Complete mode's top-level `final_result=NOT READY` is slice-scoped by design and does not override the combined full Phase 0.5 assessment. With the full run `20260811-162754` and targeted run `20260811-201439`, the core Phase 0.5 result is **READY WITH CONDITIONS**.
- Remaining conditions are non-core release/Phase 1 hardening: production service-account selection remains `UNRESOLVED`, and the broad Authenticated Users pipe ACL must be narrowed. Phase 1 remains stopped; no C++ or timeout change is implied by this acceptance.

## 2026-08-12 人工运行 20260811-153431：核心场景证据与三项 Harness 时序修正

- 管理员 High Integrity 运行在真实 D: NTFS 卷完成 direct V0 probe、Debug/Release build、LocalService-first 和显式 LocalSystem fallback。LocalService 的卷句柄成功，但 `FSCTL_QUERY_USN_JOURNAL` 为 Win32 `5 / ERROR_ACCESS_DENIED`；LocalSystem 初始索引成功：`910734` files、`101734` directories、`1012650` records seen，MFT links `1012468`、USN records `182`、MFT enumeration `140.645 s`、parent-tree `136.197 s`、total `147.708 s`、CPU `146.984375 s`、peak `756981760` bytes、Save `6.81049 s`、BLK5 `113091531` bytes。
- BLK5 load/Journal validation 到达 `Healthy`。真实服务 Search benchmark 的结果数为 `1/1/13/11/1`，平均耗时为 `2225.81/2289.87/2339.04/213.76/2227.41 ms`，P95 为 `2259.48/2556.31/2386.81/240.41/2288.81 ms`；没有通过增大 IPC timeout 掩盖 Search 性能。
- Search known-hit 结果的 identity 与 scope 均有效，但 Harness 再次因 conditional collection 被标量化后访问 `.Count` 而失败；known miss 通过。最小修正是强制该集合保持数组，不改 Search 实现。
- USN mutation 的 File ID、Parent ID、lazy path、delete、hard-link、Unicode 和目录断言通过；非初始 CatchUp operation 102 记录 `error=0`、75 USN records 和 53 user mutations。连续写入的 writer、15 个 fixed-target CatchUp batches、单调 USN 和 IPC 延迟均通过，但 sentinel 在新 USN 可搜索前被检查，Harness 需轮询 Search 本身后才可接受 create/delete 闭环。
- BLK5 停止期间变更恢复、无完整 MFT rebuild、Service stop/kill/reconnect 和 corruption 均通过。Synthetic Journal ID mismatch 已执行，但第一次 status 是预期的异步 `Busy/997/Rebuilding`；Harness 未等待终态 `RebuildRequired`。服务实际 mismatch 映射为 `ERROR_JOURNAL_ENTRY_DELETED (1181)`，旧记录中的 `1178` 是错误的 Harness expected code，最小修正是有界轮询终态并校验 `1181`。
- 本次问题均为 Harness 断言/时序问题，未发现需要修改核心索引架构的证据。已加入数组化、Search sentinel 等待和 `RebuildRequired` 等待；未再次运行 elevated acceptance。Windows Restart Acceptance 仍 `NOT EXECUTED`，Phase 0.5 仍 `NOT READY`，Phase 1 不启动。

## D-0009: Privileged index boundary

`Blink.IndexService` is a separate Windows service. `Blink.exe` remains a normal-user process and reaches the service only through a versioned, read-only index protocol. The protocol has no arbitrary file-operation command. UAC is required only for service installation/configuration, never for ordinary index queries.

## D-0010: Service account and least privilege

The installer tries `NT AUTHORITY\LocalService` first and reports volume-handle access failures explicitly. `LocalSystem` is an explicit fallback option for the prototype because direct change-journal/volume operations are security-restricted; it is never selected silently. The service surface is intentionally limited to volume status, MFT/USN indexing, progress, cancellation, and read-only search. A production release must validate a dedicated virtual/service account before accepting LocalSystem.

## D-0011: MFT/USN fast index data model

`FSCTL_ENUM_USN_DATA` builds identity/path links from MFT records. A node is addressed by Volume ID + File ID and stores Parent ID + name links; paths are resolved lazily through the parent tree. This preserves rename/move behavior without rewriting every descendant and permits multiple hard links. The fast index contains identity/path/attribute data; expensive Shell metadata remains lazy enrichment.

## D-0012: Persistent checkpoint and journal recovery

The Phase 0.5 store records volume identity, journal ID, USN checkpoint, root, links, format version, and a checksum. Saves use a temporary file, flush, and replace. A mismatched volume, journal ID, deleted USN range, invalid checksum, or impossible checkpoint changes the visible state to `RebuildRequired`/`Corrupted`; the service never silently continues from stale data.

## D-0013: Technical API floor versus product support policy

Windows SDK/API validation uses the technical floor Windows 11 24H2 / build 26100 and the installed 10.0.26100.0 SDK. This is not the same as the product's supported-OS policy: product support may target a newer/current Windows release and can be raised independently after compatibility and installer validation. No legacy API fallback is introduced by this distinction.

## D-0014: MetadataHost remains a formal follow-up decision

The Phase 0.5 privileged boundary is for indexing only. `IPropertyStore` stays off the UI thread as a prototype, but a production MetadataHost remains the chosen isolation direction for untrusted/slow property handlers. It is not implemented in Phase 0.5 and is tracked as technical debt for the next phase.

## D-0001：目标系统基线为 Windows 11 24H2 及以后

附件要求优先当前 Windows 的新接口，并禁止为 Windows 7/8/10 主动增加兼容回退。微软当前文档将 CreateFile3、CreateDirectory2、DeleteFile2、RemoveDirectory2 的桌面客户端最低版本标为 Windows 11 24H2；因此 Blink Phase 0 的最低系统基线设为 10.0.26100.0。低于该版本时应明确提示不受支持，而不是静默退回旧 API。

本机实际构建号为 26200，安装的 Windows SDK 为 10.0.26100.0，满足本阶段验证条件。

## D-0002：Windows App SDK 使用稳定版 2.3.1

截至 2026-08-10，微软稳定下载页列出的最新稳定补丁为 Windows App SDK 2.3.1（2026-07-16）。Phase 0 不使用 Preview 或 Experimental API。WinUI 3 由 Windows App SDK 提供，C++/WinRT 使用 Microsoft.Windows.CppWinRT 2.0.250303.1 NuGet 构建支持。

## D-0003：新文件 API 只对目标 Windows 直接调用

文件打开和写入路径使用 CreateFile3。这里特意记录 API 审计结论：Windows SDK 中的实际函数符号是 CreateFile3，不存在应直接调用的 CreateFile3W。目录创建/删除和文件删除使用 CreateDirectory2W、RemoveDirectory2W、DeleteFile2W，传入规范化绝对路径并启用路径重定向保护标志。没有为旧系统加入 fallback。

## D-0004：文件身份使用 Volume ID + 128-bit File ID

GetFileInformationByHandleEx(FileIdInfo) 返回 FILE_ID_INFO，其中 VolumeSerialNumber 与 128-bit FileId 组合用于身份。重命名不应改变 NTFS 文件 ID；删除后重新创建可能复用或产生新的 ID，不能只依赖完整路径，也不能把 ID 误称为跨文件系统永恒 GUID。

## D-0005：全卷变化以 USN Journal 为主，目录观察使用 ReadDirectoryChangesExW

NTFS Fast Path 使用 FSCTL_ENUM_USN_DATA 完成初始索引；DFS 仅作为不支持 Fast Index 或 IndexService 不可用时的 fallback。USN Journal 用于全卷增量路线，Journal reset、wraparound、卷离线和不可用均导致重新扫描。ReadDirectoryChangesExW 只作为目录级实时观察 Prototype，不把它当作全卷索引替代。当前本机非管理员测试查询卷 Journal 返回 ERROR_ACCESS_DENIED，该限制会向上层暴露，不能假报“USN 已验证”。

## D-0006：Property Handler 与网络 Provider 不在 UI 线程

本阶段 Property System 读取会在后台任务执行，避免正常属性查看阻塞 UI；由于 IPropertyStore 可能进入第三方 Handler，正式产品仍需要独立 MetadataHost。网络 Provider 运行在 Blink.ProviderHost.exe，主进程只通过受限 IPC 请求，不直接加载第三方扩展。

## D-0007：复制采用 CopyFile2，取消已验证，暂停/恢复不虚报

CopyFile2 的进度回调能够返回 CANCEL/STOP/PAUSE。Phase 0 已验证后台复制、进度和取消；暂停/恢复没有在本机形成可靠可重复验证，因此不包装成已完成能力。

2026-10-15 Phase1D Slice3实现补充：Phase0 CopyFile2保留为未接产品入口的原型，不能直接复用其覆盖/future/请求标志语义。首个产品单普通文件Copy选择identity-checked handle读取和CREATE_NEW目标，失败按owned handle清理；在现有能力下优先证明身份、不覆盖和取消真相，特殊存储/ADS明确Unsupported。该选择不扩大D-0027范围，不将完整元数据、冲突策略或递归Copy标为完成；详见[Slice3计划](docs/PHASE1D_SLICE3_DEVELOPMENT_PLAN.md)。

2026-10-15 Slice4补充：同一handle copy路径扩展到默认及命名$DATA流，避免下载文件Zone.Identifier被静默遗漏；Skip/Cancel为零I/O显式决策，KeepBoth仍CREATE_NEW且确定命名。普通Replace probe未得到可接入的目标身份绑定和失败保留语义，保持禁用/Unsupported，不把“held ID没变+路径rename”当作原子安全替换，不启用fallback。详细证据和metadata限制见Slice4计划与PROGRESS。

目标ADS为兼容owned默认句柄需分享DELETE，不能宣称阻止单独的流删除；提交前同时核验枚举及每个流的FileStandardInfo.DeletePending，检测到即清理并失败，不报copied/100%。源/default目标的DISALLOW与父链保护保留，目标ADS使用独立probe验证的固定OPEN_REPARSE参数；不按失败动态重试更弱的参数。

2026-10-15 Slice5补充：单文件与多文件队列共用同一coordinator native action和CopyFileSafely，整批输入检查后串行执行；失败保留已提交项与未开始tail，显式Resume不重做先前成功且保留partial历史。CF_HDROP有界支持1024路径/4MiB，原source身份按真实发布payload+sequence绑定，不因路径规范化改绑新对象。队列不承诺事务回滚，也不把单项字节完成当作全批完成。

2026-10-15 Slice6后台补充：目录复制采用同handle有界manifest，创建时保持owned目录guard、同ID metadata handle后序提交；实际叶内容仍仅由CopyFileSafely复制。失败/取消保留新建树并真实报告partial，不按路径回滚。首版只支持全新root，目录ADS/reparse/Merge/Replace明确不支持，尚未接入UI/队列，完整原生验证状态见PROGRESS与[Slice6计划](docs/PHASE1D_SLICE6_DEVELOPMENT_PLAN.md)。

## D-0008：Provider Prototype 使用独立进程 + 命名管道

FakeProvider 的正常、延迟、永久等待、错误、崩溃、断开模式由 Blink.ProviderHost.exe 承载。主 UI 线程不等待命名管道响应；请求、超时、取消和主机终止均由后台协调层处理。Phase 0 使用消息模式命名管道验证隔离，并通过不含反斜杠的 token 传参避免命令行转义歧义；生产版可继续演进为完成端口/更严格的身份校验协议。

## D-0021：安装卷跟随数据策略与按卷隔离索引恢复架构

> **Status: APPROVED (Architectural Decision Record)**  
> **Supersedes: D-0016**

正式产品确立以下核心持久化与数据存储架构决策：

1. **安装卷跟随策略 (`Install-volume-following DataRoot`)**：
   Blink 所有大容量可变数据（包括 Index Store、Metadata/Thumbnail/Preview Cache、Logs、Crash Dumps、Temp）默认必须位于 Blink 安装路径所在的 Windows Volume / 文件系统卷 (`InstallVolume = VolumeOf(InstallRoot)`)。当 Blink 安装在非系统卷时，禁止因为 Windows 默认目录惯例而静默将大容量 Index/Cache/Temp 数据写入系统卷；如果 Blink 本身安装在系统卷，则 DataRoot 按“安装卷跟随”原则正常位于该卷。
2. **禁止静默 Fallback 到系统盘**：
   数据盘空间不足或不可写时，禁止静默自动转移大量数据至系统盘；必须由用户通过 UI 明确选择（清理缓存/迁移数据目录/修改数据位置）。非常微小的系统集成状态（如 SCM 服务注册、注册表键值）属于允许的 Windows 系统目录例外。
3. **按卷独立持久化索引 (`Per-Volume Persistent Index`)**：
   索引架构禁止设计为全局不可分割的大单体数据库。物理结构必须按 Volume 独立存储：`Index\Volumes\Volume-{GUID-or-stable-id}`。每个卷独占独立的 Volume Identity、Serial、Journal ID、Last Applied USN、Checkpoints 与状态机（Healthy / CatchingUp / RebuildRequired / Offline）。Blink 的架构身份严格针对 Windows Volume / 文件系统卷，而不是底层物理磁盘设备；禁止把同一块物理磁盘上的不同 Volume/Partition 混为一个索引身份。
4. **卷身份与盘符解耦 (`Volume Identity != Drive Letter`)**：
   Drive Letter 仅为当前 Mount Point，不是持久身份。Volume GUID / Volume Serial / Root Identity 用于持久 Volume Identity matching，并在每次恢复时重新验证。挂载点变化（如 `D:` 变为 `E:`）只要底层卷身份一致，直接识别为既有卷并尝试 USN CatchUp，避免触发无谓的 Full Rebuild。
5. **重装 Windows 后的单卷隔离恢复 (Windows Reinstall Recovery)**：
   格式化并重装系统盘后，对于未格式化的 `D:` / `E:` 数据卷，只要卷身份匹配且 USN Journal 连续，恢复后直接载入现有 Checkpoint 并进行 USN CatchUp 达到 Healthy；仅对格式化后的系统卷触发重建，切断“重装系统即作废全盘索引”的传统劣根。
6. **自索引环路隔离 (`InternalExcludedRoots`)**：
   当 `DataRoot` 与被索引卷位于同一 Windows Volume / 文件系统卷时，Blink 自身文件产生的 USN Record 仍必须被读取、消费并正确推进 journal checkpoint；Internal Exclusion 只发生在 index mutation / user-visible indexing / persistent dirty propagation 层（禁止将其纳入用户搜索结果，禁止因此引发索引自循环脏化与持久化触发）。禁止通过直接跳过这些 USN Record 来实现排除。
7. **容量控制与独立卷删除重建**：
   设置 `MaxCacheSize`、滚动日志（Rolling Logs）与 Dump 留存限额。支持单卷独立重建 (`Rebuild Volume Index`) 与跨卷并行搜索（个别卷 Rebuilding 不阻塞 Healthy 卷的搜索）。

## D-0022：索引一致性校验与防幽灵条目策略 (Anti-Stale Index & Consistency Scrub)

> **Status: APPROVED (Architectural Decision Record)**

确立索引一致性与防幽灵条目的核心架构规约：

1. **强制增量删除与零幽灵条目定义 (Mandatory Incremental Deletion & Zero Stale Results)**：
   USN CatchUp 必须实时消费 `File Delete`、`Directory Delete` 与 `Rename` 记录。索引不得设计为 Append-only 数据库。在 Volume Index 状态为 Healthy / 已完成 CatchUp 时，不允许已知已删除对象继续作为有效搜索结果存在。CatchingUp 状态允许存在极短的最终一致性窗口，但必须最终收敛。
2. **低频后台一致性校验与 Hard Link 验证 (Periodic Index Consistency Scrub & Hard Link Verification)**：
   USN 为主要实时同步路线；额外增加低频后台 Consistency Scrub 机制作为安全网。利用 MFT / File ID 比较既有 Persistent Index 与磁盘实际 MFT 身份集。对支持 Hard Link 的文件系统，Scrub 不得只比较 File ID 是否存在，同时必须验证该 File ID 对应的 Parent ID + Name link 集合；Identity 存在但 link/name/parent 关系错误同样属于 consistency discrepancy。
3. **按卷隔离与损坏阈值修复策略 (Scrub Repair Policy)**：
   Scrub 以单卷为单位运行。少量明确偏差采取增量修复（Incremental Repair）；一旦发现偏差数量超过安全阈值或结构性关系无法可信恢复，明确将该卷标记为 `RebuildRequired`，禁止尝试无限修补已不可信的索引。
4. **Search/Open 自愈回退与删除回归测试**：
   用户从搜索结果打开文件收到 `ERROR_FILE_NOT_FOUND` 时，触发异步自愈验证 (`VerifyIndexedEntry`)。该自愈为兜底机制，不能替代正确的 USN Delete 消费。未来必须包含增删改查、目录树删除、离线删除与控制注入的反幽灵回归测试。

## D-0023：Hardened LocalSystem baseline 与单用户 Named Pipe ACL

> **Status: ACCEPTED (Security Closure Run `20260811-233345`)**

本条关闭并接受 Phase 0.5 的 Service Security Baseline 与 Named Pipe ACL Hardening。它区分 SCM 登录账户、Service SID、RequiredPrivileges 和 Pipe authorized client SID；四者不是同一个安全概念。

1. **Logon account** 是 SCM 配置的服务登录账户。接受的基线为 `LocalSystem`；LocalService 的 `FSCTL_QUERY_USN_JOURNAL` 仍为 Win32 5 / `ERROR_ACCESS_DENIED`，因此没有把 LocalService 静默写成可用账户。
2. **Service SID** 是独立于 Logon account 的 NT SERVICE\Blink.IndexService 身份。安装器在 HardenedLocalSystemCandidate 下通过 SERVICE_CONFIG_SERVICE_SID_INFO 请求 SERVICE_SID_TYPE_UNRESTRICTED；本轮不启用 SERVICE_SID_TYPE_RESTRICTED。
3. **RequiredPrivileges** 是 SCM 从服务进程令牌中保留的最小权限集合。第一候选只声明 SeImpersonatePrivilege，不因为卷操作名称猜测加入 SeManageVolumePrivilege、SeDebugPrivilege 或 SeBackupPrivilege。安装器通过 SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO 写入，并用 QueryServiceConfig2 与 sc.exe qsidtype/qprivs 形成机器可读证据。
4. **Pipe authorized client SID** 是单用户安装中允许 Blink 普通客户端连接的当前安装用户 SID；它不是 Logon account、Service SID 或 RequiredPrivileges 的替代物。Pipe DACL 只包含 SYSTEM、Service SID 与该用户 SID，不再使用 Authenticated Users 或 Everyone。
5. Pipe 客户端只获 `FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE = 0x00120183`；`FILE_CREATE_PIPE_INSTANCE` 仅给服务端主体。SYSTEM 与 Service SID 均获 `FILE_GENERIC_READ | FILE_GENERIC_WRITE | SYNCHRONIZE = 0x0012019F`。`PIPE_REJECT_REMOTE_CLIENTS`、命名管道 impersonation、PID/token 校验与协议边界继续保留。
6. 安装器将授权 SID 与 Service/privilege 配置写入 `security\installer-service-config.json`；服务首次创建 Pipe 后用 GetSecurityInfo 查询实际 descriptor，并写入 `security\pipe-security.json`。Run `20260811-233345` 的实际 descriptor 为 SYSTEM `0x0012019F`、Service SID `0x0012019F`、Authorized Client `0x00120183`，不存在 Everyone 或 Authenticated Users ACE。
7. Security acceptance 读取正在运行的 Blink.IndexService.exe 进程 Token：通过 OpenProcessToken + GetTokenInformation(TokenUser/TokenGroups/TokenPrivileges) 获取 TokenUser、全部 TokenGroups、全部 TokenPrivileges，并通过 LookupPrivilegeName 规范化 privilege 名称。`security\service-token.json` 与 telemetry\service-token.json 同时保存 SCM registration/configuration、运行时 process image/PID、Service SID group attributes、SeImpersonatePrivilege attributes/enabled state 以及完整 privilege 列表。Run `20260811-233345` 的 runtime predicate 通过：PID/image 匹配、TokenUser 为 LocalSystem、Service SID 存在于 TokenGroups、SeImpersonatePrivilege 存在且 enabled。
8. 单独的人工入口 `scripts\run-phase05-security-acceptance.ps1` 只调用 targeted BLK5/USN/Search 路径，不重跑 Initial MFT。Run `20260811-233345` 的 targeted security slice 为 PASS、failures 为 0；其顶层 `final_result=NOT READY` / `phase1_recommendation=DO NOT PROCEED` 是 slice-scoped runner 的固定聚合策略，不是 security failure。
9. Manual attempt `20260811-220237` exposed an installer interop defect before service start: the P/Invokes did not explicitly bind the Unicode `ChangeServiceConfig2W`/`QueryServiceConfig2W` entry points, and the query parser used the success-call `pcbBytesNeeded` as the MULTI_SZ length. The observed `SERVICE_SID_TYPE_UNRESTRICTED` persisted, while `RequiredPrivileges` was empty in both `sc qprivs` and the SCM registry value. The minimal W-entry-point/buffer-bound fix is now implemented; no runtime Token/Pipe/BLK5 evidence was produced by that attempt. The follow-up `20260811-220253` was only the expected existing-service guard caused by the failed attempt's orphan registration, not an independent security result. Acceptance cleanup now tracks a pre-return CreateService attempt so future installer failures do not orphan a run-owned service.
10. Manual attempt `20260811-221121` reached a running hardened candidate and recorded a matching actual Pipe descriptor, but the PowerShell Harness could not connect because its default `NamedPipeClientStream` constructor requested generic read/write rights that exceed the intentionally exact client ACE. The production `Blink.Index.Contracts` native client already requests the exact `0x00120183` rights. The minimal Harness-only fix uses the `PipeAccessRights` overload with `FILE_READ_DATA | FILE_WRITE_DATA | FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES | READ_CONTROL | SYNCHRONIZE`; an ordinary exact-DACL temporary-pipe smoke passed. No service policy, Pipe DACL, C++ business logic, or timeout was changed; at that time manual security acceptance remained pending and was subsequently closed by Run `20260811-233345`.

Security Closure additionally recorded and fixed two real defects before the accepted run:

- **Impersonation lifecycle / unsafe process-token fallback**：`ImpersonateNamedPipeClient` 成功后通过异常安全 RAII guard 保证 `RevertToSelf`；Revert 失败会记录 Win32 error、fail closed 并终止服务条件；身份查询失败不再用服务进程 Token 冒充客户端身份。Run `20260811-233345` 的 telemetry 反复记录 `impersonate_success`、`revert_attempt`、`revert_success`，无 revert error。
- **PIPE_ACCESS_DUPLEX server ACE 缺少扩展属性权限**：普通 A/B 回归证明旧 `0x00120187` 的第二实例以 `ERROR_ACCESS_DENIED(5)` 失败，而 `0x0012019F` 成功；生产 Server ACE 已修正为 `0x0012019F`，Authorized Client 保持 `0x00120183`，不授予客户端 `FILE_CREATE_PIPE_INSTANCE`。

历史 FAIL/NOT READY runs 保持各自原始记录，不回写为 PASS；它们是 Harness、权限和安全收敛过程的证据。虚拟服务账户与 `SERVICE_SID_TYPE_RESTRICTED` 保留为 future hardening experiment，不再作为 Phase 1 blocker。

## D-0024：Phase 0.5 综合状态与 Phase 1 gate

> **Status: ACCEPTED / COMPLETE**  
> **Phase 1 gate: READY TO PROCEED**

Run `20260811-233345` 完成 Security Closure 后，当前综合状态汇总如下：

- full MFT/USN initial index 与增量维护：ACCEPTED；
- persistent BLK5 save/load、checkpoint 与 journal recovery：ACCEPTED；
- Search correctness/performance acceptance：ACCEPTED；
- continuous-write Search sentinel convergence：ACCEPTED（Run `20260811-201439`）；
- service failure isolation（Stop/Kill/reconnect）：ACCEPTED；
- corruption 与 journal-invalid / `RebuildRequired`：ACCEPTED；
- Windows Restart Acceptance：ACCEPTED（Run `20260811-204329`）；
- Security Closure：ACCEPTED（Run `20260811-233345`）。

这些结论不改写任何历史运行的 `FAIL` / `NOT READY` 字段。Targeted/security/restart runner 的 slice-scoped `final_result=NOT READY` 仍按其固定聚合策略解释；本条只记录跨运行、已完成条件的当前综合 gate。Phase 0.5 不再有已识别的核心 blocker；Phase 1 gate 更新为 `READY TO PROCEED`。这不是本轮进入 Phase 1 的指令。

## D-0025：第三方扩展的进程外架构准备（Extension-capable by design）

> **Status: APPROVED AS LONG-TERM ARCHITECTURE PRINCIPLE**

### Context

Blink 未来可能需要 Provider、Command、Metadata、Preview、Search/Index Enricher 和 Automation/Agent 类扩展，但当前 Phase 1 的目标仍是稳定的本地文件管理器。若现在把第三方代码直接加载进 `Blink.exe`，会把扩展故障传播、UI 卡顿、内存破坏、ABI 耦合和安全边界问题引入主进程；若现在冻结完整公开 SDK，又会过早承诺尚未验证的对象模型和权限语义。

### Decision

Blink is extension-capable by design. Third-party extensions are expected to run out-of-process through versioned capability-based contracts. Phase 1 does not implement the public extension SDK.

未来第三方扩展默认沿以下方向隔离：

```text
Blink.exe
    │ versioned extension protocol
    ▼
Blink.ExtensionHost.exe   (future, ordinary-user component)
    ├─ Provider extension
    ├─ Command extension
    ├─ Metadata extension
    ├─ Preview extension
    └─ other third-party extensions
```

第三方插件失败、崩溃或卡死不得直接冻结或崩溃 `Blink.exe`。默认禁止 `LoadLibrary(third_party_plugin.dll)` 这类把不可信第三方代码注入主进程的公开扩展机制。现有 `ProviderHost.exe`、以及未来可能的 `MetadataHost`/`PreviewHost`，继续遵循故障隔离方向；不在本条中统一重构这些 Host。

### Extension categories and composition

- **Provider Extension**：继续遵守已有 Provider abstraction，不建立第二套平行文件系统模型。
- **Command Extension**：未来可注册 context-menu、toolbar、file action 或 directory action，但不得取得 Blink 内部 WinUI 控件或内部对象指针。
- **Metadata Extension**：与 Windows Property System / Blink metadata architecture 组合，第三方 handler 不运行在 UI thread。
- **Preview Extension**：在独立隔离边界中运行。
- **Search / Index Enricher**：只能增加派生字段、标签或辅助搜索信息；不得修改 `Blink.IndexService` 的 authoritative filesystem facts、Volume/File identity、USN、delete 或 checkpoint correctness。
- **Automation / Agent Extension**：可复用稳定 contract 的设计原则，但 `Plugin System != AgentBridge`，两者不合并为同一组件。

Phase 1 只保留组合能力，不为了未来可能的插件创建空的 `CommandRegistry`、`ProviderRegistry`、`MetadataProviderRegistry` 或 `PreviewProviderRegistry`。只有当当前产品已经有多个同类实现需要统一注册时，才建立相应的应用内 registry。

### Capability and identity boundary

未来插件采用 Blink-level capability 声明方向，例如 `filesystem.read`、`filesystem.write`、`network`、`provider.register`、`command.register`、`metadata.read`、`metadata.provide`、`preview.render`、`index.read` 和 `index.enrich`。这只是最小授权 contract 的架构方向，不表示当前已经提供浏览器级 sandbox、权限管理 UI 或 OS 级隔离保证。

未来公共数据模型不得依赖 `BlinkInternalViewModel*`、WinUI control、`TabState`、内部 C++ class address、raw pointer 或内部 STL ABI。`FileIdentity` 继续使用 Volume identity + File ID；path 只是可变化的定位信息。当前不冻结完整 `FileRef` ABI。

插件不得通过扩展边界获得 `Blink.IndexService` 的任意特权文件能力；不得增加 `ReadAnyFileAsSystem`、`WriteAnyFileAsSystem`、`DeleteAnyFileAsSystem`、`RunCommandAsSystem` 等 privileged backdoor。`Blink.IndexService` 仍只负责已定义的 privileged indexing duties。

### Custom UI separation and versioning

Plugin System 与 Custom Frontend System 是两个独立边界。未来自定义 UI 应通过独立 frontend process 和 versioned frontend contract 工作，不通过 `CustomUi.dll` 注入或直接操作 Blink 内部 WinUI controls。本阶段不实现 frontend protocol。

产品版本与扩展协议版本独立演进，例如 `Blink Product Version`、`Extension Protocol`、`Provider Contract` 和 `Agent Protocol` 不得互相绑定。未来 contract 需要考虑 capability negotiation、protocol version negotiation、unsupported capability handling 及向前/向后兼容；本阶段不实现 negotiation code。

### Phase 1 implications and deferred work

Phase 1 的 `WindowState`、`TabState`、`NavigationModel`、`FolderViewController`、未来的 `IndexServiceCoordinator` 和 `FileOperationCoordinator` 保持 app-private，不作为第三方 ABI。WinUI 继续只是 Application Model 的一个 frontend；第三方以后只能访问明确暴露的稳定 contract。

以下内容明确延期：公开 Plugin SDK、`ExtensionHost.exe`、plugin manifest/parser、capability manager、权限管理 UI、第三方加载器、插件安装/签名/市场/分发/自动更新、兼容性保证、sandbox 最终实现、frontend protocol 和完整稳定公共 `FileRef` ABI。`Extension readiness != Extension implementation`，本条不授权进入插件实现或 Phase 1B/1C/1D。

### Audit result

对当前 Phase 1 代码的只读审计未发现第三方 DLL 注入、把 path 当作永久 identity、Provider UI 直接依赖具体 host 实现或 IndexService privileged mutation backdoor。现有 `ProviderHost.exe` 隔离、`FileIdentity` 模型和 app-private UI state 边界与本决策一致；因此本条 **NO CODE CHANGE REQUIRED**。

## D-0026：Phase 1A 本地化资源基础

> **Status: APPROVED FOR PHASE 1A FOUNDATION**

### Decision

Phase 1A 使用 MRT Core `.resw` 作为固定用户可见文案的资源来源，首批资源为
`en-US` 与 `zh-CN`。Blink 仅增加 app-private 的 `blink::app::Localization`
薄封装，不引入复杂本地化框架，不改变 WinUI/应用模型，也不把资源键作为公共
插件或 IPC ABI。

### Scope and boundary

- 应用标题、导航、工具栏、列头、状态、文件类型、大小单位和常见浏览错误资源化。
- 路径、文件名、FileIdentity、Win32 数字码、operation stage、IPC/telemetry 字段
  仍按数据语义输出，不将其翻译成用户文案。
- 语言遵循系统资源选择与回退；运行时语言切换、翻译管理工具和插件文案 contract
  延后到后续阶段。
- 缺失 PRI 的普通测试/降级环境允许安全英文 fallback，但发布构建的正常资源来源
  是生成的 PRI。

## D-0027：Phase 1 产品交互与本地文件管理范围

> **Status: APPROVED FOR PHASE 1 PRODUCT-SCOPE BASELINE; PRODUCT SURFACE FROZEN**

### Decision

Blink 的产品行为以 Windows 文件资源管理器的用户肌肉记忆为兼容目标，视觉样式保持
Blink 自己的 UI 方向。Blink 使用自己的 `Location`、文件系统和 Application Model；
Windows Shell Namespace 不是主导航模型，第三方 Explorer Shell Extension/Context-menu
handler 也不进入 Blink 主进程。

### Location and navigation

- 默认启动位置是 Blink-native `ThisPC`，不是 exe 目录、Current Working Directory、项目目录或 Desktop。
- `ThisPC` 是虚拟 Location，不伪造 `C:\This PC` 路径；地址栏显示本地化 This PC/此电脑。
- This PC 至少显示 Desktop、Downloads、Documents、Home/用户目录（按最终 UI 设计）以及当前可访问的固定/可移动本地卷。
- 导航树按需异步展开，不预先递归扫描；卷插入/移除、mount point 变化和不可用卷必须有明确状态，不能崩溃或阻塞其它标签。
- Folder、ThisPC、Search result 和 unavailable volume 不能被压扁成路径字符串；每标签独立保存 Location、history、selection 和 view。

### Explorer-compatible interaction contract

Phase 1 正式冻结：Single click、Ctrl/Shift selection、Ctrl+A、double-click directory、
double-click file/Enter via current-user association、F2、F5、Delete、Shift+Delete、
Ctrl+L/T/W、Ctrl+C/X/V、Ctrl+Shift+N，以及可行时 Alt+Left/Right/Up。Details、List、
Grid/Icons 切换不得改变 Location、FileIdentity、selection truth 或 history。

Blink-owned context menu 提供 Open、Cut、Copy、Rename、Delete、Properties 策略；空白处
提供 Paste、New Folder、Refresh、View、Sort。Drag & Drop 是 `FileOperationCoordinator`
的输入方式，不是另一套 copy/move engine；Blink、Explorer/Desktop 和跨标签拖放都复用
同一 operation model。

### Filesystem operation safety

- 普通 Delete 请求 Windows 系统 Recycle Bin；Blink 不建立 `.BlinkRecycle`、Trash database
  或自定义 deleted-files storage。回收不可用时报告失败并保留源文件；Shift+Delete 才能
  进行永久删除且必须确认。
- Phase 1D 包含 File Copy、递归 Directory Copy、同卷 Move 和跨卷 File/Directory Move。
  跨卷 Move 使用 copy-then-delete，只有确认所有 required items 成功后才删除源树。
- 冲突策略按 operation/object type 区分：File Copy/Move/Paste 使用 Replace、Skip、Keep Both
  或 Cancel；Directory-to-existing-directory 使用 Merge、Skip、Keep Both 或 Cancel。Merge
  不得先删除整个目标目录，子项冲突继续递归使用对应 conflict policy。Rename target-exists
  至少使用 Rename Again 或 Cancel，不机械套用 Copy 选项；其它替换语义必须先单独完成安全设计。
  Keep Both 名称生成必须 deterministic、Unicode-safe、保留 extension 且不得覆盖已有对象。
- Directory Copy/Move 默认绝不 follow junction、symlink 或其它 reparse point；只有能可靠复制
  reparse object 本身时才复制，否则明确 `Unsupported`。禁止为了成功而跟随 target contents；
  实现前重新完成 API、循环和权限审计。所有操作在普通用户进程执行，不增加 IndexService mutation command。
- External→Blink 与 Blink→Blink 的 Clipboard/Drag & Drop 实际 operation 由
  `FileOperationCoordinator` 拥有。Blink→External 时 Blink 只发布标准 Windows filesystem
  transfer data，由 external target 拥有实际 operation。Cut 在 external target 明确执行操作前，
  Blink 不得自行删除 source。Blink 内部 clipboard 不能取代 Windows filesystem clipboard representation。

### Search and external state

- Search scope 至少为 `ThisPC`/All indexed local volumes 与 `CurrentFolder`。每卷状态独立，
  一个卷 Rebuilding 不冻结其它卷；IndexService 不可用时禁止偷偷全卷 DFS fallback。
- 结果携带 Name、Path、Type、FileIdentity、source volume 和可区分的 row/link identity，支持
  Open/Locate，且操作前重新验证 identity/path/link。两个不同 Parent ID + Name links 即使
  FileIdentity 相同，也必须作为独立结果和独立 action target。路径是定位信息，不是永久身份。
- 当前目录的外部 Create/Delete/Rename/Move 通过 watcher 的 debounce/reconcile 最终收敛，
  保留仍有效的 selection，关闭 tab/window 后取消 watcher 和 callback。
- 默认不显示 hidden items；Phase 1 的 Blink-owned Show hidden items 只定义 hidden-items
  policy。Protected operating-system items 默认继续隐藏，完整 protected-system-items policy
  保持 Deferred；不依赖 Explorer 全局设置作为 correctness 条件。

### Hard-link row identity

Authoritative `FileIdentity` 继续是 Volume identity + File ID。同一 File ID 的多个
Parent ID + Name links 必须在 UI 和 Search result 中作为独立 row/result：可单独选择、Rename A
不影响 B、Delete A 只删除 A link，每个 result/action target 都指向正确的 link。可在 app-private
层引入 `LinkIdentity`/`RowIdentity`，但本 ADR 不冻结公共 ABI。

### Explicitly deferred

本阶段不实现或不承诺：Recycle Bin 浏览/恢复/清空 UI、完整 Open With UI、Favorites/Pinned
自定义、Undo history UI、Preview pane、Thumbnail pipeline、完整 Metadata/Property Handler、
Archive/SMB/SFTP/WebDAV/Cloud/MTP、Provider 产品化、Shell Namespace、第三方 Shell
context-menu handlers、公开 Plugin SDK、ExtensionHost、AgentBridge/MCP、multi-window、
session restore（若后续未另行纳入）、高级主题/动画和完整 accessibility polish。

以上范围补全不授权本轮实现 This PC、Tabs、RecycleBinAdapter、DirectoryCopyEngine、
DragDrop、Clipboard adapter、ContextMenu 或 Search UI；本条只是 Phase 1 产品范围和验收基线。

### Freeze status and Phase 1E boundary

本次语义澄清后，D-0027 定义的 Phase 1 Product Surface 正式冻结；后续 Phase 1 工作只能在既定范围内完成实现、验证和最小 ownership 整理，不得借机扩大功能族。Phase 1E Code Hygiene Closure 是 review/audit slice，不是新的产品功能或架构重写；其要求包括唯一 authoritative navigation/search/file-operation 路径、受管理的 worker/task 生命周期、UI 与业务事实解耦、删除或隔离被替代的 Phase 0 prototype、无重复实现、无 silent fallback、无 swallowed correctness/security error，以及明确的 TODO/FIXME 分类。


## D-0033：搜索结果使用 Windows 原生完整菜单 — 2026-11-08

用户明确要求搜索结果支持基础文件操作、复制位置和定位，并进一步要求兼容 Windows 已注册菜单（例如 Bandizip）。本次授权将搜索结果的第三方 Shell 菜单承载纳入开发范围，覆盖 D-0027 对该项的延后；其余产品范围不借此扩大。

采用独立普通用户 `Blink.ShellMenuHost.exe` 承载 `IContextMenu`，转发系统级联/绘制消息，使用系统完整经典菜单。Windows 标准 open/copy/cut/paste/rename/delete 回到 Blink 现有导航、标准剪贴板和唯一操作队列；其余系统/第三方命令由 Windows 执行。补充复制完整路径、复制父目录、定位和明确粘贴到父目录，避免搜索结果来自不同目录时目标含糊。

菜单取消只作用于尚未选择的请求；已选原生操作有自己的窗口与 Shell 线程引用，不随 Blink 关闭而终止。原生结果回包仅代表已交付调用，不提前报告成功。此处沿用 D-0025 的进程外承载原则，不建立公开扩展 SDK、扩展平台或新的索引/本地防护服务。实现和验证结论见 [搜索右键记录](docs/SEARCH_CONTEXT_MENU_HANDOFF.md)。


## D-0034：搜索主菜单采用 Blink 外观，完整系统菜单保留为扩展入口 — 2026-11-08

用户在看到D-0033的原生完整菜单后，明确要求风格与Blink一致。默认右键展示改为同一WinUI控件体系的轻量MenuFlyout，常用命令复用既有搜索意图和操作队列；图标、圆角、分组、主题与快捷键提示由Blink控制。压缩及其它已注册扩展通过明确的“压缩与更多系统选项…”进入D-0033的原生helper。系统属性保留完整原生页。

不把任意第三方owner-draw扩展转录为另一套自绘菜单，不新增扩展协议或公开SDK。主菜单展示不加载扩展、不读文件元数据；点击后沿用原有真实校验和取消边界。最终验证见docs/SEARCH_THEMED_MENU_HANDOFF.md。


## D-0035：普通目录与搜索使用同一菜单外观及共同操作 — 2026-11-08

用户要求普通目录同步搜索结果的Blink风格菜单。两处共用ContextMenuAppearance定义圆角、主题、图标、行高和快捷键提示；普通目录保留新建、刷新、视图及排序。复制完整路径在多选时每行一个原名称路径，复制所在目录路径只输出当前父目录一次，文本剪贴板仍在原worker发布。基本文件操作继续使用原唯一队列；普通目录的属性继续使用既有Blink只读属性窗口。

普通目录的“压缩与更多系统选项…”支持同一父目录多选，以一次IContextMenu查询保留Windows注册扩展。原生基本命令仍映射回Blink策略，多选不悄悄只处理右键的一项。菜单绑定标签、目录呈现、文件操作及选择代次；取消只针对尚未选择的请求，原生任务一旦选中仍由helper保活。无新增索引协议、独立防护或第二套文件操作管线。验证记录见docs/FOLDER_MENU_SYNC_HANDOFF.md。
