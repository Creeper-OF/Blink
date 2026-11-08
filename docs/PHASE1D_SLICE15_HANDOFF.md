# Slice15 文件夹移动/重命名后的标签与历史位置跟随

状态（2026-11-05）：本轮功能已接入，完整Debug/Release各1191 PASS、0 FAIL、4权限1314 SKIP，187冻结输入/271资源/冻结合同/旧EXE核对通过。人工UI、真实剪贴板和服务验收依用户要求暂缓，整体项目active，主模型/compiler1，无子代理。以[PRODUCT_SCOPE.md](PRODUCT_SCOPE.md)的实用文件管理功能为主，不扩展额外防护平台。

## 当前实现

Location的RebaseFolderLocation/IsFolderAtOrBelow按目录边界、ordinal大小写和普通/extended drive表示匹配根及子目录。同名前缀相邻目录不受影响。CompletedFolderRelocation只从实际moved目录或committed目录Rename产生事实，使用实际目标（含KeepBoth编号）；文件、失败、Skip/no-op或跨盘部分移动不让来源根跳向目标副本。

NavigationModel以relocated_history暂存新历史；新Browse成功后一起提交路径、列表和历史，不向Back追加已消失旧页。当前页、过去/未来历史与正在进入移动树的请求都按新位置更新；旧token结果被拒绝。用户正在去别处时保持其导航，成功后同步历史；失败保留旧页，后续Refresh/Up用已知新位置。提交后清除临时映射，新的显式旧路径访问可以打开该处的新对象。仅大小写重命名更新显示拼写。

TabState使用正常枚举取得目标目录身份和文件完整路径；同身份有效选择、视图和排序保持，跨盘身份改变时移除失效选择。TabWindowState为根/子目录标签生成请求，不重建关闭标签；当前根已是不同身份的新对象时不跟随。main的Move/Rename完成回调调用共享事实/去重及现有StartNavigation，后台标签标题在新页提交后更新，Relocate地址栏不提前提交；正常位置跟随按普通导航退出当前目录搜索。

## 自身目录监听与移动

新增真实监听回归发现：根和子目录的Blink监听同时开启时，同卷父目录Move返回5，跨盘可完成。操作前只暂停相关来源树的监听，旧FolderChangeState token失效，Stop action作为FileOperationPrerequisites交给同一个协调器worker；等待真实原生I/O排空后再调用原Move/Rename。UI不等待句柄，取消仍由原任务协调器处理，未新增worker引擎/服务，也未修改原rename flags。

成功/失败/取消/提交拒绝后按operationID恢复当前或待导航位置，暂停期间的新监听不会重开来源句柄；恢复后补齐暂停空隙。完整移动后按新位置重新枚举/监听，部分移动保留剩余来源位置并刷新。外部程序占用或用户同时另行访问来源仍可能产生正常占用/权限错误，没有试图绕过系统限制。UI恢复归属、后台标题等为已编译接线，真实交互仍按人工清单暂缓。

## 验证与恢复

- `.build/phase1d-slice15/integration-20261105-2013/`含native-validation.json、input-sha256.json、expected-checks.json、change-scope.json、slice15-location-follow.patch、review.md及完整build/test状态/日志。源码187项在最终双配置构建前冻结，271 en/zh/fallback键和工程引用对齐，冻结IPC三文件不变。
- 两配置完整构建均exit0，各自结束后串行普通测试各1191 PASS/0 FAIL/4权限1314 SKIP。新增43项涵盖路径/历史/pending/迟到结果、重访旧路径、选择/偏好、关闭/多标签、真实C/D Move后重新枚举与身份、原监听排空、后续目录Rename和新位置监听、prerequisite失败与等待时取消；原Slice14的1148项继续通过。4项symlink权限跳过不计实际通过。
- 初始RED1158/21/4，未实现位置跟随造成21项失败；首次实现1179/0/4留pre-review。加入监听回归后两轮1183/2/4（watcher-attempt1/2），诊断INFO明确cross=0 opened=1 moved=0 error=5；cross=1成功。修正后同卷也moved=1/error=0。所有失败/中间构建日志和独立assessment保留，不冒充最终通过。
- 最终10 EXE/两PRI SHA已保存；六旧EXE与Slice14目录基线相同，明确复用既有普通证据，未重新运行旧suite。旧fixed-rename/owned-reparse观察沿用历史摘要，不计本轮PASS。原有非致命编译warning保留，未修改工具链屏蔽它们。
- native进程/临时ASCII别名0，自有C/D测试PID目录残留0，WSL swap0/OOM0。附录B既有6根散落文件/1 project散落文件/17其它项目产物目录/0缺README无新增，未清理他项目资产。工具链仍在D:\DEV\C++，正式源码/产物留D盘，C盘仅唯一自有异卷测试目录。
- 源码/文档/证据快照与逐项SHA见本轮source-snapshot.json和`.build/phase1d-slice15/snapshots/`，工作区索引#4 v4.24。恢复先核对输入/摘要、任务状态和实际进程；不盲目重跑在途工作、不reset/clean或自动解压覆盖未提交代码。

## 继续工作

用户未新设停止时间，整体目标继续active，本模块通过不等于Phase1D或整项目完成。[开发计划](PHASE1D_SLICE15_DEVELOPMENT_PLAN.md)、[人工清单](PHASE1D_SLICE15_MANUAL_UI_CHECKLIST.md)同步；人工仍暂缓。接着推进系统删除/回收站、合并/覆盖、拖放、属性及外部performed-effect/剪切外观反馈，继续保留后续SFTP/自建网盘Provider，独立RaiDrive式挂载工具不纳入主线。

API参考：[FILE_RENAME_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/ns-ntifs-_file_rename_information)。文档说明目录内打开对象可能阻止rename；本机具体错误5来自本轮原生测试，不宣称已覆盖所有文件系统/驱动/竞争场景。
