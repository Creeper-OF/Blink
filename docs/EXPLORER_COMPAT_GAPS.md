# Explorer 兼容差距与切片顺序

2026-11-13 Clipboard后续：本机结果模型、单DirectoryCopy跟踪和三视图/搜索淡化已实现，Release独立64/0/0及完整1808/0/4通过；默认9fe19d原生验证版已更新；View失前台SKIP和外部OLE待办见[交接](CLIPBOARD_TRANSFER_HANDOFF.md)。下文G06c等是先前交付记录，G08仍为PARTIAL。

2026-11-13更新：G06c同一修复版Release的NewFolder7项、Rename8项、Delete5项已分别完整PASS/0 FAIL/0 SKIP，均正常exit0；三组在不同日期验证，先前中断记录单独保留。删除验证限自有导航目录的Shift+Delete，普通回收站等范围仍待验。关闭前退役TitleBar及TabView已在原新建序列和未提交改名草稿下实测正常退出。默认运行目录已更新至同一79f915…版本，新PID39240可见；见[审计](DEVELOPMENT_AUDIT.md)与[本轮交接](EXPLORER_NAVIGATION_MUTATION_HANDOFF.md)。

G06b最新（2026-11-11）：目录Cut/Copy/Paste、已加载树分支事件/F5刷新已接入，双配置1636/0/4、249输入/386资源；13项不写Clipboard窗口检查通过，实际传输UI为SKIP（0项）。其余对象菜单/完整G06/P0待续。见[交接](EXPLORER_NAVIGATION_TRANSFER_HANDOFF.md)。

G06a最新（2026-11-11）：导航/位置常用目标菜单已接入；双配置1618/0/4、249输入/386资源，最终Release10项窗口检查通过。树文件操作/其它对象菜单仍待做，真实属性页/鼠标/剪贴板与环境待验，完整G06/P0未完。见[交接](EXPLORER_OBJECT_MENU_HANDOFF.md)。

G03c最新（2026-11-11）：目录自动完成已接入；双配置1608/0/4、249输入/383资源，同Release自动完成15+地址20项通过。以下G03a/G03b为历史快照，当前地址剩余以G03行和[交接](EXPLORER_ADDRESS_COMPLETION_HANDOFF.md)为准。

阶段A审计2026-11-08；G02实现更新2026-11-09；G03a实现更新2026-11-10（同一最终Release限定GUI53项通过）。接受用户提供的[任务书](EXPLORER_UI_MASTER_PROMPT.md)，以 `7135a14` 为阶段 A 的源码基线。该文档与[UI 规范](EXPLORER_UI_SPEC.md)、[菜单台账](EXPLORER_MENU_MATRIX.md)、[交互矩阵](EXPLORER_INTERACTION_TESTS.md)共同记录事实；下面的优先级是实施顺序，不是完成比例。


G04a区域焦点更新：两配置各1583 PASS / 0 FAIL / 4既有符号链接权限SKIP，249输入/378资源；同一Release两段覆盖23项GUI全PASS，已发布默认版。G05b新行选择历史验证保留；完整G04/P0及输入/环境矩阵仍为`PARTIAL`。

G03b最新记录（2026-11-11）：逐级下级目录菜单已实现；两配置1583/0/4，249输入/382资源，同Release菜单15+地址20项通过。以下G03a描述为历史快照，当前地址余项以G03行及[本轮交接](EXPLORER_ADDRESS_CHILDREN_HANDOFF.md)为准。

## 基线与证据边界

