# Blink Progress

## 普通目录菜单同步 — 2026-11-08

- **BOTH BUILDS / 1457 REGRESSIONS PER CONFIG / 39 OWNED GUI CHECKS VERIFIED / DEFAULT PUBLISHED AND OPENED**。普通目录与搜索主菜单共用ContextMenuAppearance（8px圆角、16px Fluent图标、32px行、主题和快捷键提示），补路径复制与当前目录粘贴，保留多选/新建/刷新/视图/排序和Blink只读属性；系统扩展支持同父目录多选，基本动作沿用原队列。
- 233冻结输入、367资源；两配置各1457 PASS/0 FAIL/4权限1314 SKIP，新增12项。普通目录21项、搜索18项均全过：单/多选路径、两种粘贴目标、三视图和排序实际顺序、属性、过期菜单选择绑定；两处主菜单进入Bandizip均核对真实ZIP文件名和全部字节。自有测试窗口正常退出0，无遗留helper或fixture，不声称像素级或人工体验验收。
- 原默认预览通过空闲检查后正常关闭；7个变化文件发布并核对哈希，新版PID 39604可见，未强制抢焦点。清理31个已确认归属路径，释放205094895字节，另清理7个错误TEMP形成的空目录（0字节）；保留当前程序、有效缓存和健康C/D索引，最近两恢复点策略持续执行。
- 首次合并构建/测试进程继承D盘TEMP，导致原跨盘fixture缺少第二卷；相关1413/2/7及纯空alias失败记录保留。改为独立PowerShell后最终双配置1457/0/4，产品输入未因此修改。详见[同步与验证记录](docs/FOLDER_MENU_SYNC_HANDOFF.md)和`.build/folder-menu-sync/integration`。其余项目计划待续。

## 搜索结果 Blink 风格主菜单 — 2026-11-08

- **BOTH BUILDS / REGRESSIONS / 18 OWNED GUI CHECKS VERIFIED / DEFAULT PUBLISHED AND OPENED**。按用户外观要求新增轻量SearchContextMenuPresentation，11项常用操作用Blink WinUI主题/8px圆角/16px Fluent图标/32px行/分组及快捷键提示；不增加全局重复快捷键，主菜单不加载系统扩展。
- “压缩与更多系统选项…”延续原生完整菜单，Properties仍走系统属性；helper/索引/协议不变。两配置各1445 PASS/0 FAIL/4权限1314 SKIP，232冻结输入、367资源。终轮18PASS/0FAIL/0SKIP，真实Blink主菜单→系统扩展→Bandizip精确短名ZIP及全部源字节通过，基础操作和旧peer失效均覆盖。
- 首轮仅因Bandizip缩略长GUID标签跳过ZIP，测试改短名后全过；不放宽目标归属、不改产品。测试剪贴板与清理payload归属另有明确约束。菜单截图surface未能捕获，未冒充视觉验收并删除无用图。
- 之前代理打开的默认窗口经空闲/无未完成对话框检查后正常关闭，发布6变化文件并哈希核对，已打开可见新窗口，无强杀/抢焦点。清理重复候选，保留当前程序/缓存/健康C/D库；恢复点只留本轮与上一原生菜单。详见[外观与验证记录](docs/SEARCH_THEMED_MENU_HANDOFF.md)，证据`.build/search-themed-menu/integration`。人工体验和其余项目功能待续。

## 搜索结果 Windows 原生菜单 — 2026-11-08

- **IMPLEMENTED / BOTH BUILDS AND REGRESSIONS PASSED / NATIVE AND 14 OWNED UI CHECKS VERIFIED / DEFAULT RELEASE PUBLISHED AND OPENED / HUMAN ACCEPTANCE DEFERRED**。搜索结果使用独立普通用户ShellMenuHost承载Windows完整经典菜单和已注册扩展；基本操作复用既有导航/剪贴板/唯一队列，补路径、父目录复制和定位。
- 双配置各1445 PASS/0 FAIL/4权限1314 SKIP，新增29项，230输入/363资源。Release原生23PASS/0FAIL/1托管UIA代理SKIP，Bandizip7.46真实ZIP的Unicode名称和完整原始内容校验通过；系统文件/目录属性页保活和正常关闭通过。
- 生产自有窗口14PASS/0FAIL/0SKIP，覆盖实际鼠标右键、路径和文件复制、定位、属性、CtrlEnter、F2改名、父目录粘贴、ShiftF10及确认后的永久删除。全部仅自有fixture，正常退出0，无遗留helper/测试数据。
- 用户解锁并关闭旧Blink后完成剩余实机验证，发布7个变化文件到默认路径并核对程序、资源及随附组件，已打开默认新版且确认可见、处于前台。先前超时确认为harness Name动态作用域覆盖（实际搜了USN search result前缀），已修正等待标签，产品源码未因此改动。
- 证据`.build/search-context-menu/integration`，候选与最终版230项原生输入相同；最终恢复包包含修正脚本和发布证据，校验后删除候选包，仅留本轮与上一搜索范围两点。默认程序、依赖和健康C/D索引保留，重复候选在校验后清理；服务PID6848/协议/库未改。见[实现与验证记录](docs/SEARCH_CONTEXT_MENU_HANDOFF.md)。其余项目计划和人工体验不标完成。

## 位置跟随与高级搜索 — 2026-11-08

- **SEARCH SCOPE NATIVE / LIVE SERVICE / 19 OWNED UI CHECKS VERIFIED / DEFAULT RELEASE PUBLISHED / HUMAN ACCEPTANCE DEFERRED**。位置跟随与高级搜索已完成：此电脑搜所有本地盘，目录内默认搜所在整盘；支持指定多盘、当前/指定目录及子目录开关，设置按标签独立、按搜索/回车提交。Debug/Release各1416 PASS/0 FAIL/4权限SKIP，新增34项与4项Phase05检查；359资源/220输入一致，真实C/D范围和目录深度、19项自有窗口检查通过，默认Release已更新。人工体验仍暂缓，完成本任务后停止，项目其余功能待续。
- Query scope支持FollowLocation意图解析、CurrentVolume、SelectedVolumes、SelectedFolder；旧All/CurrentFolder值保留。新只读type7复用原payload/version，在limit前筛直接父目录，原Search及BLK5保持兼容。输入同步提交与隐藏控件owner问题在本轮修正，不新增防护平台。
- 真实服务验证ThisPC C+D、当前C不搜D、D目录整D、多盘去重、目录递归2条/非递归limit1仅直接项，原USN及Open/Locate继续通过。19项UIA中旧隐藏peer实际接受写入，新Tab仍默认Follow；窗口退出0、fixture删除，无强杀。普通Search客户端36样本0失败；不据此声明UI首屏或Everything性能对比。
- 首轮构建已过但复查发现TextChanged异步，最终同步读控件后重构建再测试，紧凑记录保留。中途按用户暂停恢复稳定服务，随后明确恢复再完成验证发布。三模块子代理、主模型集成、compiler1；220输入/359资源，索引#4 v4.34，证据`.build/search-scope/integration`，见[续接记录](docs/SEARCH_SCOPE_HANDOFF.md)。完成本任务后停止，其余功能与人工体验不标完成。

## 搜索性能专项与实机对比 — 2026-11-08

- **SEARCH PERFORMANCE NATIVE AND LIVE SERVICE VERIFIED / DEFAULT RELEASE PUBLISHED / MANUAL UI DEFERRED**。用户明确授权多子代理解决明显慢于Everything的搜索，主模型与三个模块代理协作，仍compiler1；服务按卷驻留与轮转CatchUp，名称casefold预处理、有效limit及父路径复用，不改IPC/BLK5、不增防护平台。
- 同一只读基准EXE、相同C→D/5词/limit200/3轮各36样本：旧3次5秒超时、新0失败。普通查询每类6样本总IPC中位数：无匹配356→55ms、稀疏380→109ms、宽泛4862→116ms、扩展1583→33ms、目录范围1884→136ms。实际UI首屏及Everything实测未做，不能冒称已追平；同卷CatchUp/保存Busy峰值仍优先待做。
- 完整Debug/Release构建与各1382 PASS/0 FAIL/4权限SKIP，旧四suite两配置新跑通过，Phase05新增3项缓存/limit/范围回归；220冻结输入、345资源。真实C/D已有库复用、联合搜索、D盘创建/改名/删除及Open/Locate通过。独立console harness与真正服务串行隔离，不用假服务结果代替Release实机证据。
- 用户关闭窗口后，完整Release程序及随附文件已同步至默认启动目录。用户已主动关闭原窗口，未强制终止进程；服务已更新为优化版并自动启动。记录和成功恢复包归`.build/search-performance`，新包验证后保留本轮与上一索引修复两个成功恢复点，分类清理候选输出、重复日志及自有测试临时数据。
- [实测、边界及续接](docs/SEARCH_PERFORMANCE_HANDOFF.md)，工作区索引#4 v4.33；人工界面验收继续暂缓，项目剩余功能未完成。

## 索引修复与真实服务验证 — 2026-11-07

- **INDEX REPAIR NATIVE AND LIVE SERVICE VERIFIED / DEFAULT RELEASE UPDATED / MANUAL UI DEFERRED**。已处理用户“无法读取索引服务，本地浏览仍然可用”：现场缺少常驻注册和默认库；通过生产启用入口注册自动启动服务并完成C/D首次建库，约146万条，服务当前Running/LocalSystem，数据仍位于项目D盘。
- 修复安装器可选TelemetryRoot同名强类型变量导致空路径失败、USN32/App64卷身份绑定、跨卷Load失败污染与搜索末尾覆盖真实健康状态。BLK5使用64KiB读取缓冲，默认冷搜索在同一任务内等待正常Busy；不改IPC和磁盘格式，也没有增加本地防护平台。
- 单并发完成Debug/Release全solution构建；每配置1382 PASS/0 FAIL/4权限1314 SKIP。11项身份、6项启用/建库回归及Phase05四项加载检查通过；Phase0/Phase05专项与完整/Transfer两配置均新跑通过。旧Phase05完整harness固定Debug console host，真实Release系统服务另由live probe验证，不混用证据。
- 当前产物重启服务后复用既有C/D库，生产SearchCoordinator完整覆盖两盘；D盘自有文件创建、改名、删除后的结果收敛以及实际Open/Locate模型路径通过。未执行整机重启、启动关联程序或WinUI人工操作，不据此标记人工验收通过。219冻结输入、345中英/fallback资源保持一致。
- 原生构建的盘根字面量错误、安装空路径错误、首次冷读耗时及新增buffer fixture字面量错误均留有紧凑日志和原因；断言保持。默认Release程序及PRI已发布，清理重复输出/临时fixture和已替代候选产物；成功源码包逐项核验后仅保留本轮与Slice20两个恢复点。验证和清理记录归于`.build/index-repair/integration`。
- [修复与恢复说明](docs/INDEX_REPAIR_HANDOFF.md)。索引#4同步v4.32；3模块代理已结束，主模型统一集成、compiler1，WSL持续检查。接下来仍为目录移动合并、clipboard外部反馈/剪切外观与组合场景，随后按原计划推进远端功能。

## 索引修复暂停供环境转移 — 2026-11-06 11:55

- **INDEX REPAIR WIP / UNBUILT / UNTESTED / SERVICE NOT ENABLED / PAUSED FOR TRANSFER**。索引问题已优先处理中，按用户要求于北京时间11:55暂停供转移环境。现场确无索引服务注册/默认store；启用与首次建库入口、搜索USN32/App64身份绑定、跨卷Load失败隔离及新增回归已写入，当前WIP未编译/未跑测试/未启停服务。345资源仅静态核对通过，当前运行版仍为Slice20（历史各1365 PASS/4权限SKIP），不宣称索引已修复。恢复见docs/INDEX_REPAIR_PAUSED_HANDOFF.md，主模型统一集成、compiler1，保留Slice19/20成功恢复点及一个最新暂停WIP包。
- 用户症状为“无法读取索引服务，本地浏览仍然可用”；当前SCM无注册、默认store不存在，旧验收数据不能替代常驻索引。三模块代理与主模型完成源码改动和静态核对，新增11项身份、3项Load、6项setup回归及显式live probe均未执行。
- 继续时先核对输入摘要、单并发构建和原生测试，再通过Windows UAC真正启用服务/MFT/USN验证；现有运行版和两个成功恢复点未被替换。

## 目录复制合并与原任务内冲突决策 — 2026-11-06

- **Slice20 DIRECTORY COPY MERGE IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY PASS / MANUAL DEFERRED**。目录复制保留目标根与独有项，同名子目录递归合并；文件冲突按Replace/Skip/KeepBoth/Cancel处理，类型不匹配仅Skip/KeepBoth/Cancel。既有目录metadata不被来源替换，显示实际复制、新建与跳过数量。
- 同一个worker通过条件变量等待当前operation/conflict ID的选择，继续当前项和尾项，不重新执行已提交前缀。取消、关闭和界面发布失败均结束等待。Merge部分失败后只能跳过当前树继续外层队列或取消，禁止整树自动重试；目录Move Merge仍独立待做。复用原复制、替换和队列，没有增加本地防护平台。
- 原有1345项及20项新检查，最终完整Debug/Release各1365 PASS/0 FAIL/4权限1314 SKIP。213冻结输入、340中英/fallback资源。首轮和诊断轮1345/20/4，新增目标metadata open均返回160；仅移除其额外FILE_SHARE_DELETE，沿用Slice19已有共享参数后全部通过。失败输入/状态/日志紧凑保留，不削弱断言或堆整份重试副本。
- 用户明确重新允许子代理：3模块代理负责引擎、UI、测试，主模型协调集成，构建与原生测试仍串行compiler1。人工WinUI/Explorer/真实剪贴板和服务验收暂缓，整项目未完成；后续目录移动合并、clipboard外部完成反馈/剪切外观、Phase1组合场景与必要整理，再按原计划推进SFTP/自建网盘。
- 213输入/340资源、冻结三文件、10 EXE/两PRI核对通过，六旧EXE摘要未变而复用历史普通证据。本轮native进程/ASCII别名/自有fixture残留0，WSL swap使用0/OOM0，附录B既有6/1/17/0无新增。清理4份重复日志释放193177字节及4个空测试目录，保留合并日志、当前产物和有效依赖；新包校验后仅留最近两个成功恢复点。
- 最终证据`.build/phase1d-slice20/integration/native-validation.json`及source-snapshot.json，索引#4同步v4.30；[续接记录](docs/PHASE1D_SLICE20_HANDOFF.md)。工作台继续按固定目录分类，收尾清理本轮重复日志、空fixture及过期恢复包，保留运行产物、有效依赖和最近两个成功恢复点。

## 普通文件覆盖替换与工作台分类清理 — 2026-11-06

- **Slice19 FILE REPLACE IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY PASS / MANUAL DEFERRED**。显式Replace接入普通文件Copy/Move及原冲突续传，菜单/复制粘贴/剪切粘贴/拖放共用现有coordinator。完整Debug/Release各1345 PASS、0 FAIL、4权限1314 SKIP，新增25项检查，211冻结输入/338资源，既有拖放/属性/删除/同跨盘操作保持通过。目录Merge仍独立待做，整项目未完成。
- Copy沿用现有支持的数据流与基础元数据复制，先独占新建目标目录内的临时兄弟文件，完整复制后只用一次普通replace-if-exists相对句柄rename替换同名条目；失败或提交前取消不清空旧目标，临时文件清理失败时报告真实残留路径。其它硬链接保留原字节，原目标独有ADS不混入新文件，同一来源文件/硬链接别名拒绝。
- 同卷Move保留源身份，跨盘Move沿用同一个Copy实现，提交后才移除来源；只读/占用目标、检测到的目录或junction及已观察到的目标身份变化明确失败。目标名称替换不宣称原子expected-target-FileID比较交换，见D-0029；没有加入TxF、oplock、POSIX覆盖、只读强制覆盖或先删旧文件的回退。冲突对话框默认取消，普通文件才开放Replace；目录仍禁用。
- 第一轮Debug1344 PASS/1 FAIL/4权限SKIP，唯一失败为长路径fixture实际只有250字符。改为实测290字符，INFO error0/copied1，最终两配置1345/0/4；未修改生产代码迎合此失败。紧凑失败日志、原输入摘要与原因保存于`attempts/first-native`，不复制整套重试源码或二进制。
- 按用户补充要求将工作台分类/收尾清理写入`<本机 Codex 配置目录>/AGENTS.md`及项目README。WSL无在途Go构建时清理编译缓存3532679183字节（约3.29GiB），保留工具链和模块下载；Blink清理旧探针二进制、21个过期源码包、根目录search_repro.obj及空PID测试目录，保留当前运行版、依赖与最近两个成功恢复点。原清理汇总器字典求和错误已修复；项目释放量保守计至少15.18MiB，未持久化的旧探针大小不冒充精确统计。源码与当前产物未被清理删除。
- 证据`.build/phase1d-slice19/integration/native-validation.json`和source-snapshot.json，[续接记录](docs/PHASE1D_SLICE19_HANDOFF.md)。独立研究克隆已清理，外部来源只留链接与阅读版本，Pulse公开发布状态作为参考，不把其宣传或作者性能数据当作Blink验收。索引#4同步v4.29；主模型/compiler1，人工界面/Explorer/真实剪贴板/服务继续暂缓。下一步目录合并、clipboard外部反馈与剪切外观，再按既有规划推进SFTP/自建网盘。

## 文件系统拖放完成开发与双配置原生验证 — 2026-11-06

- **Slice18 DRAG/DROP IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY PASS / MANUAL DEFERRED**。用户明确恢复后，先逐项核对暂停时201项输入未变，再只恢复8项草稿，补工程引用、338项资源与44项拖放回归。完整Debug/Release各1320 PASS、0 FAIL、4权限1314 SKIP；活动209项输入冻结，原有复制、同/跨盘Move、删除和属性回归保持通过。整项目未完成。
- 文件区背景、目录行、侧栏目录、This PC位置/驱动器及标签目标共用拖放控制器；列表/网格发布选中行的完整原始身份。目标与所属页面独立，投到子目录不强制导航，保留冲突和改名重试的目标。标签悬停约500ms激活、关闭/取消和旧准备失效已编译接线，实际鼠标体验仍暂缓。
- 标准IDataObject/CF_HDROP配合DataPackage IDataObjectProvider，普通测试真实传递40项中文路径、独立HGLOBAL、稳定包对象身份和跨apartment读取。按同卷Move/异卷Copy及Ctrl/Shift选择，Alt或Ctrl+Shift链接不支持，同目录默认移动和目录自身/后代拒绝。实际传输仍由唯一FileOperationCoordinator执行；混合文件/目录复制及外部非异步来源移动使用真实自有fixture验证。
- 来源发布及完成反馈不删除文件。Blink作为接收方自行完成Move，反馈performed=None/logical=Move，避免来源再次删除；异步来源只开始一次，实际队列完成后结束，可重试冲突保持，取消不报完整成功。无异步能力的来源持Drop deferral；其真实UI交互尚未验收。内部刷新由既有操作发布负责，外部逻辑Move触发来源刷新。
- 前两次产品构建因头文件/COM具体对象转换、终态函数引用、Windows命名空间和const目标查询失败，精确输入、日志与源文件保存在attempts，修复后重冻并完成双配置验证。第二次失败构建产生的测试二进制曾诊断1320通过，单独标注，未充当产品完整构建通过证据。
- 证据`.build/phase1d-slice18/integration-resume-20261106-083946/native-validation.json`及source-snapshot.json；[续接记录](docs/PHASE1D_SLICE18_HANDOFF.md)、[人工清单](docs/PHASE1D_SLICE18_MANUAL_UI_CHECKLIST.md)。40-vs16历史API探针不计普通PASS，不替代真实WinUI/Explorer交互；原始暂停包仍保留。索引#4同步v4.28，主模型/compiler1，无子代理；下一阶段Merge/Replace、clipboard外部反馈与剪切外观及后续远端仍待做。

## 历史暂停：封存未完成拖放 — 2026-11-06

- **PAUSE REQUESTED / ACTIVE SOURCE RESTORED TO VERIFIED SLICE17 / SLICE18 UNVERIFIED WIP**。用户要求这轮后休息并追问是否结束，停止新增开发，完成检查点后暂停goal，不再开启下一轮。拖放没有完成，不能把源码草稿或API观察当作交付。
- 本轮仅标准IDataObject/DataPackage/CF_HDROP探针获得原生结果：自有40项在高层GetStorageItemsAsync只返回16，标准包对象、视图raw与后台marshal均返回40；fixture已清理，独立观察贡献0产品PASS。初始缺少头文件编译失败保留，未运行产品窗口或全局clipboard。
- 新增6个FileDragData/Model/Controller文件并修改main.cpp和FileOperationOrigin.h，共8项草稿；工程引用、资源与回归尚未接齐，未执行任何本轮产品构建/测试。完整207输入草稿及证据已归档并逐项校验，8项改动另存unverified-source和patch，未丢弃代码。
- 仅恢复本轮修改的两个既有源码文件、隔离6个新增文件；未使用git reset/checkout/clean，未动其它已有改动。活动源码201项逐一匹配Slice17已验证摘要，10 EXE/两PRI也完全一致；继续使用Slice17各1276/0/4历史证据，未声称新回归通过。
- WIP归档`.build/phase1d-slice18/snapshots/slice18-drag-drop-unverified-wip-20261106-003427.zip`，SHA256 `6f26941cd763395f3a1080269c206545889b4561bd072dac34f10e850de075a1`。恢复见[Slice18清单](docs/PHASE1D_SLICE18_HANDOFF.md)，索引#4同步v4.27；停止状态与只读工作区检查保存于本轮integration目录。拖放、Merge/Replace、外部剪切反馈及远端仍待后续用户恢复，人工验收仍暂缓，整项目未完成。

## 只读文件属性与后台目录统计 — 2026-11-05

- **Slice17 PROPERTIES IMPLEMENTED / DEBUG AND RELEASE ORDINARY PASS / MANUAL DEFERRED**。Slice17只读属性窗口、右键/Alt+Enter已接入，展示原选择的实际大小/占用、秒级本地时间和常见属性，后台统计目录与混合多选，支持取消、部分失败、硬链接按名称计数和目录链接目标排除。完整Debug/Release各1276 PASS、0 FAIL、4权限1314 SKIP，新增36项检查，201冻结输入/329资源/冻结合同与六旧EXE核对通过。复制、同卷/跨盘Move和删除回收站保持通过。继续拖放、Merge/Replace、外部效果/剪切外观反馈及后续远端；人工UI/真实剪贴板/服务验收暂缓，主模型/compiler1，不增加额外防护平台，整体目标active。
- 菜单和Alt+Enter共用选择捕获，按原父/源身份读取普通文件系统basic/standard/tag/id元数据，展示名称/路径/类型、逻辑大小、磁盘占用、创建/修改/访问时间和常见属性。独立秒级本地时间使用动态时区转换，未知0时间不显示1601，多选不同类型/时间显示多个值；最多1024项，超限有明确提示。未改旧文件列表格式、Properties原型模块、Index/协议或服务权限。
- 后台复用Core枚举统计目录与多选内容，包含隐藏/空目录，按硬链接名称累计，已知目录junction/链接不递归；稀疏/压缩文件查询FileCompressionInfo占用。取消在枚举和项目之间检查，当前I/O结束前可能继续等待；部分读取失败保留已统计值并标明不完整，不承诺原子快照、额外数据流总量或无限树压力验收。
- worker只持纯数据/取消标志和weak UI，150ms节流、最多一个待发布inbox提示，完成项不重复发布。窗口关闭、主动导航、切/关标签及应用退出使旧Ui关闭并合作取消；自动目录刷新不直接关闭属性。只读属性不因写队列繁忙而禁用，目录导航/呈现忙时仍按统一策略。当前为已编译接线，真实窗口体验继续暂缓。
- 初次Debug1272 PASS/1 FAIL/4权限SKIP保留；占用子目录fixture补足FILE_LIST_DIRECTORY后，INFO实测opened1/error32/3 bytes/1 file/1 unavailable。复核把缺少源ID改成单项不可用，其它选择继续，新增回归；两配置1274/0/4保留pre-review。补秒级时间/多值显示与选择上限文案后重冻，最终两配置各1276/0/4，36项新增，原1240项继续通过。
- 普通用户真实fixture覆盖当前大小/时间/占用与内容/ADS不变、嵌套/空目录、多选、硬链接、junction不计目标、稀疏物理占用、长中文路径、缺失/来源或父替换、占用partial、初始/途中取消、callback异常、后台无COM读取及显示/命令策略。稀疏实际占用与GetCompressedFileSizeW一致；未以普通测试冒充UI/剪贴板/管理员验收，也未新增symlink权限跳过。
- 完整Debug/Release均先构建exit0再串行测试，各1276/0/4。201输入、329 en/zh/fallback键/工程引用、冻结三文件、10 EXE/两PRI摘要核对；六旧EXE与Slice16一致，明确沿用原普通证据，未重跑旧suite。native进程/ASCII别名0，自有PID目录0，WSL swap0/OOM0；附录B既有6/1/17/0无新增，未清理其它项目资产。
- 证据`.build/phase1d-slice17/integration-20261105-2230/native-validation.json`、source-snapshot.json与[恢复清单](docs/PHASE1D_SLICE17_HANDOFF.md)，索引#4同步v4.26。[人工清单](docs/PHASE1D_SLICE17_MANUAL_UI_CHECKLIST.md)暂缓。下一步继续拖放、合并/覆盖及外部效果/剪切反馈，再按既有计划推进SFTP/自建网盘；本模块通过不等于整项目完成。

