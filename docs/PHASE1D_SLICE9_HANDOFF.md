# Phase1D Slice9 — 单目录产品入口恢复清单

状态：2026-10-16完整Debug/Release构建与各931项普通检查PASS、0 FAIL，各4项权限1314 SKIP。165输入、10个EXE和两个Blink.pri摘要、242资源及冻结IPC/服务已核对；最终证据见PROGRESS及`.build/phase1d-slice9/integration-20261016-2110/native-validation.json`为准。人工UI、实际Windows剪贴板互操作、真实IndexService及管理员/MFT验收继续暂缓，Phase1D和整项目未完成。

## 已实现的用户流程

普通文件多选维持串行队列；单个普通本地目录的Ctrl+C/右键复制和粘贴现在进入同一个FileOperationCoordinator::CopyDirectory。混合文件/目录、多目录、reparse及不支持的存储整包拒绝。来源在worker捕获，UI不遍历目录树；实际engine仍重验所有身份并执行有界整树预检，目录metadata捕获成功不代表内容必定可复制。

目录状态独立显示检查/复制、实际文件完成数、目录创建数、当前文件字节与失败路径。DirectoryCopyResult的copied/partial_tree_left决定原目的地刷新；通用partial_file_left按设计为false，不用于目录判断。即使只创建空root，取消后仍保留并显示真实partial tree。

同名root只提供新名称或取消；没有Merge、Replace、自动KeepBoth或路径回滚。失败/取消后的目录重试是新任务，保留原source及目标父身份，与文件重试和新建空目录分支明确区分。终态发布后异步等真实native action返回，期间不启动新mutation。重试成功仍保留早先partial提示。

## 异步失效与身份

- FileOperationOrigin保留tab/location/parent identity以及TabRecord.file_operation_generation。手动导航（含实际发起后失败）、切出标签、进入/离开搜索和Shutdown使旧intent永久失效；A→B→A不能重新提交。自动同ID刷新不增加世代，真实替换identity仍拒绝。全部新建/重命名/文件与目录复制、对话continuation共用此门禁。
- 用户离开页面会合作取消未完成的clipboard准备；UI在准备完成后再次检查取消与origin，然后才绑定目的地/提交。已经执行的复制继续拥有原目的地，不因导航重定向。
- 内部剪贴板cache包含typed模式、实际发布有序路径、sequence及最初source/parent IDs。标准外部COPY数据由worker捕获；内部payload匹配时必须保留最初身份，目录同名替换不能退化成外部来源接受。成功的实际剪贴板发布即保存原身份，即使发布调用开始后发生取消/导航，避免丢失已发布数据的来源绑定；迟到回调不自动粘贴。
- 来源列表的顺序、重复项和硬链接名称保留。PrepareCopyDestination以完整新mode一次绑定当前父identity，错误清空输出，无有效前缀，file/dir不会相互fallback。旧CaptureFileCopySource(s)接口继续files-only。

## 有界partial提示

DirectoryCopyPartialHistory只属于当前显式目录复制及其改名重试链。operation ID去重并拒绝旧事件，保存最早至多32条路径且合计不超过65536字符；计数包含所有留下partial的尝试，显示被截断时明确说明。新的显式目录复制可开启新提示链，磁盘目录始终保留，不暗示已清理。它不是任务恢复/续传或持久化队列。

## 代码与证据

| 位置 | 责任 |
| --- | --- |
| `FileOperationOrigin.h`、`TabWindowState.h/.cpp` | 共享来源门禁与世代生命周期；普通确定性延迟测试。 |
| `CopyTransferPresentation.h` | typed缓存匹配、来源身份比较、整包目的地绑定、目录刷新与partial提示事实。 |
| `main.cpp` | Copy/Paste worker准备、UI二次门禁、目录独立发布/展示/名称重试；唯一coordinator和实际action寿命。 |
| `FolderNameDialog.h/.cpp` | 复用输入对话，目录目的名称标签/automation ID独立。 |
| `CopyPresentationTests.cpp` | 12项origin门禁、21项typed transfer及目录partial展示检查，其中包含真实owned同名目录替换fixture。 |
| `.build/phase1d-slice9/integration-20261016-2110/` | 两配置构建/检查日志、输入/产物摘要、执行ledger、实际审计与最终native-validation。 |

首轮原门禁Debug905 PASS/5 FAIL/4权限SKIP，补世代后12门禁通过；typed/partial缺实现时919 PASS/12预期FAIL/4权限SKIP。两轮RED日志独立保留，完整旧Slice8输入快照含161项；最终本刀165项输入。服务重启后读取持久状态确认在途任务已结束，没有重复启动。

产品入口从普通检查与编译中获得证据，未执行生产窗口/实际剪贴板验收；[17项人工清单](PHASE1D_SLICE9_MANUAL_UI_CHECKLIST.md)仍全部空框。旧Slice8的31条实际reparse重定向诊断证据仍独立保留，本刀没有重跑或增加普通PASS。

## 下一轮

先核对最新source_sha256和本清单，再推进目录冲突策略及后续Move/Cut、拖放、Delete/回收站、完整元数据和远端Provider。Merge/多目录队列需独立设计与身份/partial验证，不能直接把已存在目录当新root。安全Replace仍禁用。按当时用户规则决定协作与时限，不自动沿用旧子代理时段。

保持工具链`D:\DEV\C++`及compiler并发1，native验证串行。使用新证据目录，不覆盖本刀日志；保护大量既有未提交/未跟踪文件，不reset/clean或用旧HEAD取代当前代码。新的完整两配置及资源/冻结IPC/产物/进程审计后才能更新通过状态，后台通过不能替代人工验收。

最终legacy补证：Phase0/Transfer共4个EXE在新构建中重新生成，已重新执行两配置owned普通回归/空入口smoke，不沿用旧摘要冒充运行。只有2个Phase05 EXE摘要不变并复用旧普通证据。Release有既有C++警告及2条CS1668无效LIB搜索路径警告；未影响链接/回归，没有额外安装或调整全局工具链。附录B既有6/1/17/0与本轮初始审计相同；`.sock.lock`在恢复首次审计已有，未擅自整理。