- 已有本地浏览/多标签/三视图/选择、异步目录监听、MFT/USN 常驻索引、多盘范围搜索、文件关联打开、复制/移动/目录合并、覆盖、回收站/永久删除、属性统计、剪贴板及拖放管线；以真实源码和最新 Slice21 为准，不能依据历史计划中的“尚未开始”否认现状。
- 当前G02 Hidden/Search修正后：Debug/Release完整构建成功，普通回归各1542 PASS、0 FAIL、4项符号链接权限1314 SKIP；240个冻结原生输入哈希匹配、375套中英/fallback资源，26项新增检查（23项命令栏策略/选择模型、3项搜索生命周期）。修正前正式尝试13个子项PASS仅为红证据，私有Search3 PASS仅属诊断；修正后新Release完整18项GUI已18 PASS/0 FAIL/0 SKIP，Hidden→Search/PasteParent同一串联通过；4个未点击排序叶仍待验。
- G03a为246输入/378资源；最终WM_CLOSE修正后两配置原生各1565/0/4，同一Release地址20/命令18/查看15合计53 PASS/0 FAIL/0 SKIP；独立关闭A/B/C/D四项全PASS/exit0，含C真后台及D无WM_CLOSE fallback的Ctrl+W唯一标签。仅升级实际覆盖子项，旧Debug52项只preflight，完整G03/P0仍`PARTIAL`；不由验证通过预先声称默认版发布成功。
- G01历史有效记录为235输入/369资源、两配置各1516/0/4、GUI15/0/0。更早Slice21为235输入/368资源、GUI8项，此前普通/搜索菜单39项仍是各自候选的历史证据；不冒称G02重跑。
- 本轮真实参考主机为 Windows 11 家庭版 **25H2 / 10.0.26200.9711 / zh-CN / 125%**，自有 Explorer 窗口呈现深色。任务书目标24H2、其他主题/倍率、多显示器和文本放大本轮 **NOT RUN**。
- Explorer 仅在新 HWND 的精确 GUID fixture 中观察“查看/显示”。主菜单11项，显示子菜单新增5项；完整Show树的16项包含父层11项，不能当作16个子命令。主菜单包括8种视图、详情/预览窗格和显示；显示包括导航窗格、紧凑视图、项目复选框、扩展名、隐藏项目。这是本机版本事实，不硬编码成所有版本的唯一列表。
- 实际主窗口截图仅用于布局参考，PrintWindow未捕获弹出菜单。菜单内容证据来自UIA；没有菜单像素对照或人工验收结论。未切换Explorer开关，测试窗口正常关闭，83字节fixture已清理。
- 原始证据归 `.build/explorer-ui-p0-view-menu/integration/explorer-reference.json` 和 `reference/explorer-owned-window.png`，不上传本机截图、窗口dump或索引数据。

## 差距台账

状态仅使用任务书约定的六类。已有后端但缺少前台入口标 PARTIAL；系统注册、设备和账户决定的能力标 OS_DEPENDENT；后续规划标 DEFERRED。具体每条菜单及测试编号在另外两份台账中维护。

