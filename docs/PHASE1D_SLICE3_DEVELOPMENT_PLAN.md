# Phase 1D Slice 3 — File Copy安全底座（下一阶段）

状态（2026-10-15恢复开发）：单普通文件复制、窗口coordinator进度、标准CF_HDROP复制/粘贴与冲突改名重试已实现，完整Debug/Release构建及各685项普通检查PASS、0 FAIL、exit0；各2项file-symlink fixture因1314明确SKIP，完整证据见PROGRESS。此前16:59停止记录保留为历史；本次用户明确授权今天使用3个子代理，主模型集成，compiler并发1，人工UI验收仍暂缓。

## 已核对的现有实现

Blink.Transfer/CopyService当前是Phase0 prototype：CopyFileAsync使用std::async，dwCopyFlags只有COPY_FILE_RESTARTABLE，不能直接作为禁止覆盖的产品操作；future析构可能等待后台，不能让窗口关闭在UI释放最后一个在途future。canceled/paused目前读请求标志，必须分离请求与实际结果；S_OK后晚到取消不能伪装撤销。progress_callback需要在C ABI回调边界捕获异常。

## 下一阶段行为

- 先完成单个普通文件的identity-bound copy与coordinator进度/合作取消，不冒充递归Directory Copy或完整剪贴板。源/目标本地父目录身份与reparse链在worker核验，source保护到复制实际返回；未知或不可靠reparse复制明确Unsupported，绝不follow。
- 审计[CopyFile2](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-copyfile2)及COPYFILE2_EXTENDED_PARAMETERS的FAIL_IF_EXISTS、symlink/reparse与取消清理语义。必须真实验证已有目标、同一对象/硬链接、目标reparse及并发替换，不能以先检查exists再写入充当原子防覆盖；API无法保证时改用受控handle方案或明确Unsupported。
- 冲突由明确策略驱动；第一刀默认不替换已有目标，保留源，失败/取消不得删除非本操作创建的目标。实际S_OK/ERROR_REQUEST_ABORTED/ERROR_REQUEST_PAUSED决定结果，精确保存partial状态，未验证恢复能力时不开放Pause UI。
- owned worker直接持有复制/guard/control生命周期，不嵌套无人持有的future；进度合并派发，关闭立即使UI回调失效，实际I/O完成后释放。源完整、目标正确和取消结果用owned fixture oracle证明。

## 后续仍独立

递归目录复制及循环/reparse/partial策略、同/跨卷Move、系统剪贴板CF_HDROP/DropEffect、Drag & Drop、回收站Delete与确认永久删除按后续slices推进；不扩大服务mutation、ACL或提权边界。

## 本轮完成的API核对

[扩展参数](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-copyfile2_extended_parameters)中FAIL_IF_EXISTS是原子冲突入口，但与RESUME_FROM_PAUSE组合有restart-header例外，首刀不开放恢复。COPY_FILE_DIRECTORY仅处理目录对象的数据流/reparse/EA，不等于递归复制子树；OPEN_AND_COPY_REPARSE_POINT要求调用者理解具体tag，不能据此宣称所有reparse均可可靠保留。

[进度回调](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nc-winbase-pcopyfile2_progress_routine)的CANCEL删除partial，STOP保留partial，两者都返回ERROR_REQUEST_ABORTED；PAUSE可能留下restart header，也可能在完成后返回成功。结果必须保存实际status与操作语义，不能仅由Cancel/Pause请求布尔值推断已取消或残留。

[Windows11 V2参数](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-copyfile2_extended_parameters_v2)提供DONT_COPY_JUNCTIONS及IO节奏设置，仍需测试其它reparse类型和目标端竞态，不能替代source/parent identity和no-overwrite保证。当前没有采用新flag或改变prototype行为。

下一次先实现/验证受控native File Copy入口：零字节、Unicode/long path、source identity替换/删除、已有普通/硬链接/reparse目标、占用/权限、成功后晚取消、实际取消清理、callback异常以及源保留；再接窗口任务进度和复制命令。

