# Slice12 同卷移动 — 已验证恢复清单

状态（2026-10-28）：**BACKEND AND COORDINATOR IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MOVE PRODUCT UI UNCONNECTED / MANUAL DEFERRED**。完整两配置各1041 PASS、0 FAIL、4权限SKIP，175输入与248资源核对通过。Phase1D及整项目未完成。

## 已实现边界

MoveEntrySafely使用原来源/两父chain lease、same-handle身份和固定目标父handle-relative rename。普通文件与目录同卷移动保留FileIdentity/ADS/子树，源/父reparse拒绝，自身/后代目标拒绝，错误source ID先于祖先判定。metadata到DELETE guard过渡再次核对同ID；两父chain和source guard持有至native返回。source snapshot的directory bit必须与type一致，错位返回1006。

相同父/原名为真实no-op。Skip/Cancel无FS I/O；Replace50，跨卷17，不隐式copy/delete。仅显式KeepBoth以Flags0原子rename尝试最多1000候选，文件保留extension，目录整名编号，截断保护UTF-16。返回路径在native前准备；真实moved立即保存，晚取消/observer异常不逆转。提交后复核失败为独立post_commit_error，不能伪报无改动。

FileOperationCoordinator::MoveEntry已经替换Unsupported占位，使用原Submit与唯一owned IAsyncAction。独立move_request/progress/result保留原来源/目的身份与真实warning，不混入copy/directory-copy上下文或partial_file_left。仅moved代表committed；合法原地no-op成功但committed=false，合法Skip独立Skipped，错误或无事实的零error不会报成功。

进度检查operationID、kind、closed和运行阶段，拒绝未知/倒退/超限路径/终态后回调，并合并UI发布。progress Completed不能替代native结果，terminal publication不能提前释放当前action；Cancel/Shutdown、旧callback和失败COPY队列pending保护复用既有路径，不另建worker。默认native backend noexcept；注入executor自行产生effects再throw不属于默认contract，不能凭progress反推一次真实提交。

## 验证与恢复

- 当前入口：`.build/phase1d-slice12/integration-20261028-0852/session.json`、`progress.md`、`native-validation.json`。175项构建输入固定在input-sha256.json，10个EXE及两PRI摘要在validation；工程引用、248资源与冻结三文件均核对。
- 两配置各1041 PASS/0 FAIL/4权限1314 SKIP；58项新增含36 backend与22 coordinator。源身份、内容、ADS、existing target/sentinel、真实parent-junction、跨卷C:只读拒绝、1000候选、动作与生命周期边界均有owned fixture oracle；4权限跳过不算no-follow通过。
- 恢复RED1020/15/4与扩充RED1020/21/4均保留；此前987/31/4及中间1018属于旧轮历史版本，当前完整1041证据替代其作为恢复基线，未改写旧日志。
- `tests-debug-overlap-attempt.*`是一次早于完整构建结束10秒的过程运行，不计最终验收；tests-debug.*已在完整build结束后重跑，Release同样严格串行。不要把成功通知当所有验证完成，先看对应状态/时间/hash。
- 六个旧EXE与Slice11同摘要而复用历史普通证据；Transfer空入口不是行为证明。NT策略probe在research/handle-rename-probe/assessment.json，仅历史独立诊断、不混1041；实际backend reparse/identity/content检查在本轮两配置执行。真实STATUS_PENDING、allocator exhaustion、磁盘故障未注入，不宣称所有驱动/竞态验证。
- native进程/临时ASCII别名0，WSL OOM0，附录B既有6/1/17/0无新增；主模型执行和审查，compiler1，无子代理。人工UI、实际剪贴板、真实服务/admin/MFT验收均继续暂缓。

## 下一步

见[Slice13计划](PHASE1D_SLICE13_DEVELOPMENT_PLAN.md)：有序多项移动、COPY/MOVE剪贴板意图与缓存身份、Ctrl+X/粘贴、移动专用状态/冲突/名称重试及source/destination双端刷新。Cut/Move产品命令尚未开放，不能宣称文件管理器已经具备完整移动体验。跨卷Copy→Delete/source保护、目录Merge、安全Replace、拖放、删除/回收站、完整metadata与远端仍待做。

保留当前大量未提交/未跟踪开发；不reset/clean、自动回滚解压或提交。恢复先读最新session和实际任务状态，不重复启动旧任务。旧两小时截止已结束且原超时记录保留；2026-10-28用户要求继续，本轮未指定新时限。

API依据：[FILE_RENAME_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information)、[NtSetInformationFile](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntsetinformationfile)。固定策略的选择来自自有fixture实际观测；不存在native失败后切换其它API的产品fallback。