| ID | 优先级 | 现状/差距 | 状态 | 真实入口与复用边界 | 取舍/风险与验收 |
| --- | --- | --- | --- | --- | --- |
| G01 | P0，历史限定切片已交付 | 顶部已改真实查看/显示菜单：Grid/List/Details及Hidden | IMPLEMENTED_VERIFIED | main `BuildFolderViewCommands/SetFolderViewMode/SetShowHiddenItems`，`FolderCommandPolicy`、`ContextMenuAppearance` | 只收拢三个已实现视图与隐藏项；复用每标签视图、窗口级visibility、既有命令；用实际布局、勾选、双标签和stale peer验证，不塞预览等占位 |
| G02 | P0，限定验证完成/4排序叶待验 | 标准CommandBar已集成8个主按钮、New/Sort/View菜单、原生More的7个次级动作及动态溢出；旧排序Combo/方向按钮已移除 | IMPLEMENTED_NEEDS_MANUAL | `CommandBarPresentation`仅展示；`ExplorerCommandBarModel`从现有Tab/Search推导能力；main复用 `ExecuteFolderCommand`、原搜索动作和唯一队列 | Hidden/Search修正后Debug/Release各1542/0/4，240冻结输入哈希匹配/375资源；新Release完整GUI18/0/0；实际覆盖24条顶部入口已验，Sort Name/Modified/Type/Ascending4条未点仍待验。命令行跨窗格，bar本体Left内容宽；640宽原生溢出已验，其他环境及整P0不外推 |
| G03 | P0，功能已接入/组合待验 | 编辑/面包屑、历史、逐级目录、前缀候选与键盘已接入 | IMPLEMENTED_NEEDS_MANUAL | AddressBarModel/Presentation、StartAddressSuggestions，复用Location/枚举/隐藏项与原导航 | 双配置1608/0/4、383资源，最终同Release补全15+地址20项；实物鼠标/真实IME/UNC及完整环境仍待验，不标完整G03/P0完成 |
| G04 | P0，G04a所列子项已验 | F3共享搜索入口；F6/Shift+F6显式六区域切换，标准TabView/ListView快捷键保留 | PARTIAL（所列子项IMPLEMENTED_VERIFIED） | main `CanUseFocusShortcut/CurrentFocusRegion/CycleWindowFocus`；原生文本组合事件 | 双配置1583/0/4、最终23项窗口全PASS，零结果页原生“更多”入口已补并复测；真实IME/读屏及全部组合仍待验 |
| G05 | P0，G05a/G05b所列场景已验 | 文件主名初选，目录及新建等全选；保留ContentDialog，改名后选中并滚动到精确新行 | PARTIAL（实测子项IMPLEMENTED_VERIFIED） | `ShowFileNameDialog`、`TabState::CommitBrowse`、`RestoreRenderedSelection`；搜索仍先定位父目录 | 双配置1583/0/4、最终17项窗口检查，覆盖同身份其他链接不误选、仅改大小写、分批列表滚动和编辑中正常关闭。真实IME与完整区域焦点组合仍待续，Ctrl+A可编辑全名 |
| G06 | P0，G06a/b已交付；G06c所列新建/改名/ShiftDelete子项已验 | 常用目标/剪贴板及G06c新建改名删除已有接线，更多系统/其它对象菜单待续 | PARTIAL（G06c实测子项IMPLEMENTED_VERIFIED） | BeginTargetDirectoryOperation、FileDropOrigin、原唯一queue/对话框及串行已加载树刷新 | 同一79f915…Release独立NewFolder7/Rename8/Delete5均PASS、exit0；Delete限自有导航目录Shift+Delete取消/明确确认及树刷新。普通回收站、Quick真实用户目录、盘根写入、外部Clipboard及完整矩阵待验；默认版已更新79f915…，新PID39240可见 |
| G07 | P0，后续 | Shell菜单单/同父多选与2/3消息转发已做；host无wall-clock deadline，现代IExplorerCommand未对接 | PARTIAL | `ShellContextMenuController::Dispatch`、`Blink.ShellMenuHost` | 进程隔离不等于超时已覆盖；先验证实际挂起/取消边界再做必要处理；动态扩展按系统返回，不硬编码软件名，不进Blink主进程 |
| G08 | P0，继续 | 标准COPY/MOVE有Preferred；本机结果/剪切淡化已实现，外部performed完成回传待接通 | PARTIAL | ClipboardTransferPresentation、原cache/唯一coordinator；三目录模板和搜索PropertySet | Release原生64项/完整1808/0/4，包含单DirectoryCopy；真实Clipboard与Opacity未验，默认9fe19d已更新，View失前台SKIP边界见[交接](CLIPBOARD_TRANSFER_HANDOFF.md)，不以发布或进度冒充真实已移动 |
| G09 | P0，后续 | 同/跨卷拖放、Ctrl/Shift与OLE完成已有；link、右键拖放选择菜单、虚拟文件未接 | PARTIAL | `FileDragDropController/Model/Data`与 `FileDragDropTests` | 现有操作如实验证；未支持的effect返回NONE。FileDescriptor/FileContents不是CF_HDROP的既有能力 |
| G10 | P0，后续验证 | 文件操作真实partial/取消/冲突已有，组合状态和更直观进度仍需验收 | IMPLEMENTED_NEEDS_MANUAL | `FileOperationCoordinator/MoveTransferPresentation`及既有对话框 | 继续唯一任务队列，不增加假暂停/剩余时间；失败重试不能重放成功前缀，回收站失败不降级永久删除 |
| G11 | P1 | 搜索与G06a侧栏/ThisPC目录/盘根已接系统属性；普通目录/多选仍未统一 | PARTIAL | PropertiesDialog/ItemProperties、ShellHost properties及单目录捕获 | 本轮目录/盘根捕获10项原生检查、菜单适用性通过；新目标实际属性页未验，普通目录只读详情仍作补充 |
| G12 | P1 | 三视图已有，其余图标尺寸/平铺/内容、分组和列宽/重排/选择列缺失 | PARTIAL | `FolderViewPresentation/Formatting`、`SortDescriptor`、详情列头 | 分组不是排序，不显示尚未实现视图；排序状态继续每标签持有，逐个功能单独验证 |
| G13 | P1 | 单滚动/lazy树、常用导航菜单和目录操作/F5刷新已接；图片音乐视频、固定管理/主页及其它菜单待续 | PARTIAL | NavigationTree BeginExpansion(refresh)、增量TreeViewNode复用、串行已加载branch更新 | G06b原生刷新8项和窗口F5增删/容器/展开保持通过；不重复全盘枚举，KnownFolder/固定模型保持实际范围 |
| G14 | P1 | 详情窗格/预览/缩略图未产品化，属性库/Provider原型不能代替前台功能 | DEFERRED | `Blink.Properties`、`PropertiesDialog`；未来独立metadata/preview边界 | 不提前开放无处理能力菜单，第三方预览不在主UI无限等待；格式与故障处理按实际后端落实 |
| G15 | P1 | 标题/Mica/浅深色与基础UIA已做，激活、高对比、全部缩放/多屏/文本放大未完成矩阵 | IMPLEMENTED_NEEDS_MANUAL | main `BuildWindowFrame/SyncWindowFrameTheme`、App.xaml、现有框架检查 | 保留系统caption按钮/Snap/命中测试，不用自绘替换；本轮125%观察不能代替所有倍率和读屏验收 |
| G16 | P1 | 每标签有路径/历史/排序/视图/选择/search；G03a地址草稿在切Tab时取消，同Tab自动刷新保留；滚动offset与完整会话持久化未完成 | PARTIAL | `TabRecord/TabUiState/TabWindowState`、`AddressBarState`、`PresentActiveTab` | 地址草稿不跨Tab或重启持久化；不把UI控件变成唯一真相源；用户切标签/关闭不能复活异步结果；会话落盘另立切片 |
| G17 | P1 | 顶部New已接真实新建目录，原生More已接7个真实动作；ShellNew模板/快捷方式、系统动态顶部动作及Blink设置页仍缺 | PARTIAL | `NewFolderOperation/FolderNameDialog/FolderCommandPolicy`、G02展示与命令绑定 | 系统Office等注册项动态获取；无实际模板不伪造Word/ZIP；设置仅承诺Blink自身可兑现部分 |
| G18 | P1 | 本地索引搜索可用；同卷Busy/保存时等待与UI首屏仍待专项 | PARTIAL | `SearchState/Coordinator/Presentation`、`Blink.Index*` | UI任务不改索引模型/IPC/BLK5，不用全盘DFS掩盖服务离线；性能另给量化证据 |
| G19 | P2 | 回收站浏览/恢复/清空、操作Undo/Redo未产品化 | DEFERRED | 当前仅 `DeleteOperation` 回收站提交，非回收站浏览器 | 不绑定假Ctrl+Z/Y，输入框自身编辑撤销仍由文本控件处理；不可把清空放普通目录菜单 |
| G20 | P2 | 特殊Shell Namespace、MTP、图库、库、网络发现、云盘状态未原生管理 | DEFERRED | `Location`当前ThisPC/本地folder，Provider为原型 | 逐项区分系统跳转/有限操作/暂缓；路径不存在时不能造“文件夹”，不承诺替代完整Shell Namespace |
| G21 | P2 | SFTP/WebDAV/自建网盘仍属既有后续Provider路线 | DEFERRED | `Blink.Provider.Contracts/Client/Host/Fake` | 保留原架构，协议权限/断线/上传状态按远端事实；系统已挂载盘不等于Blink已有SFTP连接管理 |
| G22 | P2，非默认路线 | 系统默认管理器/Win+E/注册表Shell接管不在此次UI任务 | UNSUPPORTED_BY_DESIGN | 无对应安装或关联改动 | 需另立明确任务才考虑，不以外观兼容为由更改系统配置 |
| G23 | 环境条件 | UNC/网络映射、可移动/光驱、云占位、注册动词及第三方菜单 | OS_DEPENDENT | 普通本地API及Shell返回能力；相应网络/设备实测未做 | 不把synthetic路径测试写成真实SMB/设备兼容，不静默提权或强行使用本地专属操作 |

