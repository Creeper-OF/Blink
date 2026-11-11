# Explorer 交互能力与验证矩阵

阶段A审计2026-11-08；G02实现更新2026-11-09；G03a实现更新2026-11-10（同一最终Release限定GUI53项通过）。阶段A基线`7135a14`为235输入/368资源，只读审计提交`e497600`。G01顶部View/Show为235输入/369资源；G02标准CommandBar为240输入/375资源；G03a为246输入/378资源。依据完整316行任务书和实际源码/报告持续更新，保留完整待验范围；各候选结果不互相套用。

**最终WM_CLOSE修正后，两配置原生各1565 PASS / 0 FAIL / 4既有权限SKIP（23项新增地址模型），246输入/378资源；同一最终Release地址20/命令18/查看15，合计53 PASS / 0 FAIL / 0 SKIP。独立关闭A/B/C/D四项均PASS、exit0，C真后台、D实际Ctrl+W关闭唯一Tab且无WM_CLOSE fallback；另列不加入53。旧Debug52项仅preflight，完整G03/P0仍`PARTIAL`。** 本文更新代理只读源码、报告与主模型结果，没有另行构建/GUI；不从验证通过推断默认发布成功。H/B/C为各自历史，D为本轮G03a，完整环境/人工矩阵仍按未测保留。


G05a重命名主名选区更新：两配置原生各1578 PASS / 0 FAIL / 4既有符号链接权限SKIP（13项新增选择模型），249输入/378资源。同一最终Release名称对话框GUI已15 PASS / 0 FAIL / 0 SKIP，仅升级报告实际子项；完整G05/P0与未覆盖焦点/输入环境矩阵仍为`PARTIAL`。

## 1. 状态、环境与证据

实现状态沿用任务书：`IMPLEMENTED_VERIFIED`、`IMPLEMENTED_NEEDS_MANUAL`、`PARTIAL`、`DEFERRED`、`UNSUPPORTED_BY_DESIGN`、`OS_DEPENDENT`。它们描述指定能力，不代表整个场景通过。执行状态另列 `PASS`、`HISTORICAL PASS`、`OBSERVED`、`NOT RUN`、`SKIP`；没有执行结果时不得填 PASS。

| 环境项 | 核实结果与限制 |
| --- | --- |
| 任务书目标 | Windows 11 24H2 x64；本轮没有 24H2 实机结果，`NOT RUN` |
| 本轮参考主机 | Windows 11 家庭版 **25H2，10.0.26200.9711**，zh-CN；所观察 Explorer 窗口 DPI 120（125%） |
| G01历史 Blink GUI | G01最终 Release 报告：25H2 / build 26200 / UBR 9711，zh-CN，x64、非管理员；**window_dpi=96**，主题未采集。Explorer 参考的 DPI 120 不能带入 Blink 结果，不推断差异原因，不声称跨 DPI 对照完成 |
| G02当前Blink GUI | 修正后Release最终报告：25H2/build26200.9711、zh-CN、非管理员；**window_dpi=96**、**theme="not captured"**。18 PASS/0 FAIL/0 SKIP；不把该窗口采样当作100%完整矩阵或25H2/24H2视觉对照 |
| G03a本轮Blink GUI | 同一最终Release地址20/命令18/查看15合计53/0/0；25H2/build26200.9711、zh-CN、x64、非管理员、window_dpi=96，系统AppsUseLightTheme=0、Blink实际主题not captured。关闭A/B/C/D另全PASS，含真后台和Ctrl+W唯一标签；不外推完整环境矩阵 |
| 参考观察 | 主模型新建并核对自有 GUID 目录/新 HWND，UIA 读取 View/Show，未改变系统选项；正常关闭窗口，清理 83 字节 fixture |
| 参考证据 | [explorer-reference.json](../.build/explorer-ui-p0-view-menu/integration/explorer-reference.json)，`OBSERVED_VIEW_SHOW_ONLY`；不是 12 类上下文、其他主题或其他 DPI 的验收 |
| 参考图像边界 | PrintWindow 只捕获主窗口，未包含弹出菜单；不作为菜单视觉、像素一致或可读性凭据。View 的 UIA 记录 11 项；Show 记录含父层，新增子项实际为 5 项，不能把全部 16 项当成 Show 子项 |
| 构建配置 | README：x64、MSVC v143、SDK `10.0.26100.0`、Windows App SDK `2.3.1`、C++/WinRT `2.0.250303.1`；工具位于 D:\DEV。G02主模型已串行完成Debug/Release完整构建与普通回归，见C-NATIVE；B-NATIVE为G01历史 |
| 权限与数据 | 历史普通回归非管理员，文件变更限自建 fixture；真实跨卷测试使用独立卷。服务权限不等于 Blink UI 提权 |
| 交互桌面 | 历史 smoke 检查同会话交互桌面和窗口 PID/HWND；锁屏时跳过需要真实输入的步骤。LockApp 进程存在本身不能证明仍锁屏 |
| 第三方扩展 | 历史记录实际安装 Bandizip 7.46，已验证代表性真实 ZIP；7-Zip、Git、VS Code 和其他扩展不因规范列举即视为已安装/验收 |
| 自动化限制 | 本机 .NET Win32 UIA proxy 注册曾失败；WinUI UIA 可用，经典菜单/属性采用 Win32 HMENU/页签证据。没有把代理失败归因于产品无控件 |

证据索引（H-*为早期历史；B-*为G01；C-*为G02；D-*为G03a最终结果，D-PREFLIGHT仅旧Debug镜像）：

| 证据号 | 产物与结果 | 能证明的范围 |
| --- | --- | --- |
| H-NATIVE | [Slice21 native-validation](../.build/phase1d-slice21/integration/native-validation.json)、[handoff](PHASE1D_SLICE21_HANDOFF.md)：2026-11-08 Debug/Release 各 **1516 PASS / 0 FAIL / 4 SKIP**，构建单并发 | 该摘要对应的源码与二进制；包括真实 FS、纯模型和注入 executor/RPC 测试，不能全算 GUI 检查 |
| H-SKIP | [Debug 日志](../.build/phase1d-slice21/integration/tests-debug.log)、[Release 日志](../.build/phase1d-slice21/integration/tests-release.log) | 四项真实符号链接 fixture 因 `1314` 未创建：`file_copy_destination_existing_symlink_fixture_unavailable`、`file_copy_destination_dangling_symlink_fixture_unavailable`、`directory_copy_source_symlink_fixture_unavailable`、`typed_capture_source_symlink_fixture_unavailable`；不得改写为通过 |
| H-MERGE-UI | [ui-move-merge-report](../.build/phase1d-slice21/integration/ui-move-merge-report.json)：8 PASS / 0 FAIL / 0 SKIP | 自有窗口 Cut/Paste、Merge、子项 Replace/Skip、留源/移空及字节；正常关闭与清理。组合 GUI 的标签跟随没有覆盖 |
| H-FOLDER-UI | [ui-folder-menu-report](../.build/folder-menu-sync/integration/ui-folder-menu-report.json)：21 PASS / 0 FAIL / 0 SKIP | 普通目录的选择、路径复制、Copy/Paste、改名/新建、刷新、View/Sort 子菜单、Blink 属性、原生扩展及实际 ZIP、旧菜单失效 |
| H-SEARCH-UI | [ui-themed-menu-report](../.build/folder-menu-sync/integration/ui-themed-menu-report.json)：18 PASS / 0 FAIL / 0 SKIP | 搜索主菜单、原生扩展 ZIP、路径/文件复制、定位、属性、Ctrl+Enter/F2/Shift+F10/Shift+Delete、父目录粘贴与旧 peer 失效 |
| H-SHELL | [native-menu-smoke-report](../.build/search-context-menu/integration/native-menu-smoke-report.json)：23 PASS / 0 FAIL / 1 UIA proxy SKIP | Unicode 文件/目录经典菜单、取消、系统属性页保活/关闭、Bandizip ZIP 名称和内容；原生回复“选择成功”与 ZIP 真实完成分别核对 |
| B-NATIVE | [G01 native-validation](../.build/explorer-ui-p0-view-menu/integration/native-validation.json)：最终 Debug/Release 构建 exit0，普通回归各 **1516 PASS / 0 FAIL / 4 SKIP**；235 输入、369 资源，构建并发 1 | 本轮候选输入与输出摘要；四项仍为符号链接权限 1314 SKIP，没有删测试/放宽断言。原有 native helper、索引协议/库未变；不能由总数推导所有 GUI/设备场景已验 |
| B-VIEW | [G01最终 ui-view-menu-report](../.build/explorer-ui-p0-view-menu/integration/ui-view-menu-report.json)：**15 PASS / 0 FAIL / 0 SKIP**，2026-11-08 14:42:32–15:02:11 UTC | 13 项功能检查 + 正常关闭 + 逐路径/字节清理；不是 15 个全面兼容类别。候选 SHA256 `940dad8bea687bdcc5fb6cac71a2d19665845f048d90c3bceec98b4a35b342b2`，自有 PID 39376 正常 exit0、无强杀；清理 1015 字节 fixture、无归属失败，独占 compiler-temp 已清理 |
| C-NATIVE | `.build/explorer-command-bar/integration/tests-debug.log`、`tests-release.log`及构建状态；Hidden/Search修正后Debug/Release完整构建成功，普通回归各 **1542 PASS / 0 FAIL / 4权限1314 SKIP**；240冻结输入哈希匹配/375套中英/fallback资源，单并发 | 新增26项检查（23项命令栏能力/反选模型、3项搜索生命周期）；包含三surface、模态/忙碌/剪贴板和权威选择边界，不等于实际按钮、菜单键盘、文件动作或overflow通过；4项符号链接SKIP仍保留 |
| C-STRUCT | 启动probe读取到真实类ApplicationBar、8个AppBarButton和系统MoreButton；独立Release [ui-overflow-probe.json](../.build/explorer-command-bar/integration/ui-overflow-probe.json)正常exit0，真实overflow为`OverflowPopup / Popup / ControlType.Window`，ControlView链`CommandSelectAll → OverflowPopup → ExplorerCommandBar`，RawView另含无ID ScrollViewer | TopMoreMenu仅为源码给Presenter的Style ID，本机未暴露为UIA入口；结构观察不能证明bar bounds全宽、窄宽动态溢出、菜单键盘、焦点或Narrator通过 |
| C-UI | [最终ui-command-bar-report](../.build/explorer-command-bar/integration/ui-command-bar-report.json)：**18 PASS / 0 FAIL / 0 SKIP**，2026-11-09 05:41:20–05:45:02 UTC；修正后Release SHA256 `5AFE3C46D519EF13C378FF7539C80821F313A860820949FBC5A5CF7F8E41662A` | 同一完整脚本#08 Hidden→#14真实索引/PasteParent串联通过；正常exit0、无强杀；fixture清理3152字节，compiler-temp已清但释放字节未记录，clipboard_restored=true。修正前13子项PASS红尝试和私有Search3 PASS仅保留诊断，不算本次最终结果；4个未点击排序叶、完整环境和人工验收仍未验 |
| D-NATIVE | `.build/explorer-address-bar/integration`的最终两配置build/test状态与`tests-debug.log/tests-release.log`：WM_CLOSE修正后Debug/Release均构建exit0，原生各**1565 PASS / 0 FAIL / 4既有权限SKIP**；246输入/378资源，单并发 | 新增23项地址模型；不能证明真实IME/UNC或GUI，不将权限SKIP改写为通过 |
| D-UI | [最终Release地址GUI报告](../.build/explorer-address-bar/integration/ui-address-bar-report.json)：**20 PASS / 0 FAIL / 0 SKIP**，2026-11-10 12:33:45–12:40:38 UTC；SHA256 `CF1D8A2BAE1A8B67ED2C99E53DFF373369EE8362060D69B8DAD3481A8E5FCA9D` | 18功能+2收尾；#18保留未提交draft/实际editor焦点直接Close，#19正常exit0、#20清理366字节fixture。compiler-temp已清、字节未记录；无系统剪贴板/全局输入。与下列命令/查看共53项，关闭四场景另列 |
| D-CMD | [最终Release命令兼容报告](../.build/explorer-address-bar/integration/ui-command-bar-compat-report.json)：**18 PASS / 0 FAIL / 0 SKIP**，2026-11-10 12:43:04–12:45:07 UTC，与D-UI同SHA | 当前镜像重跑G02命令18项限定场景；正常exit0、3152字节fixture/临时目录已清、clipboard_restored=true，不由历史C-UI代替 |
| D-VIEW | [最终Release查看兼容报告](../.build/explorer-address-bar/integration/ui-view-compat-report.json)：**15 PASS / 0 FAIL / 0 SKIP**，2026-11-10 12:45:33–12:54:28 UTC，与D-UI同SHA | 当前镜像重跑查看/显示15项限定场景；正常exit0、1015字节fixture/临时目录已清；旧G01的DropDownButton类名不因此套用于当前host。D-UI+D-CMD+D-VIEW=53，非全P0验收 |
| D-PREFLIGHT | [旧Debug地址报告](../.build/explorer-address-bar/integration/ui-address-bar-debug-report.json)19项，旧镜像另有命令18/查看15，共52 PASS；该地址SHA256 `50B3D782D7737E9017914C78C08245BAB3DACAF2FA44BBBDC7218BB6A3C24A17` | 只作preflight历史，不是最终WM_CLOSE修正后镜像GUI，不混入D-UI的20项或冒称最终Debug52项重跑 |
| D-CLOSE | [最终固定版关闭报告](../.build/explorer-address-bar/integration/window-close-probe/fixed/probe-report.json)：同一CF1D...镜像A/B/C/D四项均**PASS、exit0**，2026-11-10 12:54:43–12:54:51 UTC；无新dump | C以自有WinForms伴随窗口核验Ctrl+L前后真后台，background_branch=COVERED；D实际Ctrl+W关唯一Tab、无WM_CLOSE fallback。前期两dump的StackWalk64共64帧匹配、顶栈WinUIEdit/Xaml，仅诊断，不宣称还原内部根因；四项不并入53 |