## 删除与系统回收站完成原生验证 — 2026-11-05

- **Slice16 DELETE IMPLEMENTED / DEBUG AND RELEASE ORDINARY PASS / MANUAL DEFERRED**。Slice16已接入Delete系统回收站、Shift+Delete确认永久删除及右键命令，支持文件/文件夹多项队列、进度、取消、失败重试或跳过和可能部分删除反馈；删除后已打开的根/子目录标签及历史退回父目录。完整Debug/Release各1240 PASS、0 FAIL、4权限1314 SKIP，新增49项检查，194冻结输入/291资源/冻结合同与六旧EXE核对通过。同卷/跨盘Move及位置跟随保持通过。继续完成Merge/Replace、拖放、属性、外部效果反馈和后续远端，人工UI/真实剪贴板/服务验收暂缓，主模型/compiler1，不增加额外防护平台，整体目标active。
- 普通Delete明确请求系统回收站，非回收PreDelete被否决，没有应用内永久fallback；Shift+Delete单次确认后才提交，默认取消。真实PostDelete/回收Shell项及原来源链接变化共同记录实际结果，不把排队或Perform的S_OK当作成功。原选择过期与确认构造异常提供反馈。
- 复用唯一FileOperationCoordinator队列和owned action，等待一个临时STA线程串行处理选中根项；原有监听Stop prerequisites先排空。已完成前缀不重做，失败可重试或跳过继续尾项，取消保留事实；目录中途失败如实显示可能部分删除，不记成partial_file_left，不承诺回滚。最后一次真实删除不被晚取消或回调异常反转。
- 删除完整目录后，根/子目录标签和前进/后退中的失效位置折返源父目录，重新枚举取得父身份，保持视图/排序，尊重用户正在去别处的请求；不误保留同ID但来自父目录的硬链接选择。根仍在的部分删除只刷新原位置。相关Blink监听暂停/恢复复用Slice15逻辑。
- 首轮Debug1234 PASS/4 FAIL/4权限SKIP保留。实际回调探针发现非空目录根PostDelete先以COPYENGINE_S_NOT_HANDLED回报，再删除子项；现等Perform结束后确认原根链接消失才记完成。另修正测试对第一STA可能为MAINSTA的假设；产品原本即接受两者。新增空目录完成与子项途中取消回归，最终49项新增检查通过，原1191项继续通过。
- 普通用户真实fixture覆盖文件/非空目录回收及按原返回Shell项恢复（含ADS）、确认永久删除、只读、硬链接、junction目标保留、长中文路径、占用文件/子项失败与retry、取消和监听标签折返；fake executor仅用于队列/lifecycle确定性检查。独立API四项观察和回调顺序探针不计普通PASS，不枚举或清空用户回收站。真实不可回收卷/磁盘故障未验证，不用标志veto探针冒充环境验收。
- 完整Debug/Release均先构建exit0再串行测试，各1240/0/4。194输入、291 en/zh/fallback键、工程引用、冻结三文件、10 EXE/两PRI摘要已核对；六旧EXE同Slice15摘要，明确复用原普通证据，未重跑旧suite。native进程/ASCII别名0，自有PID目录0，WSL swap0/OOM0；附录B既有6/1/17/0无新增，未清理其它项目资产。
- 证据`.build/phase1d-slice16/integration-20261105-2116/native-validation.json`及source-snapshot.json，初始失败、源码差异、复核和探针均保留；工作区索引#4同步v4.25。恢复见[Slice16](docs/PHASE1D_SLICE16_HANDOFF.md)，[人工清单](docs/PHASE1D_SLICE16_MANUAL_UI_CHECKLIST.md)暂缓。下一步属性查看、拖放、同名合并/替换与其它剩余范围，整体目标保持active。

## 目录移动/重命名后的标签和历史跟随 — 2026-11-05

- **Slice15 FOLDER LOCATION FOLLOW IMPLEMENTED / DEBUG AND RELEASE ORDINARY PASS / MANUAL DEFERRED**。Slice15目录移动/重命名后的根/子目录标签与历史位置跟随已接入，使用实际目标路径，尊重正在进行的用户导航，新位置枚举成功才提交页面；修复自身子目录监听阻塞同卷移动，原任务worker等待相关监听停止后执行并恢复。完整Debug/Release各1191 PASS、0 FAIL、4权限1314 SKIP；187输入/271资源、冻结合同与六旧EXE核对通过。既有同卷/跨盘文件和有界目录移动、剪切粘贴及部分完成保持通过。开发继续聚焦实用文件管理功能；删除回收站、Merge/Replace、拖放、属性和后续远端待做，人工UI/真实剪贴板/服务验收暂缓，主模型/compiler1，整体目标active。
- 当前页、根/子目录和前进/后退历史按实际目标（含KeepBoth编号）更新，同名前缀相邻目录不匹配。位置与历史先暂存，新Browse提交时统一替换，不多出失效旧页；旧加载token作废。用户正在进入移动树内时更新目标，正在去别处时保持该请求，成功时同步历史。失败保留旧页，Refresh/Up用已知新位置；后续显式访问重新创建的旧路径不永久重定向。
- 同卷身份相同的有效选择与视图/排序保留，跨盘新目录/文件身份重新枚举并清理旧选择。移动/重命名完成回调接入共享事实与去重，后台标签标题随提交更新；部分跨盘移动、普通文件、Skip/no-op及未提交操作不重定位来源目录。人工窗口仍暂缓，编译/共享逻辑不等于实物体验验收。
- 新增真实监听测试发现自有子目录watch使同卷Move错误5，而跨盘可完成。仅暂停相关来源树的Blink监听并使旧通知token失效，Stop action交同一个FileOperationCoordinator worker等待原生I/O排空，再调用原后端；UI可取消，成功/失败/取消/提交拒绝后恢复当前或待导航位置并补齐暂停空隙。按operationID归属，不让旧回调恢复新任务；没有修改rename flags、新建服务或防护平台。
- 初始RED1158 PASS/21 FAIL/4权限SKIP；首轮实现1179/0/4保留pre-review。补后台标题与真实监听后两轮1183/2/4，诊断明确同卷返回5；所有失败记录保留。修复后最终完整Debug/Release分别结束再串行测试，各1191 PASS/0 FAIL/4权限1314 SKIP，43项新增检查包括真实C/D Move、后续Rename、根/子目录重枚举/身份/重新监听，以及prerequisite失败、等待时取消、历史/pending/迟到回调、重访旧路径和关闭标签。
- 187冻结输入、271资源键/工程引用、冻结三文件、10 EXE/两PRI、六旧EXE证据复用均核对；native进程/ASCII别名0、自有测试目录残留0、WSL swap0/OOM0。附录B既有6/1/17/0无新增，未清理其它项目资产。证据`.build/phase1d-slice15/integration-20261105-2013/native-validation.json`及source-snapshot.json，索引#4更新v4.24。
- 恢复入口[Slice15](docs/PHASE1D_SLICE15_HANDOFF.md)，人工清单[位置跟随](docs/PHASE1D_SLICE15_MANUAL_UI_CHECKLIST.md)依用户要求暂缓。接着推进删除回收站、合并覆盖、拖放和属性，并保留外部效果反馈、后续SFTP/自建网盘范围；整体目标继续，不能将本模块通过当作整项目完成。

## 跨盘文件夹移动与真实部分完成反馈 — 2026-11-05

- **Slice14 FILE AND DIRECTORY MOVE IMPLEMENTED / DEBUG AND RELEASE ORDINARY PASS / MANUAL DEFERRED**。Slice14跨盘文件与有界文件夹移动已接入既有剪切粘贴/移动队列，复用Copy逐文件提交并移除来源，最后移除空目录。嵌套/空目录、ADS、只读、KeepBoth、取消及混合队列部分完成反馈通过普通验证。完整Debug/Release各1148 PASS、0 FAIL、4权限1314 SKIP；184输入/271资源、冻结合同与六旧EXE摘要核对通过。开发遵守文件管理器功能范围，不增加额外防护平台。Merge/Replace、删除回收站、拖放、属性、移动目录位置跟随及后续远端仍待做；人工UI/真实剪贴板/服务验收暂缓，主模型/compiler1，整体目标保持active。
- 复用DirectoryCopyImpl的有界目录计划、目标创建与metadata，文件沿用CopyFileAndRemoveSource，不新建复制引擎。每个文件复制提交后移除其原来源链接，再按捕获ID逆序移除空来源目录。采用常见逐文件移动语义，替代初稿的整树复制后重新处理所有来源，减少额外读取/长期文件handle与实现复杂度。
- MoveResult以独立directory_copy_result和source_files_removed/source_directories_removed记录事实；只在根完整移除后moved。目录移除失败不抹去完整copy，文件阶段失败保留部分树与已移除前缀；新增来源文件不被递归删除。中途通知异常停止并保留实际效果，最终晚取消/通知异常不逆转已完成移动。
- MoveEntries、Ctrl+X/Paste与原sequence/身份缓存支持跨盘文件/目录混合。目标已产生的部分失败只可显式Skip/Cancel，retained_result保存移除数量和路径，Skip继续未开始尾项而不再Copy当前树；有来源变化时刷新双端，否则只刷新目标。中英文数量、部分完成提示和失败位置接入原状态栏/tooltip。
- 初始四项真实C/D目录RED为1127 PASS/4 FAIL/4权限SKIP。首次实现与诊断Debug均1146/1/4，唯一失败为fixture假定rename必成功；实际既有lease返回32，callback异常被正确报告574并保留已移动前缀。修正fixture核对系统阻止或实际变更被拒两种结果，补充独立中间callback异常回归；失败日志保留，不冒充最终通过。
- 最终完整Debug/Release构建各自结束后串行普通测试，各1148 PASS/0 FAIL/4权限1314 SKIP，新增21项目录功能检查；原1127回归继续通过。184冻结输入、271资源键/工程引用、冻结三文件、10 EXE/两PRI及六旧EXE证据复用均核对。native进程/ASCII别名0，WSL swap0/OOM0；附录B既有6/1/17/0无新增，未操作其它项目历史资产。
- 当前证据`.build/phase1d-slice14/integration-20261105-1955-directory/native-validation.json`、source-snapshot.json与[恢复清单](docs/PHASE1D_SLICE14_HANDOFF.md)。人工跨盘清单见[Slice14](docs/PHASE1D_SLICE14_MANUAL_UI_CHECKLIST.md)，暂缓执行。索引#4同步v4.23；下一步继续实用文件操作与既有剩余范围，不能将本模块通过当作整项目完成。

## 跨盘文件移动与功能范围明确 — 2026-11-05

- **Slice14 FILE MOVE IMPLEMENTED / CROSS-VOLUME DIRECTORY MOVE PENDING / DEBUG AND RELEASE ORDINARY PASS / MANUAL DEFERRED**。Slice14跨盘普通文件移动已接入既有剪切粘贴/移动队列，复用Copy流程后按原来源句柄移除，文件与ADS、KeepBoth、取消及来源保留反馈完成普通验证。完整Debug/Release各1127 PASS、0 FAIL、4权限1314 SKIP；184输入/270资源、冻结合同与六旧EXE摘要核对通过。按2026-11-05用户要求明确文件管理器功能范围，开发以实际浏览/搜索/文件操作/后续远端为主，不增加额外防护平台。跨盘文件夹移动、Merge/Replace、删除回收站、拖放、属性及远端仍待做；人工UI/真实剪贴板/服务验收暂缓，主模型/compiler1，整体目标保持进行中。
- 恢复先核对Slice13全部182输入与归档摘要一致，原生无在途任务。上一goal回合为完成Slice13的实际进展，本轮继续完整项目目标；用户重新激活后的旧日期截止不另作新时段，未自行暂停或标整项目完成。
- 用户明确避免没有实际用途的防护：功能说明落[PRODUCT_SCOPE.md](docs/PRODUCT_SCOPE.md)，只以操作正确性保留必要的同名处理、来源检查、取消及失败反馈。API探针中的TxF/oplock留在research，不进入产品代码或依赖；src/tests无该依赖。
- CopyFileSafely与跨盘Move共用同一个复制实现。Move期间原source handle持有READ/DELETE及既有父目录上下文，Copy完成后按该handle移除原链接；文件/ADS及metadata仍沿用原Copy流程。跨盘普通文件从MoveEntry、Ctrl+X/Paste、同一协调器队列调用；跨盘目录仍明确17，不能宣称目录跨盘已支持。
- MoveResult新增独立copy_result/source_removed；目标已复制但来源移除失败或取消时不伪报moved，来源由本次操作保留。队列partial_count/retained_result显示已有副本，部分项仅显式Skip/Cancel，不能通过KeepBoth/改名重试重复Copy；跳过后继续尾项并保存此前副本事实。目标父按复制/残留刷新，来源父仅实际moved刷新。
- 初轮完整Debug观察1110 PASS/12预期FAIL/4权限SKIP，再实现。最终Debug/Release各1127 PASS、0 FAIL、4权限1314 SKIP，新增17项真实跨卷fixture检查，覆盖C/D不同卷、ADS、同名/KeepBoth、只读、硬链接、占用、来源替换、复制期间取消、复制后取消/移除失败、晚取消、共享入口dispatch与partial续尾。原普通文件跨卷拒绝回归改为目录范围，跨盘文件由新增真实fixture验证；不使用伪造卷ID冒充跨盘成功。
- 184输入、270资源键/工程引用、冻结三文件、10 EXE/两PRI摘要通过；六旧EXE与Slice13相同而沿用明确标注的历史普通证据。原生进程/临时别名0，WSL swap0/OOM0；附录B已有6/1/17/0无新增。人工/真实剪贴板/服务继续暂缓，未启动产品窗口。
- 当前证据`.build/phase1d-slice14/integration-20261105-1912/native-validation.json`、source-snapshot.json与[恢复清单](docs/PHASE1D_SLICE14_HANDOFF.md)。索引#4同步v4.22。后续先复用目录Copy完成跨盘目录和真实部分来源移除状态，再推进合并覆盖、删除回收站、拖放和属性；远端按既有后续阶段推进，整体目标保持active。

## 多项同卷移动与剪切粘贴完成开发验证 — 2026-10-28

- **Phase1D Slice13 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL UI AND REAL CLIPBOARD DEFERRED**。Phase1D Slice13多项同卷移动与剪切粘贴已完成开发验证：文件/目录混合队列、逐项Skip/KeepBoth/Cancel及改名续队列、原地未变更与提交后warning、标准COPY/MOVE意图与Ctrl+X/右键剪切、原始身份缓存/同序列防重放、移动专用状态和双端刷新。完整Debug/Release各1110 PASS、0 FAIL、4项symlink权限1314 SKIP；182输入、268资源、冻结合同与六旧EXE摘要核对通过。打开目录二次刷新修复保留。跨卷移动、Merge/Replace、拖放、删除回收站、属性产品化和远端待做；人工UI/真实剪贴板互操作/服务验收继续暂缓，主模型/compiler1。
- MoveEntries/ResumeMoveBatch使用同一个Submit、State与owned native action，每项仍调用MoveEntrySafely。顶层最多1024项，队列来源/目标请求路径合计4MiB预算；整包预检scope/结构/同卷后才执行。已移动前缀不重做，失败项与尾项由显式KeepBoth/Skip/新名称续队列；重复或父子重叠来源保持原捕获路径，第二次指向已移动来源明确失败。COPY与MOVE待处理队列互相保护。
- moved/unchanged/skipped及warning独立计数；最后真实提交后的晚取消不反转完成，进度Completed不代替结果，旧operation/item回调不可推进下一项，真实action未退出不得恢复或提交另一个操作。按真实moved生成原source/destination父身份刷新，同父目录去重；原地未变更/Skip无修改声明。
- 标准CF_HDROP路径codec保持有界，Preferred DropEffect明确区分COPY/MOVE，缺失继续按COPY，link/含糊值拒绝。Ctrl+X只发布数据，Paste才由Blink拥有实际操作；Blink到外部目标由外部程序执行。Cache保留sequence、顺序payload、意图与原始source/parent IDs；同一MOVE序列接受提交后不再重新捕获已完成前缀，COPY仍可重复粘贴。实际Explorer互操作没有人工执行。
- 回归先观察14项批量/意图RED，再观察15项clipboard准备/能力/资源RED；两组完整原生日志保留。第一轮实现Debug1108/0/4及Release构建成功后，复核收紧全量NoOp文案，补两项UI共用决策检查。前轮标pre-review-attempt保留；最终重新冻结182输入，两配置严格先完整构建结束、再串行普通测试，各1110 PASS/0 FAIL/4权限1314 SKIP。
- 新增53项有效检查覆盖真实文件/中文目录/ADS/硬链接、KeepBoth/Skip续尾、重复/重叠、源替换、整包结构/跨卷拒绝、取消/Shutdown、旧项回调、warning与action退出；剪贴板意图/同序列防重放/目标绑定/能力/双端刷新及混合NoOp/Skip提示。新增测试catch-only失败诊断不计PASS，计数纠正过程保留。
- 182输入、268项en/zh/fallback资源、冻结三文件、10 EXE与两PRI摘要通过，六旧EXE与目录刷新基线相同而明确复用历史普通证据。保留原有C++编译warning，未修改工具链掩盖诊断。native进程/ASCII别名0、WSL swap0/OOM0，附录B既有6/1/17/0未变。
- 主模型实现、复核与验证，compiler1，无子代理。恢复入口[Slice13清单](docs/PHASE1D_SLICE13_HANDOFF.md)，证据`.build/phase1d-slice13/integration-20261028-1015/native-validation.json`及同目录`source-snapshot.json`，工作区索引#4更新v4.20。下一步[Slice14跨卷计划](docs/PHASE1D_SLICE14_DEVELOPMENT_PLAN.md)先审计source保护与copy后安全移除边界，不能把本轮同卷移动当完整跨卷支持；整项目及Phase1D均未全部完成。

## 打开目录二次刷新修复完成原生验证 — 2026-10-28

- 用户报告打开文件夹似乎会再刷新一次。根因为每次显式导航启动监听后，Ready 被当成目录变化送入合并定时器；旧实现通过这次补刷覆盖首次枚举/监听启用空隙，稳定目录也会多加载一次。
- 首次 Browse 改为异步等待当前监听 Session 的就绪事件再枚举；Ready 仅在导航失败恢复旧页面时补刷，Changed/Overflow 仍保留，Unavailable 继续显示错误。成功启用、失败与 Stop 均释放等待；每个等待各自拥有 action，generation、UI pending 及原生 I/O 排空门禁保留。
- 修复前已观察到 Ready 额外刷新、枚举抢在监听前两项 RED。过程里的测试字段笔误、误选旧二进制及 scoped helper 缺失导致 wrapper 中断均保留，不能当作完整 RED suite 或最终结果。最终使用完整 solution 输出，严格先构建结束再测试，普通 native exit 可靠保存。
- Debug/Release 各1057 PASS、0 FAIL、4项symlink权限1314 SKIP，较Slice12新增16项，涵盖真实 Windows 监听/枚举、稳定目录只加载一次、首次加载期间真实变化、监听失败、独立等待、A-B-A、取消/关闭及16轮启用前停止。177源码输入冻结，248资源、冻结三文件、10 EXE/两PRI摘要及六旧EXE证据复用均核对通过。
- 最终 native进程/临时别名0、WSL oom_kill0；附录B既有6根文件/1 project文件/17他项目产物目录/0缺README，与接手基线一致。主模型直接实现和复核，compiler1；未启动产品窗口或执行人工UI/真实剪贴板/服务验收。
- 修复记录见[目录二次刷新](docs/FOLDER_OPEN_REFRESH_FIX.md)，证据 `.build/folder-open-refresh-fix/integration-20261028-0917/native-validation.json`，源码快照记录见同目录`source-snapshot.json`。索引#4更新v4.19；Slice12功能点和Slice13待办保持原范围，整项目未完成。

## 同卷移动后端与唯一任务协调完成原生验证 — 2026-10-28

- **Phase1D Slice12 = BACKEND AND COORDINATOR IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / PRODUCT MOVE UI UNCONNECTED / MANUAL DEFERRED**。Phase1D Slice12同卷文件/目录Move后端与唯一任务协调器已完成原生验证，独立保留移动请求/进度/结果及post-commit warning，准确区分moved/no-op/skipped；取消/晚取消、旧回调、action退出与pending COPY队列门禁已覆盖。完整Debug/Release构建及各1041 PASS、0 FAIL、4项symlink权限1314 SKIP；175输入、248资源、冻结合同与六个旧EXE摘要核对通过。产品Cut/Move/Paste、多项/跨卷移动、Merge/Replace、拖放、删除和远端仍待做；人工UI/实际剪贴板/真实服务继续暂缓，主模型/compiler1。
- 本轮恢复先核对停止快照及全部175输入，复用已结束的当前Debug test image观察真实RED1020/15/4；补充目录移动、独立提交后warning与unknown/regressing/oversize进度检查后，RED为1020/21/4。失败来自未接线coordinator与source属性/type一致性，日志保留，不再把旧1018中间记录套用于当前源码。
- MoveEntry通过现有Submit、同一个owned IAsyncAction和默认MoveEntrySafely执行，不新增worker/服务/IPC。MoveProgress按operationID/kind/closed/stage检查，阶段单调、路径有界并合并发布；进度Completed不是结果，也不是action退出。仅真实moved置committed，合法原地no-op完成但无mutation，显式Skip独立Skipped，非法无结果/Skip/no-op不伪报成功。提交后warning与晚取消不能逆转真实提交。
- 两配置各1041 PASS、0 FAIL、4权限SKIP；相较Slice11新增58项（36个backend检查、22个coordinator检查），覆盖实际文件/非空目录/ADS/硬链接/Unicode与long path、占名/KeepBoth/UTF16/1000上限、source/两父chain身份与guard、实际junction native边界、真实C:异卷拒绝、取消/晚取消、NoOp、warning、Shutdown、旧代数进度与action/admission边界。
- 保留一轮在完整Debug构建退出前10秒启动的测试，仅作overlap-attempt过程记录；最终Debug重新在完整构建结束后串行执行，Release也严格先构建再测试。最终输入175、10 EXE/两PRI摘要、248资源/工程引用、冻结三文件与审计见`.build/phase1d-slice12/integration-20261028-0852/native-validation.json`。
- 六旧EXE摘要与Slice11一致，明确沿用历史Phase0/Phase05/Transfer普通证据。Native relative rename策略probe为历史独立诊断，未重跑、不计入1041；当前backend的实际parent-junction及身份/内容回归在两配置均重新执行。STATUS_PENDING、allocator exhaustion及磁盘故障未做故障注入，不宣称所有驱动/竞态覆盖。
- native进程/临时ASCII别名0、WSL oom_kill0，附录B既有6/1/17/0未增减；用户明确禁止子代理，由主模型审查/实现/验证，compiler1。原两小时时段的超时停止记录保留为历史；本轮用户仅要求继续，未另设停止时间。工作区索引更新为v4.18。
- 完整恢复入口见[Slice12清单](docs/PHASE1D_SLICE12_HANDOFF.md)。下一步[Slice13](docs/PHASE1D_SLICE13_DEVELOPMENT_PLAN.md)先有序多项移动，再做标准COPY/MOVE意图、Ctrl+X/粘贴和移动专用状态/冲突/双端刷新；完整跨卷Move等目标仍保留，不以同卷backend替代产品完成。