## 执行顺序与本轮边界

A. 三代理只读审计UI、菜单、交互，主模型复核源模型/历史、采集自有Explorer参考，先提交四份规范与原始任务书。此阶段不修改业务代码。

B. 历史首片**只实施 G01**：顶部查看MenuFlyout含现有三视图，显示子菜单含隐藏项目；文案/图标/勾选/Tooltip/UIA共用WinUI和已有资源。开菜单时从TabState/窗口visibility读取，调用原命令。旧peer不能修改新标签或新目录，搜索面板仍按既有边界隐藏目录视图工具条。

C. G01当时Debug/Release与真实UI检查已通过（各1516/0/4、GUI15/0/0），仅按这些动作范围保留验证。

D. G02已将真实New/Cut/Copy/Paste/Rename/Delete/Sort/View与原生More接入标准CommandBar，保留原状态/文件操作/搜索/索引边界。该候选双配置原生1542/0/4与修正后完整18项GUI18/0/0已完成；仅升级实际覆盖入口，4个未点击排序叶和完整环境矩阵仍保留，不将原生总数替代GUI。

E. G03a最终WM_CLOSE修正后，两配置原生各1565/0/4；同一Release地址20/命令18/查看15合计53项通过，独立关闭A/B/C/D四项exit0，限实际覆盖`IMPLEMENTED_VERIFIED`；旧Debug52项仅preflight。下级目录菜单、自动完成建议、F3/F6焦点循环和其他P0/P1/P2继续独立推进；G05a主名选区本轮已按完整15项Release独立验证，完整G03/P0仍`PARTIAL`。

