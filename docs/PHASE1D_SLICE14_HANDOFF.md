# Slice14 跨盘文件与文件夹移动恢复清单

后续更新（2026-11-05）：目录移动/重命名后的标签与历史位置跟随已在[Slice15](PHASE1D_SLICE15_HANDOFF.md)完成开发验证，当前恢复优先使用该清单；本页保留Slice14阶段记录。

状态（2026-11-05）：跨盘普通文件和有界普通文件夹移动已实现并接入。完整Debug/Release各1148 PASS、0 FAIL、4权限1314 SKIP；184输入/271资源及冻结合同/旧EXE核对通过。Phase1D与整项目未完成，目标active，人工UI、真实剪贴板和服务验收暂缓。用户明确的功能取舍见[PRODUCT_SCOPE.md](PRODUCT_SCOPE.md)：推进文件管理器本身，不增加没有实际用途的防护机制。

## 实现与结果

同卷沿用原handle rename。跨盘文件由FileCopySafely和detail::CopyFileAndRemoveSource共享CopyFileImpl，原来源handle持有到文件/ADS复制提交与选中链接移除。Skip/KeepBoth/Cancel、基本metadata和进度复用原实现；无TxF/oplock、新服务或额外依赖。

跨盘文件夹复用DirectoryCopyImpl的有界计划、目标创建、文件复制和目录metadata。逐文件复制成功后通过原handle移除来源，最后逆序移除空来源目录，采用常见的逐文件移动语义；替代初稿“整树复制成功后再移除全部来源”，无需重新读取和比较整树。复制阶段目录handle释放后，在校验父目录下按捕获ID重新打开原目录执行空目录移除；新增/未完成子项留下，不做按名称递归删除或整树回滚。

MoveDirectoryAcrossVolumesSafely由MoveEntrySafely按实际卷分派；PrepareMoveDestination和MoveEntries已接受跨盘文件/目录混合，Ctrl+X/Paste沿用原队列、身份和sequence缓存。同卷/COPY原有回归继续通过。

MoveResult独立保留文件copy_result、目录directory_copy_result与source_files_removed/source_directories_removed。只有根完整移除才moved。目标完整复制而来源空目录移除失败时保留copied；文件阶段失败保留部分目标树和已移除前缀。失败后仅显式Skip/Cancel，不能KeepBoth/改名重做已有目标；Skip保存retained_result后继续未开始尾项。来源实际有移除时刷新来源父，目标有复制/残留时刷新目标父。UI分别显示目标复制和来源移除数量、实际路径、failure_path及“跳过当前项并继续”。

## 验证与证据

- `.build/phase1d-slice14/integration-20261105-1955-directory/`：native-validation.json、input-sha256.json、expected-checks.json、change-scope.json、slice14-directory.patch、review.md和完整构建/测试状态日志。184输入在最终双配置构建前冻结，271项en/zh/fallback资源及工程引用核对。
- Debug/Release完整构建均exit0，分别结束后串行普通用户测试各1148 PASS/0 FAIL/4权限1314 SKIP。21项新增检查用真实不同C/D卷，覆盖嵌套与空目录、文件ADS、只读、同名/KeepBoth、不同取消阶段、文件/空目录移除失败、来源新增、后续文件变更、回调异常、混合队列与显式跳过续尾。链接权限跳过不当作通过。
- 初始四项新增目录RED为1127/4/4。两轮中间Debug1146/1/4是fixture错误假设rename必须成功；实际现有lease阻止rename返回32，异常被正确报告574。改用真实API返回与文件状态检查两种正确结果，并另加中间回调异常回归；日志保留在red-*、attempt1-*、attempt2-*，见attempts-assessment.json。
- 单文件移动历史基线在`.build/phase1d-slice14/integration-20261105-1912/`，1127/0/4；新增目录不改变这些原回归。10 EXE/两PRI摘要保存；六旧EXE与该基线一致，明确复用既有普通运行证据，未冒充新执行。冻结三文件不变。
- 最终原生进程/临时ASCII别名0、WSL swap0/OOM0，附录B原有6根散落文件/1 project散落文件/17其它项目产物目录/0缺README无新增。主模型完成、compiler1，不使用子代理，不清理其他项目。工具链仍为D:\DEV\C++，正式源码/产物均在D盘，C盘仅真实异卷自有临时测试目录，已由fixture清理。
- 快照与逐条SHA记录见本轮source-snapshot.json和`.build/phase1d-slice14/snapshots/`。工作区索引#4更新v4.23。恢复先核对当前输入、归档摘要、状态与实际进程；不重启在途任务，不reset/clean或自动解压覆盖未提交工作。

## 后续范围

[Slice14计划](PHASE1D_SLICE14_DEVELOPMENT_PLAN.md)与[人工清单](PHASE1D_SLICE14_MANUAL_UI_CHECKLIST.md)已同步。继续移动目录位置跟随与外部效果反馈、Merge/Replace、删除回收站、拖放和属性，再按既有后续阶段推进SFTP/自建网盘Provider；不另做RaiDrive式挂载工具。当前跨盘完成不代表这些目标或整项目完成，用户未设置新的停止时间，目标继续active。

目录Copy既有10000项/256目录/128深度/16MiB路径预算继续适用；目录ADS、reparse与特殊存储未扩展，不宣称原子快照、完整备份metadata或磁盘故障注入覆盖。人工窗口/Explorer剪贴板/真实服务验收依用户要求暂缓，不能用编译和普通测试替代。

API依据：[SetFileInformationByHandle](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfileinformationbyhandle)、[文件处置标志](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/ns-ntddk-_file_disposition_information_ex)。API用于普通文件移动，没有新增安全软件功能。