## 跨时段恢复后停止并保存 — 2026-10-28

- 已暂停。最近完整两配置源码检查点为Slice11文件/多个目录混合Copy/Paste：Debug/Release各983 PASS、0 FAIL、4权限SKIP，248资源/167输入及冻结合同核对通过并已归档。Slice12同卷移动backend中间Debug记录1018/0/4已取得；随后添加的协调器测试/API仍仅Unsupported占位，当前175输入版本只通过Debug构建，未运行新增测试，Release仍是Slice11；不得将中间1018结果套用于当前源码。人工UI/剪贴板/真实服务继续暂缓，Merge/Replace、完整Move/Cut/拖放/删除及远端仍待做。
- 用户授权的窗口为2026-10-27 14:32:37至16:32:37北京时间；核对时钟已超时后停止新增开发。实际保存时间为2026-10-28T08:32:59.308126+08:00。中间存在时钟/会话跨时段恢复，coordinator Debug构建记录起于2026-10-27 12:00:47UTC，终于2026-10-28 00:25:58UTC，不将其描述为按时停止或持续工作；没有为补完功能继续开发。
- Slice12普通用户同卷移动backend已实现：原来源/两父链身份、same-handle rename、原地no-op、显式Skip/KeepBoth/Cancel、有限1000序号、文件extension/UTF-16、目录自身/后代、跨卷17拒绝及已提交后晚取消保护。35项新真实fixture的中间Debug版本1018/0/4通过；未保存该中间版本逐输入冻结清单，因此该记录只作中间native证据，完整可恢复两配置基线仍为983。
- 为确定目标父绑定策略，隔离探针比较Win32 relative（87）、Win32 absolute控制组（实际follow junction写入自有outside）与固定Native extended relative方案（实际拒绝junction1921）。后者10项观测满足身份/数据/ADS/目标/source保护，诊断不混入普通PASS数。最初探针的sharing/ID读取fixture错误已修正，全部失败记录保留；没有产品内失败后fallback。
- 协调器只新增Move kind/request/progress/result、四参构造签名、测试和返回Unsupported的MoveEntry占位；尚未实现State::RunMove/进度/终态/Submit接线，也未运行这些新测试。另新增source属性与type不一致回归，需先运行观察RED再修正。当前Debug完整构建exit0不代表这些未实现检查通过；Cut/Paste/移动产品命令仍未开放。
- 当前175项输入及实际构建/遗留、历史1018记录范围、Release仍与Slice11匹配的摘要见`.build/phase1d-slice12/integration-20261027-1525/stop-state.json`。冻结三文件不变；native/aliases清理状态=True，附录B相对既有6/1/17/0未增减=True，WSL oom_kill=0。恢复入口见[Slice12清单](docs/PHASE1D_SLICE12_HANDOFF.md)。

## 混合文件/多目录复制队列完成原生验证 — 2026-10-27

- **Phase1D Slice11 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL DEFERRED**。Slice11已接入文件/多个目录混合Ctrl+C/Paste与标准CF_HDROP，顺序和重复项保留，复用唯一coordinator串行执行。每项Skip/KeepBoth/Cancel或改名后继续尾项，已提交前缀不重做；目录partial独立计数/有界历史，旧项进度隔离，非法尾项结构在任何写入前拒绝。Debug/Release完整构建及各983项普通检查PASS、0 FAIL、4权限SKIP；248资源、167输入/冻结合同与旧六EXE摘要核对通过。人工UI/实际剪贴板/真实服务暂缓，Merge/Replace、Move/Cut、拖放、删除及远端仍待做；主模型compiler1。
- ClipboardCopySources保留旧文件列表/单目录形态，新增有序typed列表；完整1024项/4MiB结构与metadata捕获后交付，坏后项清空输出。内部缓存匹配完整顺序payload及sequence，并保留原source/parent/type身份。目录子树仍在实际执行时逐项有界预检，不承诺整队列原子快照。
- 每个目录仍复用CopyDirectorySafely；目录partial不写入generic partial_file_left。每项保存最早32个partial结果/65536 root+failure字符及全部partial尝试次数，重试成功或跳过不抹除；实际empty root亦刷新原目的父identity。根创建后失败不自动换root；只有零effects root同名才自动出现目录冲突选择。
- 两配置各983 PASS、0 FAIL、4项symlink权限1314 SKIP；新增34项普通检查，原有4项mixed-unsupported回归改为新typed正向要求。真实fixture覆盖ADS/空目录、重复目录与KeepBoth、partial后显式Skip/KeepBoth、取消/晚取消、来源替换、旧item进度、Shutdown、40次partial历史与真实action退出门禁。
- RED依次945/4/4（旧捕获限制）、950/24/4（新typed API旧后端）、982/1/4（max_directories=0尾项结构检查遗漏），失败日志全部保留。最后一处已与engine约束对齐为整队列预检；异常后保留已观测真实root及数量。另按源码审查让提交后的summary复制失败不反转真实commit，实际分配器耗尽未做故障注入。
- 最终167输入/10个EXE/两份PRI摘要、248资源、工程引用、冻结三文件及工作区审计见`.build/phase1d-slice11/integration-20261027-1500/native-validation.json`。六旧EXE与Slice10相同，明确沿用历史Phase0/Phase05/Transfer普通证据；Transfer空入口不视为复制行为测试。
- native进程/ASCII别名0、WSL oom_kill0，附录B既有6/1/17/0不变，索引#4同步v4.16。人工清单全部暂缓，没有产品窗口、真实剪贴板或服务验收。恢复说明见[Slice11清单](docs/PHASE1D_SLICE11_HANDOFF.md)。当前两小时窗口继续至16:32:37，不标整项目完成。

## 目录根冲突完成原生验证 — 2026-10-27

- **Phase1D Slice10 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL DEFERRED**。Slice10目录同名冲突已接入Skip/KeepBoth/Cancel，KeepBoth保留整目录名并原子选择空闲编号；任何root创建后的失败或取消保留真实partial，不自动换名重做，Merge/Replace仍禁用。完整Debug/Release构建与各949项普通检查PASS、0 FAIL，各4项symlink权限1314 SKIP；248项资源对齐、165项输入与冻结IPC/服务核对通过，六个旧二进制摘要未变而沿用既有普通证据。人工UI/实际剪贴板/真实服务继续暂缓；多目录队列、Merge、安全Replace、Move/Cut、拖放、删除和远端Provider待做。主模型执行、compiler1。
- 2026-10-16的两配置构建均已成功，上次中断只缺Release测试与归档；本轮恢复先逐一核对165输入，未更改源码或重复构建，再执行Release普通检查。Debug/Release各949 PASS、0 FAIL、4权限SKIP，18项新增根冲突/协调器检查通过。完整RED935/14/4及fixture扩展路径修复记录原样保留。
- KeepBoth最多1000次根占名，复用同次held source plan；新目的路径按old+new峰值预算整体准备，长组件截断保护UTF-16代理对。相同父目录复制可生成兄弟目录，真正自身/后代目标仍拒绝；root创建后不再换名。Skip/Cancel不触碰文件系统，非法带effects的Skipped结果不能隐藏partial。
- `.build/phase1d-slice10/integration-20261016-2150/native-validation.json`保存两配置结果、165输入/10个EXE/PRI摘要、248资源、冻结合同与工作区审计。六个旧EXE摘要与Slice9完全一致，明确复用已有普通证据；S8重定向探针只是历史证据，没有在本刀重跑。
- 本轮进程/临时ASCII别名0，WSL oom_kill0，附录B既有6/1/17/0未增加。工作区索引#4同步v4.15；恢复说明见[Slice10清单](docs/PHASE1D_SLICE10_HANDOFF.md)。人工清单全部保留待执行，未运行产品窗口、真实剪贴板或服务验收。
- 用户新授权两小时开发窗口为2026-10-27 14:32:37至16:32:37（北京时间），主模型直接执行、compiler1。接着推进多目录/混合复制队列，不回填过往中断为持续运行；整项目未完成。

## 单目录产品入口完成原生验证 — 2026-10-16

- **Phase1D Slice9 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL UI DEFERRED**。这是当前最新已验证源码检查点，详见[Slice9恢复清单](docs/PHASE1D_SLICE9_HANDOFF.md)。
- 用户要求继续项目，承接已保存Slice9计划；昨晚22:00停止记录保留。今日主模型直接开发和串行native验证，compiler并发1，不延用昨晚已结束的子代理时段；人工UI、实际剪贴板互操作、真实IndexService与管理员/MFT验收继续暂缓。
- 已核对Slice8全部161项源码输入摘要，并保存独立恢复包。目录Copy/Paste已接typed capture、同窗口唯一coordinator、目录专用计数/当前文件bytes、真实partial root刷新与改名重试；同名不合并/覆盖，不按路径回滚，完整Debug/Release构建与各931项普通检查PASS、0 FAIL，各4项symlink权限1314明确SKIP；未把权限跳过当作no-follow通过。
- 对原位置/身份门禁先做确定性延迟回归，首轮905 PASS/5 FAIL/4权限SKIP：A→B→A、切回标签、退出搜索、导航失败和Shutdown可使旧回调重新匹配。增加TabRecord command generation及共享origin gate后，12项origin检查全部通过；手动导航/切标签/搜索同时合作取消未完成剪贴板准备，自动同ID刷新保留intent。
- typed缓存保存模式、实际发布有序路径、sequence及原始source/parent IDs；原目录同名替换不能被当外部来源重新接受。目录partial历史有界保留最早32条路径/65536字符，计数包含全部尝试；显式改名重试成功也不抹去此前partial。新模型先观察919 PASS/12预期FAIL/4权限SKIP，再实现；两轮RED日志保留，不覆盖为通过。
- 最终165项构建输入、10个EXE及两配置Blink.pri摘要、242项资源/工程引用、冻结三文件与当前审计见`.build/phase1d-slice9/integration-20261016-2110/native-validation.json`。4个Phase0/Transfer二进制重新生成，已补跑两配置owned身份/copy/cancel/watcher/FakeProvider和Transfer空入口smoke；仅2个Phase05二进制与旧摘要一致而沿用既有普通证据。Transfer不当作复制行为测试，Phase0 USN error5不当作高权限验收。
- 最终native进程及临时ASCII别名0、WSL oom_kill0。附录B为本轮恢复时已有6/1/17/0；相较昨晚新增`.sock.lock`于本轮恢复首次审计已存在，本轮未创建/整理该资产。Release保留既有C++警告及2条无效LIB搜索路径的CS1668非致命警告，构建/回归通过，没有为消除警告改动工具链。
- 当前证据目录`.build/phase1d-slice9/integration-20261016-2110/`及同目录`session.json`、`progress.md`。界面/剪贴板真实体验仍未执行，Merge/KeepBoth、多目录队列、Replace、Move/Cut、拖放、删除和远端Provider继续待做。

## 已按22:00截止停止 — 2026-10-15 22:00（北京时间）

- 已按用户截止停止新增开发，3个子代理均结束，无在途native构建/检查或ASCII别名。当前源码/文档快照与逐文件摘要见`.build/development-session-20261015-1932/handoff-manifest.json`，实际停止时间见同目录`state.json`。最新898/0/4两配置证据保持，人工验收暂缓，Phase1D及整项目未完成。

- 用户最新明确允许恢复子代理，并要求今晚22:00停止、报告进度；本轮并行3子代理处理队列后台、批量剪贴板与独立回归，主模型负责产品接线和统一串行验证，compiler并发1。此前禁止子代理记录保留为历史，本轮按最新授权执行。
- 本轮已完成Slice5多文件队列与Windows剪贴板、Slice6有界目录复制后台、Slice7唯一coordinator及Slice8互斥typed source metadata capture。最新完整Debug/Release构建及各898项普通检查PASS、0 FAIL、各4项symlink权限1314 SKIP；目录UI/剪贴板命令仍未开放。228项资源、冻结IPC/服务摘要一致，人工UI/真实服务继续暂缓。截止/恢复状态见`.build/development-session-20261015-1932/state.json`。

## 互斥来源捕获完成原生验证 — 2026-10-15 21:38

- **Phase1D Slice8 = TYPED SOURCE CAPTURE IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / DIRECTORY UI UNCONNECTED / MANUAL DEFERRED**。这是当前最新已验证检查点；目录命令仍未在窗口/Windows剪贴板开放，下一刀为[Slice9目录产品入口](docs/PHASE1D_SLICE9_DEVELOPMENT_PLAN.md)。
- CaptureClipboardCopySources先清空旧输出，对整个列表先做1024路径/4MiB结构校验，再用同一no-follow source handle和parent DirectoryLease捕获身份/type。成功只发布有序普通文件列表或单个普通目录；混合/多目录、取消或任一失败整包回到monostate，destination保持空。保留重复项及不同名称硬链接，旧files-only API行为不变，不通过失败切换另一类API。
- root metadata捕获不遍历树、不读取ADS或证明内容可复制；带目录ADS/后代junction的普通root可通过capture，实际DirectoryCopySafely仍会按自己的完整预检拒绝不支持的树。稀疏fixture实际设置成功，typed入口明确Unsupported，旧metadata入口仍保持兼容。
- 完整Debug/Release构建及各 **898项普通检查PASS、0 FAIL、native exit0**，新增29项来源捕获检查。各4项file-symlink因普通权限1314明确SKIP，未算通过。161个冻结输入、10个产物SHA256、两配置日志见`.build/phase1d-slice8/integration-20261015-2120/native-validation.json`；228项资源对齐，冻结三文件及6个沿用旧二进制摘要不变。本刀没有失败build/test尝试，S7首次RED及此前能力探针失败日志保留。
- 独立owned reparse探针实际以WRITE_ATTRIBUTES-only handle成功把已创建目标root改成junction：同ID、tag和sentinel目的地均核实，engine以error50拒绝，copied=false、partial_tree_left=true、files=0、dirs=1；source与sentinel身份/字节/唯一子项不变，fixture已清理。31条观测、0个必需不变量失败，与898普通PASS分开；这只证明该真实hook时点的拒绝，不宣称share READ冻结所有metadata/reparse或穷尽竞态。证据见`.build/phase1d-slice8/research/owned-reparse-probe/`。
- 本轮不再扩展功能，整理恢复快照、索引与只读审计，22:00停止。目录UI/名称重试/partial展示与刷新、Merge/KeepBoth、安全Replace、Move/Cut/拖放/Delete、远端Provider及人工验收继续待做；Phase1D及整项目未完成。
- 收尾静态审查补入[恢复清单](docs/PHASE1D_SLICE8_HANDOFF.md)：现有origin没有navigation generation，A→B→A可能让旧准备结果重新匹配，下一刀先可控延迟复现并补世代失效；本轮未UI复现/修复。目录新接线还须独立重试分支、跨重试partial保留和typed内部剪贴板身份，不能让目录失败重试进入通用新建空目录。898普通检查不代表这些UI边界已验证。

## 目录任务协调器完成原生验证 — 2026-10-15 21:20

- **Phase1D Slice7 = BACKEND / COORDINATOR IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / DIRECTORY UI UNCONNECTED / MANUAL DEFERRED**。这是Slice7历史已验证检查点；目录菜单、目录剪贴板命令、mixed batch和Merge在该检查点仍未开放。后续进度见上方Slice8。
- CopyDirectory接入唯一Submit/owned native action，独立保留directory request/progress/result，tree partial、root/failure path及真实文件/目录数量不丢失，通用partial_file_left始终false。当前文件切换后bytes归零，count与bytes合法性受限；旧id/kind/closed/terminal进度无效。native返回copied决定成功，晚Cancel不反转提交；completed进度和terminal发布均不等于action退出。
- 完整Debug/Release构建及各 **869项普通检查PASS、0 FAIL、native exit0**；各3项symlink普通权限1314明确SKIP。新增26项coordinator检查覆盖真实engine成功/取消/child rival partial、源oracle、提交/action两边界、Shutdown/销毁、跨kind旧callback、bytes重置、pending file batch保护、异常effects及错误映射；source身份优先级另加1项确定性回归。
- 首轮Debug 867 PASS/1 FAIL记录保留attempt1：检查顺序先使用未验证expectedSourceID作祖先判断，错误ID恰指目标祖先时返回87。已改为先同handle核验实际源ID，再作self/descendant判断；新fixture明确expected源ID=目标父ID，验证1006、零目标写入、源完整，没有放宽断言。S6曾通过的源快照与旧日志仍保留，不回填历史通过。
- 修复同一file-batch item多次失败各留新路径但历史partial item数量不变时的刷新遗漏，派发条件同时检查本次实际committed/partial事实；保留前刀轻量UI通知和live冲突检查。人工UI体验未执行。
- 最终159输入/10产物SHA256、两配置日志及审计见`.build/phase1d-slice7/integration-20261015-2100/native-validation.json`。228项资源对齐、冻结三文件不变，6个旧二进制摘要不变且明确沿用证据；进程/ASCII别名0，WSL oom_kill0，附录B既有5/1/17/0未变，工作区索引#4同步v4.12。
- 目录UI/剪贴板与名称重试/partial展示刷新、Merge/KeepBoth、目录ADS/reparse对象、完整metadata、Move/Cut/拖放/Delete、远端及其它后续阶段仍待做，Phase1D及整项目未完成。主模型统一验证，compiler1，本轮3子代理授权有效至22:00。

## 目录任务协调器验证中 — 2026-10-15 21:15

- Slice7已实现独立DirectoryCopy入口、request/progress/result和唯一native action适配；Debug完整构建及869项普通检查PASS、0 FAIL、3项权限SKIP，Release构建中。26项新协调器生命周期检查全部通过。
- 首轮Debug为867 PASS/1 FAIL：既有wrong-source-ID fixture偶发把错误期望ID碰成目标祖先，原engine在验证实际源ID之前先判断祖先，返回87。已把实际源身份验证提前，并新增确定性expectedSourceID=目标父ID的1006/零写/源保留回归；失败日志和原输入/构建保留attempt1，不回填通过。
- 当前目录UI/剪贴板未接入，旧Slice6 842检查点保持历史完整；完整两配置证据完成前不标本刀通过。

## 有界目录复制后台完成原生验证 — 2026-10-15

- **Phase1D Slice6 = BACKEND IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / DIRECTORY UI UNCONNECTED / MANUAL DEFERRED**。这是Slice6历史已验证源码检查点；目录复制尚不在菜单、剪贴板或批量队列开放，已可用的产品命令仍以Slice5普通文件复制为限。当前继续独立Slice7 coordinator adapter，不扩大IndexService/IPC。
- CopyDirectorySafely先用held source handle进行有界枚举、类型/身份/路径预检；最多10000后代、256目录、128深度、16MiB UTF-16路径预算，可下调。自身/后代目的地、目录ADS、reparse、未支持存储和case-colliding名称明确拒绝。source目录持续保护，最后复核名称/身份/type库存，不宣称原子快照。
- 目标root原子新建并保持creation guard，metadata handle核验同ID，子文件只复用CopyFileSafely及现有ADS路径。后序提交目录基础属性/时间，失败或取消保留新树并报告真实partial/count/path；即使只创建空root也不伪报无改动，不按路径递归回滚。最终真实提交后observer异常/晚取消不逆转成功。
- 完整Debug/Release构建及各 **842项普通检查PASS、0 FAIL、native exit0**，新增44项真实目录检查全部通过；各3项file-symlink普通权限1314明确SKIP。覆盖嵌套/空目录、Unicode/长路径/文件ADS、元数据、限额、源/父身份替换、同名root/child rival、真实junction、取消/partial、源库存新增、guard与释放。source leaf替换企图实际Move被error32阻挡，未伪称执行过成功替换分支。
- 独立矩阵197条API观测定位CreateDirectory2W加share WRITE在本机返回87；采用固定LIST|READ_ATTR|SYNC/share READ创建及独立同ID READ_ATTR|WRITE_ATTR/share RW metadata，所有实际path操作保留DISALLOW。metadata访问不受data sharing完整限制，故仍逐层检查reparse/DeletePending。矩阵不混入842普通PASS，前三次编译/链接/原生RED保留，详见`.build/phase1d-slice6/research/owned-dir-probe/assessment.json`。
- 最终验证、157构建输入/10产物SHA256及只读审计见`.build/phase1d-slice6/integration-20261015-2030/native-validation.json`。228项资源一致，冻结三文件不变，6个旧Phase0/Phase05/Transfer二进制未变且明确沿用证据；Transfer空入口不当作行为测试。附录B既有5/1/17/0未变，native进程/临时别名0，WSL oom_kill0，索引#4同步v4.11。
- 本刀还减少多文件UI队列保留的完整batch副本，并在派发旧冲突前复查live stage/resumable；人工界面体验仍未验收。目录Merge/KeepBoth/UI/剪贴板、Replace、Move/Cut、Drag & Drop、Delete及后续阶段独立待做，整项目未完成；本轮仍按最新3子代理授权、compiler1继续至22:00。

## 多文件复制队列与批量剪贴板完成原生验证 — 2026-10-15

- **Phase1D Slice5 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL UI DEFERRED**。此节为当前已验证产品状态，以下Slice4与此前记录保留为历史。Ctrl+C/右键复制支持最多1024个普通本地文件，Windows Unicode CF_HDROP完整有界解析；内部实际发布路径和sequence匹配时保留原source identity，外部数据在worker整批捕获，任一非法/目录/特殊存储项拒绝整批。准备阶段可合作取消，不触碰源文件。
- 唯一窗口coordinator以串行队列复用CopyFileSafely，记录每项实际结果和重试历史。冲突停当前项，显式Skip/KeepBoth/改名继续；已经提交的项不重做，未开始项不伪报已取消/已跳过。取消保留成功前缀与真实partial路径，最后一项实际提交后晚取消保持成功。旧operation/item回调不能推进当前队列，导航不重定向原目的地。
- 完整Debug/Release构建和各 **798项普通检查PASS、0 FAIL、native exit0**。保留此前全部单文件/ADS/DeletePending回归，并增加真实多文件/重复/硬链接、冲突续队列、源变化、取消/关闭、partial历史及剪贴板边界检查。各2项file-symlink fixture普通权限1314明确SKIP，未算通过。228项资源唯一且中英文/fallback一致，工程引用有效，冻结IPC/IndexService三文件摘要未变。
- 最终日志、153项构建输入及10个产物摘要见`.build/phase1d-slice5/integration-20261015-1932/native-validation.json`；此前Phase0/Phase05/Transfer共6个二进制摘要未变，明确沿用Slice4回归证据，未重复执行，Transfer空入口不视为复制行为检查。没有本刀失败native尝试；此前失败证据保留。
- 按用户本轮最新授权使用3个子代理，主模型负责统一接线与串行native验证，compiler并发1，今晚22:00停止报告。未运行产品窗口、用户实际剪贴板互操作、真实IndexService或管理员/MFT验收；[Slice5人工清单](docs/PHASE1D_SLICE5_MANUAL_UI_CHECKLIST.md)全部待执行。
- 检查点进程及临时ASCII别名均0，WSL oom_kill0；附录B既有Work根层5文件、project根层1文件、其它工程产物目录17、缺失分区README0，未增加污染或整理其它资产。工作区索引#4同步v4.10。
- 目录递归/Merge、安全Replace、Move/Cut、Drag & Drop、Delete和后续Provider/远端能力仍待开发，Phase1D及整项目未完成。正在先验证目录创建句柄与已有复制保护兼容性，独立推进目录复制后台。