### G01 交付前必须取得的证据

- 三个菜单项的唯一勾选与实际Details/List/Grid控件/列头一致；普通右键切换后再开顶部菜单，同一权威view状态。
- 显示子菜单真实展开；隐藏项的当前/其他标签、目录树策略一致，系统属性项保持原策略，外部搜索visibility入口能同步。
- 切标签、导航、关闭之后旧菜单不作用于新位置；菜单Esc关闭和焦点返回在自有窗口中验证；排序仍使用既有入口，不误改其默认值。
- 本轮限定环境的真实窗口自动化和Debug/Release回归通过，资源键一致；24H2/其余DPI/主题/IMEs/网络等按交互矩阵仍NOT RUN，不升级整项目或整个P0验收。

风险限于WinUI菜单生成、既有命令绑定和展示状态；索引服务、协议、存储、文件操作引擎与ShellHost均不在此切片修改范围。失败修原因，不通过关闭测试或添加无动作按钮掩盖问题。

## G06c导航目录操作限定实测 — 2026-11-12–13

本轮已发布Release SHA256为`79f9157d09b8fed21a135e94071e7886755a273832438f80f30a815f3c2fe522`。NewFolder独立报告7 PASS/0 FAIL，覆盖目录/盘根/虚拟目标适用性、Esc取消与导航回焦、Ctrl+Shift+N精确目标创建、重名失败后Retry、旧菜单退役及正常关闭/清理；Rename独立报告8 PASS/0 FAIL，覆盖F2取消、真实目录和子项身份/字节保持、另一个标签跟随、重名Retry、文件区F2作用域及未提交草稿关窗。PID6064/44760均exit0、无强杀，各清理363字节自有夹具；两组分别计数，不套用旧候选或原All14项结果。

Delete最终独立报告为5 PASS/0 FAIL/0 SKIP，2026-11-13 01:06:42–01:07:39 UTC，与NewFolder/Rename为同一79f915…二进制。#01/#08/#09/#13/#14分别覆盖适用性、默认Cancel/Enter保留非空目录、明确确认后只删除身份/字节核对的自有非空目录并自动刷新树、正常关闭和归属清理；PID23720 exit0、无强杀，剩余242字节夹具已精确清理。先前2 PASS/1前台中断FAIL仅保留为历史，不混入最终5项。普通回收站、Quick真实用户目录/盘根实际写入、外部剪贴板、物理鼠标、IME/UNC及全主题/DPI/多屏/读屏仍待验。

此前两次关闭AV的匹配dump/PDB栈指向App析构释放TitleBar的FrameworkElement列表，连带TabView/自动化peer/ToolTip在宿主关闭后析构。修复在原生宿主仍活时先失效回调/选择同步，停用TitleBar拖动区自动刷新，清空TabItems、解除Content及SetTitleBar绑定，并释放TabView/TitleBar；原NewFolder序列和Rename未提交草稿均已正常退出。正式索引服务PID29080及真实服务23项搜索检查为独立证据，见[搜索验证](SEARCH_RESPONSE_VALIDATION.md)；默认主程序已更新至同一版本，完整G06/P0保持PARTIAL。

