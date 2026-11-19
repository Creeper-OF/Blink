# Clipboard OLE 发送端

2026-11-19。本批由主模型实现 Blink → Windows 文件接收端的 OLE 发送路径；人工验收继续暂缓。Release原生验证和默认运行包更新已完成；本批没有启动、关闭或激活用户窗口。接收端历史见[接收交接](CLIPBOARD_RECEIVE_HANDOFF.md)。

## 实现

- `ClipboardSourceData` 从实际文件创建 Shell 数据对象，保留 CIDA/文件系统互操作；CF_HDROP 的路径顺序、重复项及 Unicode 仍用已有编解码。普通目录、侧栏和搜索的 Copy/Cut 都接入同一发送器，路径文本复制保留即时 Win32 发布。
- 正常移动的源端清理由 Shell 对象处理。优化移动已经由目标完成，适配器在本地消费该完成通知，避免把 Paste Succeeded MOVE 再交给原生对象而删除旧路径上的新文件。重复 MOVE 完成只释放收到的 medium，不重做删除；普通 COPY 可以重复粘贴。
- `MakeClipboardSourceValidation` 复用现有文件身份捕获与比较，在提供路径/内容和正常移动清理前核对原文件，防止文件被替换后误用新文件。优化移动的完成不要求已移走的原文件仍存在。失败/取消后清除本次反馈状态，后续尝试不沿用旧删除效果。
- 这是外部接收端操作的源端协议，Blink 不再为它重复排一条删除任务；Blink 主动发起的本地复制、移动和删除仍使用原 `FileOperationCoordinator`。接收和发送两端均不把进度或一个成功标签当成整批实际文件提交证明。
- `ClipboardSender` 延迟启动一个 STA，所有创建/发布/所有者核对/flush/最终 COM 释放均在该线程。后台等待使用内核事件与消息泵，不轮询；界面只提交请求，不等待外部 COM 或 join。
- 完成反馈按当前发布上下文返回 UI；早于发布 UI 回调的反馈暂存到该上下文，迟到的旧对象反馈不能覆盖新 Copy/Cut。完成或真实失败恢复剪切外观；MOVE 完成保留原 cache 的不可整批重播语义。
- 原生关闭和最后一个标签关闭共用异步关闭入口。关闭时先封住新发布请求，再保留当前有效 Clipboard 数据；成功后才退役标题栏并关闭窗口。失败保留原 STA/对象，窗口显示真实错误并允许重试。
- 若原文件已删除或被替换，只在 Win32 Clipboard 锁内确认仍为本发送器原所有者时撤回失效内容；不在锁内调用 OLE/RPC，不清理新所有者的数据。核对源身份期间所有者发生变化时跳过 flush。有效内容才走正常 OleFlushClipboard。
- flush 仅枚举 CF_HDROP、Preferred DropEffect 与可用的 Shell IDList 路径元数据，避免退出时因广告了文件内容流而读取整个大文件。现有文件是本批范围，虚拟 FileDescriptor/FileContents 功能仍在后续拖放/Provider 路线。

