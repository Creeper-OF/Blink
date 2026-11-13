# Clipboard 来源对象与粘贴完成回传

2026-11-13。本批完成 Explorer 等外部应用 → Blink 的文件剪贴板接收接线；发送端仍沿用已有 Win32 即时发布，完整双向 OLE 未完成。Release原生验证与默认程序更新已完成，真实Explorer收发仍待验。

## 实现范围

- `ClipboardDataObject` 从同一个 `IDataObject` 读取 CF_HDROP 与 Preferred DropEffect，复用严格的路径/effect 解码；真实来源 HRESULT 保留。缺少可选 Preferred 时使用 COPY，不把 RPC 或格式损坏转换成成功。
- `ClipboardPasteSession` 每次粘贴只取得一次原对象。OLE 初始化、读取、消息泵、完成通知、对象释放与反初始化均在该会话自己的 STA 中；界面只提交完成/取消，不等待外部 COM 调用或 join 线程。
- 读取前后序号不同则提示重新粘贴，不自动重新取得另一批内容。每次完成通知前再次检查原序号；剪贴板已变化时只跳过通知，保留实际文件结果。
- `ClipboardPasteController` 按唯一文件任务 ID 持有独立结果模型。冲突恢复更新任务 ID，目录根冲突等待用户决定；离开上下文、弹窗失败和关闭释放对应会话。后来的 Copy/Cut 和文件任务不会覆盖较早任务的来源绑定。
- 仍复用唯一 `FileOperationCoordinator` 队列。只有整批实际提交完成才通知成功；部分、跳过、未变化、取消和失败均不发送整批 Paste Succeeded。已经全部提交后的迟到取消或事后警告不抹去实际完成事实。
- 已由 Blink 实际移动的整批使用优化移动反馈 NONE / MOVE / MOVE，复制使用 COPY / COPY / COPY。源端通知失败不会回滚文件操作；可选反馈不支持不显示为文件任务失败。

读取与回传使用微软的 [Shell 数据传输约定](https://learn.microsoft.com/en-us/windows/win32/shell/datascenarios)、[Shell 剪贴板格式](https://learn.microsoft.com/en-us/windows/win32/shell/clipboard)和 [OleGetClipboard](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-olegetclipboard)。序号检查是本实现的保守取舍：非 OLE 默认包装对象或延迟呈现也可能触发序号变化，届时要求重试或跳过反馈；不宣称任意旧来源在剪贴板被替换后仍可回传。

## 独立验证入口

```powershell
.\scripts\run-blink-clipboard-smoke.ps1 -Configuration Release -Feature All -OutputDirectory '.build\clipboard-receive\integration'
```

| Feature | 精确原生 CLI | 预期正常检查数 |
| --- | --- | --- |
| Presentation | clipboard-transfer-presentation | 64 |
| DataObject | clipboard-data-object | 60 |
| Session | clipboard-paste-session | 33 |
| Controller | clipboard-paste-controller | 23 |

All 顺序启动四个独立进程。脚本核对精确分组、唯一标签、完成标记、退出码与同一测试二进制 SHA；不构建、不管理服务、不操作 GUI 或系统剪贴板。默认仍为 Presentation，旧批次默认输出目录保持兼容。修复时可以只复测受影响分组。

Session 使用注入来源，在真实 STA 和消息泵中验证获取/释放线程、读取途中更换、通知途中更换、取消、重复完成与对象生命周期。Controller 使用实际会话验证任务绑定、迟到终态、整批与部分结果、单目录根冲突继续及放弃。这些原生验证不是实际 Explorer 窗口收发或剪切淡化验收。

## 本轮验证与交付事实

- Release首次构建454.162秒，单文件修复后增量123.739秒，均单并发；没有重建Debug。最终267个原生输入及中英各393资源键/SHA核对一致。
- 首轮Presentation64、DataObject60、Controller23通过，Session32 PASS/1 FAIL。失败为工厂内存不足的HRESULT投影顺序，通用Win32分支绕过了原定ERROR_NOT_ENOUGH_MEMORY特判；只修产品映射顺序，保留原HRESULT和原测试断言。首轮日志/报告在`integration/first-attempt/`单独保留，没有提升为通过。
- 修复后的同一测试EXE SHA `5621aa59c76583deea7c0ad13f4754948bdce9a36bc7f9acd98ddfff14977889`：四组64/60/33/23，合计180 PASS/0 FAIL/0 SKIP；完整Phase1为1924 PASS/0 FAIL/4既有1314符号链接权限SKIP，耗时33.406秒。180已包含在完整组内，不再相加。未知/缺失suite两项均exit2且未运行原生组。
- 原默认PID28400正常exit0；仅替换5个变化文件108377897字节，运行目录295文件220069478字节逐项SHA一致。新默认PID36708可见，Blink.exe SHA `e1e66d592ffae6835c22b62f0bdf12b20fc22d098d210801a4344f24f1abb6c2`；原单个“此电脑”标签、真实选中序号0及地址只读核对恢复。
- 正式索引服务PID29080与EXE SHA `5e921f86617437ceea6012e95b9a49b87ddd04e9b3c55407e1f6ce1305043073`保持，没有重启服务、重建索引或修改配置。正常USN保存不算本批直接写索引。
- 系统剪贴板预检因`Chromium internal source URL`无法完整恢复而在写入前SKIP，0项实际传输检查，没有启动传输GUI/创建数据夹具；未改系统Clipboard。真实Explorer收发、剪切透明度、通知实际对端效果仍未验。本批没有重跑源码未变的旧View失败/SKIP，也不把窗口重新打开当作这些功能的验收。
- 分类收尾确认删除327036270字节/313文件/94目录，含已发布重复候选220069478字节、过期79f915运行恢复、最旧源码包及重复日志/旧辅助副本。最近两个源码恢复包保留；只留紧邻上一默认9fe19d的5变化文件99616074字节作为运行恢复，待真实Clipboard/Opacity与旧View边界补验后处理。当前程序/匹配符号/有效增量缓存/15包/正式数据不清。原始证据与清理明细在`.build/clipboard-receive/integration/`；删除量是文件长度累计，不是净磁盘变化，开发构建目录不当发布包体积。

## 后续

1. 在原系统剪贴板可完整恢复且窗口可供测试时，补真实 Explorer → Blink COPY/CUT、淡化、冲突和部分完成验证。用户剪贴板不得为测试清空。
2. Blink → Explorer 的自有 OLE 发送对象、源 STA 持续服务和退出时 flush 另做一批；当前没有把即时 Win32 发布冒充完整双向 OLE。
3. 搜索首屏/Everything同机测量、其余高频交互和远端功能仍按 README 的待办排序，不以本批检查数替代性能或全应用验收。