2026-11-13实际发布：旧默认PID15036在完整单标签“此电脑”快照后正常exit0；`published-runtime.json`为PASS/COMPLETE，复制5个变化文件、108077386字节，默认运行目录的全部295文件/219148935字节逐项SHA与候选一致。随后`default-runtime-verification.json`只读PASS：新PID39240、HWND13241572可见，单标签/选中序号0/地址“此电脑”与原快照及79f915…哈希一致。首次opener在选择前失去前台、记录OPENED_RESTORE_INCOMPLETE，未记为完整成功；后续只读核验现有窗口，无再次输入或启动新窗口。发布后C/D只读就绪探针均exit0，正式服务PID29080运行且未重启。

## G01历史实测更新

最终235输入/369资源、Debug/Release各1516 PASS/0 FAIL/4权限SKIP；15项自有窗口检查通过，涵盖新菜单、实际三布局、selection/order/sort保持、Hidden/SYSTEM、双现有及新Tab、旧peer、纯键盘菜单和焦点、空目录/ThisPC、地址Ctrl+A/Delete作用域，以及正常关闭/归属清理。原始报告ui-view-menu-report.json记录Blink窗口DPI96、主题未捕获；Explorer参考窗口DPI120，两者不能混同。树展开与搜索Hidden联动、本轮未覆盖的其他焦点/IME/主题/DPI/24H2仍NOT RUN。G01功能已验证不代表全部P0或全部视觉/人工矩阵完成。


## G02当前实现与待确认项

代码入口为`main.cpp::BuildFolderViewCommands/CommandBarState/CaptureCommandBarContext/SyncCommandBarState/ExecuteCommandBarAction/PopulateCommandBarMenu`。标准展示helper不执行I/O；菜单打开读取权威状态并绑定上下文，切标签、换目录、搜索结果/选择代次更新时关闭旧菜单，动作重验。Search的粘贴明确为“粘贴到所在目录”；ThisPC不继承背后目录目标；剪贴板变化同步启用态。反选由`TabState::InvertRowSelection`完成，保留硬链接名称独立性。

当前原生记录归`.build/explorer-command-bar/integration/tests-debug.log`及`tests-release.log`：修正后两配置各1542 PASS/0 FAIL/4权限SKIP，240冻结输入哈希匹配/375资源。新增共26项检查（23项命令栏派生能力/选择模型、3项搜索生命周期），不代替GUI操作。启动probe确认真实ApplicationBar、AppBarButton和系统More；独立Release探针`ui-overflow-probe.json`正常exit0，实际overflow UIA为OverflowPopup（Popup / ControlType.Window），ControlView链为CommandSelectAll→OverflowPopup→ExplorerCommandBar，RawView另含无ID ScrollViewer。TopMoreMenu仅为源码Style给Presenter设置的ID，本机未暴露，不能继续把它当实际UIA入口。

`TabWindowState::SetVisibilityPolicy`原先先执行`LeaveSearch()`取消当前查询，又额外调用`SearchCoordinator::Shutdown()`，将所有已有标签的搜索状态永久置为Closed；随后`SearchState::Begin()`恒为0。独立只测搜索会成功，完整脚本执行#08 Hidden开关后，#14搜索连续失败两次，才暴露这一真实组合问题。修复仅删除visibility切换中的多余Shutdown，保留LeaveSearch取消/离开语义、CloseTab和全局Shutdown的永久关闭职责。新增3个模型回归覆盖现有前后台标签的旧代次取消、显示/隐藏后均可重启，以及关标签只永久关闭被关标签且不影响存活标签。

修正前正式GUI曾出现13个子项PASS，但在#08 Hidden之后#14连续失败两次；这份红尝试只用于定位问题，不能当最终验证。私有只搜索探针3 PASS同样仅属诊断，不能替代完整场景。修正后Release同一完整18项GUI已18 PASS/0 FAIL/0 SKIP，报告归`ui-command-bar-report.json`；#08 Hidden→#14真实索引/PasteParent及字节核对串联通过。早期第09的harness旧TopMoreMenu定位问题已通过真实OverflowPopup校正，与本次搜索生命周期缺陷分开记录。 最终报告已覆盖640宽原生overflow、More实际动作/键盘焦点及旧peer；24条实际顶部入口已按范围验证，4个未点击Sort Name/Modified/Type/Ascending仍IMPLEMENTED_NEEDS_MANUAL，因此G02整组仍保留待验边界，不将全部28条统一升级。