## 普通复制探测结果与未执行项

08:52–08:53自有fixture中，FAIL_IF_EXISTS对已有普通目标返回0x80070050且两边数据保留；新目标返回S_OK且内容一致。destination symlink的existing/dangling两项在正确BOOLEAN(I1) marshalling与reparse属性检查后，因普通用户CreateSymbolicLinkW返回1314而未执行。初版probe错误地以默认BOOL处理BOOLEAN返回值，造成fixture成功假象，该JSON保留但不作为链接行为证据。完整判定见`.build/phase1d-slice3/probe-assessment.json`。

下一阶段必须补真实链接fixture及并发替换测试；当前并未证明该端no-follow，也没有开放产品复制入口。

## 本刀采用的执行路径

`ClipboardFileTransfer`在worker处理标准Unicode CF_HDROP和Preferred DropEffect COPY。Ctrl+C/右键复制只发布路径，不删除源；Blink→外部由外部目标拥有实际操作。Ctrl+V/背景粘贴只接收单个本地普通文件，拒绝剪切/链接和多选/虚拟对象。Blink自己发布的序号与路径仍相符时保留当时选择的source identity；外部数据在worker捕获source/parent identity，提交前再核验目的标签committed identity。窗口关闭或导航后不能由迟到捕获启动错误位置的操作。

`FileCopyOperation::CopyFileSafely`使用受控handle读写而非调用prototype `CopyFileAsync`。原因是产品路径必须把源身份、默认流读取、CREATE_NEW原子防覆盖与失败清理绑定到同一批owned handles；当前CopyFile2路径接口没有满足这组所有权证明，prototype也有隐式覆盖/future析构阻塞/取消结果失真的问题。这里不做失败后切换API的fallback，不建立第二条产品复制路径。

源和目标父目录复用DirectoryLease的本地目录链/reparse检查及无DELETE共享lease。源GENERIC_READ且只share READ；目标CREATE_NEW、OPEN_REPARSE_POINT、DISALLOW_PATH_REDIRECTS且share0。源、已有文件、硬链接、目录和目标reparse均不被覆盖。读取/写入按1MiB合作取消；失败只通过本次CREATE_NEW目标handle设置disposition，绝不按名字删除可能属于其它人的对象。清理失败真实标注partial_file_left并刷新目标目录，不伪造回滚。

提交前再次检查stream/attributes；本刀仅支持单个默认数据流和普通基础属性。ADS、reparse、压缩、稀疏、加密、云召回/离线等源明确Unsupported。保留默认流、基础属性和创建/访问/修改时间，ACL继承目标父目录；不是备份API，不承诺EA/security descriptor/object ID/任意同权限并发metadata的完整原子快照。后续扩大metadata兼容前需独立审计与fixture。Flush及基础metadata成功后才提交copied=true，再发布唯一100%进度；此后晚取消/observer异常不能逆转实际成功。不支持Pause、递归Copy或Move。

窗口单worker负责所有mutation；Copy progress按100ms合并，每个publication保存不可变快照，取消保留实际native所有权，Shutdown不在UI等待。后台进度和剪贴板派发都受窗口关闭与operation id/origin限制。成功或真实partial按目的父identity刷新相关标签，不改服务IPC。

现有目标第一刀只允许显式修改目标名后重试或取消，永不自动覆盖/自动KeepBoth。Phase1D要求的Replace/Skip/KeepBoth/Cancel完整冲突策略仍独立待实现，此刀不标Phase1D完成。

依据：[CreateFile3](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfile3)、[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)、[File Streams](https://learn.microsoft.com/en-us/windows/win32/fileio/file-streams)、[Shell Clipboard Formats](https://learn.microsoft.com/en-us/windows/win32/shell/clipboard)、[SetClipboardData](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setclipboarddata)。