## 复制冲突与附加数据流完成原生验证 — 2026-10-15

- **Phase1D Slice4 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL UI DEFERRED**。此节为Slice4历史检查点，当前状态见上方Slice5。单文件同名冲突可明确选择Skip/KeepBoth/Cancel；Skip独立显示已跳过，零文件I/O，不会因弹窗期间目标消失又复制。KeepBoth以CREATE_NEW寻找首个空闲确定序号，保留扩展名和Unicode边界；默认取消，失败/取消后手动名称重试继续可用。
- 唯一CopyFileSafely路径扩展到默认及命名$DATA流，保留Zone.Identifier、Unicode/空流、基础属性和时间，进度汇总所有流。保留双parent链、source身份与源流read-only sharing、目标原子占名及owned handle清理。冲突弹窗异步等实际native action结束后开放，关闭/导航使迟到continuation失效，不在UI等待。
- 修复真实目标ADS删除缺陷：FileStreamInfo仍可能列出删除中的流，仅比较inventory会在句柄关闭后丢流却报成功。提交前逐流核验identity/size/type/DeletePending，检测到即失败并清理本次副本，不报copied/最终100%；真实fixture在两配置均执行成功删除，随后error1006、copied=false、partial=false、无残留、源完整。正常source/write guard及提交后晚取消另有独立检查。
- 最终完整 **Debug/Release构建及各724项普通检查PASS、0 FAIL、native exit0**。相较Slice3新增39项检查；各2项真实file-symlink因普通权限1314明确SKIP，未当作no-follow通过。216项中英文/fallback键唯一且一致，工程引用有效，冻结IPC/IndexService三文件摘要未变。真实磁盘故障导致清理残留、完整EA/security备份及原子metadata快照仍未覆盖/实现。
- Replace保持禁用/显式Unsupported：隔离审计未证明目标身份绑定与失败保留旧目标，不以旧handle ID未变加路径rename冒充原子安全替换。ADS/目录共享矩阵定位本机CreateFile3的DISALLOW+DELETE sharing错误160；源/default目标guard保留，目标ADS和exists-only探测采用固定已验证参数，没有失败后降级重试。35条Replace观测（含2条权限SKIP）及ADS 1355/488、目录127/61条诊断记录分别归档，不混入普通PASS计数。
- 最终状态、日志、当前源码/10个产物SHA-256及审计见`.build/phase1d-slice4/integration-20261015-1813/native-validation.json`。保留首次缺Primitives头的编译失败，以及714/9、721/2、722/1三轮RED；长路径失败来自测试metadata读取缺扩展前缀，已改为实际/预期两ID均有效后比较，没有减弱身份断言。此前Phase0/Phase05/Transfer共6个二进制与Slice3已验证摘要一致，沿用其普通回归证据，本刀未重复运行；Transfer空入口不当作复制行为证明。
- 恢复后按最新AGENTS限制停止原有后台子代理，主模型直接完成剩余定位/修改/探测/集成/验证；此前并行实现作为历史保留。compiler并发1、原生构建与检查串行。未启动产品窗口、修改用户实际剪贴板、运行真实IndexService或管理员/MFT验收；[Slice4人工清单](docs/PHASE1D_SLICE4_MANUAL_UI_CHECKLIST.md)全部待执行。
- 最终进程/临时ASCII别名均0，WSL available约4.32GiB、swap约67.3MiB、oom_kill0。附录B只读审计仍为既有Work根层5文件、project根层1文件、其它工程产物目录17、缺失分区README0，没有新增污染，也未整理删除其它工程资产。工作区索引#4同步为v4.9。
- Phase1D及整项目未完成。Replace、完整metadata备份、多文件/目录复制、Move/Cut、Drag & Drop、Delete及后续Provider/远端能力独立待开发；下一刀见[Slice5多文件复制队列计划](docs/PHASE1D_SLICE5_DEVELOPMENT_PLAN.md)。


## 继续Slice4开发 — 2026-10-15 18:13（北京时间）

- Skip/KeepBoth/Cancel、独立冲突弹窗、Skipped终态及附加数据流复制已实现，完整Debug/Release验证进行中。Debug完整构建及724项普通检查PASS、0 FAIL，Release完整构建进行中。首轮714 PASS/9 FAIL、随后721 PASS/2 FAIL及722 PASS/1 FAIL保留为RED；已修复目录冲突识别、目标ADS参数组合、长路径测试读取及目标ADS删除后误报成功。Replace审计未通过，保持显式Unsupported/禁用。人工UI/真实服务继续暂缓，compiler并发1。
- 恢复时收到最新AGENTS协作限制，已停止原有后台子代理；后续修改、探测、集成与验证由主模型直接完成。此前并行开发记录保留为历史，不继续沿用旧授权。


## 单普通文件复制与Windows文件剪贴板接线通过原生验证 — 2026-10-15

- **Phase1D Slice3 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL UI DEFERRED**。此节为当前权威状态。Ctrl+C/右键复制发布标准Unicode CF_HDROP与COPY意图；Ctrl+V/背景粘贴只接受单个普通本地文件，由窗口唯一FileOperationCoordinator执行。Blink→外部由外部目标拥有实际操作；不发布Cut，不删除源。自己的剪贴板seq/path匹配时保留原source identity，外部数据worker捕获；提交前核验destination origin，迟到结果不会写入已切换的位置。
- 使用双parent DirectoryLease、源same-handle identity与受控默认流读取、CREATE_NEW原子防覆盖、owned target handle清理，复用coordinator真实committed/partial结果及100ms合并进度。取消发生在实际提交前才清理，提交后晚取消/observer异常不能逆转成功；Shutdown不在UI等待。完成或真实partial按目的父identity刷新相关标签。CopyFile2/future原型不接产品路径，无失败后自动切换API。
- 最终完整 **Debug/Release构建及各685项普通检查PASS、0 FAIL、native exit0**。新增内容/身份、Unicode/long path/zero、普通属性/时间、父链、源保留、实际取消清理、callback异常、100%后晚取消、已有目标/硬链接/junction/竞态、ADS拒绝、coordinator生命周期及有界CF_HDROP codec/source capture检查。各2项真实file-symlink fixture因普通权限1314明确SKIP，没有当作no-follow通过；真实磁盘故障/清理失败残留分支尚无故障注入覆盖。203项资源/fallback一致，冻结IPC/服务源码摘要未改。
- 追加普通回归：两配置Phase0的身份/旧CopyFile2/取消/watcher及6种FakeProvider结果已核对；Phase05的progress/lifetime/search-index普通分支各exit0，未启动真实IndexService。Transfer.Tests当前为空wmain入口，仅记录exit0，不把它当作复制行为证明。Phase0首次因完整中文路径导致wcout日志截断，改用项目内ASCII相对fixture重跑核对，首次日志保留；首轮过程退出0不代替输出oracle。
- 第一次Debug编译发现clipboard模块缺DROPFILES所需头文件，已补shlobj_core.h并完整重建通过。失败日志保留`.build/phase1d-slice3/integration-20261015-1730/build-debug-attempt1.log`；最终build/tests state、日志与摘要位于同目录，追加普通入口结果单独记录。工作区历史遗留资产没有删除或重整。
- 用户明确授权今天使用3个子代理（native Copy、真实fixture回归、coordinator），主模型负责标准剪贴板/UI/工程/本地化与集成，compiler并发1、构建/普通检查串行。此前16:59两小时停止记录保持为历史，并未伪改为持续运行。人工UI/真实服务仍暂缓，未启动产品窗口或修改用户实际剪贴板；[Slice3人工清单](docs/PHASE1D_SLICE3_MANUAL_UI_CHECKLIST.md)全部待执行。
- 最终普通进程审计：MSBuild/compiler/linker、当前Blink及测试/helper/Provider进程0，临时ASCII别名0；WSL available约4.3GiB、swap约67MiB、oom_kill0。附录B只读检查仍为既有Work根层5文件、project根层1文件、其它工程产物目录17、缺失分区README0，没有新增或清理其它工程资产，详情见workspace-final.json。
- 范围：仅单默认数据流和普通基础属性/创建访问修改时间，ACL继承目标父目录，不是完整metadata备份；目录、多文件、ADS、压缩/稀疏/加密/云等特殊存储明确Unsupported。已有目标只允许改名重试或取消；完整Replace/Skip/KeepBoth/Cancel、递归Copy、Move/Cut、Drag & Drop和Delete仍未完成。Phase1D及整项目未完成，下一步见[Slice4计划](docs/PHASE1D_SLICE4_DEVELOPMENT_PLAN.md)。

## 按本轮两小时时限停止 — 2026-10-15 16:59（北京时间）

- 本轮从14:59:10持续至16:59:10截止，已停止新增开发并保存源码、证据与恢复入口；没有在途构建/测试或临时ASCII别名。旧goal的usageLimited状态保留，整项目未完成，不伪标complete。
- 当前完整Debug/Release构建及各603项普通检查通过，189项资源/fallback对齐，冻结IPC/服务源码未改；搜索完全匹配、新建和同目录重命名已接线，人工UI与真实/高权限服务验收继续暂缓。build.ps1默认并发改1并通过Parser检查，本轮由主模型执行。
- 下一阶段File Copy的API约束、prototype问题与ordinary探测已保存。普通已有目标/新目标copy验证成功；链接fixture因普通权限1314未执行，不当作no-follow证明，产品复制仍未开放。恢复入口：[Slice3计划](docs/PHASE1D_SLICE3_DEVELOPMENT_PLAN.md)、`.build/development-session-20261015-1459/handoff.md`和`stop-state.json`。
- 最后只读工作区审计与进程详情见stop-state.json，既有历史文件未整理或删除。WSL可用约4.3GiB、swap约67MiB、oom_kill0。人工门禁、其它已打开目录的路径自动跟随、复制/移动/删除/剪贴板及后续阶段未完成。

## 新建与重命名产品接线通过原生验证 — 2026-10-15

- **Phase1D Slice2 = IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL UI DEFERRED**。此节为当前权威状态，覆盖之前的新建文件夹。F2/右键重命名、名称输入、取消、冲突后修改名称、操作状态与父目录身份刷新已接线；仍只允许单选和有效committed对象，This PC/搜索结果不提交修改。
- RenameEntrySafely通过父链保护后打开并核验source identity/type/reparse，以同一source handle执行Flags=0的FileRenameInfoEx，不覆盖目标。真实fixture覆盖文件/目录/硬链接、case-only、Unicode/长路径、源/父替换、占用、取消及内容/身份保留。统一coordinator以kind与实际committed区分新建、重命名、成功no-op，取消/关闭仍不在UI等待。
- 最终完整 **Debug/Release构建及各603项普通检查PASS，exit0**；189项中英文及fallback资源唯一且一致，工程引用与冻结IPC/服务源码摘要均通过。证据`.build/phase1d-slice2/native-validation.json`，早期RED、relative RootDirectory返回87和copy2旧时间戳沿用stub的失败记录保留，均未当作最终通过。
- 未启动产品窗口或真实/高权限服务，人工清单保持待执行：[新建](docs/PHASE1D_SLICE1_MANUAL_UI_CHECKLIST.md)、[重命名](docs/PHASE1D_SLICE2_MANUAL_UI_CHECKLIST.md)。Phase1D、其余人工门禁与整项目未完成。
- 已知范围：旧名称row选择失效后清除，不自动聚焦新名称；其它标签已打开被改名目录/子目录时，不自动跟随更新其路径，需重新打开。原路径即使被替换，后续操作仍核验identity，不依据旧路径静默修改新对象。
- 下一阶段见[File Copy安全底座计划](docs/PHASE1D_SLICE3_DEVELOPMENT_PLAN.md)：已核对prototype覆盖/取消/future生命周期问题，复制、移动、删除、剪贴板及目录递归仍未开放。本轮继续收尾至16:59，停止记录待到实际截止写入。

## 重命名接线开发中 — 2026-10-15

- 已有RenameEntrySafely原生后台、源handle identity/type/reparse检查、父目录链保护和Flags=0禁止覆盖；初步Debug共600项普通检查通过，包含新增19项rename fixture。随后接入coordinator操作种类、actual committed结果、F2/菜单、共享输入/重试和父identity刷新，并新增3项coordinator检查。
- **当前源代码完整Debug构建进行中，Release验证尚未完成，不标为完整原生通过。** 上一已验证产品是下方Slice1，Debug/Release各581项。人工UI/真实服务验收继续暂缓，见[Slice2人工清单](docs/PHASE1D_SLICE2_MANUAL_UI_CHECKLIST.md)。
- 普通probe验证RootDirectory+relative leaf在此系统返回87、源保留；RootDirectory=NULL与核验过的absolute DOS目标文件/目录均成功，选择后者，不在失败后自动切换策略重复操作。原生RED及一次因copy2旧时间戳导致沿用stub对象的失败记录保留，强制编译后后台600项通过。

## 新建文件夹完成原生开发检查 — 2026-10-15

- **Phase1D Slice1 = IMPLEMENTED / DEBUG AND RELEASE BUILD AND ORDINARY TEST PASS / MANUAL UI DEFERRED**。此节为当前权威状态。文件区右键及Ctrl+Shift+N共用名称输入，支持新建、取消、重名后重新输入、独立操作状态及相关目录标签刷新；This PC与搜索结果不提交创建，缺少可信目录identity时禁用写入。
- Native后台核验leaf、父目录身份、整条目录链与reparse，使用带FILE_LIST_DIRECTORY且不共享DELETE的短期句柄；实际创建继续CreateDirectory2W及DISALLOW_PATH_REDIRECTS。Unicode、长路径、同名文件/目录保护、父路径替换、句柄占用、reparse、取消和提交后取消均有普通fixture检查。Snapshot保存实际结果，窗口关闭不在UI等待，旧完成事件不会改变新任务的刷新目标。
- 最终完整Debug/Release构建及各 **581项普通检查PASS，exit0**。181项中英文资源与fallback对齐，冻结IPC/服务源码摘要保持不变。证据`.build/phase1d-slice1/native-validation.json`；[人工清单](docs/PHASE1D_SLICE1_MANUAL_UI_CHECKLIST.md)全部保持待执行，未启动产品窗口或真实/高权限服务。
- 初期真实检查捕获“读取属性”句柄不能阻止改名，已改正并验证执行后释放；Volume GUID/GLOBALROOT与禁止重定向标志组合返回4395，采用核对盘根的handle DOS路径，保持禁止重定向。首次完整Debug联网还原停滞约9分钟，取消已核验owned进程，核对3个项目的锁定包/本地缓存后完成缓存构建；取消及失败日志保留。
- 继续按[Slice2计划](docs/PHASE1D_SLICE2_DEVELOPMENT_PLAN.md)推进重命名原生后台，UI/命令尚未接线，仍显示未开放；当前Release完整产品保持已验证的Slice1。删除、复制、移动、剪贴板及后续阶段未完成，整项目未完成，本轮截止16:59。

## 恢复第二轮两小时开发 — 2026-10-15（北京时间14:59）

- 本轮截止16:59:10，由主模型完成、禁止子代理，编译并发1，人工UI与真实/高权限服务验收继续暂缓。会话记录`.build/development-session-20261015-1459/state.json`。
- **1C Slice4 = IMPLEMENTED / DEBUG AND RELEASE NATIVE BUILD AND ORDINARY TEST PASS / MANUAL DEFERRED**。重新完成两套完整构建、各538项普通检查及exit0确认，名称完全匹配和候选截断提示已纳入Release；证据`.build/phase1c-slice4/final-20261015-1459/native-validation.json`。上次缺失退出码的记录保留，不回填历史成功。
- 1D Slice1“新建文件夹”开发中：当前有独立后台coordinator、名称输入和命令/状态接线。真实测试发现只读属性句柄不能阻止父目录改名，已改用FILE_LIST_DIRECTORY与不共享DELETE的目录链，并使用句柄解析且核对盘根的DOS路径提交，保留DISALLOW_PATH_REDIRECTS。真实探测发现Volume GUID/GLOBALROOT与此标志组合返回4395，因此明确拒绝未支持命名空间，不减弱禁止重定向；正在复测保护与释放。当前1D源码尚未完成两套产品构建，不标为原生通过或人工接受。
- 原生验证编排曾使Release还原与Debug检查的末尾重叠约1.6秒，没有并行编译；已改为确认会话退出后再启动下一项，事实保存在会话ledger，不宣称该次所有步骤完全串行。

## 本轮时限结束并保存 — 2026-10-15（北京时间）

- **当前权威状态：Phase1C Slice3已完成原生构建/普通检查；Slice4名称完全匹配已实现，完整原生验证待补。** 本轮从11:39开始，约定13:39停止；收尾时已超过截止时间，停止新增开发，保存源码与证据，不启动下一阶段。Phase1D尚未开始，整项目未完成。人工UI、真实服务和高权限/MFT验收继续暂缓。
- Slice3搜索界面、每标签草稿/范围/模式/结果、窗口请求队列、逐卷状态/重试及Open/Locate身份检查已接线；该阶段最终Debug/Release构建和各526项普通检查通过，证据`.build/phase1c-slice3/native-validation.json`。下方阶段记录保留为历史证据。
- Slice4新增名称完全匹配，复用冻结协议的prefix请求，在最多1000个候选中筛选完整名称；达到候选上限明确显示结果可能不完整，0项也不误报完整无结果。正常显示上限与候选截断分别保存，硬链接独立，重试只补未成功的卷。当前Debug普通检查 **538项PASS，0项FAIL，native exit0**；RED与最新结果保存在`.build/phase1c-slice4`。
- Slice4 Debug日志记录了Blink.exe链接和复制产物，未见编译错误；原构建会话已无法恢复，缺少可靠的最终进程退出码，**不将该次完整构建标为PASS**。Slice4 Release尚未构建/检查，现有Release仍为已验证的Slice3版本。下次先串行完成Debug/Release构建及各自普通检查，再推进1D首个文件操作阶段。
- 冻结IPC契约及IndexService源码摘要未变；本轮由主模型直接执行，禁止子代理，MSBuild/compiler并发1。结束检查未发现本项目构建/测试或Blink窗口残留；WSL可用约4.1GiB、swap使用73MiB、oom_kill 0。工作区只读审计仍为既有Work根层文件5、project根层文件1、其它工程产物目录17、缺失分区README 0，没有整理其它工程。
- 恢复入口：[Slice4计划](docs/PHASE1C_SLICE4_DEVELOPMENT_PLAN.md)、`.build/phase1c-slice4/native-validation.json`和`.build/development-session-20261015/stop-state.json`。旧goal仍为usageLimited，未覆盖为paused或complete；本轮实际停止状态独立保存。

## 搜索界面与结果动作已接线 — 2026-10-15

- **Phase1C Slice3 = IMPLEMENTED / NATIVE BUILD AND ORDINARY TEST PASS / UI AND REAL-SERVICE MANUAL ACCEPTANCE DEFERRED**。本节为当前状态。搜索框已启用，Enter提交，Ctrl+F/E聚焦，Back/Escape/返回按钮回目录；范围与匹配方式按标签独立，F5沿用已提交查询，不擅自提交草稿。当前提供名称包含/开头/扩展名；名称完全匹配按后续Slice4推进。
- 每标签拥有SearchState/Coordinator、草稿、scope/mode、结果选择和动作generation；窗口SearchRequestQueue按ticket串行服务请求，保留每标签最新pending意图，取消仍保留在途native action所有权。状态面板轮询在搜索在途时延后，关闭不在UI等待IPC mutex或worker。
- 独立SearchPresentation支持完整路径/源卷/文件类型、逐卷错误、部分结果/上限、重试未完成卷、共享隐藏策略与键盘动作。ListView虚拟化，数据以128项低优先级分批填充并受presentation generation约束；搜索不覆盖committed目录、历史和原选择，watcher自动对账保留搜索surface。
- CurrentFolder在worker内核对真实目录FileIdentity、候选scope祖先与Parent ID，排除大小写敏感sibling和索引中父目录已失效的行；alias/reparse明确不支持。Open/Locate后台重验文件、父目录、名称路径和类型；文件继续普通用户关联helper，目录/定位在枚举后再核对目录ID，Locate按完整RowIdentity选硬链接。目标消失时清除旧选择并显示已变化；stale token不能选择新页。
- 最终Debug/Release构建与各 **526项普通检查PASS**，native exit0；新增39项真实Unicode/硬链接/替换/删除、scope/reparse、队列/标签、草稿与定位检查。RED与最终证据见`.build/phase1c-slice3/native-validation.json`。固定DataTemplate、项目和资源XML有效；IPC/服务冻结摘要未变，没有运行产品UI、真实服务或高权限/MFT验收。
- 人工清单：[Slice3待执行验收](docs/PHASE1C_SLICE3_MANUAL_UI_CHECKLIST.md)。原1B/1C门禁仍独立待验收，Phase1C及整项目不标完成；本轮继续至13:39，下一刀见[Slice4计划](docs/PHASE1C_SLICE4_DEVELOPMENT_PLAN.md)。


## 恢复持续开发 — 2026-10-15

- 用户授权从北京时间11:39起继续两小时，计划13:39停止并保存；主模型执行、禁止子代理，构建/普通检查串行且编译并发1，人工验收仍暂缓。
- 正按1C Slice3计划接入搜索输入、范围/模式、独立结果列表、标签隔离、窗口请求队列、身份验证与Open/Locate。当前集成Debug构建及523项普通检查通过，Release构建进行中；界面与真实服务尚未验收，不标记Phase1C完成。
- 目标工具因旧goal保持usageLimited而拒绝新建，未将旧项目目标伪标完成；本轮截止与阶段状态独立记录在`.build/development-session-20261015/state.json`，开发/失败证据在`.build/phase1c-slice3`。


## 按约定停止 — 2026-10-14 23:00（北京时间）

- 已停止本轮开发，保存全部源码、验证证据和下一刀计划，没有在途构建/测试或残留 Blink 窗口。当前 Debug/Release 构建及各487项普通检查通过；状态面板与搜索后台已实现，搜索界面/Locate/Open、1D及后续阶段未完成，人工验收仍暂缓。
- 恢复入口：[1C Slice3计划](docs/PHASE1C_SLICE3_DEVELOPMENT_PLAN.md)、`.build/phase1c-slice2/progress.md` 和 `native-validation.json`；停止记录 `.build/development-goal/stop-state.json`。目标工具报告 usageLimited，遵守系统用量状态优先规则，不覆盖成 paused 或 complete；实际工作已按用户23:00停止。


## 搜索状态与后台请求层开发完成 — 2026-10-14

- **Phase 1C Slice 2 = BACKEND IMPLEMENTED / NATIVE BUILD AND ORDINARY TEST PASS / SEARCH UI AND RESULT ACTIONS NOT CONNECTED**。此节为当前状态。SearchState / SearchCoordinator 独立保存 query、scope、generation 和逐卷结果/错误，不改浏览快照、历史或选择；搜索框仍未启用，不将后台完成描述为用户可用的完整搜索。
- 仅使用既有只读 Search 命令，支持 substring/prefix/extension、All local volumes 与 Current Folder 请求映射。Current Folder 附带路径分隔边界并再次过滤 sibling prefix；拒绝 UNC/相对目录、无效 identity/name/path、错误卷与未知成功状态。结果携带 source volume、FileIdentity、Parent ID、Name 与完整路径，重复 link 去重，硬链接保留独立行，上限显式保存。
- 每 controller 只拥有一个 native 后台 action，逻辑取消仍保留在途 RPC 的所有权；关闭立即使 generation 失效，后台 bounded IPC 返回后丢弃旧回调。一个卷 Busy 保持未确认，其他成功结果保留；同一次查询 retry 仅查未完成卷，避免单活动索引在卷间反复加载。新查询独立清空旧结果，不增加 DFS fallback、服务 mutation 或 IPC timeout。
- 最终 Debug/Release 构建及各 **487 项普通检查 PASS**，native exit0；新增 40 项模型、known-hit/miss、目录边界、硬链接、只读路由、部分失败/重试、取消/关闭和自查回归。RED 与初次测试 helper 参数编译失败保留，证据 `.build/phase1c-slice2/native-validation.json`；冻结的 contract、service 源码摘要保持不变。
- 后续按 [Slice 3 计划](docs/PHASE1C_SLICE3_DEVELOPMENT_PLAN.md) 接标签、虚拟化搜索列表、范围/模式、状态轮询与多标签调度，以及 Open/Locate 的当前对象和父目录身份验证。现有协议没有 exact 开关，不以截断 substring 结果伪装完整 exact 搜索；大小写敏感目录范围与 reparse/long-path 的身份对账在 UI 产品接线前完成。
- 本轮没有启动产品 UI、真实服务或管理员/MFT 验收，人工门禁按用户要求暂缓；原 1B/1C 门禁、Phase 1D 和后续 Provider/远端功能仍未完成。工作区只读审计：Work 根层既有文件5、project根层1、其它工程产物目录17、缺失分区 README0、残留测试/当前 Blink进程0；没有整理其它工程。WSL 可用内存约4.5GB、swap0、oom_kill0。主模型自行完成，无子代理，当前刀构建/普通检查串行且编译并发1。


