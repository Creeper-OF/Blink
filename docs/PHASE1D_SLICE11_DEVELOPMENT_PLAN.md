# 多目录及混合复制队列 Implementation Plan

> 主模型使用superpowers:executing-plans逐项执行；禁止子代理。沿用项目docs位置，不新建平行计划体系。用户已要求直接执行，不额外等待方案审批。

**Goal:** Ctrl+C/外部CF_HDROP可包含多个普通目录或文件目录混合，顺序执行，冲突续队列，正确保留真实部分结果。
**Architecture:** 复用唯一FileOperationCoordinator及FileCopyBatchSnapshot，不新建工作线程体系或复制engine；每项复用现有CopyFileSafely或CopyDirectorySafely。ClipboardCopySources增添有序variant列表，保留已有文件列表和单目录形态。目录请求在队列内部保存公共字段及独立limits/result/history，目录partial不映射为partial_file_left。
**Tech Stack:** C++20 / C++/WinRT / WinUI3，普通用户，D:\DEV\C++既有工具链。
**Spec:** PHASE1_PLAN.md中1D/Windows选择剪贴板语义；Slice10_HANDOFF.md约束。

## Global Constraints

- 主模型执行；compiler1；人工UI、实际剪贴板、IndexService/admin/MFT验收暂缓。
- 1024顶层项、4MiB CF_HDROP；每目录原有10000后代/256目录/128深度/16MiB路径预算，逐项释放。
- 不合并/覆盖目录，不启用Replace，不移动/删除源，不对路径回滚已产生的树。
- 捕获完整结构与根metadata后才允许提交，失效来源逐项重核；不承诺整个队列或整树原子快照。
- 原有文件专用CaptureFileCopySources/CopyBatch仍拒绝目录；新CopyItems显式接受typed项。
- 全部既有回归保留；旧的4项mixed-rejection检查改为正向typed顺序/身份检查，新行为替代旧unsupported要求。

## Review Focus

1. 源目录与其子项同时选择或重复选择：保留用户顺序和身份，不擅自去重；真实冲突独立处理。
2. 前缀完成后后项来源变化/失败：保留已完成项和未开始Pending，不能回滚或重做前缀。
3. 冲突等待及取消：必须等实际native action退出，旧operation/item回调不能更新后续目录进度。
4. 目录已创建空root/partial后KeepBoth重试：仅显式开始新尝试，保留原partial路径/计数；历史最多32条/65536字符但计数真实。
5. 内部剪贴板同路径来源替换：缓存仍绑定最初typed来源身份，不以重捕获的新对象冒充原来源。

### Task1: Typed capture/cache/destination

Files: ClipboardFileTransfer.h/.cpp, CopyTransferPresentation.h, ClipboardCopySourceTests.cpp, CopyPresentationTests.cpp。
Interfaces: CopyItemRequest=variant<FileCopyRequest,DirectoryCopyRequest>; CopyItemRequests=vector<CopyItemRequest>，ClipboardCopySources增加该形态。单目录/纯文件保持既有形态；多个目录或混合使用有序typed列表。

- [x] 修改4项旧unsupported检查，新增混合序列、重复目录、空输出/取消/坏后项清空检查，运行并确认RED。
- [x] 实现完整metadata捕获、typed顺序/身份比较、序列与payload数量检查及目的地绑定；不枚举树。
- [x] 验证真实源ID/父ID/内容不变、缓存迟到替换不通过。

### Task2: Shared queue

Files: FileCopyBatch.h, FileOperationCoordinator.h/.cpp, new MixedCopyBatchTests.h/.cpp, Phase1 test project/main。
Interfaces: CopyItems(CopyItemRequests,PublishCallback)->operation id，ResumeCopyBatch复用policy/new_name/same-id-parent重绑。batch item公共request投影外加optional DirectoryCopyLimits、独立directory_result及有界partial历史。

- [x] 新增真实mixed顺序复制、多个目录含ADS、duplicate目录冲突、Skip/KeepBoth续队列、源后项替换、错误目录类型/limits预检、empty partial/取消/晚取消、Shutdown/旧item进度检查，观察RED。
- [x] 扩展ValidateBatch/SelectRequest/RunCopyBatch/RecordItem/Resume，以当前item索引及operation ID绑定目录进度，单独规范化目录结果。
- [x] 目录partial不设置generic file partial；取消不伪造未开始结果；已提交前缀不在resume重做。
- [x] native异常保留已观察effects；重试按原目的身份，仅当前和pending尾项重新绑定，保留bounded历史及总次数。

### Task3: Product commands and presentation

Files: main.cpp, en-US/zh-CN Resources.resw, Localization.cpp。
Interfaces: selection -> canonical ClipboardCopySources -> PrepareCopyDestination -> CopyItems，现有FileCopyPublication/ResumeFileCopy/ShowCopyConflictDialog支持当前目录。

- [x] 多选文件/目录发布标准COPY CF_HDROP，typed缓存保持源身份，外部payload整包捕获。
- [x] mixed队列显示顶层项计数及当前目录files/dirs/currentbytes，partial路径与次数可见并触发原目的身份刷新。
- [x] 仅零effects root同名自动开目录冲突dialog；partial不自动换root。原生action结束、origin generation及op ID双重门禁。手动名称重试及Skip/KeepBoth/Cancel走同一resume。
- [x] 更新限制文案，Merge/Replace禁用、实际体验待人工验收。

### Task4: Verification and handoff

- [x] 保存冻结输入，完整Debug/Release构建，各自普通检查exit0/0FAIL，4权限SKIP明确记录。
- [x] 资源键对齐、工程引用/冻结三文件/源摘要/产物摘要/legacy相同摘要复用或必要时补跑；git diff --check；Appendix B只读对比，无native/ASCII残留/OOM。
- [x] 主模型按Review Focus复查；更新PROGRESS/README/阶段计划/工作区索引；保存逐文件SHA256检查点和恢复说明。
- [ ] 截止2026-10-27 16:32:37停止新增工作并保存实际完成/待做，整个项目不标complete。

原生验证已通过983/0/4（两配置），167输入/248资源。人工验收仍未执行。审查与异常分配限制见HANDOFF；项目整体未完成。
