# Phase 1D Slice 1 — 新建文件夹

执行授权：用户要求继续开发两小时，2026-10-15北京时间14:59:10至16:59:10。主模型执行，禁止子代理，原生构建/普通检查依次执行，编译并发1；人工UI与真实服务验收继续暂缓。本阶段先完成新建文件夹，不将整个1D标记完成。

## 行为与边界

- 文件区右键“新建文件夹”和Ctrl+Shift+N共用入口，This PC与搜索surface不提交文件操作。输入对话框显示目标目录，初始名称可编辑，取消不提交；拒绝分隔符、设备名、控制字符、尾随点/空格及超长名称。
- 目录枚举快照携带从同一目录句柄读取的FileIdentity；UI只捕获已提交目录、身份、TabId与页面generation，不执行文件I/O。无有效身份时明确拒绝操作，不能在路径替换后向新目录误写。
- FileOperationCoordinator在窗口内拥有单个后台任务，保存Queued/Running/Cancelling/Completed/Cancelled/Failed、错误、请求及实际创建结果。调用者关闭只请求取消，worker与状态仍有所有权。取消在提交前生效；创建已成功时保留Completed，不伪装已撤销。
- Native执行先逐段打开本地目录链，核对目录类型、身份与reparse属性，短暂持有不共享DELETE的句柄，避免验证后父目录/祖先被改名或替换；不在输入对话框等待期间持有这些句柄。现有CreateDirectoryModern/CreateDirectory2W使用DISALLOW_PATH_REDIRECTS，绝不follow reparse。仅支持当前本地固定/可移动盘路径，UNC与未知命名空间明确Unsupported。
- 不覆盖同名文件/目录，不自动创建中间目录；冲突允许重新输入名称，失败显示实际错误。成功后按目录身份合并刷新相关标签，不能把非活动标签的结果导航到当前页；刷新保留草稿、视图、历史和仍有效选择。独立操作状态避免被普通浏览提示覆盖。
- 不新增服务命令、IPC字段、管理员路径、剪贴板或删除操作。Rename/Copy/Move/Delete保持后续阶段，索引最终收敛不能冒充已执行真实服务验收。

## 验证顺序

1. 先关闭1C Slice4的Debug/Release原生构建与各538项普通检查，保存可靠退出码和冻结摘要。
2. 新建操作以真实自有临时fixture验证Unicode、长路径、同名文件/目录保护、父目录替换、占用/拒绝访问、reparse和取消；纯模型/注入执行器验证并发拒绝、关闭与提交后取消语义。
3. 接入名称输入、命令策略、窗口操作状态及多标签刷新。业务与对话框分别放独立app-private文件，main.cpp只接线。
4. 完成当前源码Debug/Release构建与普通检查、主模型审查、资源/冻结摘要和文档索引更新；到截止时间停止新增开发，未闭合项如实保存。

API依据：[CreateDirectory2W](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createdirectory2w)、[CreateFile3](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfile3)。拒绝DELETE共享用于短暂禁止改名/删除，DISALLOW_PATH_REDIRECTS拒绝路径重定向；这不等于跨多个文件操作的事务。
