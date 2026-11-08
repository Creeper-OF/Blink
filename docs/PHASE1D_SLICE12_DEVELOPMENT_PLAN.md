# 同卷移动backend与任务协调 Implementation Plan

> 使用superpowers:executing-plans主模型直接执行。用户要求继续开发，禁止子代理，不另等审批。

**Goal:** 完成普通用户文件/目录同卷Move后端与唯一coordinator接线，为后续完整Cut/Move/Paste提供真实结果。
**Architecture:** 原source/target DirectoryLease + same-handle来源身份，目标父handle-relative的固定NtSetInformationFile extended rename，Flags=0。仅明确KeepBoth尝试最多1000个序号，占名靠单次原子rename；无fallback、覆盖、递归copy或删除。
**Tech Stack:** C++20、Windows11 24H2、既有D:\DEV\C++。
**Spec:** PHASE1_PLAN.md 1D第256–260条、D-0027及Slice11_HANDOFF.md。

## Global Constraints

- 主模型/compiler1/native串行；人工UI、实际剪贴板、真实服务/admin/MFT暂缓。
- 全部正常copy和rename回归保留；已有983/0/4恢复包先保存；冻结IPC/服务不改。
- Skip/Cancel明确无FS访问；Replace仍Unsupported；预提交取消保留源，真实提交后晚取消/observer异常不逆转。
- 同卷保留真实FileIdentity/ADS/目录子树，不follow reparse目标；source根/parent reparse拒绝。跨卷明确ERROR_NOT_SAME_DEVICE，不隐式copy/delete。
- 完整目标仍含跨卷Move、Cut/Paste和多项Move；本刀是实现顺序，不能据此标整个Move或Phase1完成。

## Review Focus

- 目标parent在最后检查后转junction：同句柄相对rename不写外部target，真实探针及fixture验证。
- 源ID错误碰为目标祖先：先核验actual source，再判断目录自身/后代，返回1006。
- 实际rename已成功后observer/summary错误或晚取消：保留moved及实际目标path，不能回滚/伪报无改动。
- KeepBoth不替换竞争者、文件保留extension、目录整名编号及UTF-16截断。
- source和两条parent链held至native返回，路径变化/失效reparse在创建/移动前拒绝。

### Task1 — native adapter与Move API/test

Create HandleRename.h/.cpp、MoveOperation.h/.cpp、MoveOperationTests.h/.cpp；Modify两个vcxproj与test main。
Interfaces: detail::RenameRelativeByHandle(HANDLE source,HANDLE parent,const wstring& leaf)->DWORD；MoveRequest公共source/destination字段及conflict_policy；MoveResult(error,moved,skipped,unchanged,source_path,path,post_commit_error)；MoveProgress(phase,source_path,destination_path)；MoveEntrySafely(request,cancel,progress,optional NativeMoveFunction) noexcept。

- [x] 先真实owned fixture测试并运行RED：文件/目录/ADS/硬链接、same-name no-op/case-only、占名、wrongIDs/type、self/descendant/cross-volume、parent/source guard、取消/晚取消、来源替换、long path、KeepBoth/竞态/UTF16/上限、root/chain reparse及实际commit窗口重定向。
- [x] 核实固定native adapter、boundedleaf buffer、0 flags、明确NTSTATUS→Win32映射及同步句柄等待；方法缺失Unsupported，不尝试其它API。
- [x] Move实现先核对实际源ID再查祖先；DELETE guard重新核对同ID；预分配返回路径和native buffer后调用；成功立即保存真实moved。

### Task2 — coordinator

Modify FileOperationCoordinator.h/.cpp，新增MoveEntry API、kind、独立move request/progress/result，复用Submit和唯一IAsyncAction。

- [x] 先测试真实成功、取消/晚取消、占名、action退出边界、Shutdown、异常与旧callback，观察RED。
- [x] 原生moved/unchanged/skipped决定终态，不以最后stop flag猜测结果；独立warning保留，generic committed反映真实移动。
- [x] UI/Cut/批量Move未接时保持现有用户命令反馈；不宣称可用。后续Slice继续完整产品接线。

### Task3 —验证归档

- [x] 冻结全部输入，完整Debug/Release构建及普通检查，资源248一致、原有4权限SKIP明确；核对冻结合同和legacy摘要。
- [x] 主模型针对Review Focus审查，审计native/aliases/OOM及附录B既有资产，保存源/docs/evidence快照，更新进度/恢复说明和工作区索引。
历史时限记录：2026-10-27 16:32:37截止未按时收束，原停止记录保留；2026-10-28用户恢复推进，本轮未指定新截止，项目整体仍未完成。

## 停止记录 — 2026-10-28

用户窗口已过，停止新增开发并保存。Task1 backend中间Debug1018/0/4；Task2仅测试和Unsupported API占位，尚未观察该轮RED；当前175输入只有完整Debug构建通过、Release未执行。测试日志不能套用于新coordinator源码，详见HANDOFF和stop-state.json。

## 恢复后完成原生验证 — 2026-10-28

唯一Move coordinator、真实结果/NoOp/Skip/警告及全部生命周期接线已实现；恢复RED1020/15/4，扩充RED1020/21/4，最终完整Debug/Release各1041/0/4。175输入、248资源、冻结合同/旧EXE/工程及工作区审计通过。当前backend/coordinator检查点完成；产品Cut/Move/Paste和多项/跨卷Move继续Slice13等后续任务，不把本刀标为整项目完成。