## 索引服务状态面板开发完成 — 2026-10-14

- **Phase 1C Slice 1 = IMPLEMENTED / NATIVE BUILD AND ORDINARY TEST PASS / MANUAL ACCEPTANCE DEFERRED**。此节为当前状态。已接入后台连接、异步重试、选定本地卷状态与全局操作真实计数，服务不可用不影响目录浏览；每窗口最多一个在途状态请求，15 秒状态刷新不在 UI 执行 IPC。
- 独立 IndexServiceState / Coordinator / Presentation 模块保存 generation 与弱生命周期。先查询独立进度，活动操作期间不读取可能等待索引锁的统计；IPC v1 无进度卷根，Busy 保持待确认，不误报所有卷 Healthy/Rebuilding。服务只保留一个活动索引，状态面板每轮仅查询用户选定卷，避免轮询反复切换索引。
- 关闭仅逻辑取消，shared worker 持有 Client 至既有 bounded IPC 返回，不在 UI Disconnect 或等待。检查 protocol、wire request ID、枚举和错误，未知/失败进度显式不可用。中英文资源已补齐。
- 最终 Debug/Release 构建及各 **447 项普通检查 PASS**，native exit 0；新增 36 项状态、fake RPC、readonly 路由、busy、断开/重试、阻塞 RPC 下关闭、过期回调检查。证据 `.build/phase1c-slice1/native-validation.json`，RED 与缺失 Primitives header 的首次编译失败保留。首次过早启动的 Release 检查不纳入证据，构建完成后已重跑当前产物。
- IPC ABI、ACL、Search timeout、FastIndex/BLK5 和 IndexService 源码摘要保持不变；没有启动产品 UI、安装/启停服务或运行高权限/MFT 验收。搜索入口尚未接线，继续开发 1C 搜索；Phase 1B 原人工门禁、1C 真实服务人工门禁均保留，Phase 1C 和整个项目未完成。用户授权持续至北京时间 23:00，由主模型直接执行。


## 本地目录变化监听实现与开发自检 — 2026-10-14

- **Phase 1B Slice 8 = IMPLEMENTED / NATIVE BUILD AND ORDINARY TEST PASS / PRODUCTION UI AND MANUAL ACCEPTANCE DEFERRED**。本节为当前权威状态；暂停、失败与此前 Slice 证据原样保留。用户要求持续开发至北京时间 23:00，若整个项目确实完成可提前停止；人工验收暂缓，不把 Phase 1B 标为 ACCEPTED / COMPLETE。
- 每目录标签拥有非递归 FolderWatcher/FolderChangeState，通知只触发失效与现有 Refresh/BrowseController 对账，目录快照仍是事实来源。事件 coroutine 等待不占用空闲常驻 worker；每订阅最多一个待派发提示，dirty timer 以 200 ms 静默期/1 s 连续变化上限合并，browse/projection 忙时继续保留变化。Ready 初次对账覆盖建立监听与枚举间隙，溢出走完整对账。
- 自动刷新保留地址草稿、历史、视图/排序与仍有效的 RowIdentity 选择；删除或改名后的旧行按现有规则移除选择，不误选其它硬链接。非活动标签只更新自己的页和状态。显式导航/刷新重新打开目录路径，支持手动刷新重新绑定同路径的新目录对象；自动对账复用订阅，防止 Ready→Refresh 循环。
- TabId/路径/generation 拦截旧回调，This PC 不监听，导航失败恢复 committed 目录订阅。关闭先使 token 失效并发取消信号，再处理 timer；timer 的运行时停止异常不能阻断 native 取消。后台 CancelIoEx 后等待实际完成再释放 OVERLAPPED/缓冲区/句柄，arm 后再次检查取消，覆盖事件被重置的竞态。监听不可用有中英文提示，手动刷新可重试，不加轮询或旧 API fallback。
- 最终 Debug/Release 构建及各 **411 项普通开发检查 PASS**，退出码 0；新增 40 项合并、真实 Unicode 目录通知、取消、标签/窗口关闭、非递归和同路径目录替换检查，含 20 次快速启动/取消。纯状态与 native no-op 基线分别观察到预期失败后恢复实现并通过；证据和当前源码/产物摘要见 `.build/phase1b-slice8/native-validation.json`。暂停时的 Release 构建已终止，恢复后清理了经目标验证的自建 junction 并完成一次单并发 Rebuild；中断日志保留。
- 本轮没有启动生产 UI 或进行人工验收。目录树缓存变化对账、当前目录自身的自动重定位、递归/Provider 监听留待后续；服务与 IPC ABI 未更改，未运行高权限或 MFT 验收。所有工作由主模型完成，无子代理，构建/测试串行。后续按 1C 推进索引服务状态与本地搜索开发，原门禁继续暂缓。

## Slice 8 开发暂停 — 2026-10-14

- 用户因笔记本电量剩余 10% 要求暂停；本轮构建已中断，停止继续开发和测试，等待用户恢复。
- 本地目录 watcher、通知合并、标签生命周期和自动刷新入口已实现。此前 Debug 构建及 408 项开发自检通过；随后非活动标签状态隔离修改尚待最终 Debug 构建，Release 构建/验证未完成，不声明当前 Slice 8 原生通过或人工验收通过。
- 人工验收按用户要求暂缓。恢复入口为 `docs/PHASE1B_SLICE8_DEVELOPMENT_PLAN.md` 与 `.build/phase1b-slice8/progress.md`，取消记录见 `pause-state.json`。下一步完成当前源码 Debug/Release 构建与普通回归、审查和文档收尾。

## 标签栏空白拖动与文件区命令 — 2026-10-14

- **Phase 1B Slice 7 = IMPLEMENTED / NATIVE PASS / PRODUCTION MENU AND SYSTEM INPUT PASS / MANUAL UI PENDING**。本节为当前权威状态，下方条目及其当时的状态保留为历史记录。Phase 1A、Slice 1–3/Stabilization 的已接受基线保留；Slice 4/5/6/7 人工门禁独立待验收，Phase 1C/1D 尚未开始。
- 标签栏的 `TitleBarContentHorizontalAlignment` 从 Stretch 改为 Left，TabView 只占标签及“＋”按钮所需宽度，后续空白由 WinUI TitleBar 管理拖动；AutoRefreshDragRegions 保留。受控系统鼠标验证了中部空白、“＋”之后、增删标签后的拖动，实测窗口移动 40 × 25 像素；原标签新增/选择/关闭、Ctrl+T/W/L、原生窗口按钮及窄窗口多标签检查通过。截图确认标签与系统按钮同排，顶部白条没有恢复。
- 文件区提供 Blink 自有 MenuFlyout：打开、刷新、三种视图及四字段升降序排序复用现有路径。右键未选中行替换选择，已选中行保留多选，空白处保留选择；空目录支持焦点和 Menu 键。This PC 禁用文件系统专属命令及排序。菜单捕获稳定 TabId、committed Location 和 presentation/view generation，执行时再次检查页面与选择，过期入口不能作用于新标签。ContextCanceled/窗口关闭清理菜单，回调使用弱引用。
- F2、Delete/Shift+Delete、Ctrl+C/X/V、Ctrl+Shift+N 接入统一命令策略；文件区 PreviewKeyDown 从当前输入消息读取并一次保存 Shift 状态。三种视图中的两个删除入口分别提示“删除”和“永久删除”暂未开放。复制、剪切、粘贴、新建、重命名、删除和属性的实际产品操作仍属于 Phase 1D，本刀不改动文件或发布系统剪贴板数据；地址编辑器保持文本快捷键语义。新增中英文资源及 22 项普通策略检查。
- 最终 Debug/Release 单并发构建、各 **371 项普通检查**、各 **48 项菜单检查**、各 **64 项受控系统输入检查**及各 **28 项标题栏检查**通过。菜单脚本使用窗口按键消息/UI Automation；系统输入探测覆盖三种视图的鼠标右键、Shift+F10、Ctrl+C/X/V、Ctrl+Shift+N、Delete/Shift+Delete，并核对文件内容与剪贴板序号。测试窗口均正常退出 0，无强制终止；六份最终 UI 报告均与当前对应 Blink.exe 的 SHA-256 一致。当前源码、产物与报告索引见 `.build/phase1b-slice7/native-validation.json`。既有隐藏、视图及滚轮证据保留，实物鼠标/触摸、多显示器 DPI 和系统主题门禁不以自动检查替代。
- 初期探测遇到桌面锁屏/前台切换、失效的 UIA peer 和模拟键扫描码问题；失败日志原样保留，修正验证器后以当前产物重新完成检查。用户确认当时在处理其它问题，前台切换检查仍保留。所有工作由主模型直接完成，无子代理，MSBuild/compiler 并发 1，原生构建/验证串行。下一项 Phase 1B 工作为本地目录 watcher。

- 新版 Release 已恢复本轮保存的 1 个标签地址与活动标签，PID **34096**；UIA 确认标签位于 TitleBar 内且地址正确，仅此用户窗口保留。恢复范围为标签地址/活动标签，历史、视图/排序、选择和隐藏偏好未持久化。全局只读审计发现 Work 根层散落文件 5、project 根层文件 1、其它工程产物目录 17、缺失分区 README 0，测试进程残留 0；本任务产物位于 Blink 项目内，没有整理或删除其它工程。其它工程产物目录数量从先前记录的 14 变为当前 17，按本次审计事实记录。工作区索引 #4 与 v3.9 已同步。WSL 可用内存约 4.3 GB、swap 使用 0、oom_kill 0。

## Tabs integrated into the Windows 11 titlebar — 2026-10-14

- 用户要求标签栏与最小化、最大化、关闭按钮同排且高度对齐。现有 TabView 已移入 TitleBar.Content，标题栏与原生窗口按钮统一使用 48 DIP / Tall；移除业务区的独立标签行，保留 Win11 Mica 和系统窗口按钮。标题栏局部资源将标签区改为 Stretch，内置最小拖动区域留在窗口按钮旁。
- AutoRefreshDragRegions 更新动态标签的交互区域。窗口级快捷键移到外层 window_frame，使标题栏标签与业务内容共享 Ctrl+T/W/L、F5 和 Alt 导航命令。测试源码的命令路由定位去掉原 root 名称依赖，检查含义保持不变。
- **IMPLEMENTED / NATIVE BUILD PASS / WINDOW UI PASS / VISUAL CAPTURE PASS**。当前 Debug/Release 单并发构建、各 **349 项普通检查**和各 **24 项窗口检查**通过；Release 原视图 **35 项**及窗口消息滚轮 **15 项**回归通过。窗口检查覆盖原生最小化/最大化/还原/关闭、resize 与多标签布局、受控系统鼠标新增/选择/关闭标签、标签焦点下 Ctrl+T/W/L 及业务区 Ctrl+T。截图确认标签与原生按钮同排。测试窗口均正常退出 0，无强制终止；当前源码、产物摘要和报告见 `.build/titlebar-tabs/native-validation.json`。
- 初次临时探测把直接发送 WM_NCHITTEST 返回 HTCLIENT 作为控件交互条件，但 SDK 管理的非客户区消息路径返回 HTCAPTION；失败记录保留。最终使用受控系统鼠标输入及标签状态变化证明交互，拖动区域另行检查 HTCAPTION；未把这些检查表示为实物鼠标拖动或 Snap 验收。
- 新版 Release 已打开，恢复本轮保存的标签地址 `D:\Gsmesapps\BatterGI` 和活动标签，PID **27040**；UIA 确认标签位于 TitleBar 内且地址正确。重开恢复范围仅为标签地址与活动标签，历史、视图/排序、选择和隐藏偏好未持久化。只读工作区审计仍为既有根层文件 5、project 根层文件 1、其它工程产物目录 14、缺失分区 README 0，本轮未新增；仅用户窗口保留。主模型独立执行，构建/验证串行，MSBuild/compiler 并发 1，WSL swap 0、oom_kill 0。原 Slice 4/5/6 人工门禁及多显示器/主题/拖动体验保持待确认。

## Windows 11 titlebar follow-up — 2026-10-14

- 用户要求移除窗口上方的白色系统标题栏，改为 Windows 11 风格。已使用 WinUI 内置 TitleBar 与 Window.SystemBackdrop(MicaBackdrop)，内容延伸到标题栏；窗口按钮保持 Windows App SDK 提供的原生操作。TitleBar 负责拖动区域与 caption inset/DPI，外层 Grid 将标题栏和原业务内容分成两行。
- 标题栏按钮主题跟随 XAML ActualTheme，普通与非活动按钮背景透明。弱引用的 Loaded/ActualThemeChanged 回调在窗口关闭后停止更新。没有增加自绘窗口按钮、窗口操作业务模型或全局主题设置。
- **IMPLEMENTED / NATIVE BUILD PASS / WINDOW UI PASS / VISUAL CAPTURE PASS**。Debug/Release 单并发构建、各 349 项普通检查及各 11 项窗口检查通过；当前 Release 原视图 35 项与窗口消息滚轮 15 项通过。所有测试窗口正常退出 0，无强制终止。当前显示器截图确认白色顶栏已移除、顶栏与内容一致且窗口圆角保留；最小化/最大化/还原、resize 后拖动区域、标签新增和原生 Close 均已验证。当前源码/产物摘要和报告索引见 `.build/window-style/native-validation.json`。
- 初始探测中 DWM caption bounds 返回零宽，不能据此判定按钮；已改用实际原生按钮的 UIA peer/Invoke。Windows 根元素不能 SetFocus，截图前改为聚焦本测试窗口的地址框并检查前台 PID。失败探测记录保留，不宣称通过。
- 本轮由主模型执行，无子代理，构建/验证串行且 compiler/MSBuild 并发 1。Slice 4/5/6 原人工门禁保持独立；窗口实际拖动、Snap hover、多显示器 DPI 与切换系统主题体验由用户后续试用确认。
- 新版 Release 已恢复用户本轮保存的标签/目录 `D:\Gsmesapps\BatterGI`，PID 3120；UIA 确认新 TitleBar 存在且地址恢复正确。工作区只读审计仍为既有根层文件 5、project 根层文件 1、其它工程产物目录 14、缺失分区 README 0，本轮未新增；测试窗口均已退出，WSL swap 0、oom_kill 0。

## Shared hidden-items policy — 2026-10-14

- **Phase 1B Slice 6 = IMPLEMENTED / NATIVE PASS / PRODUCTION VISIBILITY UI PASS / MANUAL UI PENDING**. 本条为最新权威状态，下方历史证据保留。 窗口级“显示隐藏项目”开关已接入所有标签与导航树，共用纯属性策略；原始枚举快照保留，文件区按可见快照显示、计数、选择与激活。默认排除 Hidden/System，开启只放行 Hidden；完整系统项目策略和持久化设置保持延期。
- 切换重新筛选已提交目录，不改变历史/在途浏览/视图/排序/地址草稿；保留可见选择，移除隐藏项选择和焦点。旧投影与树结果受取消和 generation/lifetime gate 约束。树重建惰性缓存并重新定位当前目录，其它手动展开状态可能重置。
- Debug/Release 单并发构建、各 **349 项普通检查 PASS**；其中新增 19 项隐藏策略检查，含真实 Unicode 文件属性。50k 初始 CommitBrowse 测得 Debug 约 195.7 ms、Release 约 25.6 ms；该测量是状态提交，不是大目录实物 UI 响应性验收。
- Debug/Release 生产隐藏项目 UI 各 **26 项 PASS**，使用 609 个原始根层条目（默认可见 603、开启可见 605），覆盖三视图、树过滤、可见硬链接选择、地址草稿、无重新 browse、现有/新增/非活动标签、快速切换、空目录和在途导航。Release 原视图 **35 项 PASS**，导航栏窗口消息滚轮 **15 项 PASS**。所有测试窗口正常退出 0，无强制终止。当前源码与产物摘要、日志和报告索引见 `.build/phase1b-slice6/native-validation.json`。
- 初始普通测试遗漏策略重置、UI 脚本遗漏中文“空文件夹”匹配，以及导航期间未等待 UIA 控件可用，均已修正并复测；对应失败日志/报告保留，未将失败尝试表示为通过。产品源码最后改动后的 Debug/Release 构建和上述当前回归均通过。
- 主模型直接完成本轮工作，无子代理；MSBuild/compiler 并发 1，构建/测试/UI 串行。[Slice 6 人工清单](docs/PHASE1B_SLICE6_MANUAL_UI_CHECKLIST.md) 不预勾；Slice 4/5 的原人工门禁保持独立，Phase 1C/1D 尚未开始。
- 已保存用户本轮实际标签/目录并正常重启新版 Release，恢复至 `D:\Gsmesapps\BatterGI`，PID 31976；记录见 `.build/phase1b-slice6/restored-windows.json` 和 `final-process-check.json`。最终仅该用户 Blink 窗口存活，无测试 Blink 或 FileActivationHost 残留。工作区只读审计仍为既有根层文件 5、project 根层文件 1、其它工程产物目录 14、缺失分区 README 0，本轮未新增；WSL 可用约 4.5 GiB、swap 0、oom_kill 0。

## Navigation wheel follow-up — 2026-10-14

- **IMPLEMENTED / NATIVE WINDOW-MESSAGE PASS / HARDWARE RESPONSIVENESS ACCEPTED**. 用户报告展开驱动器目录后滚轮不能翻阅，随后反馈响应慢。导航栏保持一个外层 viewport；禁用内层模板的纵横滚动，通过 UI 线程的窗口 subclass 接收普通纵向滚轮，在导航栏范围内即时 ChangeView，并累计待完成的连续/细分输入。鼠标按下、按键、窗口关闭和 HWND 销毁都有清理路径；滚动不导航、不枚举、不改变选择。
- Debug/Release 构建、各 **330 项普通检查 PASS**。生产窗口消息回归各 **15 项 PASS**，覆盖目录树/常用位置上下滚动、8 次连续输入距离、4 个细分输入等价一次整轮、滚动条定位、上下边界和地址不变；正常退出 0、无强制终止。Debug 观测约 154–173 ms，Release 约 3–205 ms；这是包含 150 ms UIA 查询间隔的消息派发到 offset 观测时间，不是实物鼠标端到端延迟。
- 当前 Release 视图回归 **35 项 PASS**，覆盖视图/排序/选择/标签及正常关闭。证据与源码/产物摘要：`.build/navigation-wheel/native-validation.json`；滚轮报告 `final-verified-debug-20261014-033345-11188/report.json`、`final-verified-release-20261014-033729-13484/report.json`；视图报告 `.build/phase1b-slice5/ui-20261014-033226-28240/report.json`。
- 系统级 SendInput 没有可靠送达滚轮，保留失败记录并单独标为未验收。保留的 pointer-message 模拟也被系统拒绝；未将其标为通过。临时 InputPointerSource 接口尝试在测试启动时触发 0xC0000005，已经从源码撤回；当前产品启动、消息滚动和关闭均通过。普通回归的单项旧函数名检查已同步为 DisableNavigationInternalScrolling。
- 本轮只由主模型执行。构建与测试串行，MSBuild 并发 1；WSL 可用约 4.5 GiB，swap 0，oom_kill 0。工作区只读审计保留既有 5 个根层文件、1 个 project 根层文件和 14 个其他工程产物目录；本轮未新增此类问题。
- 用户原窗口的标签/目录已保存，最终 Release 恢复记录见 `.build/navigation-wheel/restored-windows.json`。2026-10-14 用户实物鼠标试用后反馈“试过了够及时了”，导航栏滚轮响应性人工确认通过。该确认只覆盖响应体验，[补充人工检查](docs/PHASE1B_SLICE5_MANUAL_UI_CHECKLIST.md) 中各悬停位置、选择与独立文件区滚动等组合项不预勾；Slice 4/5 原人工门禁保持独立。

## Current authoritative status — 2026-10-14

- Accepted Phase 0 / 0.5, Phase 1A, and Phase 1B Slice 1–3 / Stabilization remain unchanged. Slice 4 retains its native pass and outstanding associated-app/manual UI gate.
- **Phase 1B Slice 5 = IMPLEMENTED / NATIVE PASS / PRODUCTION VIEW UI PASS / MANUAL UI PENDING**. Added Details, compact List and basic-icon Grid, per-tab FolderViewMode, a shared Name/Type/Size/Modified sort selector, and ascending/descending commands. Directories stay first in both directions.
- TabState owns view and sort truth. FolderViewPresentation owns cached virtualized UI templates only. All modes use the same RowIdentity values, tab-owned row source, selection and browse/navigation pipeline. Switching does not enumerate, clear the source, restart progressive projection, overwrite an address draft or synchronize navigation again.
- Control replacement prepares templates first, suppresses selection feedback while unbinding/binding, ignores events from old controls, and preserves suppression until a pending presentation completes. Delayed actual-row focus restoration waits for control loading and checks the active tab, source/view generation and window lifetime. Realized/recycled item containers receive explicit accessible names; long Details names use a bounded Grid column.
- Type sorting prepares current-language FileTypeLabel keys per normalized extension outside the comparator, then stably sorts by type/name while preserving identity, path and committed listing.
- Debug/Release builds and **330 ordinary checks per configuration PASS**, including 22 view/type checks. Production UI automation **PASS in both configurations**, 35 checks each over a 604-row Unicode fixture and real hard links: ThisPC Grid, six view switches without re-enumeration, independent hard-link selection, address draft preservation, Grid Ctrl+A, all-selection preservation, Type ascending/descending, tab view/sort/selection isolation and close-tab restoration. Both owned windows exit normally with code 0 and no forced termination. Evidence: `.build/phase1b-slice5/build-{debug,release}.log`, `tests-{debug,release}.log`, `ui-debug.log`, `ui-release-shutdown.log`; reports `ui-20261014-010137-2812/report.json` and `ui-20261014-010242-17104/report.json`. Consolidated source/binary hashes, warnings and resource/workspace audit: `.build/phase1b-slice5/native-validation.json`.
- The first compile caught Grid.IsEnabled misuse; the control children now receive enabled state. Early UI harness attempts exposed an incorrect .NET SelectionPattern method and a non-focusable list root; the harness now uses Current.GetSelection and realized focusable items. Historical failed logs/reports remain, and no failed attempt is represented as product acceptance.
- Resource controls: one lightweight child agent, Windows native MSBuild/compiler concurrency 1, and serial builds/tests/UI runs. WSL available memory has remained at least approximately 4.5 GiB, swap use 0, current boot oom_kill 0. No owned test Blink processes remain; workspace checks still report the previously recorded unrelated loose files/generated directories.
- Manual review stays independent in [Slice 5 checklist](docs/PHASE1B_SLICE5_MANUAL_UI_CHECKLIST.md). Slice 4 associated-app acceptance and 10k/50k visual/responsiveness checks remain pending; Slice 5 and Phase 1B are not labeled ACCEPTED / COMPLETE. Hidden policy, watcher and context-menu/mutation command routing remain later 1B work; Phase 1C/1D are not started. No network Provider or system drive mounting was added.

## Current authoritative status — 2026-10-13