H-FOLDER-UI/H-SEARCH-UI使用上一菜单切片候选的SHA，不等于对G01或G02重新跑了39项。G01历史结果仅证明其当时候选。native中源码文字扫描断言、fake executor/RPC、真实文件操作的证明力不同；按下文具体测试入口使用，不把总计数作为 Windows Explorer 对比结果。

B-VIEW 的输入仅自有 PID/HWND InputSite 键盘消息及 WinUI UIA，未使用全局 SendInput 或剪贴板。弹出菜单查找优先自有主窗口，再由 EnumWindows 按 PID/可见性筛选自有 HWND，不为负查找遍历其他进程的桌面 UIA provider。菜单/布局结论来自实际控件与 checked 状态，未提供菜单视觉截图。

## 2. 快捷键矩阵

代码入口：`main.cpp::BuildUi/AddAccelerator/CreateFolderViewControl/BuildAddressBar/BeginAddressEdit/CancelAddressEdit`，`AddressBarPresentation::Build`、`SearchPresentation::Build`、`FolderNameDialog::Show`。普通目录列表的 accelerator 设置 `ScopeOwner(control)`；通用 accelerator 和 Ctrl+T/W 检查 `IsOperationDialogOpen()`。搜索动作绑定 `SearchResults` 列表，地址/搜索编辑框不是文件操作快捷键的 owner。

**优先级规则**：文本控件内部编辑 → 当前模态对话框 → 所在列表/菜单 → 窗口导航命令。当前代码对列表作用域与对话框已有门控，但跨焦点的完整实际键盘序列尚未逐项跑过，不凭源码宣称所有输入法和焦点状态均正确。

| 测试编号 | 按键/操作 | 当前实现、适用焦点 | 文本/对话框优先级与待验项 | 状态/既有执行 |
| --- | --- | --- | --- | --- |
| EXP-KEY-01 | Alt+← / Alt+→ / Alt+↑ | 窗口 accelerator 调用 Back/Forward/Up；使用导航状态机 | 操作对话框打开时门控；地址输入中的 Alt 组合/IME 待实机 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-02 | Ctrl+L | G03a从面包屑进入编辑全选；重复Ctrl+L保留最新草稿再全选 | Release D-UI #02/#04已验；模态/真实IME另验 | IMPLEMENTED_VERIFIED（最终Release所列键盘）；其余NOT RUN |
| EXP-KEY-03 | Alt+D、F4 | Alt+D进入编辑全选；F4打开真实已commit历史；空历史只进入编辑 | Release D-UI #04/#08/#16已验Alt+D及F4实际导航；原生Combo/Menu自身F4、空历史仍待专项；无自动完成/下级目录菜单 | IMPLEMENTED_VERIFIED（最终Release所列子项）；其余IMPLEMENTED_NEEDS_MANUAL |
| EXP-KEY-04 | F5 | 窗口刷新当前位置 | 刷新不是新历史记录；模态时不提交 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-05 | F6、Shift+F6 | 未接区域焦点轮转 | 需另定地址/命令/树/列表/状态的循环顺序 | DEFERRED / NOT RUN |
| EXP-KEY-06 | F3 | 未接搜索聚焦键 | 已有 Ctrl+F/E 不等于 F3 | DEFERRED / NOT RUN |
| EXP-KEY-07 | Ctrl+F、Ctrl+E | 窗口聚焦搜索 TextBox 并全选 | 模态时门控；文本框焦点下的行为待验 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-08 | Tab、Shift+Tab | WinUI 标准焦点导航；列表明确 IsTabStop(true) | 没有显式跨区域焦点恢复模型；菜单关闭/导航重投影后的落点待验 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-09 | Enter：地址 | G03a同步读取实际AddressEditor.Text并提交原BrowseAsync；成功恢复面包屑 | D-UI #03/#09已验Unicode目录和不存在路径失败恢复/不入历史；composition守卫已实现，真实IME/其他错误未验 | IMPLEMENTED_VERIFIED（最终Release所列子项）；真实IME/其他错误NOT RUN |
| EXP-KEY-10 | Enter：搜索/高级范围输入 | 搜索框及指定盘/目录输入提交查询 | 搜索草稿/范围属于当前 Tab；不可当文件 Open | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-11 | Enter：普通列表 | scoped accelerator 打开焦点/所选行；目录导航、文件关联 helper | 不作用于地址、搜索、重命名框 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-12 | Enter、Ctrl+Enter：搜索列表 | Enter 打开；Ctrl+Enter 打开父目录并选中原命中项 | 只作用于结果列表；历史 H-SEARCH-UI #11 覆盖 Ctrl+Enter | IMPLEMENTED_VERIFIED（限定历史）/ NOT RUN |
| EXP-KEY-13 | Esc：地址 | G03a取消草稿、恢复committed地址并回到当前普通/搜索列表；原生历史菜单先按自身取消语义关闭 | Release D-UI #02/#04/#06/#17已验普通/真实SearchResults回焦和菜单取消；composition/LostFocus/完整焦点组合仍待验 | IMPLEMENTED_VERIFIED（最终Release所列子项）；真实IME等NOT RUN |
| EXP-KEY-14 | Esc：搜索 | 搜索框/结果列表离开搜索 | 菜单、模态内 Esc 应先关闭当前界面；IME 候选取消优先级待验 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-15 | Esc、Enter：菜单/名称对话框 | MenuFlyout/ContentDialog 标准取消/默认按钮；名称 Primary 启用取决于合法性 | B-VIEW #10 验证顶部菜单 Space、Home/Down/Right、Enter、两级 Esc 并返回 FolderViewMode；名称对话框由G05a完整15项Release限定已验，真实IME未验 | IMPLEMENTED_NEEDS_MANUAL / 顶部菜单 PASS；G05a名称所列子项PASS、IME NOT RUN |
| EXP-KEY-16 | Ctrl+T、Ctrl+W | 窗口新建/关闭活动标签；关闭最后标签请求关窗口 | D-CLOSE D已验实际Ctrl+W关唯一Tab、正常exit0且无WM_CLOSE fallback；D-UI/D-VIEW有限多Tab动作通过；全部模态/焦点组合另验 | IMPLEMENTED_VERIFIED（上述实际子项）；完整模态/焦点NOT RUN |
| EXP-KEY-17 | Ctrl+Tab、Ctrl+Shift+Tab | TabView **默认**相邻标签选择；SelectionChanged 已接业务状态 | 不能凭 main 无手写绑定认定未实现；从地址/列表等 TabView 内容树之外焦点调用的范围未核验 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-18 | Ctrl+F4 | TabView 默认 TabCloseRequested，Blink 已接 | TabView 焦点与内容区域范围、模态时框架默认事件是否与 Ctrl+W 一致待验 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-19 | Ctrl+A：普通列表 | `SelectAllFolderRows` → 权威 TabState 可见行；不按虚拟可见容器截断 | 地址/搜索/重命名框保留文本全选；搜索结果目前单选，不承诺多选结果 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-20 | Ctrl+单击、Shift+单击、Shift+方向键 | 普通列表 `SelectionMode::Extended`；SelectionChanged 同步权威选择 | 由 WinUI 控件提供，含硬链接别名的模型测试不代替实际手势 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-21 | Home/End、PageUp/PageDown、方向键 | ListView/GridView/TreeView 标准键盘行为 | 焦点在 TextBox 时编辑文本；快速滚动/虚拟化后的选择需实机 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-22 | F2 | G05a普通文件初选主名，目录全选；搜索先定位父目录，再共用名称对话框 | Opened从实际Text调用Select；Ctrl+A可全名编辑；不是inline rename，新名行定位选择未实现 | PARTIAL（G05a报告所列子项最终Release PASS） |
| EXP-KEY-23 | Ctrl+C、Ctrl+X | 普通选择/已验证搜索命中发布 CF_HDROP+Preferred DropEffect | TextBox 中必须文本复制/剪切；对话框不得把背后列表当源 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-24 | Ctrl+V | 普通列表粘贴当前目录；搜索列表粘贴命中项的**所在目录**（目录命中也取父目录） | TextBox 中为文本粘贴；不能因存在文件剪贴板而提交文件队列 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-25 | Ctrl+Shift+N | 普通列表新建文件夹对话框；搜索结果未接此创建命令 | 不在地址/重命名文本框中触发背后创建 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-26 | Delete | 普通/搜索列表走回收站删除；Ctrl/Alt 修饰不转普通删除 | TextBox Delete 应只删字符；对话框优先，回收失败不转永久删除 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-27 | Shift+Delete | 普通/搜索列表走确认后的永久删除；与 Delete 共用 command 路由 | 文本框保持文本编辑语义；H-SEARCH-UI #15 仅证明自有搜索项确认删除 | IMPLEMENTED_VERIFIED（限定历史）/ NOT RUN |
| EXP-KEY-28 | Alt+Enter | 普通列表 Blink 只读属性；搜索列表 Windows 原生属性 | 两处存在产品差异；文本焦点不应启动背后属性 | PARTIAL / NOT RUN |
| EXP-KEY-29 | Shift+F10、菜单键 Apps | 列表打开 Blink 主菜单，搜索在 PreviewKeyDown 先处理 | TextBox 使用其文本菜单；H-SEARCH-UI #14 覆盖搜索 Shift+F10，普通空列表可聚焦 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-KEY-30 | Ctrl+Z、Ctrl+Y | 没有文件操作撤销/重做日志或绑定 | TextBox 的编辑撤销/重做由控件管理；不能扩展成文件撤销成功 | DEFERRED（文件操作）/ NOT RUN |
| EXP-KEY-31 | Ctrl+Shift+数字 | 未接 Explorer 视图数字快捷键 | 已有 Details/List/Grid 菜单/工具栏不等于所有 8 视图键 | DEFERRED / NOT RUN |
| EXP-KEY-32 | F11、Ctrl+滚轮缩放 | 未接全屏和列表图标缩放 | 普通滚轮、系统窗口最大化是其他能力 | DEFERRED / NOT RUN |
| EXP-KEY-33 | 空格即时预览 | 未实现预览入口 | TabView 焦点下 Space 属控件选择标签行为，不是文件预览 | DEFERRED / NOT RUN |
| EXP-KEY-34 | Alt+F4 | 系统窗口关闭入口；Blink 关闭时取消并失效异步发布 | 模态、正在冲突/传输/原生属性时生命周期组合仍须实机 | OS_DEPENDENT / NOT RUN |

