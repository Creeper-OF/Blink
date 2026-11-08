# Phase 1D Slice 2 — 同目录重命名

状态：coordinator、F2/菜单/输入框接线已实现，最终完整Debug/Release构建和各603项普通检查通过、退出0；人工UI/真实服务验收保持暂缓。Slice1已完成Debug/Release构建及各581项普通检查；人工UI/真实服务仍暂缓，不因开发推进升级接受状态。本轮截止2026-10-15北京时间16:59:10，未闭合项保存继续。

## 预期行为

- F2与文件区右键重命名共用入口，只允许一条有效committed row；This PC、搜索surface、无有效对象/父目录identity时不提交。名称对话框复用已有生命周期与校验，显示当前对象，允许Unicode/长路径、文件和目录；多选暂不批量命名。
- 捕获父目录identity、当前FileEntry/RowIdentity与新leaf。Worker核验目录链及源对象身份/类型/reparse属性，源句柄直接拥有DELETE权限；不在核验后按可变路径重新打开另一个对象。
- 优先使用[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)的RootDirectory+relative leaf和FileRenameInfoEx，Flags=0不替换已有目标；先通过真实自有fixture验证该组合及所需权限。现有Core RenameFileModern缺少目录BACKUP_SEMANTICS，不能把已有bool wrapper直接当作已产品验证的目录重命名。
- 同名目标必须保留身份与内容，允许Rename Again/Cancel；大小写变更、硬链接单独命名、目标被替换/删除、占用、权限与取消均有明确结果。提交前取消不变更；提交成功不伪装回滚。
- FileOperationCoordinator继续窗口拥有、单个在途worker和immutable completion。将操作种类及committed结果表达清楚，不以created字段冒充rename；未完成后端/UI不得开放Ready命令。
- 复用按父目录identity的多标签失效刷新与已有BrowseController，不改历史或导航其它标签；旧RowIdentity消失后不误选同ID的另一硬链接。IndexService不参与mutation。

## 验证与限制

先以普通用户临时fixture验证真正rename后源名消失、目标identity不变、同名文件/目录/硬链接保护、目录内容和子项保留、case-only rename、source/parent replacement与no-follow reparse。再覆盖coordinator种类/取消/关闭、F2策略和对话框接线，完成两套构建/普通检查与冻结摘要。手动UI清单保持待执行。此阶段不承担Move、Copy、Delete、回收站、剪贴板或跨卷操作。

## 2026-10-15 接口探测与本轮边界

普通owned fixture显示FileRenameInfoEx的RootDirectory+relative leaf组合在当前系统文件/目录均返回87，原名称保持；相同句柄配RootDirectory=NULL与经核验的absolute DOS目标均成功。实现选择后者，保持source handle identity、父目录链短期保护和Flags=0禁止覆盖；不在失败后盲目切换调用重试。

本轮已完成RenameEntrySafely后台、DirectoryLease及普通fixture检查，并继续接入窗口coordinator、F2/菜单、共享名称输入和失败重试。完成状态使用实际committed标志区分创建、重命名与成功no-op，旧结果继续携带immutable源entry和目标目录。重命名成功按父目录身份刷新；原旧名称选择失效时清除，不自动选择同ID的其它硬链接，也暂不自动聚焦新名称。

当前源码已完成完整Debug/Release构建及各自603项普通检查；后台600项初步证据与最终603项集成证据分别保留。人工窗口与真实服务验收继续暂缓，到16:59停止新增开发并保存未闭合项。