- Accepted baseline remains Phase 0 / 0.5, Phase 1A, and Phase 1B Slice 1–3 / Stabilization (2026-08-18). The entries below preserve their original historical evidence.
- **Phase 1B Slice 4 = IMPLEMENTED / NATIVE PASS / SLICE 4 MANUAL UI PENDING**. Debug/Release builds and ordinary regressions pass. The existing production UI lifetime smoke also passes. This slice adds file activation and basic keyboard commands within D-0027. Actual associated-app opening and the new keyboard/manual checklist still require acceptance; this slice and Phase 1B are not marked complete.
- Double-tap and list-scoped Enter share one activation route; directories and This PC use existing navigation. File rows resolve through committed RowIdentity, are checked on an owned worker, and are dispatched by a separate ordinary-user `Blink.FileActivationHost.exe` after another full-identity check.
- Shell execution lives only in the helper because ShellExecuteEx may invoke third-party COM/Shell handlers. The helper starts suspended, has per-request ownership, is stopped on cancel/close, and has a 30-second dispatch deadline. Target applications remain outside Blink's process ownership. Delayed outcomes are not automatically retried.
- Added Ctrl+L, list-scoped Ctrl+A, F5 and Alt+Left/Right/Up. Selection uses valid RowIdentity values, preserves hard-link independence, and leaves navigation/history unchanged. Address-box editing keeps its own Ctrl+A/Enter behavior.
- Debug/Release ordinary regressions **PASS** for real Unicode/identity/replacement/deleted-link cases, helper argument quoting, controlled launch failure/success, cancel/shutdown, all-row selection, and literal braces in localized error paths. The added real controller/helper case verifies an owned unassociated Unicode fixture returns `0x80070483`, without launching an associated application. Logs: `.build/phase1b-slice4/tests-phase1-{debug,release}-verified.log`.
- C++ grammar parsing passed for all 12 changed/new C++ files; resource parity/uniqueness, project XML/source references, solution GUIDs and the Shell execution boundary passed static checks. A pre-existing dangling `src\\App.xaml.h` project item was corrected to the existing `App.xaml.h`. Durable static report: `.build/phase1b-slice4/static-validation.json`. This is not SDK type checking, compilation or runtime validation.
- The initial Debug attempt exited 1 at **`MSBuild.exe was not found`** (preserved in `.build/phase1b-slice4/build-debug.log`). User-authorized toolchain setup is now complete under `D:\DEV\C++`: 2026 MSBuild plus the full 2022 v143 toolset; SDK `10.0.26100.0` is registered under the system Windows Kits path. Installer exit codes were 0 and no reboot is required.
- Native verification exposed and fixed unrestricted compile parallelism (C1060), generated App startup compilation in the wrong translation unit, and omitted path fields when using metadata-only `ReadEntryMetadata`. Explicit `/utf-8` preserves Chinese literals on this ACP 936 machine. Real unassociated dispatch originally reached the helper deadline; a current-user ProgID query with Unknown fallback disabled now returns a proper no-association error. `/restore /t:Rebuild` fixes stale Pkg paths during restore/build evaluation. XamlTypeInfo code is compiled once through main.cpp, with generated standalone items removed after markup generation; duplicate source/object warnings are gone. Full-build logs are `build-{debug,release}-verified.log`; final generated-source/link closure logs are `build-{debug,release}-closed-final.log` in `.build/phase1b-slice4`. Both final builds have no reported errors. Pre-existing C4834 and optional LIB-path CS1668 warnings remain recorded.
- Release production UI lifetime smoke **PASS**: AddButton, tab/tree switching, five AddButton repeat runs, and five runs of ten Ctrl+T commands; 64 recorded actions, no crash or fatal error. Report: `.blink-data/phase1b-tab-lifetime/release-slice4-20261013/report.json`. This covers the existing lifetime gate, not the unexecuted new Slice 4 manual actions. Consolidated native/binary evidence: `.build/phase1b-slice4/native-validation.json`.
- Added root README, updated current build/output-path guidance and [Slice 4 manual checklist](docs/PHASE1B_SLICE4_MANUAL_UI_CHECKLIST.md). Original untracked diagnostic files remain owned by the previous work.
- **Phase 1C/1D = NOT STARTED**. Remaining 1B views/watcher/context-menu command routing are separate later slices. IndexService, FastIndex, BLK5, privileged IPC/security and elevated acceptance were not changed or run.

## Current authoritative status — 2026-08-18

- **Phase 1A = ACCEPTED / COMPLETE.**
- **Phase 1B Stabilization Gate = ACCEPTED / COMPLETE.**
- The original Tab/Tree `0xC0000374` lifetime crash is fixed; production
  AddButton/Ctrl+T UI lifetime regression coverage passed.
- The 50k `CommitBrowse` performance gate passed in isolated measurements:
  Release approximately 24–29 ms and Debug approximately 219–245 ms.
- The former test-order allocation issue is closed. Its evidence is recorded
  as **test-order / allocator-state sensitive**; no production resource leak
  was found, and no lower-level allocator mechanism is claimed.
- **Phase 1B Slice 4 = NOT STARTED. Phase 1C/1D = NOT STARTED.**

## 2026-08-18 Phase 1B Stabilization closure accepted

- The final closure includes This PC, multi-tab state ownership, navigation-tree
  synchronization, RowIdentity/selection behavior, hardlink independence,
  localization round-trip behavior, and the Tab/Tree heap-corruption repair.
- The destructive TreeView lifetime ordering is now authoritative: selected
  nodes are cleared before root/presentation-node destruction; ThisPC completion
  has one active-tab presentation path; and TreeView-node map iterators are not
  retained across recursive insertion.
- The external production-Blink UI lifetime smoke passed real AddButton,
  Ctrl+T, TabView switching, and Tree-root interactions. Debug and Release
  Phase1 tests both pass.
- The benchmark gate now runs before unrelated large allocation-heavy tests and
  records initial `CommitBrowse` status/error/timing. This is a test-isolation
  measure, not a product performance workaround.
- No IndexService acceptance, elevated acceptance, Phase 0.5 acceptance, or
  Slice 4 work was performed for this closure.
- Earlier `IN PROGRESS`, manual-pending, and failure notes below remain
  historical evidence; this entry is the current status.

## 2026-08-15 Phase 1B Slice 2 implemented: real multi-tabs and per-tab state isolation

- Implemented the smallest app-private window owner, `TabWindowState`, with stable `TabId`
  values independent of WinUI selected indices. Startup retains one This PC tab; Ctrl+T and
  the TabView add button create and activate a new This PC tab through the existing async
  navigation entry point.
- Each tab now owns its own `TabState`, `BrowseState`, `BrowseController`, committed
  Location/history/listing/This PC content, selection, `SortDescriptor`, and presentation
  generation. Switching tabs restores that tab's committed presentation without filesystem
  re-enumeration; active path/sidebar/content/status/buttons/selection/sort remain one active
  presentation sync path.
- Folder and This PC callbacks are gated by TabId, tab-local generation, TabState/BrowseState
  request state, and window lifetime. Inactive work may finish, while closing a tab cancels its
  presentation/This PC work, shuts down its controller/state, and drops late results. Closing
  the active tab selects the right neighbor or, at the right edge, the left neighbor; closing
  the final tab requests window close.
- WinUI `TabView` is presentation/input only; it is not authoritative business state. No second
  Browse/Navigation pipeline, Search productization, file mutation, watcher, Provider, or
  Shell Namespace path was introduced.
- Added ordinary coverage for stable IDs, tab creation/activation/preservation, Location/history/
  selection/sort isolation, deterministic close behavior, per-tab execution-state isolation,
  and invalidating a closed tab's async state; the existing BrowseController shutdown regression
  continues to cover dropping late results. Existing Phase 1A and Slice 1 tests remain
  passing. Debug/Release Blink and Blink.Phase1.Tests builds and tests passed; no Blink UI,
  IndexService, elevated acceptance, or Phase 1C/1D flow was run.
- **Phase 1A = ACCEPTED / COMPLETE. Phase 1B = IN PROGRESS — Slice 3 IMPLEMENTED, manual
  acceptance pending. Phase 1 Product Surface = FROZEN.** Slice 1/2 ordinary and manual acceptance
  is complete; only Slice 3 manual UI acceptance remains.

## 2026-08-14 Phase 1B Slice 1 started: Location model, Blink-native This PC, and default startup

- Began Phase 1B within the already frozen D-0027 Product Surface. Phase 1A remains
  `ACCEPTED / COMPLETE`; Phase 1B is now `IN PROGRESS — Slice 1`; Phase 1C/1D remain
  unstarted and no new product surface was added.
- Added the app-private `Location` value model: `ThisPC` is a real virtual location and
  `Folder(path)` stores a normalized filesystem path. `NavigationModel` history,
  `NavigationPane`, and `TabState` now use the same Location model; compatibility path
  adapters remain only for existing ordinary tests.
- Default startup now commits This PC asynchronously. This PC content contains Blink-native
  Places and accessible fixed/removable local volume entries, and is rendered through the
  existing list projection surface without making a fake filesystem path authoritative.
- Back/Forward/Up/Refresh, address-bar restoration, sidebar synchronization, This PC item
  activation, and ordinary folder navigation converge through the existing NavigationModel /
  BrowseController path. This PC work has cancellation, generation checks, weak app capture,
  and window-lifetime validation; it does not add detached workers or a second browse pipeline.
- Added ordinary Location/history/This PC state coverage, including ThisPC↔Folder navigation,
  root Up returning to ThisPC, Back/Forward preservation, and Refresh without extra history.
