# Phase1D Slice14 跨卷移动执行计划

后续更新（2026-11-05）：目录移动/重命名后的标签与历史位置跟随已在[Slice15](PHASE1D_SLICE15_HANDOFF.md)完成开发验证，当前恢复优先使用该清单；本页保留Slice14阶段记录。

当前状态（2026-11-05）：跨盘文件与有界文件夹均已接入；完整Debug/Release各1148 PASS、0 FAIL、4权限SKIP，184输入/271资源及冻结合同/旧EXE核对通过。整项目保持active，人工UI、真实剪贴板与服务验收暂缓。功能取舍遵守[PRODUCT_SCOPE.md](PRODUCT_SCOPE.md)。

## 已实现路径

同卷文件和目录沿用原handle rename。跨盘普通文件复用CopyFileImpl和detail::CopyFileAndRemoveSource；默认数据及ADS复制提交后，通过仍持有的原来源handle移除选中链接。Skip/KeepBoth/Cancel、metadata、进度与错误反馈复用既有路径，不引入事务文件系统或额外防护机制。

跨盘目录复用DirectoryCopyImpl的有界计划、创建与metadata流程，每个文件使用同一CopyFileAndRemoveSource。逐文件复制成功后移除该来源，最后逆序移除空来源目录。此方式直接复用已验证文件流程，无需复制整树后重新读取和比较全部文件，也无需长期持有所有文件handle；替代初稿“整树复制后再移除全部来源”的方式。中途失败可以已经移动一部分文件，必须如实反馈。

所有来源文件处理结束后，释放复制阶段的来源目录handle，在已校验父目录下按捕获ID打开原目录，再调用普通空目录移除。新增文件或其他未完成子项使移除失败，剩余来源保留；没有按路径盲目递归删除或整树回滚。

## 队列与界面结果

文件copy_result与目录directory_copy_result分别保留；目录记录目标files_copied/directories_created、来源source_files_removed/source_directories_removed。只有选中根来源完整移除才为moved。复制完成而空目录移除失败仍保留copied事实，不能变成复制失败；文件阶段失败则保留部分目标树与真实已移除数量。

跨盘文件/目录通过同一MoveEntries、Ctrl+X/Paste和原来源身份缓存。失败保留已完成前缀和未开始尾项。部分目标已经产生时，仅显式Skip/Cancel；Skip保存retained_result后继续尾项，不能再用KeepBoth或改名重复复制。来源实际有移除时更新来源父，目标有复制/残留时更新目标父；提示与tooltip显示真实路径、数量和失败位置。

## 验证与后续

普通用户自有C/D目录测试涵盖嵌套/空目录、文件ADS、只读、根同名与KeepBoth、不同阶段取消、文件/目录移除失败、来源新增与后续文件替换、部分双端刷新、混合队列和跳过续尾。保留四项新增目录RED，原1127项回归保持。Debug/Release完整构建严格各自结束后再运行测试，主模型执行、compiler1，不启用子代理；冻结输入、资源/合同/旧EXE与工作区审计独立核对。

目录ADS、reparse和特殊存储继续沿用既有明确边界；不宣称无限大树、原子快照或完整备份metadata。移动后的目录位置跟随、外部performed-effect、Merge/Replace、删除回收站、拖放和属性仍需继续完成；SFTP/自建网盘按后续Provider阶段推进。本轮通过不代表Phase1D或整项目完成，恢复入口见[HANDOFF](PHASE1D_SLICE14_HANDOFF.md)。