TabView 默认键的事实依据：[Microsoft Tab view — Keyboard navigation](https://learn.microsoft.com/en-us/windows/apps/develop/ui/controls/tab-view#keyboard-navigation)。它证明框架能力，不证明 Blink 所有焦点区域的实际效果。

### 2.1 文本焦点专项（必须独立于文件快捷键验证）

| 测试编号 | 自有 fixture 上的操作与判据 | 已有依据 | 既有结果与未验项 |
| --- | --- | --- | --- |
| EXP-FOCUS-01 | 地址 TextBox 输入文本，Ctrl+A/C/X/V、Delete、Ctrl+Z/Y 只编辑文本；背后文件/选择/队列不变化 | Release D-UI #05已验新AddressEditor的Ctrl+A/Delete保留文件字节/选择/行序/视图；B-VIEW/C-UI为历史 | 部分PASS（最终Release Ctrl+L/A/Delete）；C/X/V/Z/Y NOT RUN |
| EXP-FOCUS-02 | 搜索框及高级盘/路径输入执行同样组合，Enter 只提交当前查询，IME 候选 Enter/Esc 不误提交/退出 | SearchPresentation 文本 handler；IME 未实测 | NOT RUN |
| EXP-FOCUS-03 | F2 对话框只改主名、扩展名选择边界、Ctrl+A、粘贴、Delete、Enter/Esc、失焦；背后文件不受快捷键影响 | FolderNameDialog 当前全选为已知差距 | NOT RUN |
| EXP-FOCUS-04 | 新建/冲突/永久删除确认/属性窗口中 Tab 顺序、默认按钮、Esc、Ctrl+T/W、Alt+F4；退出回到合理列表焦点 | IsOperationDialogOpen/ContentDialog；属性和冲突实际打开仅历史部分覆盖 | NOT RUN |
| EXP-FOCUS-05 | 换 Tab/快速导航/视图重投影/菜单关闭后，焦点与权威选择一致，Enter/Ctrl+C 作用于当前行 | B-VIEW #03 选择/行序保留、#09 旧 top peer 换 Tab 不修改任一 Tab、#10 菜单 Esc 返回按钮；H-FOLDER/SEARCH 仍为历史 | 部分 PASS：所列子项；快速导航、列表 Enter/Ctrl+C 组合 NOT RUN |
| EXP-FOCUS-06 | G03a同Tab自动刷新保留地址草稿/焦点；切Tab取消未提交文本；旧history/祖先peer不能导航新Tab；被动读取HelpText不改变菜单/焦点 | Release D-UI #07/#12/#13/#14已验被动读取、真实watcher、Tab和旧history peer；不用F5冒充自动刷新 | 部分PASS（最终Release所列子项）；真实IME和旧祖先peer专项UIA仍NOT RUN |

## 3. 鼠标、选择、剪贴板与拖放

| 测试编号 | 能力与当前语义 | 源码/历史证据 | 状态/既有执行 |
| --- | --- | --- | --- |
| EXP-MOUSE-01 | 单击选中、双击目录导航/文件关联打开；普通目录按事件命中的行，不以另一所选首行替代 | main `OnFolderDoubleTapped/ActivatePresentedRow`；`clicked_item_resolution_maps_selected_a_b_double_click_b_to_b` | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-MOUSE-02 | Ctrl/Shift 多选；右击已选项保持集合，右击未选项调整选择；背景不复用旧 item ticket | main `ShowFolderContextMenu`；H-FOLDER-UI #05/#09/#19 | IMPLEMENTED_VERIFIED（限定历史）/ NOT RUN |
| EXP-MOUSE-03 | 空白点击取消选择、框选、修饰双击/中键打开、鼠标侧键前进后退 | 未见自有框选/侧键/修饰打开策略；标准控件空白行为须实测 | PARTIAL / NOT RUN |
| EXP-MOUSE-04 | 标签重排/溢出、滚轮、窗口拖动/缩放/Snap | TabView/TitleBar 系统控件；历史窗口拖动和导航树滚轮单独存证 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN；本轮不扩成触控板/多屏已验 |
| EXP-CLIP-01 | Unicode CF_HDROP，最多 1024 项、4 MiB，顺序/重复名保留；COPY/MOVE Preferred DropEffect | ClipboardFileTransfer；T-CLIP；H-FOLDER-UI #08、H-MERGE-UI #02 | IMPLEMENTED_VERIFIED（限定历史）/ NOT RUN |
| EXP-CLIP-02 | 内部剪贴板缓存绑定 sequence、原始有序 payload、复制/移动意图与源/父身份；续尾不重放已完成项 | main Publish/Paste、ClipboardCopyCache、MoveTransferTests | IMPLEMENTED_VERIFIED（原生历史）/ NOT RUN |
| EXP-CLIP-03 | 外部 Explorer → Blink 及 Blink → Explorer，分别 Copy/Cut 后 Paste，记录真实源/目标、完成效果、取消与剪贴板是否还属于本次操作 | 内部真实 CF_HDROP smoke 不等于 Explorer 对端；外部 Performed 反馈/剪切淡化仍 README 待做 | PARTIAL / NOT RUN |
| EXP-CLIP-04 | OLE 虚拟文件 FileDescriptor/FileContents、邮件附件/压缩包内拖出等无路径对象 | 读取入口只接受 CF_HDROP/TYMED_HGLOBAL；未实现流/描述符物化 | DEFERRED；当前 UNSUPPORTED 输入 / NOT RUN |
| EXP-DROP-01 | 真实 IDataObject 与 IDataObjectProvider、跨 apartment 引用、异步 capability；完整本地路径集合不截成 WinRT StorageItems 前缀 | FileDragData/FileDragDropController；T-DROP | IMPLEMENTED_VERIFIED（原生历史）/ NOT RUN |
| EXP-DROP-02 | 同卷默认 Move、跨卷默认 Copy；Ctrl 强制 Copy、Shift 强制 Move；allowed effect 不符拒绝；自身/子树目标拒绝 | ChooseFileDropEffect、同一串行 coordinator；T-DROP | IMPLEMENTED_VERIFIED（原生历史）/ NOT RUN |
| EXP-DROP-03 | Alt 或 Ctrl+Shift 创建快捷方式；右键拖放动作菜单 | link 修饰返回 NONE；未接右键拖放选单，不能静默当 Copy/Move | DEFERRED / NOT RUN |
| EXP-DROP-04 | 本地列表/子目录/侧栏目录/驱动器落点，悬停标签切换；ThisPC/搜索虚拟页自身不充当文件目录 | main `FileAreaDropTarget/NavigationDropTarget/FileDragTabOver`；T-DROP origin 测试 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-DROP-05 | 完成反馈：Blink 自己移动全流程，Performed 不再指示源端额外删除，Logical 表示实际结果；失败/取消/全 Skip 不报完整成功 | IncomingFileDrag::Finish/CompletedFileDrop；T-DROP | IMPLEMENTED_VERIFIED（原生历史）/ NOT RUN |
| EXP-DROP-06 | Explorer ↔ Blink 真正指针拖放：同/跨卷、Ctrl/Shift、取消、混合多选，提示与终态一致 | 当前原生 IDataObject fixture 不是 Explorer 实际手势证明 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |

搜索列表当前**单选**；不把普通目录 Extended 选择或 Shell 同父多选能力延伸成搜索多选。路径复制：普通多选按 committed 顺序 CRLF 拼接完整路径，所在目录只复制一次；搜索复制已验证命中项的路径/父目录，不读取背后的浏览页选择。Paste 在普通目录取当前目录，在搜索取命中项父目录，这是当前可观察行为，不能混用。

## 4. Shell、属性与特殊对象的边界

### 4.1 原生菜单实际机制

`ShellContextMenuController` 在 worker 启动普通用户 `Blink.ShellMenuHost.exe`。helper 通过 SHParseDisplayName/SHBindToParent、IShellFolder::GetUIObjectOf 获取 **IContextMenu**；QueryContextMenu 后用 TrackPopupMenuEx 展示经典菜单。可选查询 IContextMenu2/3，并在 owner WndProc 转发 INIT/UNINITMENUPOPUP、DRAW/DELETE/MEASUREITEM、MENUCHAR；典型下级菜单和 owner-draw 接口有真实接线。

基本 Open/Copy/Cut/Paste/Rename/Delete 返回 Blink command，经原有导航或唯一文件操作队列处理；其他动词通过 GetCommandString/InvokeCommand 由系统扩展执行。支持实际同父目录多选，保留不同硬链接名称。选中项/父目录身份和路径关系重新核对，旧 Tab/query/selection ticket 失效。

**没有 IExplorerCommand 枚举/调用实现**，不能把经典 IContextMenu（含 2/3）描述成完整 Windows 11 现代扩展菜单。动态项由真实注册/对象/系统版本决定，不硬编码 Bandizip 等名称。H-FOLDER-UI/H-SEARCH-UI 的实际 ZIP 是代表性扩展完成证据，既不是任意扩展兼容证书，也不是多选压缩对话框已验收。

控制器读回复/取消/子进程的 WaitForMultipleObjects 使用 **INFINITE**。正常取消与进程退出有处理、DLL 在 helper 中隔离，但当前没有第三方 handler 卡住的墙钟 deadline；不宣称所有 Shell hang 都有超时终止。`native` 回复在 InvokeCommand 前发送，表示选择已交给系统，不表示扩展操作完成；后续 Invoke 失败由 helper 呈现，实际压缩必须另看目标归档与内容。

### 4.2 属性能力

| 测试编号 | 对象/入口 | 当前事实 | 状态/既有执行 |
| --- | --- | --- | --- |
| EXP-PROP-01 | 普通单选/多选、Alt+Enter | Blink `ItemPropertiesDialog` 只读；worker 读名称/路径/类型/大小/分配/时间/属性与目录统计、取消/部分失败 | IMPLEMENTED_VERIFIED（H-FOLDER-UI #15、T-PROP）/ NOT RUN |
| EXP-PROP-02 | 搜索文件/目录、Alt+Enter | ShellMenuHost `--verb properties`，Windows 属性页/扩展页由系统决定，helper 保活并正常关闭 | IMPLEMENTED_VERIFIED（H-SEARCH-UI #09/#10、H-SHELL）/ NOT RUN |
| EXP-PROP-03 | 普通 SystemOptions > 属性 | 原生经典菜单 properties 由 InvokeCommand 执行，不等于普通 primary Properties 已改原生 | OS_DEPENDENT；该路径组合 NOT RUN |
| EXP-PROP-04 | ACL/只读隐藏编辑、共享、自定义/以前版本、驱动器/快捷方式/混选页签 | Blink 自有窗口没有编辑这些属性；系统页签条件依赖对象/注册 | PARTIAL / NOT RUN；不得固定承诺所有页签 |

### 4.3 文件系统与 Namespace 矩阵

| 测试编号 | 对象 | 当前可支持的范围与未覆盖项 | 状态/既有执行 |
| --- | --- | --- | --- |
| EXP-OBJ-01 | 本地固定/可移动卷普通文件/目录 | 浏览/目录监听/基本操作；ThisPC 只枚举 DRIVE_FIXED/DRIVE_REMOVABLE；可移动设备真实拔插恢复未验 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-OBJ-02 | 硬链接 | 身份加 RowIdentity 的名称/路径区分别名；选择、Copy/Move/Delete/Shell 多选保留具体链接名 | IMPLEMENTED_VERIFIED（T-ROW/T-CLIP/T-SHELL/T-DELETE）/ NOT RUN |
| EXP-OBJ-03 | 符号链接/junction/reparse | 属性可读链接对象并不统计目标树；Delete 有删除链接不删目标测试；Copy/Move、目录链捕获明确拒绝重解析输入，不能统称全功能支持；四项 symlink 仍权限 SKIP | PARTIAL / NOT RUN |
| EXP-OBJ-04 | .lnk、exe、图片、zip、脚本、无关联文件 | 普通文件 open 使用当前用户关联，独立 FileActivationHost；扩展缺失/无关联返回错误而非伪成功；未实现 lnk 目标位置、zip 内浏览或每类自有专用菜单 | PARTIAL；各类型实际打开/完整菜单 NOT RUN |
| EXP-OBJ-05 | 隐藏/系统/只读/稀疏/压缩/加密/ADS | B-VIEW #05/#06 验证真实 Hidden 文件/目录、SYSTEM 与 SYSTEM+Hidden 排除，以及隐藏选中项移除后不复活；其余特殊属性仍按引擎实际能力与历史测试判断 | PARTIAL / Hidden/SYSTEM 显示规则 PASS；其余 GUI NOT RUN |
| EXP-OBJ-06 | Unicode/emoji、长路径、大小写、尾点/空格 | Unicode/extended long path 有真实原生 fixture；GUI IME/emoji、尾点/空格、大小写敏感目录及系统关联组合未验 | PARTIAL / NOT RUN |
| EXP-OBJ-07 | UNC/SMB、映射网络驱动器 | Location 可规范化 UNC 不代表支持全部文件操作；DirectoryLease 限本地字母盘且 DRIVE_FIXED/REMOVABLE，拒绝 UNC/DRIVE_REMOTE；浏览/凭据/重连独立待核 | PARTIAL（路径浏览待核）；当前本地变更入口 UNSUPPORTED / NOT RUN |
| EXP-OBJ-08 | ThisPC/搜索 | 应用私有虚拟位置/结果模型；不伪造 C:\。基本打开真实目标；ThisPC 背景不接受普通文件写操作，搜索需解析真实来源 | IMPLEMENTED_NEEDS_MANUAL / NOT RUN |
| EXP-OBJ-09 | 回收站虚拟页/库/图库/MTP/控制面板/网络发现/搜索 Shell 虚拟容器 | 没有 Namespace 浏览器；Delete 可使用系统回收站不等于 Blink 有回收站浏览/恢复界面 | DEFERRED / NOT RUN；不提供假目录 |
| EXP-OBJ-10 | OneDrive/cloud placeholder | 真实本地普通项与占位/reparse 不同；没有占位符物化/同步状态/云盘 Namespace 集成 | DEFERRED / NOT RUN |
| EXP-OBJ-11 | SFTP/WebDAV/远端 Provider | contracts/fake/隔离原型不等于生产连接/认证/浏览/传输；按后续 Provider 路线推进 | DEFERRED / NOT RUN |
| EXP-OBJ-12 | 光驱、弹出/格式化/设备管理 | 不是当前 ThisPC 枚举与 Blink-owned 基本菜单能力；系统返回项也须按设备与条件核验 | DEFERRED / NOT RUN；不以普通目录测试替代 |

## 5. 对应任务书第 11 节的验收矩阵

以下编号是阶段A/G01建立的验收编号，B-*保留G01实际子项结果，C-*单列G02当前结果；其余保留 NOT RUN。T-* 引用仓库现存测试的真实 ID，H-* 引用历史产物，不能互换成新执行结果。

### 5.1 基础与菜单上下文（11.1）

| 验收编号 | 场景、操作与判据 | 现有来源/覆盖限度 | 既有结果与未验项 |
| --- | --- | --- | --- |
| EXP-NAV-01 | 本地目录/盘符/ThisPC 来回，快速 A→B→C，失败导航保留真实页；Back/Forward/Up/F5 可用性与历史一致 | B-VIEW #01/#07/#11/#12 自有目录、空目录/ThisPC；T-NAV/T-TAB native 重跑 | 部分 PASS：所列合法导航；快速/失败/返回前进/Up/F5 GUI NOT RUN |
| EXP-NAV-02 | 新建/切换/关闭活动和非活动 Tab、关闭最后 Tab；各自路径/历史/排序/视图/选择/草稿隔离；关闭后旧回调不复活 | B-VIEW #07–09/#12 两现有 Tab、第三新 Tab、临时 Tab 关闭及旧 top peer；T-TAB/T-SEARCH native 重跑 | 部分 PASS：路径/视图独立、Hidden 同步及旧 top peer；最后/非活动关闭、全部草稿与键盘范围 GUI NOT RUN |
| EXP-NAV-03 | 外部新增/改名/删除/属性改变、监听失败/目录删除/卷失效，恢复后无二次虚假刷新/陈旧页 | T-WATCH/T-NAV；真实设备移除待验 | NOT RUN |
| EXP-MENU-01 | 普通文件单选：txt/exe/图片/媒体/脚本/lnk/无关联；正确对象、禁用、实际打开和扩展项 | H-FOLDER-UI 只覆盖自有代表性文件；完整类型表未跑 | NOT RUN |
| EXP-MENU-02 | 普通目录单选：Open/Rename/Copy/Cut/Paste/Properties，动作目标准确 | H-FOLDER-UI #08/#10/#15/#18；普通属性仅只读 | NOT RUN |
| EXP-MENU-03 | 多文件、多目录、混合，右键保持权威集合；共同命令正确、完整路径 CRLF、父目录去重 | T-ROW/T-CLIP/T-SHELL；H-FOLDER-UI #05–07/#16。原生同父多选菜单已测，混选全部手势未测 | NOT RUN |
| EXP-MENU-04 | 真空白背景菜单：Paste/NewFolder/Refresh/View/Sort，不读旧 item ticket，不显示不适用文件动作 | H-FOLDER-UI #09/#11–14 | NOT RUN |
| EXP-MENU-05 | 搜索文件/目录：路径/身份重验、Open/Locate/Copy/Cut、父目录 Paste、Properties/F2/Delete，旧 query/Tab peer 失效 | T-SEARCH/T-SHELL；H-SEARCH-UI；当前单选 | NOT RUN |
| EXP-MENU-06 | 左树已固定/快速访问：固定/取消固定及对应菜单 | 当前 known-folder 快速入口不等于可编辑 pin 模型；尚无完整菜单 | NOT RUN（DEFERRED） |
| EXP-MENU-07 | 左树普通可展开目录：选择/展开、Lazy Load、焦点/菜单作用目标、单一滚动 | 树展开/滚轮有既有能力；专属完整右键未接，不能套普通文件菜单 | NOT RUN（PARTIAL） |
| EXP-MENU-08 | ThisPC 本地盘/可移动盘/光驱/网络映射盘：打开、属性、适用设备命令 | 当前 fixed/removable 有限入口；无完整设备菜单，设备操作不得由目录案例泛化 | NOT RUN（PARTIAL） |
| EXP-MENU-09 | ThisPC 背景/其他虚拟页：不出现普通路径写入动作，按真实能力显示 | B-VIEW #12 仅验证 ThisPC 顶部三视图/Show，地址仍为虚拟 ThisPC；C-UI #13另实测顶部变更/Sort禁用，虚拟Location保持；完整背景菜单仍待验 | 部分PASS：G01 ThisPC View/Show及C-UI顶部能力禁用；完整背景菜单/其他虚拟页NOT RUN |
| EXP-MENU-10 | 回收站根/内容菜单与恢复/清空 | 浏览页未实现；现存 recycle/restore fixture 只证明后端删除 | NOT RUN（DEFERRED） |
| EXP-MENU-11 | UNC/SMB/远端：权限、断线、菜单/凭据/传输语义 | 本地变更限制见 EXP-OBJ-07；Provider 原型不能作为生产证据 | NOT RUN（PARTIAL/DEFERRED） |
| EXP-MENU-12 | Tab/地址面包屑/编辑框/搜索框/列头特殊菜单；文本焦点不误操作文件 | Release D-UI已验G03a祖先左键/原生overflow/F4历史；不等于面包屑专属右键或下级目录菜单。Tab/列头完整菜单未实现，原生TextBox菜单另验 | PARTIAL；仅上述Release动作PASS，完整特殊菜单NOT RUN |
| EXP-OP-01 | 同/跨卷文件、目录和混合 Copy/Move；核对字节、ADS、时间/属性、同卷文件 ID、目标独有项与源留存 | T-COPY/T-MERGE；C-UI #04/#05为自有同卷单文件COPY/MOVE和字节/来源核对；不是全部对象/跨卷/元数据矩阵 | 部分PASS：所列COPY/MOVE；ADS/跨卷/目录/完整元数据GUI仍NOT RUN |
| EXP-OP-02 | 根及嵌套 Replace/Skip/KeepBoth/Cancel、类型冲突、目录 Merge；不能先删目标根 | T-MERGE；H-MERGE-UI #03–06 | NOT RUN |
| EXP-OP-03 | 中途取消/失败/观察回调异常，保留已完成前缀、真实残留和未启动尾项；只按允许方式继续，不整树重放 | T-COPY/T-MERGE/T-DELETE；冲突关闭/窗口退出实际键盘组合待验 | NOT RUN |
| EXP-OP-04 | Delete 入回收站、Shift+Delete 确认，回收失败不静默降为永久删除；删除后的选择/标签退回与刷新 | DeleteOperation的IFileOperation/recycle分支、T-DELETE；C-UI #07实测Shift永久删除取消与明确确认 | 部分PASS：所列永久删除分支；系统回收站/失败介质/组合恢复GUI仍NOT RUN |
| EXP-OP-05 | 磁盘不足/配额、ACL 拒绝、共享占用、源消失、路径过长/非法名称 | 共享占用/源身份/长路径已有 fixture；当前未找到真实磁盘耗尽专项结果，不填 PASS | NOT RUN |
| EXP-OP-06 | 移动/改名/Merge 后已打开根/子目录标签和历史按**实际**目标跟随；Skip 留源，KeepBoth 新名正确 | T-RELOCATION；H-MERGE-UI 未含标签组合 | NOT RUN |
| EXP-INTEROP-01 | Explorer → Blink、Blink → Explorer 真正剪贴板 Copy/Cut/Paste，包含多项/取消/冲突/外部完成反馈 | EXP-CLIP-03；内部 CF_HDROP PASS 不替代这两个方向 | NOT RUN |
| EXP-INTEROP-02 | Explorer 双向真实拖放，同/跨卷、Ctrl/Shift、目录/混选、取消/失败反馈 | EXP-DROP-06；实际 FileDescriptor/FileContents 当前不支持 | NOT RUN |
| EXP-PERF-01 | 自有大目录中菜单、换 Tab、F2、右键、滚动、退出分别记录响应耗时、样本数量/分位、UI 线程占用 | `large_folder_sort_and_projection_share_owned_background_pipeline` 是工程/模型约束；搜索 IPC benchmark 不等于菜单/首屏时延 | NOT RUN；无量化值不宣称无卡顿 |

### 5.2 人工 UI 与辅助功能（11.2）

| 验收编号 | 环境/场景 | 要记录的判据 | 既有结果与未验项 |
| --- | --- | --- | --- |
| EXP-ENV-01 | 25H2 / zh-CN / 125% 的 Explorer 自有目录 View/Show | 版本、窗口归属、UIA 菜单项/层级；没有改系统开关，没有菜单可视截图 | OBSERVED（仅参考，不是 Blink 通过） |
| EXP-ENV-02 | **24H2 x64** 浅色/深色/高对比度 × 100/125/150/200% | 逐组合记录字体/截断/禁用/焦点/缩放与归属截图；不能用 25H2 一次观察覆盖 | NOT RUN（全部组合） |
| EXP-ENV-03 | 25H2 其余主题/DPI、文字放大/减少动画/触摸/触控板/多显示器 | 主/子菜单边界翻转、位置、可点区域、拖动/Snap/焦点，按真实硬件记录 | NOT RUN |
| EXP-ENV-04 | zh-CN/en-US 与中文 IME、emoji/长名 | 资源可读，输入候选优先，快捷键不重复触发；没有运行时语言切换就按实际启动语言测试 | NOT RUN |
| EXP-ENV-05 | UNC、可移动盘插拔、隐藏/只读、实际安装扩展 | B-VIEW 只补真实 Hidden/SYSTEM 规则；拒绝访问/断开/设备/扩展实际动作仍单列 | 部分 PASS：Hidden/SYSTEM；其余 NOT RUN；Bandizip 仅 H-* 历史 |
| EXP-A11Y-01 | 每一级菜单/子菜单，鼠标与键盘，UIA/读屏 | B-VIEW #02 控件角色/模式与 #10 顶部菜单键盘、Esc 焦点返回；未覆盖所有菜单、边界展开、Alt 或读屏 | 部分 PASS：所列 top View/Show；Narrator/其他菜单人工 NOT RUN |
| EXP-DIALOG-01 | 属性/冲突/权限/确认/提示，默认按钮、危险确认、Esc/Alt+F4/关闭/最小化最大化 | 无误动作/重复提交、错误后可继续；系统页签按对象条件显示，不固定复制文字 | NOT RUN；历史仅特定对话框/流程 |
| EXP-VISUAL-01 | 同一受控目录/对象/尺寸下 Explorer 与 Blink 实际可视图 | 自有窗口截图必须包含待比内容；不足条件记未捕获，不合成第三方窗口或声称像素一致 | NOT RUN；参考 PrintWindow 未含 popup |

### 5.3 工程与生命周期（11.3）

| 验收编号 | 要验证的事实 | 现有证据/方法 | 既有结果与未验项 |
| --- | --- | --- | --- |
| EXP-ENG-01 | Debug/Release真实构建与普通回归，单并发、记录输入/二进制摘要 | D-NATIVE最终两配置各1565/0/4、246输入/378资源；同一Release D-UI/D-CMD/D-VIEW合计53项通过 | PASS（限定最终构建/原生/53项GUI）；旧Debug52仅preflight，4权限SKIP保持 |
| EXP-ENG-02 | zh/en/fallback键覆盖、command/Automation ID唯一、每个新可见项有实际动作 | G03a为378资源；D-UI有新地址ID/实际动作；C-NATIVE为G02的375资源和历史命令检查，4个未点击排序叶仍待验 | 部分PASS：资源/本机zh-CN所列UI；不证明全应用ID唯一或en-US实机通过，完整专验NOT RUN |
| EXP-ENG-03 | 无重复 accelerator；文本/模态优先；窗口/Tab/query 关闭后旧回调不能执行 | Release D-UI #05文本作用域、#14旧history peer、#16键盘历史；B-VIEW与T-TAB/T-SEARCH保留各自范围 | 部分PASS：所列Release地址子项；模态/真实IME/旧祖先peer/全部accelerator组合NOT RUN |
| EXP-ENG-04 | 第三方菜单代码在 ShellMenuHost，Blink 主进程不加载相应扩展 DLL | host 源码架构与历史 helper PID；此轮未重新采模块快照，其他 property handler 不能据此泛化 | NOT RUN |
| EXP-ENG-05 | 没有回收失败→永久删除、索引离线→全盘 DFS 等静默 fallback；错误不冒充成功 | DeleteOperation/SearchCoordinator 真实分支与 T-DELETE/T-SEARCH；有 fake 用例也须注明 | NOT RUN |
| EXP-ENG-06 | 正常关闭、取消待机Shell/文件任务、清理自有fixture；回调不访问已关闭UI，用户剪贴板不被测试误用 | D-UI #18–20、D-CMD/D-VIEW退出/清理通过；三套fixture各366/3152/1015字节，compiler-temp已清，未记录字节不虚构；D-CMD剪贴板恢复true | 部分PASS：上述清理及D-CLOSE真后台/唯一标签关闭；传输/Shell待机组合NOT RUN |

## 6. 现存代码与真实测试 ID 对照

以下为可定位样例，不把测试函数数量推算为已覆盖全部UI；H-NATIVE/B-NATIVE保留历史执行，当前普通回归见C-NATIVE。新验收使用 EXP-*，避免与现有 ID 混淆。

| 索引 | 业务入口 | 现存测试源码与真实 ID（节选） |
| --- | --- | --- |
| T-ADDR | [AddressBarModel](../src/Blink/src/AddressBarModel.cpp)、[AddressBarPresentation](../src/Blink/src/AddressBarPresentation.cpp)、main的Build/Sync/Begin/CancelAddress/PrepareWindowClose | [AddressBarTests](../tests/Blink.Phase1.Tests/src/AddressBarTests.cpp)：`address_breadcrumb_this_pc_preserves_virtual_target_and_localized_label`、`address_breadcrumb_long_path_retains_all_levels_for_native_overflow`、`address_history_pending_typed_location_never_enters_f4_choices`、`address_edit_same_owner_refresh_preserves_draft_and_updates_committed_truth`、`address_edit_tab_switch_discards_old_draft_even_when_committed_paths_match`；23项模型在最终两配置各1565/0/4中通过，纯模型不等于控件/IME/UNC实测 |
| T-NAV | [NavigationModel](../src/Blink/src/NavigationModel.cpp)、BrowseController、FolderWatcher、FolderChangeState | [main.cpp](../tests/Blink.Phase1.Tests/src/main.cpp)：`navigation_older_intent_rejected`、`navigation_failed_back_preserves_history`、`navigation_refresh_preserves_history`、`navigation_up_root_reaches_this_pc`；[FolderNavigationTests](../tests/Blink.Phase1.Tests/src/FolderNavigationTests.cpp)：`folder_navigation_watch_start_failure_releases_browse_to_report_real_error` |
| T-TAB | [TabState](../src/Blink/src/TabState.cpp)、[TabWindowState](../src/Blink/src/TabWindowState.cpp)、main TabView callbacks | main 测试：`tab_locations_are_isolated`、`tab_histories_are_isolated`、`tab_selections_are_isolated`、`tab_async_execution_state_is_isolated`、`closing_last_tab_requests_window_close`、`closed_tab_invalidates_async_state` |
| T-ROW | RowIdentity、TabState 的 SelectedCommittedRows、main selection sync | main 测试：`row_identity_distinct_hardlink_names`、`copy_selection_capture_preserves_distinct_hardlink_names_in_committed_order`、`select_all_cannot_reuse_previous_folder_rows_in_this_pc`；这些不证明实际 Ctrl/Shift 手势 |
| T-VIEW | FolderViewPresentation、ItemVisibilityPolicy、TabState 投影 | main 测试：`view_modes_are_isolated_when_switching_tabs`、`view_switch_preserves_committed_rows_hardlink_selection_focus_sort_history_and_pending`、`visibility_show_hidden_does_not_expose_system_rows`、`visibility_window_policy_applies_to_inactive_and_new_tabs`、`visibility_real_hidden_file_can_be_shown_and_selected` |
| T-WATCH | FolderWatcher/FolderChangeState、main watcher reconciliation | main 测试：`watch_native_external_create_invalidates_snapshot`、`watch_native_external_rename_received`、`watch_native_external_delete_received`、`watch_reconcile_prunes_deleted_or_renamed_row_selection`、`watch_window_shutdown_invalidates_all_tab_tokens` |
| T-CLIP | [ClipboardFileTransfer](../src/Blink/src/ClipboardFileTransfer.cpp)、ClipboardCopyCache、main Publish/Paste | [ClipboardFileTransferTests](../tests/Blink.Phase1.Tests/src/ClipboardFileTransferTests.cpp)：`clipboard_batch_unicode_order_and_duplicate_paths_roundtrip_without_normalization`、`clipboard_batch_payload_is_exact_unicode_dropfiles_double_null_list`；[ClipboardCopySourceTests](../tests/Blink.Phase1.Tests/src/ClipboardCopySourceTests.cpp)：`typed_capture_files_keep_order_duplicates_hardlink_names_actual_parent_ids_and_default_destination_fields`、`typed_capture_actual_extended_long_directory_keeps_type_and_both_ids_without_scanning_children`；[MoveTransferTests](../tests/Blink.Phase1.Tests/src/MoveTransferTests.cpp) 补意图/缓存/续尾；纯 codec 测试不访问系统剪贴板 |
| T-DROP | [FileDragData](../src/Blink/src/FileDragData.cpp)、[FileDragDropModel](../src/Blink/src/FileDragDropModel.cpp)、FileDragDropController | [FileDragDropTests](../tests/Blink.Phase1.Tests/src/FileDragDropTests.cpp)：`drag_real_data_object_transfers_forty_paths_without_storageitems_truncation`、`drag_same_volume_defaults_to_move`、`drag_different_volume_policy_defaults_to_copy`、`drag_unsupported_link_modifiers_never_silently_copy_or_move`、`drag_optimized_move_reports_no_source_delete_and_exactly_once_completion`、`drag_search_results_remain_unavailable_as_drop_targets` |
| T-SHELL | [ShellContextMenuController](../src/Blink/src/ShellContextMenuController.cpp)、[ShellContextMenu](../src/Blink/src/ShellContextMenu.cpp)、[ShellMenuHost](../src/Blink.ShellMenuHost/src/main.cpp) | [SearchContextMenuTests](../tests/Blink.Phase1.Tests/src/SearchContextMenuTests.cpp)：`shell_menu_command_line_preserves_full_width_handles_coordinates_and_options`、`shell_menu_reply_accepts_native_selection_with_lf_without_claiming_completion`、`folder_shell_menu_accepts_file_and_directory_siblings_together`、`folder_shell_menu_keeps_distinct_hardlink_names_in_multi_selection`；协议/请求测试不是实际 Bandizip 验证 |
| T-SEARCH | SearchResultValidation/SearchContextCommands/SearchCoordinator、TabRecord SearchIntent、main ExecuteSearchContextCommand | SearchContextMenuTests：`search_context_clipboard_uses_validated_result_instead_of_hidden_browse_selection`、`search_context_unsubmitted_draft_and_options_do_not_change_menu_target`、`search_context_new_query_generation_invalidates_old_menu`；[SearchIntegrationTests](../tests/Blink.Phase1.Tests/src/SearchIntegrationTests.cpp)：`search_follow_c_directory_defaults_to_whole_c_drive`、`search_selected_volumes_query_only_deduplicated_user_roots_without_discovery`、`search_retry_reuses_submitted_scope_depth_and_mode_while_retaining_new_draft`；fake RPC 与 [IndexServiceLiveProbe](../tests/Blink.Phase1.Tests/src/IndexServiceLiveProbe.cpp) 的 LIVE 行必须分开引用 |
| T-PROP | [ItemProperties](../src/Blink/src/ItemProperties.cpp)、[PropertiesDialog](../src/Blink/src/PropertiesDialog.cpp)；系统页签是 ShellMenuHost | [ItemPropertiesTests](../tests/Blink.Phase1.Tests/src/ItemPropertiesTests.cpp)：`properties_real_file_reads_current_handle_metadata_instead_of_cached_row_size`、`properties_multi_selection_aggregates_files_and_directory_contents`、`properties_selected_junction_reads_link_object_without_counting_target`、`properties_same_path_replacement_is_reported_instead_of_disclosing_new_file_as_old_selection` |
| T-OPEN | [FileActivation](../src/Blink/src/FileActivation.cpp)、[FileActivationHost](../src/Blink.FileActivationHost/src/main.cpp)、main ActivatePresentedRow | main 测试：`file_activation_helper_crash_and_nonzero_exit_are_never_success`、`file_activation_helper_arguments_roundtrip_path_and_full_identity`；后者是参数 roundtrip，不能证明所有文件关联应用均打开 |
| T-COPY | FileCopyOperation/FileCopyBatch、DirectoryCopyOperation、MoveOperation/MoveBatch、FileOperationCoordinator | [FileCopyOperationTests](../tests/Blink.Phase1.Tests/src/FileCopyOperationTests.cpp)：`file_copy_cancel_after_real_progress_removes_owned_partial_and_preserves_source`、`file_copy_extended_long_source_and_destination_paths_preserve_exact_contents`；[CrossVolumeMoveTests](../tests/Blink.Phase1.Tests/src/CrossVolumeMoveTests.cpp)：`cross_directory_default_mixed_cross_volume_queue_moves_files_and_directories`、`cross_directory_partial_tree_cannot_be_replayed_as_root_conflict_or_recopied` |
| T-MERGE | DirectoryCopyOperation::CopyDirectoryAndRemoveSource、MoveDirectoryMergeSafely、FileOperationCoordinator conflict resolver | [DirectoryMoveMergeTests](../tests/Blink.Phase1.Tests/src/DirectoryMoveMergeTests.cpp)：`directory_move_merge_same_leaf_native_move_keeps_file_identity`、`directory_move_merge_same_directory_file_keep_both_rebinds_nested_paths`、`directory_move_merge_cross_skip_retains_source_root_and_only_skipped_items`；[DirectoryMergeTests](../tests/Blink.Phase1.Tests/src/DirectoryMergeTests.cpp) 覆盖 Copy Merge |
| T-DELETE | [DeleteOperation](../src/Blink/src/DeleteOperation.cpp)、DeleteConfirmationDialog、FileOperationCoordinator | [DeleteOperationTests](../tests/Blink.Phase1.Tests/src/DeleteOperationTests.cpp)：`delete_recycle_real_file_and_ads_restore_from_system_bin`、`delete_confirmed_permanent_nonempty_directory_has_no_recycle_item`、`delete_open_file_without_delete_sharing_reports_failure_and_retains_source`、`delete_recycle_junction_object_does_not_delete_target_contents`、`delete_skip_current_failure_continues_tail_without_replaying_completed_prefix` |
| T-RELOCATION | FileOperationRelocation、main 已完成目录跟随 | [FolderRelocationTests](../tests/Blink.Phase1.Tests/src/FolderRelocationTests.cpp)：`relocation_real_same_volume_move_reenumerates_root_and_descendant_tabs_with_same_ids`、`relocation_real_cross_volume_rearms_root_and_descendant_watchers`；主模型新增 Merge 实际目录映射相关 5 项见 Slice21 日志，不用 UI8 代替 |

## 7. G01历史限定切片与后续验证门槛

G01当时已实施并由主模型验证：**顶部“查看”菜单，复用现有 Details/List/Grid 三个真实视图，再提供“显示 > 隐藏的项目”**。本切片没有顺带加八视图、预览面板、分组、排序改造、Shell/剪贴板平台或快捷键重构。

改动将零散顶部视图/Hidden 控件放入可理解菜单。复用 SetFolderViewMode、SetShowHiddenItems/ItemVisibilityPolicy 与每 Tab 视图/窗口级 Hidden 状态，不从 checked 值另建真相。G01候选的`FolderViewMode`为`Microsoft.UI.Xaml.Controls.DropDownButton`，UIA Button role，Invoke/ExpandCollapse 可用；Name 使用 `Command.View`（中文“查看”），HelpText 为实际 Details/List/Grid 文案。`TopViewMenu` 提供三项互斥 Toggle，`TopViewShow > TopViewShowHidden` 使用真实 Hidden 策略，SYSTEM 继续排除。

G02当前host为标准AppBarButton；以下旧候选的UIA类型与动作证据保留历史范围，不自动证明新嵌入方式。下表把原7个验收编号映射到B-VIEW 的 **13 个功能 check + 2 个退出/清理 check**。15 PASS 是报告中的实际检查数，不把包含未执行组合的验收行整体改为 PASS。

| 新验收编号 | 操作与通过判据 | 测试层次 | 当前结果 |
| --- | --- | --- | --- |
| EXP-P0-VIEW-01 | 顶部 View 的三真实视图、Show 层级、实际入口/Automation ID 与唯一勾选 | B-VIEW #01 自有生产窗口/路径；#02 qualified DropDownButton、Button role、Invoke/Expand、三项 Toggle/Show/Hidden；#03 再查唯一勾选 | **PASS（所列结构与 zh-CN 行为）**；en-US 实机 NOT RUN |
| EXP-P0-VIEW-02 | 三实际布局，唯一勾选，路径、选择/行序与排序保留；右键变化同步顶部 | B-VIEW #03 用 FolderEntries ClassName（GridView/ListView）与四个可见详情列头区别实际布局，逐次保留路径/选择/行序/sort；#04 ContextViewList→顶部 List，再切 Grid；T-VIEW 在 B-NATIVE 重跑 | **PASS（所列布局/状态）**；GUI 历史栈与列表焦点跨视图组合未单独观测，NOT RUN |
| EXP-P0-VIEW-03 | Hidden 文件/目录真实显隐、SYSTEM/SYSTEM+Hidden 始终不显示，隐藏选择不复活 | B-VIEW #05 核对真实属性/可见行，#06 隐藏选中项被移除，再显示不恢复旧选择；顶部 checked 重新读取。旧独立 checkbox 已替换，不设假同步项 | **PASS（本地目录规则）** |
| EXP-P0-VIEW-04 | Tab 各自视图，窗口级 Hidden 同步既有/新 Tab；树/搜索的组合另验 | B-VIEW #07 两 Tab 分别 Grid/List 与不同路径；#08 已有非活动 Tab/新第三 Tab 采用同一 Hidden，原视图仍独立；B-NATIVE 既有状态测试保持 | **部分 PASS：现有/新 Tab**；树展开、Search Hidden integration **NOT RUN**。helper/索引协议不变是输入/摘要事实，不作未采集的运行跟踪结论 |
| EXP-P0-VIEW-05 | 纯键盘打开/进入 Show/切换/取消及焦点返回；文本焦点优先级 | B-VIEW #10 实际 PID 绑定 `Space Home Down Down Down Right Enter`，再以两级 Esc 关闭并返回 `FolderViewMode`；#13 `Ctrl+L/Ctrl+A/Delete` 只改地址文本，原文件/行序/View 保留 | **部分 PASS：所列菜单与地址键**；搜索框、名称/其他模态对话框、IME、文本剪贴板与撤销 **NOT RUN** |
| EXP-P0-VIEW-06 | 空目录/ThisPC/搜索/枚举中能力与旧菜单生命周期 | B-VIEW #09 保存旧 top peer，切另一 Tab 后 Invoke 返回但两 Tab 布局/路径均未变；#11 空目录三布局/Hidden 无旧行；#12 ThisPC 三布局/Show 保持虚拟位置；#14 自有窗口正常 exit0 | **部分 PASS：上述边界与切 Tab 旧 peer**；搜索页、枚举中 disabled 快照、导航/关闭后旧 top peer 的专项 GUI **NOT RUN** |
| EXP-P0-VIEW-07 | 两配置构建/回归、资源/输入摘要，选择实际菜单回归与清理 | B-NATIVE 两配置各 1516 PASS/0 FAIL/4 权限 SKIP、235 输入/369 资源；B-VIEW #04 右键/顶部视图同步，#15 逐路径/字节确认后清理 fixture，compiler-temp 已清 | **PASS（本轮构建、普通回归、15 项有限 GUI）**；未把 H-FOLDER/H-SEARCH 的 39 项当作本轮重跑，完整原生/搜索菜单 GUI 仍仅历史证据 |

G01当时未处理的快捷键、F2主名初选、地址Esc、文本/IME焦点、外部剪贴板完成反馈、真实双向拖放、普通/搜索属性一致性分别列独立切片；当前G03a地址实现与待验范围见第7.2节，G05a主名选区实现/验证见第7.3节，不能通过G01的View菜单结果关闭这些验收项。完整P0日常行为门槛目前尚未达成。

## 7.1 G02当前切片与正式18项检查

当前有限GUI验证已完成：实际覆盖的24条顶部入口为`IMPLEMENTED_VERIFIED`，Sort Name/Modified/Type/Ascending4个本轮未点击叶仍`IMPLEMENTED_NEEDS_MANUAL`。独立命令行在导航下方、左右窗格上方横跨两窗格；CommandBar本体以Left按内容期望宽度布局，不由源码假定其bounds覆盖整行。8个主AppBarButton含New/Sort/View真实菜单，单个系统More承载7个次级动作及动态overflow；不增加Share或其他无后端占位。`FolderViewMode`仍保留ID、当前模式HelpText与原TopView叶，Sort提供当前字段HelpText和方向ItemStatus。

`ExplorerCommandBarContext/EvaluateExplorerCommandBarAction`只推导能力；主窗口捕获Tab/Location/selection/presentation/view/operation/search代次并在动作时重验，helper只展示与回调。ThisPC不伪装文件夹，Search动作只指当前结果、Paste明确为所在目录；不借用背后的浏览页选择/目标。剪贴板更新同步启用态。More关闭上下文按dispatcher+epoch延后清理以适应原生Click/Closed先后次序。

C-NATIVE新增共26项检查；其中23项命令栏测试见[ExplorerCommandBarTests.cpp](../tests/Blink.Phase1.Tests/src/ExplorerCommandBarTests.cpp)：`command_bar_missing_committed_target_or_directory_identity_never_uses_visible_row`、`command_bar_search_never_borrows_the_browsing_page_selection_or_destination`、`command_bar_modal_dialog_blocks_all_surfaces_including_more`等覆盖派生能力；`command_bar_invert_uses_visibility_and_excludes_system_and_invalid_rows`、`command_bar_invert_keeps_distinct_hardlink_names_independent`、`command_bar_two_inversions_restore_selection_without_resetting_navigation_view_or_sort`覆盖TabState反选。纯模型PASS不能替代下列窗口操作。

`TabWindowState::SetVisibilityPolicy`原先先执行`LeaveSearch()`取消当前查询，又额外调用`SearchCoordinator::Shutdown()`，将所有已有标签的搜索状态永久置为Closed；随后`SearchState::Begin()`恒为0。独立只测搜索会成功，完整脚本执行#08 Hidden开关后，#14搜索连续失败两次，才暴露这一真实组合问题。修复仅删除visibility切换中的多余Shutdown，保留LeaveSearch取消/离开语义、CloseTab和全局Shutdown的永久关闭职责。新增3个模型回归覆盖现有前后台标签的旧代次取消、显示/隐藏后均可重启，以及关标签只永久关闭被关标签且不影响存活标签。 实际3项回归见[`tests/Blink.Phase1.Tests/src/main.cpp`](../tests/Blink.Phase1.Tests/src/main.cpp)：`visibility_change_cancels_active_and_background_search_generations`、`visibility_show_hide_preserves_search_restart_in_both_existing_tabs`、`visibility_tab_close_preserves_survivor_search_and_permanently_retires_closed_state`。

以下编号对应正式脚本实际18项检查。修正前正式GUI曾出现13个子项PASS，但在#08 Hidden之后#14连续失败两次；这份红尝试只用于定位问题，不能当最终验证。私有只搜索探针3 PASS同样仅属诊断，不能替代完整场景。修正后Release同一完整18项GUI最终18 PASS/0 FAIL/0 SKIP，报告仍归`ui-command-bar-report.json`；#08 Hidden之后#14真实索引结果、PasteParent当前结果父目录及字节核对全部串联通过。早期第09的harness旧TopMoreMenu定位问题已通过真实OverflowPopup校正，与本次搜索生命周期缺陷分开记录。 下表记录修正后C-UI的最终18项结果；各行只对所列实际子项记PASS，不把全部28个顶部入口、未点击排序叶或其他动态分支统一升级。

| G02验收编号 / 脚本check | 操作与通过判据 | 当前结果 |
| --- | --- | --- |
| EXP-P0-CMD-01 / #01 | 真实ApplicationBar、8主按钮、单个原生More；独立命令行位于导航下方且不受左树宽度挤入内容区；记录实际位置/宽度 | PASS（C-UI #01，限所列子项） |
| EXP-P0-CMD-02 / #02 | 零选/单文件/文件目录混选使主命令启用态符合当前权威选择，Rename不对多选启用 | PASS（C-UI #02，限所列子项） |
| EXP-P0-CMD-03 / #03 | New只有真实新建文件夹，原名称对话框创建精确自有目录 | PASS（C-UI #03，限所列子项） |
| EXP-P0-CMD-04 / #04 | Copy/Paste发布COPY，目标为当前目录，逐路径/字节核对完成文件 | PASS（C-UI #04，限所列子项） |
| EXP-P0-CMD-05 / #05 | Cut/Paste发布MOVE，精确自有源仅在真实提交后移除，目标字节相同 | PASS（C-UI #05，限所列子项） |
| EXP-P0-CMD-06 / #06 | Rename仅更改自有文件名，所有字节不变 | PASS（C-UI #06，限所列子项） |
| EXP-P0-CMD-07 / #07 | Shift点击Delete沿原永久删除确认；取消无影响，再明确确认精确自有项 | PASS（C-UI #07，限所列子项）；普通系统回收站NOT RUN |
| EXP-P0-CMD-08 / #08 | Size/Descending改变实际顺序与勾选；三真实View模式保留路径/所选项/排序，Hidden开关执行且SYSTEM未显示；其余排序叶另验 | PASS（C-UI #08，限所列子项）；Name/Modified/Type/Ascending4叶未点击，仍待验 |
| EXP-P0-CMD-09 / #09 | More全选/取消/反选与两类路径复制指向权威当前行集合 | PASS（C-UI #09，限所列子项） |
| EXP-P0-CMD-10 / #10 | More Properties读取自有选择；Refresh刷新同一已提交目录，保留真实对象语义 | PASS（C-UI #10，限所列子项） |
| EXP-P0-CMD-11 / #11 | 缩窄自有窗口后主命令由系统动态移入overflow，移入命令仍可实际调用 | PASS（C-UI #11，限所列子项） |
| EXP-P0-CMD-12 / #12 | 保存More旧peer后导航另一目录，旧peer不能改变另一目录选择/内容 | PASS（C-UI #12，限所列子项） |
| EXP-P0-CMD-13 / #13 | ThisPC保持虚拟Location；New/文件变更/Sort与目录选择动作禁用 | PASS（C-UI #13，限所列子项） |
| EXP-P0-CMD-14 / #14 | 真实索引结果按原查询/当前选择派生能力，路径及PasteParent语义不取旧浏览目录 | PASS（C-UI #14，限所列子项）；#08 Hidden→#14真实索引+PasteParent在同一完整串联中通过；Search Cut/Rename/Delete仅启用态 |
| EXP-P0-CMD-15 / #15 | 自有窗口定向Space打开原生More，Escape关闭返回焦点，再用键盘执行当前选择动作 | PASS（C-UI #15，限所列子项） |
| EXP-P0-CMD-16 / #16 | 地址Ctrl+A/Delete只改文本，选中文件/字节/列表保持 | PASS（C-UI #16，限所列子项） |
| EXP-P0-CMD-17 / #17 | 自有Blink正常exit0，无强杀 | PASS（C-UI #17，限所列子项） |
| EXP-P0-CMD-18 / #18 | 按精确归属/路径/字节核对后清理自有fixture；剪贴板恢复与临时目录清理写实记录 | PASS（C-UI #18，限所列子项）；fixture3152字节已清，compiler-temp释放字节未记录，clipboard_restored=true |

修正前13个子项PASS和私有Search3 PASS仅是定位历史；修正后完整C-UI 18/0/0才是G02最终GUI结果。这18项通过仍不代表完整P0完成。G01 Hidden/SYSTEM/跨Tab历史证据保留；#08单独View/Hidden子项不足以证明Search仍可用；G02已在同一完整脚本取得#08→#14串联PASS，G01历史不被拿来代替该证据。G03a地址Esc/Alt+D/F4另见第7.2节，不能用C-UI验证新地址host；F2主名初选、完整焦点/IME、外部剪贴板完成回传、Shell扩展、系统回收站、全部主题/DPI/Narrator/24H2仍按原矩阵逐项处理。

## 7.2 G03a地址栏：最终Release正式20项检查

实现状态为**IMPLEMENTED_VERIFIED（仅本机最终Release报告实际覆盖子项）**。当前落实编辑/面包屑、祖先/原生overflow、Ctrl+L/Alt+D/Enter/Esc/F4、真实历史及编辑中直接关闭；下级目录菜单、自动完成建议未做，完整G03/P0仍`PARTIAL`。地址草稿同Tab自动刷新保留，切Tab取消，不跨Tab或重启持久化；历史仅已commit current/最近back/forward，pending/失败/草稿不入选。

最终WM_CLOSE修正后，两配置原生各1565/0/4，新增23项地址模型，246输入/378资源。下表为D-UI的**20 PASS/0 FAIL/0 SKIP（18功能+2收尾）**；同一Release另有D-CMD18/D-VIEW15，合计53项通过，候选SHA/环境/UTC见索引。旧Debug52项只preflight；D-CLOSE四场景另全PASS、exit0，独立记录不加到53。

| G03a验收编号 / 脚本check | 操作与通过判据 | 当前结果 |
| --- | --- | --- |
| EXP-P0-ADDR-01 / #01 | ThisPC保持虚拟Location；被动地址host、原生breadcrumb和历史按钮可读，无伪造磁盘路径 | PASS（最终Release） |
| EXP-P0-ADDR-02 / #02 | Ctrl+L进入编辑并全选，Delete只改草稿，Esc恢复ThisPC并回列表 | PASS（最终Release） |
| EXP-P0-ADDR-03 / #03 | 中文/Unicode真实目录Enter提交，精确列表/字节正确，回到breadcrumb显示 | PASS（最终Release） |
| EXP-P0-ADDR-04 / #04 | Alt+D全选；重复Ctrl+L保留最新草稿再全选；Esc恢复committed路径/列表/焦点 | PASS（最终Release） |
| EXP-P0-ADDR-05 / #05 | 地址Ctrl+A/Delete只改文本，选择/视图/列表及自有文件字节保持 | PASS（最终Release） |
| EXP-P0-ADDR-06 / #06 | 进入真实搜索页后编辑地址并Esc；恢复committed路径，实际可见SearchResults获得焦点，查询仍保留 | PASS（最终Release）；只验搜索UI/焦点，不要求命中或测试索引性能 |
| EXP-P0-ADDR-07 / #07 | 按钮打开真实历史；读取host.HelpText不关菜单或重建leaf peer | PASS（最终Release） |
| EXP-P0-ADDR-08 / #08 | F4打开真实MenuFlyout；选中已记录Location后精确导航 | PASS（最终Release） |
| EXP-P0-ADDR-09 / #09 | 不存在路径失败后恢复committed地址/列表，路径未创建且不进入历史 | PASS（最终Release）；其他错误分支NOT RUN |
| EXP-P0-ADDR-10 / #10 | 原生breadcrumb连续点击两个真实祖先，精确路径/列表/字节匹配 | PASS（最终Release） |
| EXP-P0-ADDR-11 / #11 | 620宽真实原生“更多”打开折叠祖先，实际popup导航；验精确路径/两条行/字节/新breadcrumb/菜单关闭，恢复宽度后查Ready | PASS（最终Release）；入口ID空、BreadcrumbBarItem/Button |
| EXP-P0-ADDR-12 / #12 | 写自有文件触发真实watcher刷新，编辑草稿/焦点保留；不用F5代替 | PASS（最终Release） |
| EXP-P0-ADDR-13 / #13 | 切Tab取消旧未提交草稿；返回原Tab展示各自committed位置，不持久化草稿 | PASS（最终Release） |
| EXP-P0-ADDR-14 / #14 | 菜单仍开着时切Tab，旧history菜单关闭；旧leaf Invoke不能改变新Tab | PASS（最终Release）；旧breadcrumb peer专项NOT RUN |
| EXP-P0-ADDR-15 / #15 | 空目录显示精确路径，零行且不继承旧内容 | PASS（最终Release） |
| EXP-P0-ADDR-16 / #16 | F4、Home、Down、Enter选择实际历史条目并导航 | PASS（最终Release） |
| EXP-P0-ADDR-17 / #17 | history Esc不导航，焦点回地址/列表；临时ThisPC标签往返仍保持虚拟语义 | PASS（最终Release） |
| EXP-P0-ADDR-18 / #18 | 保留未提交draft及真实AddressEditor键盘焦点，确认foreground为自有窗口，不发Esc/Enter，直接CloseMainWindow | PASS（最终Release）；只覆盖foreground，后台另待专项 |
| EXP-P0-ADDR-19 / #19 | 自有Blink从编辑中正常关闭，exit0，无强杀 | PASS（最终Release） |
| EXP-P0-ADDR-20 / #20 | 逐路径/UTF-8字节核对后清理自有fixture；compiler-temp已清、未记录的释放量不虚构 | PASS（最终Release）；fixture366字节已清，compiler-temp字节未记录 |

本机Release实测`AddressBar`为`NamedContainerAutomationPeer / Group`，`AddressEditor`为`TextBox / Edit`，`AddressHistoryMenu`为`MenuFlyout / Menu`，历史叶为`MenuFlyoutItem / MenuItem`。host.HelpText显示committed或draft，#07已验读取不打断菜单。620宽原生overflow真实入口为`Microsoft.UI.Xaml.Controls.BreadcrumbBarItem / Button`、Name“更多”、ID空，popup叶也为BreadcrumbBarItem/Button；此身份仅属本机zh-CN，不套用G02的OverflowPopup或假定跨语言名称。

最初preflight在原生overflow点击后因harness的Ready依赖CommandSort报超时：窄窗已将按钮收进原生More。改验精确路径、两条行、字节、新breadcrumb/旧菜单关闭，恢复原宽再查Ready后，旧Debug19项preflight通过；最终Release20项也覆盖该真实popup导航。早期`ui-address-bar-overflow-first-attempt.json`仅诊断，不当产品导航失败或最终结果。

编辑中直接关闭曾有真实AV：初始/仅Closed退役补丁均复现；两份真实dump经StackWalk64得到64帧匹配，顶栈WinUIEdit/Xaml、未见自有关闭直接栈，不臆定内部源码根因。最终WM_CLOSE转交WinUI前先焦点交可见列表，再用helper缓存committed取消编辑/隐藏editor，不调ActiveState/Present、不重建crumb；程序Close前也Prepare，Closed只退役/shutdown。最终同镜像四场景如下，独立于53项GUI计数。早期C实际foreground、未覆盖后台的记录保留历史；最终C才通过伴随窗口明确COVERED，不倒写旧报告。

| D-CLOSE场景 | 最终实际操作与通过判据 | 结果 |
| --- | --- | --- |
| A | 无键盘/焦点请求，直接正常关闭自有窗口 | PASS / exit0 |
| B | 自有SUT前台、实际UIA焦点；Ctrl+L、Esc后正常关闭 | PASS / exit0 |
| C | 先将自有SUT置前台准备，再只激活自有PS WinForms伴随窗口；严格核对前台PID/HWND及全局UIA焦点/runtimeId，后台Ctrl+L前后均为真后台，保留可见editor后WM_CLOSE | PASS / exit0；background_branch=COVERED；伴随窗口正常关闭/dispose |
| D | 自有SUT前台保留editor，实际Ctrl+W关闭唯一Tab；tab_count=1，无WM_CLOSE fallback | PASS / exit0；programmatic_close_requested=true，close_requested=false |

真实IME候选Enter/Esc、UNC、权限/断开、Narrator、全主题/DPI/语言/24H2、空白点击/普通失焦、旧breadcrumb peer专项及F3/F6仍保留NOT RUN或后续实现。D-UI/D-CMD/D-VIEW当前同一Release合计53项通过，D-CLOSE A/B/C/D另四项通过；不升级完整G03/P0。最终地址报告human_acceptance=NOT RUN、model_test_results=NOT RUN by this UI smoke；原生来自独立D-NATIVE，不能混成GUI检查数或默认发布完成。

## 7.3 G05a名称初选：最终Release正式15项检查

本片两配置原生各1578 PASS / 0 FAIL / 4既有符号链接权限SKIP，249输入/378资源，新增13项`FileNameSelection`模型；不代替GUI。策略语义为最后后缀前的主名，`.config.json`→`.config`、`archive.tar.gz`→`archive.tar`；无后缀/单前导点/目录为全名，Unicode按UTF-16 code units选取。NewFolder/Copy/Move仍WholeName，Ctrl+A可显式选全/编辑后缀，不新增确认。模型空串/尾点不代表可创建这些名称，真实IME未测。

证据E-RENAME：[`ui-rename-selection-report.json`](../.build/explorer-rename-selection/integration/ui-rename-selection-report.json)，由`scripts/run-blink-rename-selection-smoke.ps1`实际执行；同一最终Release已15 PASS / 0 FAIL / 0 SKIP，SHA256 `ef4986100848f3656dcaa0f2d8a645a96e9a40b09f44c5e86c51f16abb67434c`，PID29940正常exit0、无force，425字节夹具已按归属清理。实际UTC为2026-11-10 16:20:41–16:23:53（本地2026-11-11 00:20:41–00:23:53）；zh-CN/x64/普通用户、窗口DPI96，ActualTheme未采集、IME未测试。以下仅对应该报告实际动作，不混用早期Debug候选或先推断默认发布。所有数据为自有GUID文件；同一次序列共13项功能+2项收尾，退出与归属清理分别计数。

| 检查 | 实际断言/验证目的 | 本轮结果 |
| --- | --- | --- |
| RENAME-01 | 初始导航到自有目录，真实目录项/入口就绪 | PASS（最终Release E-RENAME） |
| RENAME-02 | F2主名初选，替换草稿后Esc保持原文件、回列表且New/Sort能力重算 | PASS（最终Release E-RENAME） |
| RENAME-03 | 替换主名后Enter，最终后缀/字节/文件身份保持 | PASS（最终Release E-RENAME） |
| RENAME-04 | 多点文件仅排除最后一个后缀 | PASS（最终Release E-RENAME） |
| RENAME-05 | 无后缀及单前导点文件全名初选 | PASS（最终Release E-RENAME） |
| RENAME-06 | Unicode/emoji主名替换与实际提交不破坏UTF-16范围 | PASS（最终Release E-RENAME） |
| RENAME-07 | 含点目录全选，改名保持子项字节与身份 | PASS（最终Release E-RENAME） |
| RENAME-08 | Ctrl+A显式全名替换，包括扩展名 | PASS（最终Release E-RENAME） |
| RENAME-09 | Delete只改变选中文本，原文件/身份/列表不删 | PASS（最终Release E-RENAME） |
| RENAME-10 | 顶部Rename入口初选一致，取消后回当前文件列表而非侧栏 | PASS（最终Release E-RENAME） |
| RENAME-11 | NewFolder保持默认全选，只创建自有新目录 | PASS（最终Release E-RENAME） |
| RENAME-12 | 真实索引搜索F2先定位父目录，再共享Rename及取消回FolderEntries | PASS（最终Release E-RENAME） |
| RENAME-13 | 保留实际名称编辑器与未提交草稿，正常关闭主窗口 | PASS（最终Release E-RENAME） |
| RENAME-14 | 自有窗口退出0，不force；非零退出独立FAIL | PASS（最终Release E-RENAME） |
| RENAME-15 | 精确归属、身份及UTF-8字节核对后清理 | PASS（最终Release E-RENAME） |

搜索F2沿原验证/定位父目录管线，不要求Esc返回已离开的SearchResults。共享Dismissed完成通知现重算命令能力，且在没有其它操作对话框时恢复当前可见文件/搜索列表焦点；实际FocusedElement已由E-RENAME #02/#10/#12限定证明，不以回调代码代验或推断完整焦点环。宿主关闭前先Hide名称对话框再移交列表焦点，编辑中关闭已由#13/#14实测exit0，不能将其外推为所有原生编辑器或后台关闭场景均已验证。G05a只验证列表回焦；成功后精确新行的选择/可见性另见G05b；完整G05/P0、F3/F6循环、IME及平台/主题/DPI矩阵继续待续。

2026-11-11实机问题修正补记：名称对话框取消后，顶部New/Sort曾保留模态期间的禁用状态；共享`Dismissed`通知在`ShowAsync`完成、`closed=true`之后统一调用`SyncCommandBarState`，从真实当前能力重算，不按入口硬设启用。另发现从工具栏Rename取消后焦点落到侧栏；同一完成回调在没有其它操作对话框时，把焦点交给当前实际可见的`SearchResults`或`FolderEntries`，不再只依赖ContentDialog返回旧焦点。搜索F2已导航父目录，故其取消后当前列表仍是`FolderEntries`。上述修正已在完整15项Release报告的实际取消/能力/焦点场景中通过；该结果不代表完整焦点环或其它环境均已验收。此G05a回焦结果不包含新名项继续选中；该行为由G05b另行验证。


发布核验已完成：原agent默认窗口PID10988正常exit0，候选发布的四项关键产物SHA与测试版本一致；新默认窗口PID7108可见并位于前台。主模型最终复核249项原生源码/11项保留产物、两配置1578/0/4、378资源及同一Release15项GUI均通过。此发布记录与测试PID29940分别记录，不把新开窗口当作再跑完整GUI或全部环境验收。

### G05b 改名后选中与滚动

最终Release同一候选的[窗口报告](../.build/explorer-rename-reveal/integration/ui-rename-selection-report.json)17 PASS / 0 FAIL，测试进程exit0、自有夹具已清。七次提交均读取到真实新行选中与可见：普通文件、中文和emoji、含点目录、Ctrl+A改全名、真实搜索F2定位父目录、272项分批目录中同身份的硬链接改名、仅改大小写。大目录场景从列表首屏改到排序末尾，脚本提交后不选中或滚动；另核对未改的硬链接和子项身份/字节。

双配置各1583 PASS / 0 FAIL / 4既有权限SKIP；新增五项覆盖精确链接、监听先刷新、同名替换、隐藏与目录身份变化。动态切标签或另选发生在待完成改名中的组合、真实IME/UNC、完整主题/DPI/多屏/读屏及F3/F6区域焦点不计入通过。G05a历史15项仍保留各自镜像和报告；当前脚本`run-blink-rename-selection-smoke.ps1`验证G05b，默认指向当前Release。

### G04a F3与区域焦点

最终同一Release完整23项窗口全PASS，结果见[焦点报告](../.build/explorer-focus/integration/ui-focus-report.json)与[交接](EXPLORER_KEYBOARD_FOCUS_HANDOFF.md)。覆盖ThisPC、目录/空目录、非空与零结果搜索正反六区域循环，F3/别名全选且不提交、地址草稿、文件/搜索选择、命名弹窗、原生高级文本输入、标签草稿隔离及编辑态正常关窗。测试进程正常退出0，44字节自有夹具清理。

首候选的锁屏与“仅原生更多可用时跳过命令栏”分别保留证据；修正后重新完成双配置1583/0/4与全部23项，未拼接旧候选的17项。真实IME/UNC、全主题/DPI/多屏/读屏及所有菜单/ComboBox组合不计入通过。复现入口为`scripts/run-blink-focus-smoke.ps1`，默认执行完整检查；`-Phase Remaining`只补后六项，单独结果不能称完整23项。

## 8. 可复现执行约定与工作台归属

以下为README的真实构建/普通回归入口。G01记录归B-NATIVE，G02历史结果归C-NATIVE；G03a最终两配置各1565/0/4归D-NATIVE，本文更新代理没有另跑：

```powershell
.\build.ps1 -Configuration Debug
& .\x64\Debug\Blink.Phase1.Tests.exe
.\build.ps1 -Configuration Release
& .\x64\Release\Blink.Phase1.Tests.exe
```

G01公开可重复入口是 [run-blink-explorer-view-menu-smoke.ps1](../scripts/run-blink-explorer-view-menu-smoke.ps1)。默认 `-Configuration Release` 使用项目标准 runtime；以候选重现最终 UI 检查可执行：

```powershell
.\scripts\run-blink-explorer-view-menu-smoke.ps1 -Binary .\.build\explorer-ui-p0-view-menu\runtime\Release\Blink.exe
```

G01历史报告归`.build/explorer-ui-p0-view-menu/integration/ui-view-menu-report.json`；脚本已随G02适配AppBarButton，重新执行属于新候选验证，不能当作原报告字节重现。`scripts/run-blink-view-smoke.ps1`、`scripts/run-blink-visibility-smoke.ps1` 已适配新入口并通过本轮 Parser，但未执行的旧 smoke 不能因语法通过改写成 GUI PASS。历史普通/搜索菜单脚本结果仍保留各自候选 SHA/范围。

G02公开入口为[run-blink-command-bar-smoke.ps1](../scripts/run-blink-command-bar-smoke.ps1)，默认`-Configuration Release`使用标准runtime；候选运行示例：

```powershell
.\scripts\run-blink-command-bar-smoke.ps1 -Configuration Release -Binary .\.build\explorer-command-bar\runtime\Release\Blink.exe
```

G02历史报告归`.build/explorer-command-bar/integration/ui-command-bar-report.json`；G03a最终同镜像重跑的命令18项、查看15项分别独立归当前`.build/explorer-address-bar/integration/ui-command-bar-compat-report.json`和`ui-view-compat-report.json`，均PASS，不套历史报告。G01历史归`.build/explorer-ui-p0-view-menu`；当前三套报告均记录同一CF1D... SHA及各自PID/HWND/环境/清理。编译单并发，未知耗时按async-task-watcher管理；不启动/结束用户窗口、不切换或绕过锁屏、不安装第三方程序。

G03a入口为[run-blink-address-bar-smoke.ps1](../scripts/run-blink-address-bar-smoke.ps1)；最终Release20/0/0，旧Debug19/0/0仅preflight。按本机zh-CN已确认的overflow身份复现最终Release入口：

```powershell
.\scripts\run-blink-address-bar-smoke.ps1 -Configuration Release -Binary .\src\Blink\x64\Release\Blink.exe -NativeOverflowName '更多'
```

脚本的`NativeOverflowId/NativeOverflowName`只传自有窗口probe已确认的精确身份，其他语言/环境先探测，不猜名称。最终Release报告为`.build/explorer-address-bar/integration/ui-address-bar-report.json`，旧Debug preflight独立保留`ui-address-bar-debug-report.json`，两者不混算。fixture/compiler-temp各在integration独立子目录；最终Release正常exit0、无强杀、366字节fixture已删、compiler-temp已清但字节未记录，不套用给未运行候选。

G01当时View脚本不使用剪贴板，也不提交文件变更命令；只生成并准确核对自有 fixture 路径/UTF-8 字节/属性，未知数据保留并报告，正常关闭后清理。其他含剪贴板的历史/后续检查仍应核对自有 Copy/Cut sequence/完整路径/intent；用户覆盖时不触发依赖动作，只在仍属测试时恢复可恢复内容，不把纯文本恢复称为富格式无损恢复。失败 attempt 只留简明根因/必要证据，不无限复制二进制/源码包。

本文归属 `D:\Work\project\Blink_文件管理器\docs`（WIN）；文档代理仅同步三份文档，不另建产物或运行构建/GUI。最终同镜像三套GUI53项通过，fixture分别366/3152/1015字节已清、compiler-temp已清但释放字节未记录；D-CMD剪贴板恢复true，D-CLOSE伴随窗口正常关闭/dispose。旧Debug52项和G01/G02历史各自保留，不合并计数。默认发布状态见相应交接，本文不先记发布成功，也不由PASS推断默认EXE或窗口PID。