- Validation passed: Blink.Phase1.Tests Debug/Release and Blink solution Debug/Release builds.
  Product build required refreshing ignored NuGet/obj restore metadata because an older cache
  contained stale `B:\`/Chinese absolute paths for MIDL; no source or product behavior change
  was made for that build-environment repair. No Blink.exe, IndexService, elevated acceptance,
  or Phase 1C/1D flow was run.

## 2026-08-14 Phase 1A manual acceptance final closure

- 用户完成了 Phase 1A 剩余普通用户手工验收。A → B → C → Back → Forward
  历史顺序通过；从 C 返回 B 后直接导航到新目录 D 时，旧 C 已从 Forward
  history 截断，通过。
- 现有 50k fixture 的进入/Back、拖动 Blink 窗口、resize 和 scrolling 通过，
  结果为 **PASS WITH ACCEPTED RESIDUAL LATENCY**。仍可能有短暂交互停顿，
  但不会复现此前持续的长时间冻结或 prolonged `Not Responding`；恢复后窗口
  会立即跟随当前鼠标输入。不得将此记录解释为零延迟或完全平滑。
- 大目录关闭期间通过：窗口正常关闭，无 crash/error popup，无残留 `Blink.exe`。
- 既有普通 Release telemetry 保持为：10k 首行 `91.447 ms`、完整呈现
  `167.948 ms`、最长 UI block `5.602 ms`；50k 首行 `155.884 ms`、完整呈现
  `493.221 ms`、最长 UI block `21.150 ms`。50k 用户感知延迟约为
  `440–500 ms`，与 Windows Explorer 大目录体验大致相当；不声称普遍快于 Explorer。
- Phase 1A large-directory performance 正式 **ACCEPTED**。剩余债务是可测量的
  非瞬时呈现和偶发短暂停顿；后续多视图、Search、Tabs 若引入回归再重新 profile。
  Speculative Folder Prefetch 仍是 `ARCHITECTURE.md` 中的 Future/Phase 2 可选优化，
  不属于本次验收。
- 窄范围 Code Hygiene review 未发现 Phase 1A blocker：active local navigation
  只有一条 authoritative path；worker/task 有 ownership/cancellation；TabState/
  NavigationModel 保持 authoritative；presentation generation、RowIdentity 和
  stale-result 防护保持有效；未新增 detached worker、重复 folder pipeline、silent
  fallback 或 Phase 1B 实现。
- 以上记录追加在历史条目之后的同一时间线中；历史 `NOT READY`/失败记录保持原样，
  不回写为 PASS。现有已通过的 Debug/Release build 与 Blink.Phase1.Tests
  Debug/Release ordinary validation 没有因本次文档收尾重复运行；本次没有运行
  Phase 0.5 elevated acceptance。
- **Phase 1A = ACCEPTED / COMPLETE。Phase 1B = NOT STARTED。Phase 1 Product
  Surface 继续按 D-0027 保持 FROZEN。**

## 2026-08-14 Phase 1A responsiveness and UI-state closure follow-up

- Incorporated the manual sidebar finding: after \`Desktop → Downloads →
  Documents → Back\`, the committed page changed to Downloads while the Places
  highlight stayed on Documents. The cause was presentation-only selection being
  updated only by click handlers.
- Added the app-private \`NavigationPane\` path matcher. After every successful
  Place, Drive, Address Bar, Back, Forward, Up, or Refresh commit—and after a
  failed request restores the committed page—the sidebar now derives selection
  from the exact committed path. Matching is case-insensitive and normalized;
  descendants do not select their parent Place, ordinary subfolders clear both
  pane selections, and a separate synchronization guard prevents a visual
  selection update from starting a second navigation. \`NavigationModel\` and
  \`TabState\` remain authoritative.
- Re-audited the current presentation path. The previous synchronous
  \`TabState::SortedEntries()\` call was a real UI-thread O(N log N) stage
  (\`~91 ms\` in the earlier 50k sample), so large-folder sorting now runs in
  the existing owned presentation ThreadPool task using the authoritative
  \`SortDescriptor\`. No second sort state or detached worker was introduced.
- Large presentation is now progressive: the worker sorts once, reuses one
  localization/projection cache, prepares 250-row projection chunks, and queues
  them to the UI. PropertySet/ObservableVector publication remains bounded;
  generation invalidation still drops batches after navigation, sort, or
  window close. A committed listing may therefore be complete while visual
  presentation is still catching up.
- Added end-to-end browse timing fields for T0–T9 and explicit
  \`navigation_to_first_row_ms\` / \`navigation_to_full_presentation_ms\`.
  Ordinary Release initial-browse samples after the fix were:
  \`entries-10000\`: enumeration \`13.732 ms\`, sort \`4.498 ms\` (worker),
  projection \`31.794 ms\`, first row from navigation \`91.447 ms\`, full
  presentation \`167.948 ms\`, UI work \`80.294 ms\`, longest UI block
  \`5.602 ms\`, peak working set \`191504384\` bytes.
  \`entries-50000\`: enumeration \`53.667 ms\`, sort \`43.782 ms\` (worker),
  projection \`88.573 ms\`, first row from navigation \`155.884 ms\`, full
  presentation \`493.221 ms\`, UI work \`353.221 ms\`, longest UI block
  \`21.150 ms\`, peak working set \`358543360\` bytes. A separate 50k sample
  measured first row \`133.101 ms\`, full presentation \`427.149 ms\`, and
  longest UI block \`16.046 ms\`; the variance is total-work variance, not a
  continuous UI freeze. No sample reproduced the original >500 ms continuous
  top-level stall.
- External folder changes not appearing until Refresh remain
  \`EXPECTED / PHASE1B DEFERRED\`; ReadDirectoryChangesExW watcher work is not
  part of this slice. Refresh remains the supported convergence action.
- Future file activation must preserve the async boundary: Blink may validate
  and dispatch an OS file-association request, then return to its message
  loop; it must not synchronously wait for target-application startup,
  metadata/properties, thumbnails/previews, enumeration, search, or long file
  operations. No file-opening feature was implemented here.
- Classification: sidebar synchronization was a Phase 1A correctness fix;
  UI-thread sorting and waiting for full projection before the first row were
  foundational debt and are now addressed. Residual occasional 100 ms-scale
  publication variance is acceptable deferred performance debt, not a current
  Phase 1A blocker. External realtime convergence and file opening remain
  Phase 1B/deferred features.
- Debug/Release Blink and Blink.Phase1.Tests builds and ordinary test
  executions passed. No IndexService, elevated acceptance, or Phase 1B work
  was run. Phase 1A remains \`NOT READY\` pending the shortest remaining manual
  UI checks (Forward/history truncation, 50k Back drag/resize/scroll,
  close during active presentation, and only genuinely unverified boundary
  checks).

## 2026-08-14 Phase 1A closure audit

- Completed the Phase 1A ordinary-permission closure pass without starting
  IndexService, running elevated acceptance, or touching the Phase 0.5 core.
- Debug/Release Blink and Blink.Phase1.Tests builds passed through a temporary
  ASCII `subst B:` view of the same D: project tree; this avoids the existing
  MIDL/native-tool path-encoding failure on the Chinese repository path. Debug/
  Release Phase 0 and Transfer ordinary regressions passed. PowerShell parse and
  `git diff --check` passed. The temporary mapping was removed afterward.
- Added the minimum app-private `RowIdentity` boundary for hard-link rows:
  `FileIdentity` remains authoritative, while the link name distinguishes two
  names for the same object inside the committed directory. Selection restore,
  SelectionChanged, and selected-row target resolution now use the row identity;
  the new hard-link regression passed in Debug/Release.
- Release ordinary startup smoke passed: Blink created a window and remained alive
  for 30.11 seconds before a normal close (`CloseMainWindow`, exit code 0).
- Phase 1A is not marked `ACCEPTED / COMPLETE` yet. Manual UI evidence is still
  required for the real 10k/50k fixtures and interaction/responsiveness items in
  `docs/PHASE1A_MANUAL_UI_CHECKLIST.md` (scrolling, scrollbar drag, sorting,
  multi-selection, resize, close during enumeration, and the remaining local-browse
  checks). No PASS, timing, or memory values are inferred from the automated smoke.
- Existing inactive Phase 0 prototype detached-worker methods remain documented
  technical debt; the active Phase 1A browse path continues through
  `BrowseController` with lifetime/generation gates and no new detached workers.

## 2026-08-14 Phase 1A Back-navigation responsiveness diagnosis

- Manual UI acceptance reported a real blocker: returning with Back to a
  relatively large directory can make the whole top-level window undraggable
  for more than 0.5 seconds. Single/multi-selection and the basic A/B/C history
  path were reported as passing; Phase 1A remains `NOT READY`.
- Added one aggregate `browse_timing` record to the active
  `BrowseController` -> UI callback -> `CommitBrowse` -> render path. It records
  the requested path, entry count, background enumeration, result publication,
  commit, sorting, projection, PropertySet boxing, ObservableVector population,
  selection restore, ListView presentation handoff, total background/UI time,
  and the longest measured UI stage. It does not log per-file timings or change
  the row publication order.
- The preserved pre-fix 10k runtime record measured `BuildFolderRowProjection`
  at `64299.694 ms` and the synchronous UI callback at `64381.145 ms`; enumeration
  was only `8.837 ms`. The blocker was therefore synchronous presentation work on
  the UI thread, not directory enumeration.
- The first minimal fix is in `src/Blink/src/FolderViewFormatting.cpp`: one
  projection-local cache removes repeated `ResourceLoader` construction/lookup
  for the fixed localization keys used by the row projection. A Release runtime
  recheck before batching measured 10k projection `23.573 ms` / UI callback
  `97.460 ms`, and 50k projection `83.204 ms` / UI callback `436.891 ms`.
- The remaining 50k evidence was still above the 100 ms UI-work debt threshold,
  so the presentation path now uses two minimal Phase 1A changes in
  `src/Blink/src/main.cpp`: pure `FileEntry -> FolderRowViewData` projection is
  prepared on the existing WinRT ThreadPool for large navigations, then
  PropertySet/ObservableVector publication is dispatched in bounded 250-row
  batches. The authoritative `DirectoryListing`, RowIdentity selection, and
  generation gates remain unchanged; new navigation/sort/close invalidates late
  work.
- Post-fix Release runtime diagnostics (initial browse of the existing ordinary
  fixtures, not a claim of manual UI acceptance) recorded:
  `entries-10000`: enumeration `24.126 ms`, sort `13.061 ms`, background
  projection `43.025 ms`, PropertySet `100.587 ms`, ObservableVector `40.345 ms`,
  UI work sum `171.184 ms`, presentation wall time `248.331 ms`, first usable-row
  candidate `91.984 ms`, longest continuous UI block `18.182 ms`, peak/stable
  working set about `182.34 MiB`.
  `entries-50000`: enumeration `78.262 ms`, sort `91.005 ms`, background
  projection `128.377 ms`, PropertySet `337.489 ms`, ObservableVector
  `126.888 ms`, UI work sum `586.852 ms`, presentation wall time `724.137 ms`,
  first usable-row candidate `224.643 ms`, longest continuous UI block
  `95.802 ms`, peak/stable working set about `336.18 MiB`. Three additional
  50k repetitions measured longest blocks of `100.260 ms`, `108.006 ms`, and
  `113.406 ms`; this remains significant performance debt, but no measured
  continuous block approached the original 500 ms symptom. The UI-work sum is
  distributed across Dispatcher slices; it is not one continuous freeze.
- The active local-browse path uses one enumeration ThreadPool task and, for
  large presentation, one pure-projection ThreadPool task; it does not use
  parallel projection chunks. Sorting remains UI-thread work, while WinUI,
  PropertySet, ObservableVector, selection, and ListView interaction remain on
  the UI thread. Coarse process CPU sampling showed approximately `68%` of one
  logical CPU equivalent for the 50k run (`4.26%` normalized across the machine),
  without evidence of a saturated pure-CPU stage or justified multi-core
  fan-out. Existing inactive Phase 0 detached-worker methods remain technical
  debt and are not part of this browse path.
- PropertySet and ObservableVector objects are still eagerly materialized for
  all rows, but publication is now sliced; ListView visual virtualization is not
  being treated as data-model virtualization. The largest remaining measured
  cost is aggregate PropertySet creation/boxing, followed by ObservableVector
  append. The remaining sort/first-presentation variance can still exceed
  100 ms, so it is recorded as Phase 1A performance debt rather than declared
  solved by the batch fix alone.
- The previous PID `19428` close is classified as an intentional Codex/user
  rebuild action: `CloseMainWindow` was requested, the process did not exit
  within the graceful wait, non-elevated force-stop was denied, and the user
  subsequently closed it. The post-fix benchmark PIDs `5816` and `35008` each
  survived to telemetry and were then deliberately closed with `CloseMainWindow`
  (diagnostic exit code `0`); none is a natural Blink crash.
- Debug/Release Blink builds and Debug/Release `Blink.Phase1.Tests` builds passed;
  the ordinary Phase1 tests passed after the presentation changes. A targeted
  manual Back reproduction, drag/resize observation, and final 10k/50k manual UI
  checklist are still required. Phase 1A remains `NOT READY`.

## 2026-08-13 Phase 1 Product Surface Audit

- Completed a documentation-only audit of `PHASE1_PLAN.md`, `ARCHITECTURE.md`,
  `DECISIONS.md`, `PROGRESS.md`, the Phase 1A manual checklist, and the current
  Phase 1A application boundary. No product implementation, IndexService,
  FastIndex, BLK5, IPC, or elevated acceptance was run or changed in this audit.
- The original plan already covered Phase 1A local browse/navigation/state foundations,
  Phase 1C IndexService search integration, and basic user-permission operations, but
  did not give complete first-class scope to This PC/default startup, navigation tree,
  multiple views, external directory convergence, drive lifecycle, system recycle-bin
  semantics, directory copy/move, conflict policy, system clipboard interoperability,
  drag/drop, Blink-owned context menus, hidden-item policy, or per-volume search scope.
- `PHASE1_PLAN.md` now records those behaviors, their 1A/1B/1C/1D/1E ownership,
  Definition of Done, explicit safety constraints, and a complete Deferred list.
  `ARCHITECTURE.md` now records the corresponding product-surface boundary.
  `DECISIONS.md` adds D-0027, which freezes the product interaction and local-file
  operation baseline without authorizing implementation in this audit.
- Current source remains a Phase 1A code-built single-window/local-browse prototype;
  the newly documented This PC, tabs, views, mutation, search UI, clipboard, drag/drop,
  watcher, and context-menu behaviors are planned scope, not claims of current support.

## 2026-08-13 D-0027 semantic clarification

- Clarified the Phase 1 product baseline without changing scope or implementation: conflict
  policy is now operation/object-specific (`Replace/Skip/Keep Both/Cancel` for file copy/move,
  `Merge/Skip/Keep Both/Cancel` for directory-to-existing-directory, and `Rename Again/Cancel`
  for rename target-exists); Merge is recursive and never deletes the target directory first.
- Clarified Clipboard/Drag & Drop ownership: External→Blink and Blink→Blink are owned by
  `FileOperationCoordinator`; Blink→External only publishes standard filesystem transfer data,
  and Blink never deletes a Cut source before the external target explicitly performs the operation.
- Clarified that hard-link row identity applies equally to Search result rows and action targets;
  authoritative `FileIdentity` is unchanged. Reparse points are never followed; unsupported
  preservation is reported as `Unsupported`. Show hidden items does not expose protected OS items;
  the complete protected-system-items policy remains Deferred.

## 2026-08-13 Phase 1 Product Surface freeze cleanup

- Synchronized the final documentation semantics: External → Blink and Blink → Blink operations are owned by `FileOperationCoordinator`; Blink → External publishes only standard Windows filesystem transfer data and the external target owns the operation; Blink does not delete an external Cut source before explicit external execution.
- Clarified that the historical Phase 0 DFS fallback is prototype/local-directory fallback only. Phase 1 keeps local browsing available when IndexService is unavailable, while product full-volume indexed Search is `Unavailable`/degraded with no silent DFS fallback.
- Added Phase 1E Code Hygiene Closure as a review/audit slice, including worker/task ownership, unique authoritative product paths, UI/business-state separation, prototype-path retirement/isolation, no duplicate paths, no silent fallback, no swallowed correctness/security errors, and classified TODO/FIXME debt.
- Product Surface is now frozen at the D-0027 baseline; this cleanup does not add a feature family or authorize Phase 1B/1C/1D scope expansion.

## 2026-08-13 Phase 1A localization foundation

- Added MRT Core `Resources.resw` resource sets for `en-US` and `zh-CN`, with a thin
  app-private `blink::app::Localization` helper and explicit PRI project items.
- Migrated current user-visible fixed UI text: application title (`Blink`), navigation,
  toolbar/tooltips/placeholders, columns, status/browse errors, file type labels, file
  size units, and prototype diagnostic status text. Paths, filenames, identities, Win32
  codes, IPC fields, and operation stages remain data/diagnostic values.
- Language selection follows the system resource context. Runtime language switching,
  translation tooling, plugin localization ABI, and Phase 1B productization remain deferred.
- Ordinary validation is pending in this slice; no manual UI acceptance or elevated
  acceptance was run. Debug/Release Blink builds, Phase 1A ordinary tests, Phase 0
  Debug/Release, Transfer Debug/Release, Phase 0.5 progress/security-only ordinary
  paths, and offline derived-search tests subsequently passed. The default Phase 0.5
  service-integration entry was intentionally not run because this slice does not
  start IndexService. PowerShell/XML parsing, resource-key parity, and `git diff --check`
  also passed.

## Current status — Phase 0.5 Security Closure complete

- **Phase 0.5:** `ACCEPTED / COMPLETE`。
- **Phase 1 gate:** `READY TO PROCEED`；本条只是 gate 状态同步，不表示本轮开始 Phase 1 工作。
- **Security Closure evidence:** Run `20260811-233345` 的 targeted security slice 为 `PASS`，`failures=0`。它的顶层 `final_result=NOT READY` / `phase1_recommendation=DO NOT PROCEED` 是 slice-scoped runner 的固定聚合策略，不是 security failure。
- **Accepted security baseline:** Logon account `LocalSystem`; `SERVICE_SID_TYPE_UNRESTRICTED`; Service SID `NT SERVICE\Blink.IndexService`; RequiredPrivileges `SeImpersonatePrivilege`；runtime TokenUser/TokenGroups/TokenPrivileges 已由该 run 的 `service-token.json` 验证。
- **Accepted Pipe boundary:** SYSTEM `0x0012019F`、Service SID `0x0012019F`、Authorized Client `0x00120183`；无 Everyone、无 Authenticated Users；保留 `PIPE_REJECT_REMOTE_CLIENTS`；客户端不含 `FILE_CREATE_PIPE_INSTANCE`。
- **No remaining Phase 0.5 blocker is identified.** 虚拟服务账户与 `SERVICE_SID_TYPE_RESTRICTED` 仅作为 future hardening experiment，不再作为 Phase 1 blocker。历史 FAIL/NOT READY runs 保持原始记录。

## Phase 0.5 scope

- Documentation/API audit recorded the pre-Phase-0.5 DFS-only/one-shot-probe baseline and the old `BLK0` store limitations; the Phase 0.5 implementation now includes the NTFS MFT/USN fast path and versioned checkpoint path.
- The service design is a separate SCM process with a versioned named-pipe contract. Installation is UAC-gated; normal Blink usage is not.
- MFT/USN fast indexing, persistent checkpointing, service IPC, continuous-write Search sentinel convergence, and Windows Restart Acceptance are implemented and accepted across the recorded privileged runs; the current security baseline is closed by Run `20260811-233345`.

## 2026-08-10

- 已读取附件中的 Phase 0 完整要求；仓库初始状态为空 Git 仓库。
- 已完成 Windows API/SDK 审计，并把路线与不采用的旧 API 记录在 DECISIONS.md、API_SELECTION.md。
- 已建立 Blink.Core、Blink.Index、Blink.Properties、Blink.Transfer、Blink.Provider.Contracts、Blink.Provider.Fake、Blink.ProviderHost 和 Blink 主工程。
- Blink 主程序已采用 Windows App SDK 2.3.1 stable + C++/WinRT，使用 self-contained WinUI 3 壳子。
- 已验证本地目录浏览、File ID + Volume ID 重命名稳定性、内存索引搜索、属性/ACL/流读取、CopyFile2 进度与取消。
- 已实现 ReadDirectoryChangesExW 目录观察 Prototype，并在测试中捕获新文件事件。
- 已验证 ProviderHost 独立进程与 FakeProvider 的 normal、delay 30s、hang、error、disconnect、crash 模式；Delay/Hang 期间本地目录浏览仍可完成。
- 已生成 ARCHITECTURE.md 与 docs/PHASE0_REPORT.md，记录验证结果、失败路径、已知问题和技术债务。

## 当前状态

Phase 0 的代码、构建和核心运行探针已完成；USN 全卷读取在本机非管理员环境返回 ERROR_ACCESS_DENIED，已作为已知环境限制保留并向上暴露。

Phase 0 到此停止，等待用户验收和明确确认；不自动进入 Phase 1。

## Phase 0.5 implementation status (2026-08-10)

- Implemented `Blink.IndexService`, `Blink.Index.Contracts`, MFT/USN Fast Index, BLK5 checkpoint persistence, service installer scripts, minimal Blink UI service actions, and `Blink.Phase05.Tests`.
- Debug and Release solution builds pass. Phase 0, transfer, and Phase 0.5 IPC/failure tests pass.
- Normal-user service probe confirms the expected direct-volume restriction: initial MFT/USN operation ends with `ERROR_ACCESS_DENIED` and visible `Unavailable` state.
- The implementation and ordinary-permission failure probes pass, but the real privileged acceptance remains `NOT READY`: Codex's desktop token is medium-integrity/non-administrator, so the run was performed manually. The latest manual elevated run crossed IPC, LocalService opened the volume handle and returned Win32 error 5 (`ERROR_ACCESS_DENIED`) at `FSCTL_QUERY_USN_JOURNAL`, while explicitly selected LocalSystem completed the real initial index, Search, mutation, recovery, failure-isolation, and corruption stages. Only continuous-write Search sentinel convergence remains unaccepted at runtime; the known-hit correctness and Journal-invalid terminal behavior were observed successfully.
- A historical pre-correction run after explicit `LOCAL_SYSTEM` captured a successful initial MFT/USN operation: `961220` MFT records, `867459` files, `93761` directories, `113` USN records, 9.34452 s MFT enumeration, 5.52448 s parent-tree build, 37.8792 s total operation, 28.2773 s BLK5 save, 37.546875 s CPU, 611774464-byte peak working set, and a 108283017-byte BLK5. These measurements are retained as historical evidence only.
- That historical run then stopped at the first Search request because the newly restarted service had to load/validate the BLK5 within the old 10-second request budget; search, mutation/catch-up, persistence recovery, failure isolation, and invalid-journal tests remain pending. The runner added bounded BLK5 load/Healthy warm-ups before search benchmarking and after service restarts, while disconnect probes retain short timeouts.
- The corrected manual run `20260811-012750` passed Administrator/High Integrity, the D: NTFS direct V0 probe (`MFT_ENUM_DATA_V0` 24 bytes and `READ_USN_JOURNAL_DATA_V0` 40 bytes), and both Debug/Release builds. LocalService again failed at `FSCTL_QUERY_USN_JOURNAL` with Win32 error 5. Explicit LocalSystem accepted the initial operation, but progress IPC exceeded the previous 30-second transport grace before terminal telemetry was available; no LocalSystem service MFT result is claimed from this run.
- Source inspection showed that `FastIndex::BuildInitial` and progress statistics serialize on the same index mutex. The runner now grants initial/index-load/reconnect stages a bounded 120-second IPC transport grace while preserving absolute stage deadlines; a new manual elevated run is required.
- `Windows Restart Acceptance = NOT EXECUTED`; no automatic reboot was attempted.
- Phase 1 is explicitly stopped. Do not proceed until the elevated acceptance is completed.
- Added `scripts/run-phase05-elevated-acceptance.ps1`: one manual Administrator/High Integrity run now builds Debug/Release, tests LocalService first, explicitly asks before LocalSystem, writes structured acceptance artifacts under `.blink-data\acceptance`, and cleans only its own fixture/service data.
- The service has acceptance-only telemetry for MFT records, USN records, MFT enumeration, parent-link application, USN read, Save, total time, CPU/working set sampling, BLK5 size, and rejected-client API/PID/Win32 diagnostics. Both PowerShell and native IPC clients now request impersonation-capable pipe security QoS; no production search/index architecture was expanded.
- Full details: `docs/PHASE05_REPORT.md`.

## 2026-08-11 Harness correctness audit

- The elevated runner now gates all writes behind Administrator/High Integrity and a cross-volume isolation check; live logs/telemetry stay under `%TEMP%\BlinkAcceptance\<RunId>`, while the intentional service DataRoot remains on the target volume.
- Initial-index, load, catch-up, and Healthy waits share absolute stage deadlines; persisted-store loading is state-aware and is not declared stuck solely because CatchUp counters remain zero.
- Process execution is shared by `scripts/phase05-harness-common.ps1`; bounded stress-writer completion, fixed-target catch-up batch telemetry, and explicit artifact-export failure status are recorded in `phase05-results.json`.
- The NTFS/ReFS API audit is corrected to use V0 input structures for NTFS and V1 input structures with explicit record-version filtering for ReFS. A new manual Administrator/High Integrity run is still required; Phase 1 remains stopped.

## 2026-08-11 Search acceptance diagnosis

- Manual elevated run `20260811-031602` completed the real LocalSystem initial MFT index on D: (`909,482` files, `101,612` directories, `1,011,451` records) and BLK5 load/CatchUp to `Healthy`, but stopped when the second Search benchmark request returned transient `Busy/CatchingUp/ERROR_IO_PENDING(997)`.
- Retained `search-timing.jsonl` proves the first Search was not blocked on the FastIndex mutex: lock wait was `0.002 ms`, actual Search was `3176.364 ms`, serialization was `14.264 ms`, and pipe write was `0.009 ms`. The second request was rejected before Search because a scheduler CatchUp was running.
- Minimal source fix applied: `user_mutations_applied` is reset per CatchUp, pure internal CatchUp no longer writes same-volume operation telemetry, and the acceptance client retries only bounded Busy/997 responses. Debug/Release builds and ordinary Phase 0/0.5/Transfer tests pass.
- A new manual elevated run is required to accept this fix. Phase 0.5 remains `NOT READY`; do not enter Phase 1.

## 2026-08-11 Internal CatchUp loop follow-up

- Manual Administrator/High Integrity run `20260811-054222` passed the D: NTFS direct probe, Debug/Release builds, LocalService-first account flow, and LocalSystem initial MFT index (`909605` files, `101627` directories, `1011286` records seen; `56.6801 s` total; `642007040`-byte peak working set; `112958985`-byte BLK5). LocalService still failed `FSCTL_QUERY_USN_JOURNAL` with Win32 `5`.
- BLK5 load did not settle: service telemetry operations 2–19 each reported `user_mutations_applied` equal to the USN records in that batch, so same-volume DataRoot/telemetry writes were still treated as user mutations. Search and all later acceptance stages were not executed. Final result remains `NOT READY`.
- Root cause of the incomplete prior fix: resetting the per-CatchUp counter worked, but `IsInternalExcludedPathUnlocked()` still depended on transient full-path reconstruction. The follow-up fix stores the DataRoot `FileIdentity` outside the FastIndex mutex and walks the in-memory parent-identity chain before the existing path fallback; it does not skip USN records or change the persistent index shape.
- Debug/Release builds and ordinary Debug/Release Phase 0.5, Transfer, and Debug Phase 0 tests pass after the follow-up fix. No elevated acceptance was run by Codex; one manual elevated run is still required.
- UTF-8 side check: the saved acceptance log is UTF-8 with BOM and preserves the Chinese path; the common harness decodes redirected PowerShell/MSBuild streams as UTF-8, while the installer decodes redirected `icacls.exe` output with OEM code page 936. The attached pasted log is UTF-8 without BOM, so Windows PowerShell 5.1 default display can make it look mojibaked; reading it explicitly as UTF-8 is correct.

## 2026-08-11 Previous manual elevated run: 20260811-145851

- Manual Administrator/High Integrity run `20260811-145851` passed isolation, direct D: NTFS volume handle/USN probes, and Debug/Release builds. LocalService again opened the volume but failed at `FSCTL_QUERY_USN_JOURNAL` with Win32 error `5`; LocalSystem was tested only after explicit `-TestLocalSystem` and completed the real initial operation.
- LocalSystem initial index evidence: `910421` files, `101718` directories, `1012222` records seen; telemetry recorded `1012139` MFT links and 83 USN records. MFT enumeration took `153.055 s`, parent-tree build `148.085 s`, total operation `160.289 s`, CPU `157.09375 s`, peak working set `755957760` bytes, Save `6.95427 s`, and BLK5 size `113058621` bytes.
- BLK5 load and Journal validation reached `Healthy` with Journal ID `134011753522823542`. Real service Search performance completed: exact/prefix/substring/extension/directory result counts `1/1/13/11/1`; extension average/P95 `244.16/276.76 ms`.
- Search correctness rows returned those non-zero counts, but all five known-hit assertions were marked failed by a PowerShell harness `([uint64]0xFFFFFFFF)` -> `-1` conversion bug; known miss passed. USN mutation assertions for File ID, Parent ID, lazy path, delete, hard link, Unicode, and directory operations all passed. The selected CatchUp had `error=0` and records, but no telemetry file; service code suppresses pure internal CatchUp telemetry to avoid same-volume self-loop writes, and the old harness treated that evidence gap as a failure.
- Continuous-write stress stopped on a strict-mode `.name` access in `Find-Hit`, before its assertions. Restart recovery, service stop/kill/reconnect, synthetic invalid checkpoint, valid-store corruption, and Windows Restart Acceptance were not executed. Final result remains `NOT READY`; Phase 1 remains stopped.
- Harness-only corrections now use a positive UInt64 mask, guard empty/malformed Search hits, classify non-initial CatchUp completion separately from missing telemetry, and preserve UTF-8 child-process behavior. Ordinary Debug/Release `--search-index-only` tests and the UTF-8 smoke test pass; no elevated acceptance was rerun after the correction.

## 2026-08-12 Latest manual elevated run: 20260811-162754

- Manual Administrator/High Integrity run `20260811-162754` passed isolation, direct D: NTFS V0 volume handle/USN probes, and Debug/Release builds. Direct enumeration counted `1012833` MFT records in `1.7399043 s` and direct journal read returned 9 records in `0.0004648 s`.
- LocalService again opened the volume but failed at `FSCTL_QUERY_USN_JOURNAL` with Win32 error `5`; LocalSystem was tested only after explicit `-TestLocalSystem` and completed the real initial operation. LocalSystem evidence: `911110` files, `101750` directories, `1012889` records seen; telemetry recorded `1012860` MFT records and 29 USN records. MFT enumeration took `138.039 s`, parent-tree build `133.662 s`, total operation `144.940 s`, CPU `144.46875 s`, peak working set `755896320` bytes, Save `6.67079 s`, and BLK5 size `113131795` bytes.
- BLK5 load and Journal validation reached `Healthy` with Journal ID `134011753522823542`. Real service Search performance completed within the 5-second IPC budget: exact/prefix/substring/extension/directory averages were `2268.52/2189.24/2413.42/245.67/2278.33 ms`, P95 `2411.72/2340.84/2508.02/284.89/2428.64 ms`, and result counts `1/1/13/11/1`. All five known-hit correctness rows and the known miss passed.
- USN mutation assertions for create/delete/rename/move, directory operations, File ID, Parent ID, lazy path, hard link, Unicode, and reparse behavior passed. The selected non-initial CatchUp completed with `error=0`, `records_seen=29859`, `usn_records=2`, and `user_mutations_applied=2`.
- Continuous-write stress in this full run completed the bounded writer and 18 fixed-target CatchUp batches with monotonic USN progress and bounded IPC latency, but did not retain enough Search payload to accept the Search-level sentinel create/delete convergence. The separate targeted run `20260811-201439` subsequently accepted that convergence; this historical limitation is no longer an unresolved runtime condition.
- Restart recovery passed BLK5 load, Journal validation, CatchUp without full MFT rebuild, and a file created during service stop. Service stop/kill disconnects were bounded at `996.855/995.906 ms`, local browse remained available, and reconnect reached `Healthy`. Valid-store corruption returned `Corrupted` with error `13`.
- Synthetic checksum-valid Journal ID mismatch reached terminal `RebuildRequired` with Win32 `1181` (`ERROR_JOURNAL_ENTRY_DELETED`). The stored run's `rebuild_required=false` flag was a stale Harness expected-code check for `1178`; the runner is corrected to expect `1181`. Windows Restart Acceptance remains `NOT EXECUTED`.
- No product/index C++ code was changed for the latest acceptance result. PowerShell parsing and the UTF-8 child-process smoke test remain passing; no elevated acceptance was rerun after the Harness corrections. Final result remains `NOT READY`; Phase 1 remains stopped.

## 2026-08-12 Targeted Continuous-write Search sentinel acceptance: 20260811-201439

- The targeted sentinel run formally **ACCEPTED** Continuous-write Search sentinel convergence. It copied the valid seed BLK5 into the new RunId-specific `service-data-targeted` root, skipped Initial MFT, used explicit LocalSystem, loaded to `Healthy`, ran the bounded continuous writer, and exercised the production IPC/Search/USN CatchUp path.
- Create convergence passed with the sentinel's exact name/path, File ID, and canonical low32 volume identity. Delete convergence passed after USN CatchUp and bounded Search polling reached an empty result. Search attempt artifacts include query, candidate name/path/identity, result count, operation ID, old/target/resulting USN.
- Evidence is retained under `.blink-data\acceptance\20260811-201439`. The copied store was isolated from the historical seed and the historical BLK5 was not modified.
- At that point in the timeline, the runner's top-level `final_result=NOT READY` was intentional targeted-mode behavior: it did not recompute the complete Phase 0.5 suite and must not be read as a full Phase 0.5 PASS or Phase 1 approval. The subsequent Windows Restart Acceptance is recorded below; the combined current result is `READY WITH CONDITIONS` with documented account/ACL conditions.
- No C++ product/index change, timeout increase, Initial Build optimization, or AgentBridge work was introduced. The next step is only the two-phase manual Windows Restart Acceptance Prepare/Complete flow; Phase 1 remains stopped.

## 2026-08-12 Windows Restart Prepare preflight correction: 20260811-204117

- The first Prepare attempt stopped before BLK5 seed copying, service installation, or reboot-state creation because Windows PowerShell/.NET did not expose `[Environment]::TickCount64`.
- The harness now obtains the 64-bit uptime counter through the Windows `GetTickCount64` API, with an explicitly recorded 32-bit managed fallback for compatibility. Prepare cleanup also leaves any pre-existing `Blink.IndexService` registration untouched unless this run successfully owns it.
- AST parsing, native boot-evidence helper compilation, UTF-8 child-process smoke, and no-auto-reboot static checks pass. The failed preflight was not a core index or service-account result; no elevated acceptance was rerun.

## 2026-08-12 Windows Restart Acceptance completed: 20260811-204329

- Prepare/Complete passed as a real two-phase Windows Restart Acceptance. The boot session changed from `2026-08-11T20:39:54.5000000Z` to `2026-08-11T20:51:29.5000000Z`; the retained LocalSystem service started after reboot and the copied BLK5 was loaded.
- Journal ID remained `134011753522823542`. Recovery operation `10` was non-initial (`initial=false`, `mft_records=0`) and reached `catchup_completed_usn=3664878520`. RebootSentinel Search returned one result with exact path and FileIdentity matches.
- The first Complete attempt exposed only a Harness `.result_count` field-access bug after the successful Search. The minimal Harness fix preserved its artifacts; the manual retry passed, ended with `reboot-state.json` status `CompletePass`, and cleaned the owned service registration and fixture.
- The Complete slice's top-level `final_result=NOT READY` remains intentional because it does not recompute the full Phase 0.5 suite. Combining the accepted full run, targeted sentinel run, and Windows Restart run yields **READY WITH CONDITIONS** for Phase 0.5. Remaining conditions are production service-account resolution and pipe ACL hardening; Phase 1 remains stopped.

## 2026-08-12 Previous manual elevated run: 20260811-153431

- Manual Administrator/High Integrity run `20260811-153431` passed isolation, direct D: NTFS volume handle/USN probes, and Debug/Release builds. LocalService opened the volume but failed at `FSCTL_QUERY_USN_JOURNAL` with Win32 error `5`; LocalSystem was tested only after explicit `-TestLocalSystem` and completed the real initial operation.
- LocalSystem initial index evidence: `910734` files, `101734` directories, `1012650` records seen; telemetry recorded `1012468` MFT links and 182 USN records. MFT enumeration took `140.645 s`, parent-tree build `136.197 s`, total operation `147.708 s`, CPU `146.984375 s`, peak working set `756981760` bytes, Save `6.81049 s`, and BLK5 size `113091531` bytes.
- BLK5 load and Journal validation reached `Healthy` with Journal ID `134011753522823542`. Real service Search performance completed with result counts `1/1/13/11/1`; exact/prefix/substring/extension/directory averages were `2225.81/2289.87/2339.04/213.76/2227.41 ms`, with P95 `2259.48/2556.31/2386.81/240.41/2288.81 ms`.
- Known-hit Search correctness returned valid fixture identities and scope checks, but the Harness failed on `.Count` for a scalar conditional collection; the known miss passed. The minimal fix now forces conditional collections to arrays. Mutation assertions and non-initial CatchUp telemetry passed (`error=0`, operation 102, 75 USN records, 53 user mutations).
- Continuous-write stress completed the bounded writer and 15 fixed-target CatchUp batches with monotonic USN progress and low IPC latency, but the sentinel create query ran before the new USN was searchable. The fix now polls Search until create is visible and delete is absent, and will not accept a stale zero-result sequence.
- Restart recovery passed BLK5 load, Journal validation, CatchUp without full MFT rebuild, and a file created during service stop. Service stop/kill disconnects were bounded at `1009.36/994.13 ms`, local browse remained available, and reconnects reached `Healthy`. Valid-store corruption returned `Corrupted` with error `13`.
- Synthetic checksum-valid Journal ID mismatch was executed, but the first asynchronous `Busy/997/Rebuilding` response was judged too early in this historical run; the store was restored and reached `Healthy`. The runner was then corrected to wait for terminal `RebuildRequired` and validate `ERROR_JOURNAL_ENTRY_DELETED=1181`. Windows Restart Acceptance remains `NOT EXECUTED`.
- No product/index C++ code was changed for these Harness failures. Debug/Release script parsing and the earlier UTF-8/ordinary regressions remain passing; no elevated acceptance was rerun after the new Harness corrections. Final result remains `NOT READY`; Phase 1 remains stopped.

## 2026-08-11 Previous manual elevated run: 20260811-112040

- The manual Administrator/High Integrity harness passed isolation, the direct D: NTFS volume handle, `FSCTL_QUERY_USN_JOURNAL`, `FSCTL_ENUM_USN_DATA`, `FSCTL_READ_USN_JOURNAL`, and Debug/Release builds. LocalService again opened the volume but failed at `FSCTL_QUERY_USN_JOURNAL` with Win32 error `5`; LocalSystem was tested only after explicit selection and completed the real initial operation.
- LocalSystem initial index evidence: `909952` files, `101670` directories, `1011679` records seen; telemetry recorded `1011622` MFT links and 57 USN records. MFT enumeration took `76.7394 s`, parent-tree build `72.6032 s`, total operation `83.6026 s`, CPU `83.375 s`, peak working set `641998848` bytes, Save `6.64188 s`, and BLK5 size `113003643` bytes.
- Valid BLK5 load and journal catch-up reached `Healthy` before Search, but telemetry operations 2–73 still counted same-volume service DataRoot records as dirty. This was not a ProgressSnapshot regression: progress IPC remained responsive during the million-record build. The remaining cause was identity comparison, not the progress lock path.
- The Search stage then stopped on a 5-second IPC read timeout. Retained Search timing shows lock wait average/max `0.00152/0.009 ms`, actual Search average/max `2615.797/5948.365 ms`, serialization duration average/max `0.01887/0.030 ms`, and pipe write duration average/max `0.00839/0.015 ms`; the primary failure is actual O(N) Search work under the still-running internal CatchUp loop, not mutex wait, serialization, or named-pipe write.
- The exact/prefix/substring CSV rows completed but all had `result_count=0`; therefore they are transport/performance samples only, not search-correctness passes. Extension and directory-scoped rows were not completed. Mutation, full client restart, service stop/kill, synthetic invalid checkpoint, and valid-store corruption stages were not executed. Final acceptance remains `NOT READY`; Phase 1 remains stopped.
- Root cause follow-up: USN identities use the DWORD volume serial from `GetVolumeInformationW`, while `ReadFileIdentity` returns the 64-bit `FILE_ID_INFO.VolumeSerialNumber`. The excluded DataRoot identity is now normalized to the USN DWORD representation in `FastIndex::SetInternalExcludedRoots`. Debug/Release builds and ordinary tests pass after this patch; it has not yet been manually elevated-accepted.

## 2026-08-11 ordinary pre-acceptance regression

- `scripts/probe-volume-serial.ps1` completed without elevation on D:, the project DataRoot, and a DataRoot child. All three matched `GetVolumeInformationW`/`BY_HANDLE_FILE_INFORMATION` DWORD `0xC8B2C41B` to the low32 of `FILE_ID_INFO` `0x6AC8B2FAC8B2C41B`; high32 was `0x6AC8B2FA`. This is now documented as a canonical comparison mapping, while full FileIdentity remains 64-bit serial plus FileId.
- The minimal FastIndex boundary comparator was added for the full root identity versus USN identities. The retained `20260811-112040` BLK5 offline regression now passes known exact/prefix/substring/extension/directory-scoped/miss assertions with result counts `1/1/12/10/1/0`.
- Five offline timing samples per known-hit query were recorded in `.blink-data\acceptance\offline-search-20260811-112040-fixed.csv`. Exact/prefix/substring/directory-scoped maximums were `3149.53/2984.62/2893.69/2796.19 ms`; extension remained over the request budget (`median=6183.51 ms`, `p95=6282.32 ms`, `max=6289.93 ms`). Search correctness is fixed for this known fixture, but the extension query is a confirmed performance blocker. Phase 0.5 remains `NOT READY`; no elevated acceptance or Phase 1 was started.
- The elevated harness now has a separate `search_correctness` result section with known-hit and known-miss assertions and an out-of-scope duplicate for the directory-scoped case. MFT/USN fast indexing remains implemented but privileged acceptance pending.

## 2026-08-11 offline Search performance precondition

- Added a derived in-memory extension index to `FastIndex`; `links_`/`FileIdentity` remain authoritative and BLK5 format/checkpoint ABI is unchanged. The map is rebuilt after BLK5 Load and maintained on link add/remove/rename paths.
- Added stage timing to ordinary offline Search (`candidate filtering`, `path resolution`, `result construction`, and `sorting`) and a full-scan reference switch. The retained million-record BLK5 was benchmarked without starting the service.
- Clean after results passed exact/prefix/substring/extension/directory-scoped/directory+extension/known-miss correctness, with result counts `1/1/12/10/1/1/0`. Extension improved from reference median/max `5336.71/5433.58 ms` to derived median/max `240.31/245.18 ms`; directory+extension improved from `5341.03/5455.67 ms` to `216.38/216.90 ms`.
- Added no-service ordinary regressions for ProgressSnapshot, hard-link multi-name maintenance, BLK5 derived-index rebuild, Create/Rename/Delete bucket updates, and InternalExcludedRoots filtering. The elevated fixture now creates hard links after Initial MFT so the later USN Catch-up test is valid.
- Search performance precondition is now ready for one manual elevated acceptance. Full Phase 0.5 remains `NOT READY`; Phase 1 remains stopped.

## 2026-08-10 Architecture Plan Integration
- **D-0021 (Install-volume-following DataRoot & Per-Volume Index Architecture)** & **D-0022 (Anti-Stale Index & Consistency Scrub Policy)** formally incorporated into `DECISIONS.md`, `ARCHITECTURE.md`, `PROJECT_PLAN.md`, and `API_SELECTION.md`.
- **D-0016** marked as `SUPERSEDED`.
- Formalized product principles:
  - `InstallVolume = VolumeOf(InstallRoot)` (Large mutable data follows install volume, no silent `C:` fallback).
  - Physical store per-volume isolation: `Index\Volumes\Volume-{GUID}\`.
  - Windows reinstall recovery (only rebuilding formatted `C:`, while preserving `D:`/`E:` volume index checkpoints for USN catch-up).
  - `InternalExcludedRoots` self-index feedback isolation.
  - Anti-stale index enforcement via USN Delete processing & periodic Consistency Scrubbing.
- Code isolation confirmed: Current Phase 0.5 MFT / USN / CatchUp code is preserved untouched for ongoing acceptance testing. Roadmap breakdown defined for Phase 1.x Storage Architecture and Installer Phase.

## 2026-08-12 Phase 0.5 Security Closure implementation (pre-acceptance record)

- Implemented a Hardened LocalSystem candidate in the installer. The explicit switch configures SERVICE_CONFIG_SERVICE_SID_INFO as SERVICE_SID_TYPE_UNRESTRICTED and SERVICE_CONFIG_REQUIRED_PRIVILEGES_INFO with only SeImpersonatePrivilege. QueryServiceConfig2 plus sc.exe qsidtype/qprivs output is persisted as machine-readable security evidence.
- At the implementation stage before Run `20260811-233345`, the four security identities were documented separately: SCM Logon account, NT SERVICE\Blink.IndexService Service SID, RequiredPrivileges, and the installing user's Pipe authorized client SID. The account decision was then still UNRESOLVED; the accepted closure result is recorded below.
- Removed the prototype Authenticated Users/Administrators Pipe ACL. The service now creates a protected single-user DACL for SYSTEM, Service SID, and the authorized client SID, retains PIPE_REJECT_REMOTE_CLIENTS and impersonation/token/protocol validation, and records the actual descriptor after GetSecurityInfo.
- The client now requests exact FILE rights and does not request GENERIC_WRITE or FILE_CREATE_PIPE_INSTANCE. Ordinary Phase05 tests passed runtime descriptor checks, ProgressSnapshot, derived-index, corruption/IPC paths, and Debug/Release builds.
- Added scripts/run-phase05-security-acceptance.ps1 as a separate manual-only targeted entry point. It copies an existing BLK5 seed into a new run DataRoot and verifies the hardened service/pipe/BLK5/CatchUp/Search slice; it does not run in this round and does not recompute full Phase 0.5.
- Added native runtime service-token evidence to the targeted security path. It opens the actual service process token, records TokenUser, every TokenGroup and every TokenPrivilege with normalized SID/privilege names and attributes, and keeps this separate from QueryServiceConfig2/sc qprivs evidence. At that pre-acceptance point the runtime predicate was still pending; Run `20260811-233345` subsequently accepted it.
- Manual security attempt `20260811-220237` did not reach service start or Token capture: `SERVICE_SID_TYPE_UNRESTRICTED` round-tripped, but `RequiredPrivileges` remained empty because the installer interop used non-explicit service-config entry points and misused the successful Query buffer-size output. The installer now binds the explicit Unicode W APIs and parses the bounded MULTI_SZ correctly. Attempt `20260811-220253` stopped at the existing-service guard caused by the first attempt's orphan registration; no security acceptance result was inferred. Future failed CreateService attempts are now owned and cleaned by the harness.
- Manual security attempt `20260811-221121` confirmed the installer fix: `SERVICE_SID_TYPE_UNRESTRICTED`, `SeImpersonatePrivilege`, and the actual Pipe descriptor were recorded successfully. It then stopped before Status IPC because the PowerShell Harness requested generic pipe rights while the hardened client ACE intentionally grants only `0x00120183`; no runtime Token/BLK5 security result was inferred. The Harness now uses the six-argument `NamedPipeClientStream` exact-`PipeAccessRights` overload, and an ordinary temporary-pipe smoke confirmed `0x00120183` connects under the exact DACL. A new manual security run is still required.

## 2026-08-12 Phase 0.5 Security Closure accepted: 20260811-233345

- The manual Administrator/High Integrity targeted security slice passed with `failures=0`: hardened LocalSystem service installation/start, `SERVICE_SID_TYPE_UNRESTRICTED`, `SeImpersonatePrivilege`, actual runtime Service SID/token evidence, exact Pipe descriptor, remote-client rejection, BLK5 load to `Healthy`, non-initial CatchUp, and Search/continuous-write sentinel convergence.
- Runtime token evidence proved the running `Blink.IndexService.exe` process, not only SCM configuration: TokenUser `LocalSystem`, Service SID membership in TokenGroups, `SeImpersonatePrivilege` present/enabled, and the complete normalized privilege list were written to machine-readable `service-token.json`.
- Security Closure closed two real defects: impersonation lifecycle/unsafe process-token fallback, and the missing `FILE_READ_EA | FILE_WRITE_EA` bits in the `PIPE_ACCESS_DUPLEX` server ACE. The accepted masks are SYSTEM/Service `0x0012019F` and client `0x00120183`; client `FILE_CREATE_PIPE_INSTANCE` remains absent.
- The slice runner's top-level `final_result=NOT READY` / `phase1_recommendation=DO NOT PROCEED` is intentionally fixed aggregation and does not override this security slice PASS or the combined Phase 0.5 status. Historical failed runs remain unchanged as evidence.
- Combined with the previously accepted full MFT/USN, persistent BLK5/journal recovery, continuous-write convergence, service failure isolation, corruption/journal-invalid handling, and Windows Restart Acceptance, Phase 0.5 is now `ACCEPTED / COMPLETE`; the Phase 1 gate is `READY TO PROCEED`.

## 2026-08-13 Future extension architecture readiness decision

- Added D-0025: Blink is **extension-capable by design**, but Phase 1 only preserves readiness and does not implement a public Plugin SDK, `ExtensionHost`, plugin loader, manifest format, capability manager, frontend protocol, or distribution system.
- The intended third-party boundary is an ordinary-user, out-of-process, versioned capability contract. Direct third-party DLL injection into `Blink.exe` is not a supported extension model; Plugin System, Custom Frontend System, and AgentBridge remain separate concerns.
- Read-only Phase 1 audit found no third-party DLL loading, no path-as-permanent-identity contract, no Provider parallel filesystem model, and no privileged IndexService mutation backdoor. Existing `ProviderHost.exe`, `FileIdentity`, and app-private UI state boundaries remain compatible with this decision.
- **NO CODE CHANGE REQUIRED.** Public contracts, capability negotiation, SDK, installation/signing, marketplace, compatibility guarantees, and sandbox implementation remain deferred.

## 2026-08-16 Phase 1B Slice 1/2 manual regression follow-up

- Manual regression found two correctness issues while Slice 1/2 remained under manual acceptance: Blink-native This PC displayed Places but omitted accessible local volume roots from both the main content and Drives projection; one physical Ctrl+T/Ctrl+W could result in two tab operations. Phase 1A remains `ACCEPTED / COMPLETE`, Phase 1B remains `IN PROGRESS`, and Slice 1/2 manual acceptance is not marked passed here.
- This PC root cause: the Blink logical-drive enumeration path called `GetLogicalDriveStringsW` with a 32768-character buffer. On the target machine this returned the required length but left that buffer empty, so the model contained Places only. The fix uses a 256-character buffer, filters actual fixed/removable roots whose directory attributes can be read, and creates normal `Location::Folder(root)` volume items; no C:/D: hard-coding or Shell Namespace identity was added. The existing generation/lifetime gate remains authoritative, and the same committed model feeds the main This PC view and Drives sidebar.
- Ctrl+T/Ctrl+W root cause: the authoritative `KeyboardAccelerator.Invoked` handlers performed the tab operation but did not mark `KeyboardAcceleratorInvokedEventArgs.Handled`. With no second explicit T/W handler in the source, leaving the routed accelerator unhandled allowed the shortcut to continue through keyboard routing. The minimal fix marks each accelerator handled immediately after its single `CreateAndActivateTab` or `CloseTab` call; no debounce or timeout change was introduced.
- Targeted ordinary validation passed: This PC model tests cover Places plus C:/D: volume roots, `Location::Folder` targets, and stale-result protection; TabWindowState tests cover one create command adding one tab and one close command removing one tab; source-contract checks cover the single accelerator route and handled event. Blink Debug/Release builds and Blink.Phase1.Tests Debug/Release all passed. No elevated acceptance or unrelated suite was run.
- Required manual recheck remains: launch and confirm C:/D: in This PC; enter D:; press Ctrl+T twice and Ctrl+W twice one press at a time; verify per-tab selection isolation. Slice 1/2 manual acceptance remains `NOT YET COMPLETE` pending that user check.
## 2026-08-16 Phase 1B Slice 3 — Navigation Tree Productization

- This Slice started from the accepted Slice 1/2 baseline; the earlier same-day
  regression notes remain historical evidence and are superseded by the completed
  manual recheck recorded before Slice 3.
- Implemented the first Blink-native navigation tree using the app-private
  `NavigationTreeModel`. The tree has an explicit This PC root, Places, and
  volume roots sourced from the corrected `EnumerateThisPcContent()` path.
- Replaced the product sidebar's separate Places/Drives ListViews with one
  WinUI TreeView projection. Tree node identity is an app-private node ID plus
  `Location`; display names are not action identity. Tree item invocation uses
  the existing `NavigateToLocation()` route, so address bar, Back, Forward, Up,
  Refresh, This PC, and tree actions share one authoritative navigation path.
- Direct child directory loading is lazy and asynchronous. The model filters
  files and hidden/system entries, exposes load failure state, and shows
  reparse-point directories without following their targets. No watcher,
  recursive prefetch, or full-volume DFS Search fallback was added.
- Expansion state is window-owned and shared across tabs. Active-tab committed
  locations drive selection; nested ancestors are revealed through a bounded,
  generation-gated asynchronous chain. Newer navigation invalidates stale
  reveal work, and window close cancels/drops tree work.
- Ordinary NavigationTree model tests cover top-level identity, lazy expansion,
  hidden/system filtering, non-expandable reparse points, stable child IDs,
  failure/retry state, stale result rejection, and shutdown invalidation.
- Slice 3 is implemented; manual UI acceptance is pending. Phase 1 Product
  Surface remains frozen and Slice 4 has not started.

## 2026-08-16 Phase 1B Slice 3 correction: navigation pane semantics and UI bug closure

- Manual inspection found a label regression after the first TreeView presentation
  fix: `TreeViewNode.Content` contained a `TextBlock`, so the default presenter could
  stringify it as a `Microsoft.UI.Xaml.Controls...`/`m...` type name; the follow-up
  binding targeted `Text` on the wrong data context and left valid labels empty.
- The presentation path now uses a `PropertySet` data item with explicit `Label` and
  app-private `NodeId` fields, with an indexer-bound `DataTemplate`. Display text is
  never used as identity, and valid localized place, volume, and folder labels are
  retained without trimming or type-name filtering.
- The previous arrow report was traced through the actual UI chain. Expansion now
  records `Expanding` receipt, node-ID resolution, BeginExpansion acceptance,
  enumeration completion with direct-child count, and TreeView child publication.
  Completion explicitly restores the expanded state after publishing children and
  still uses the existing async generation/lifetime and reparse/file filtering rules.
  No runtime manual PASS is claimed until the user verifies a real drive.
- The pane now has two semantic presentations backed by one `ThisPcContent` result:
  a non-hierarchical localized quick/common-location ListView and a separated
  hierarchical TreeView containing only ThisPC/volume/folder nodes. Quick items have
  no expansion semantics; their activation and tree activation both route through
  `NavigateToLocation()`.
- Exact Home/Desktop/Downloads/Documents committed locations select only the matching
  quick item. Descendants clear quick selection and use bounded filesystem-tree reveal;
  ThisPC selects the tree root; ordinary filesystem locations reveal/select the tree.
  Tab switching continues to synchronize both projections from the active committed
  `TabState`.
- `NavigationTreeNodeKind::Place` was removed because quick places are no longer tree
  nodes. `ThisPcItemKind::Place` remains the authoritative This PC content fact used
  by the quick presentation. No duplicate volume enumeration or navigation path was
  introduced.
- Focused ordinary model/source-contract tests pass after the correction. Blink and
  Blink.Phase1.Tests Debug/Release builds and test runs passed; `git diff --check`
  passed. A normal-user Release smoke survived eight seconds and reached the browse
  status telemetry; it was then closed diagnostically. No UI automation was available
  to click a real arrow, so expansion remains a manual acceptance item. Elevated
  acceptance and Slice 4 remain out of scope.

## 2026-08-16 Phase 1B Slice 3 final navigation-pane runtime bug closure

- Manual follow-up confirmed that quick/common-location labels were visible, but
  drive and folder labels were still blank. The TreeView is populated through
  `RootNodes`, so its `ItemTemplate` receives a `TreeViewNode`; the actual
  presentation `PropertySet` is `TreeViewNode.Content`. The template now binds
  through `Content[Label]`. `NodeId` remains a separate app-private field and is
  never used as display text.
- The navigation pane now uses one outer `ScrollViewer` as the vertical scroll
  owner. Its `StackPanel` contains the quick section, separator, drive heading,
  and TreeView as one content surface. The TreeView is top-aligned and measured
  as content of that outer surface; no second visible tree scrollbar is created
  by the product layout, and lazy child loading remains unchanged.
- The second-expand freeze root cause was confirmed in the model state machine:
  `NavigationTreeModel::BeginExpansion` previously allowed `Loaded` nodes to
  transition to `Loading` again. A collapsed node is still loaded, so expanding
  it again could start redundant enumeration and asynchronous child replacement.
  `Loaded` now rejects `BeginExpansion`; cached child IDs and realized TreeView
  nodes remain in place until a future explicit invalidation exists.
- The completion path no longer calls `IsExpanded(true)` after a user-triggered
  asynchronous expansion. That call was unnecessary and could re-enter
  `Expanding`; programmatic reveal still expands only when it must expose a
  newly revealed ancestor. Expansion telemetry now records node/state/generation,
  worker thread, enumeration duration, materialization count, rejected loaded
  expansions, and expanded-state requests without per-child logging.
- The earlier Slice 3 note saying completion explicitly restored expanded state
  is retained as historical evidence of the prior implementation and is
  superseded by this idempotent completion behavior.
- Ordinary regression coverage now verifies loaded re-expansion is rejected,
  child IDs remain stable, failed retry remains bounded, stale tokens are dropped,
  and the source contract covers the TreeView binding and single scroll surface.
  Debug/Release builds and Blink.Phase1.Tests Debug/Release are required before
  claiming runtime acceptance; no elevated acceptance is part of this slice.
- Required manual runtime recheck remains: verify drive/folder labels, one
  continuous left-pane scroll, first expansion, repeated collapse/re-expand of a
  drive and nested folder, no duplicate children, no freeze/high CPU, and reveal
  synchronization after address-bar navigation. Phase 1B remains `IN PROGRESS`
  and Slice 4 has not started.

## 2026-08-17 Phase 1B Slice 3 final navigation-pane scroll ownership fix

- Manual checks already passed for drive/folder labels and five repeated
  expand/collapse cycles without duplication, freeze, or sustained abnormal CPU.
  The remaining manual failure was unified left-pane scrolling: the visible
  scrollbar stopped at the separator instead of covering the combined quick and
  tree content.
- The nested-scroll audit identified the bounded quick `ListView` (`Height(100)`)
  as the scrollbar visible in the screenshot. The outer `ScrollViewer` existed,
  but the quick list still owned a bounded vertical viewport; the `TreeView`
  template could also create an independent internal `ScrollViewer`.
- The final composition removes the quick-list height constraint, disables
  vertical scrolling on the quick list, and disables vertical scrolling on
  realized `ListView`/`TreeView` descendant `ScrollViewer` instances. The outer
  navigation `ScrollViewer` is now the sole vertical owner for the quick section,
  separator, heading, and lazy tree. Quick-list selection/routing and TreeView
  lazy expansion semantics are unchanged.
- This is a layout/scroll-ownership change only. It does not recursively
  materialize the filesystem tree; the tree still grows only for explicit lazy
  expansion/reveal. Runtime verification of wheel behavior and expanded-branch
  responsiveness remains a user manual acceptance item. Phase 1A remains
  `ACCEPTED / COMPLETE`; Phase 1B remains `IN PROGRESS` with Slice 3 final scroll
  fix implemented; the Phase 1 Product Surface remains frozen and Slice 4 has not
  started.

## 2026-08-17 Phase 1B Slice 3 first-expansion intent fix

- The remaining Slice 3 runtime failure was isolated to a fresh first click on a
  lazy TreeView node: the model accepted `Idle -> Loading` and the worker
  completed, but the visual TreeView node could remain collapsed. A second click
  then appeared to work only because the children were already cached.
- The minimal app-private fix records a one-shot pending expansion intent keyed
  by navigation-tree node ID and model expansion generation. It records the
  initial `Expanding` event, keeps a count for any reaffirming event, and
  applies one programmatic visual expansion only after the same generation has
  reached `Loaded` and children have been published.
- A user `Collapsed` event while that generation is `Loading` cancels only the
  pending visual-open intent. The completed children remain cached as the
  authoritative `Loaded` model state; later re-expansion continues to use the
  existing no-reenumeration path. Existing generation, lifetime, reparse-point,
  filtering, and single navigation-tree scroll ownership rules are unchanged.
- Added bounded telemetry for node ID, model state/generation, unrealized-child
  and expanded flags, intent/event count, enumeration start/end and child count,
  completion state, visual expansion request/completion, and collapse-during-
  loading cancellation. No per-child logging or new expansion manager was added.
- The ordinary source-contract regression now checks the first-expand intent,
  cancellation, completion, and one-shot visual-expansion path. Blink Debug and
  Release builds passed. `Blink.Phase1.Tests` Debug and Release passed,
  including the existing loaded re-expansion/no-duplicate/stale-token tests and
  the new first-expand contract. `git diff --check` remains required.
- No automated UI interaction is available in this environment to click a real
  first drive/folder chevron. Therefore no runtime first-expansion PASS is
  claimed yet; the targeted ordinary manual check remains pending. Phase 1A
  remains `ACCEPTED / COMPLETE`; Phase 1B remains `IN PROGRESS` with Slice 3's
  first-expansion fix implemented, Product Surface remains frozen, and Slice 4
  has not started.