协议依据：[Shell 数据传输场景](https://learn.microsoft.com/en-us/windows/win32/shell/datascenarios)、[Shell Clipboard 格式](https://learn.microsoft.com/en-us/windows/win32/shell/clipboard)、[OleSetClipboard](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-olesetclipboard)、[OleFlushClipboard](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-oleflushclipboard)。真实原生对象行为仍以本机夹具结果独立核对。

## 验证入口与范围

```powershell
.\scripts\run-blink-clipboard-smoke.ps1 -Configuration Release -Feature Source -OutputDirectory '.build\clipboard-send\integration'
.\scripts\run-blink-clipboard-smoke.ps1 -Configuration Release -Feature Sender -OutputDirectory '.build\clipboard-send\integration'
.\scripts\run-blink-clipboard-smoke.ps1 -Configuration Release -Feature All -OutputDirectory '.build\clipboard-send\integration'
```

| Feature | 精确 CLI | 当前预期正常检查数 |
| --- | --- | --- |
| Presentation | clipboard-transfer-presentation | 64 |
| DataObject | clipboard-data-object | 60 |
| Session | clipboard-paste-session | 33 |
| Controller | clipboard-paste-controller | 23 |
| Source | clipboard-source-data | 38 |
| Sender | clipboard-sender | 29 |

All 顺序启动六个独立进程。Source 在自有 STA 上验证 medium 所有权、重复/取消反馈、失效源路径，以及 GUID 自有真实文件/目录的目标复制、源端清理和优化移动后的替换文件；这些是实际 Shell 对象的功能检查，没有使用系统 Clipboard 传输。

Sender 注入 Clipboard API，验证原 STA 的创建/发布/释放、替换、取消、错误、仅 flush 当前对象、关闭失败重试、过期撤回、所有者变化和隐藏消息窗口的消息泵。不读写系统 Clipboard；这些检查不等于真实 OleSetClipboard/OleFlushClipboard 或 WinUI 关闭验收。完整 Phase1 包含这些分项，数量不能重复相加。

## 本轮事实

首轮构建暴露 PIDL 指针数组的顶层 const 与 SDK 声明不匹配，已单点修复并保留失败记录。随后六分组中的旧四组全部通过，Source34 PASS/2 FAIL、Sender27 PASS/0 FAIL；两个真实 Shell 失败均为优化移动后错误转发源端删除通知，修产品适配器而不放宽原断言。另补读取身份及失效关闭/所有者变化案例。最终六组64/60/33/23/38/29全部通过，合计247 PASS/0 FAIL/0 SKIP；同一测试程序的完整Phase1为1991 PASS/0 FAIL/4既有1314符号链接权限SKIP，耗时23.279秒。247已包含完整组。测试EXE SHA `935de2825a83578c3c486994a1513ab0f8b274ba04bf7023afa892400e08ddec`。

Release单并发构建记录依次为97.770秒（SDK类型失败）、84.618秒、136.601秒、10.926秒。最后一次只调整失败测试的前置条件，明确选择应交给Shell清理的正常移动，原失败/所有权断言不变；产品EXE未重建改变，SHA前后相同。最终275原生输入、中英各396资源键/SHA一致；Debug未重建。

收尾另修验证脚本报告中的旧“四组”和范围文案，以同一测试EXE复核六组247/0/0；产品未重编，完整Phase1未重复执行。

原生异步补充探针显式启用真实Shell的IDataObjectAsyncCapability，执行Start→优化移动→End(S_OK,MOVE)并释放对象，旧路径替换文件与目标文件均保留；这是原生接口检查，不是系统Clipboard或WinUI验收。

本批开始时没有Blink用户窗口，直接发布已验证运行包，未启动窗口。只替换9个变化文件111248566字节，295个运行文件221409267字节逐项SHA一致；默认Blink.exe SHA `e59f39d152015efcacfea63009f7b59ad59c92db4ba01003b537a876b995061f`。较旧窗口的PID/标签恢复记录仅为前轮历史，本批不声称恢复不存在的窗口或完整会话。

正式索引服务当前PID6228，EXE SHA `5e921f86617437ceea6012e95b9a49b87ddd04e9b3c55407e1f6ce1305043073`自本批开始保持，没有重启、重装或重建索引。Solution增量重链了四个附带宿主/服务程序；新打包服务与旧打包/正式服务的八个PE节在仅屏蔽时间戳、校验及调试记录后逐项相同，差异不作为新索引代码，也不为此更换正式服务。匹配的最新缓存PDB/ILK继续保留。

本批分类收尾确认删除322787943字节/314文件/96目录，范围为已发布重复候选、过期应用恢复、最旧源码包及重复日志/旧辅助副本；另有自有夹具拆除580字节单列，不把功能本身删除的源文件计入清理。仅留最近两个已校验源码恢复点和紧邻上一默认e1e66d的9变化文件恢复109908777字节；当前程序/匹配符号/有效缓存/15包/独立服务恢复及正式用户数据保持。删除量不是磁盘净变化。 当前没有把原生接口或注入API通过写成真实Explorer传输、实际OleFlush或WinUI关闭通过。

原始记录统一在 `.build/clipboard-send/integration/`，源码、当前增量缓存、正式服务与用户数据不按临时产物清理。

## 尚待验证

- 真实 Explorer/Blink 双向 COPY/CUT、剪切淡化，以及系统 Clipboard 的实际 OLE flush、普通关闭/最后标签关闭组合；仅在原 Clipboard 可完整恢复的条件下做相应实机检查。
- WinUI 发布/完成回调的时序与窗口关闭体验、完整环境矩阵、真实外部接收端的部分失败和长时间异步操作。无界面原生通过不能替代这些。
- 搜索首屏与 Everything 同机性能、其它高频交互、更多视图/预览和远端功能继续按 README；不以测试项数量替代实际性能。