本切片不提供Share、ShellNew、快捷方式、分组、预览/详情窗格或未实现视图；完整24H2/主题/DPI/多显示器/IME/Narrator矩阵仍按原台账保留。G01历史View/Hidden证据不因本轮新增命令而扩大，也不自动证明AppBarButton新host。详见[命令栏交接](EXPLORER_COMMAND_BAR_HANDOFF.md)和[交互矩阵](EXPLORER_INTERACTION_TESTS.md)。

最终环境：25H2/build26200.9711、zh-CN、非管理员，window_dpi=96，theme="not captured"；这不是24H2或完整主题/DPI验收。自有进程exit0、无强杀；fixture已按精确路径/字节清理3152字节，compiler-temp已清、释放字节未记录，clipboard_restored=true。默认发布状态见[命令栏交接](EXPLORER_COMMAND_BAR_HANDOFF.md)，本页不虚构运行PID或把GUI通过当作默认发布完成。

## G03a当前实现与待确认项

`AddressBarPresentation`只负责标准WinUI显示、编辑和回调；`AddressBarState`记录当前owner的临时草稿，导航位置和历史仍取`Location/NavigationModel`。默认BreadcrumbBar，空白点击、Ctrl+L或Alt+D进入编辑；Enter同步读取实际Text并复用原导航，Esc恢复真正committed路径并回到当前普通/搜索主列表。IME composition期间Enter/Esc留给原生TextBox；普通LostFocus不强制夺回焦点。

祖先目标从Location既有Up语义生成，长地址使用BreadcrumbBar原生overflow；没有子目录枚举、下级目录菜单或自动完成。F4/历史按钮只呈现当前Tab的已commit current/最近back/forward，去重并限制数量；pending、失败和未提交文本不能成为条目。菜单和breadcrumb按Location值及owner/revision绑定，切Tab/显式关闭退役旧peer；标准history Closed不会提前失效当前Click。

同Tab自动刷新保留编辑草稿和焦点；切Tab取消未提交草稿，返回时展示该Tab的committed路径，不承诺跨Tab草稿保存或会话落盘。ThisPC保持虚拟Location，不映射到C盘；纯UNC路径模型测试不代表实际SMB、权限或设备已验。

当前仅本机最终Release实际覆盖子项为`IMPLEMENTED_VERIFIED`：两配置构建和原生各1565/0/4（23项新增地址模型），246输入/378资源；同一SHA256 `CF1D8A2BAE1A8B67ED2C99E53DFF373369EE8362060D69B8DAD3481A8E5FCA9D`的[地址20项（历史摘要）](EXPLORER_ADDRESS_BAR_HANDOFF.md)<!-- 原始报告路径: ../.build/explorer-address-bar/integration/ui-address-bar-report.json；旧目录已清理 -->、[命令18项（历史摘要）](EXPLORER_ADDRESS_BAR_HANDOFF.md)<!-- 原始报告路径: ../.build/explorer-address-bar/integration/ui-command-bar-compat-report.json；旧目录已清理 -->、[查看15项（历史摘要）](EXPLORER_ADDRESS_BAR_HANDOFF.md)<!-- 原始报告路径: ../.build/explorer-address-bar/integration/ui-view-compat-report.json；旧目录已清理 -->合计53 PASS/0 FAIL/0 SKIP。旧Debug19+18+15=52只preflight，不混入最终结果；独立关闭A/B/C/D四项全PASS/exit0，另列不加进53。

本机zh-CN、25H2/build26200.9711、非管理员、window_dpi=96；系统AppsUseLightTheme=0，Blink实际主题未采集。620宽overflow真实入口为ID空、Name“更多”、`BreadcrumbBarItem / Button`，已打开原生popup并导航折叠祖先；最初harness误用已收进More的CommandSort作Ready门槛造成假阴性，改验精确路径/两条行/字节/新breadcrumb/菜单关闭，再恢复宽度查Ready。旧Debug19项preflight和最终Release20项均覆盖此动作；早期误报不作为产品导航缺陷或最终结果。

关闭修复保留真实边界：初始/仅Closed退役补丁仍AV；两份真实dump经StackWalk64得到64帧匹配，顶栈WinUIEdit/Xaml、未见自有关闭直接栈，不臆定内部源码根因。最终WM_CLOSE转交WinUI前先焦点交可见列表，再用helper缓存committed取消编辑/隐藏editor、不重建crumb；程序Close前同样准备，Closed只退役shutdown。最终[四场景报告（历史摘要）](EXPLORER_ADDRESS_BAR_HANDOFF.md)<!-- 原始报告路径: ../.build/explorer-address-bar/integration/window-close-probe/fixed/probe-report.json；旧目录已清理 -->A/B/C/D全PASS/exit0：C先准备自有SUT前台，再只激活自有PS WinForms伴随窗口，以前台PID/HWND和全局UIA focus/runtimeId严格确认后台Ctrl+L前后都真后台，background_branch=COVERED；D实际Ctrl+W关唯一Tab，无WM_CLOSE fallback。伴随窗口正常关闭/dispose；最终地址#18/#19另覆盖foreground草稿/焦点直接关闭。

最终Release正常exit0、无强杀；366字节精确fixture已删除，compiler-temp已清、释放字节未记录，无系统剪贴板使用。下级目录菜单、自动完成、空白点击/完整错误分支、真实IME、Narrator、全部主题/DPI/语言/24H2/UNC矩阵、旧breadcrumb peer专项及F3/F6仍保留待做或待验；完整G03/P0仍`PARTIAL`。


## G05a当前实现与待确认项

主名策略已实现，不再把F2描述为固定SelectAll：普通文件只选最后非首位/非尾位点之前的范围，`.config.json`选`.config`，`archive.tar.gz`选`archive.tar`；无后缀、单前导点`.gitignore`选全。WholeName用于目录（含点目录）以及NewFolder/Copy/Move；实际对象类型由已提交FileEntry提供，不按扩展名猜类型。UTF-16/emoji范围通过纯模型；空串/尾点的模型范围不改变真实名称校验。Ctrl+A仍能全名编辑，不加扩展名确认或新操作管线。

搜索F2不是原地搜索编辑，仍先验证命中/父目录身份、导航父目录并定位，再进入同一个Rename对话框，取消回当前FolderEntries。取消后的能力与焦点改由共享Dismissed完成通知处理：重算New/Sort等真实命令能力，并在没有其它操作对话框时回到当前可见列表；宿主关闭提前Hide名称对话框再移焦点，晚提交由现有closed门控丢弃，实际编辑态关闭已在最终#13/#14中正常exit0。

2026-11-11实机问题修正补记：名称对话框取消后，顶部New/Sort曾保留模态期间的禁用状态；共享`Dismissed`通知在`ShowAsync`完成、`closed=true`之后统一调用`SyncCommandBarState`，从真实当前能力重算，不按入口硬设启用。另发现从工具栏Rename取消后焦点落到侧栏；同一完成回调在没有其它操作对话框时，把焦点交给当前实际可见的`SearchResults`或`FolderEntries`，不再只依赖ContentDialog返回旧焦点。搜索F2已导航父目录，故其取消后当前列表仍是`FolderEntries`。上述修正已在完整15项Release报告的实际取消/能力/焦点场景中通过；该结果不代表完整焦点环或其它环境均已验收。此G05a回焦结果不包含新名项继续选中；该行为由G05b另行验证。

两配置原生各1578 PASS/0 FAIL/4既有权限SKIP，249输入/378资源，13项新增纯选择模型；[`ui-rename-selection-report.json`（历史摘要）](EXPLORER_RENAME_SELECTION_HANDOFF.md)<!-- 原始报告路径: ../.build/explorer-rename-selection/integration/ui-rename-selection-report.json；旧目录已清理 -->已取得15 PASS / 0 FAIL / 0 SKIP（13功能+正常退出/归属清理），SHA256 `ef4986100848f3656dcaa0f2d8a645a96e9a40b09f44c5e86c51f16abb67434c`，PID29940 exit0、无force，425字节夹具清理完成。G05a的记录不含成功后新行选择；G05b将已提交的新RowIdentity用于同目录操作刷新。F3/F6、真实IME/候选、完整主题/DPI/24H2等仍NOT RUN，G05/P0保持PARTIAL。

发布核验已完成：原agent默认窗口PID10988正常exit0，候选发布的四项关键产物SHA与测试版本一致；新默认窗口PID7108可见并位于前台。主模型最终复核249项原生源码/11项保留产物、两配置1578/0/4、378资源及同一Release15项GUI均通过。此发布记录与测试PID29940分别记录，不把新开窗口当作再跑完整GUI或全部环境验收。
