# Explorer 菜单与命令矩阵

2026-11-12审计同步：C06常用位置已有G06a菜单，G06c新建/改名/删除有实现但分功能脚本尚待实跑；保持已实现与已验证的区别。见[审计](DEVELOPMENT_AUDIT.md)。

G06b最新（2026-11-11）：目录目标Cut/Copy/Paste已接入原队列，根仅Paste、虚拟ThisPC无传输；双配置1636/0/4、13项不写Clipboard窗口检查PASS。实际传输UI SKIP，因此相应动作仍明确限定，见[交接](EXPLORER_NAVIGATION_TRANSFER_HANDOFF.md)。

G06a最新（2026-11-11）：新增导航/ThisPC目标菜单，双配置1618/0/4、249输入/386资源及最终10项窗口检查通过；C07/C08仅升级本切片覆盖的动作。系统属性仅入口/真实目标捕获已验，实际属性页、剪贴板写入、鼠标及完整环境待验。下方G02等段为各自历史证据，见[交接](EXPLORER_OBJECT_MENU_HANDOFF.md)。

阶段A只读审计基线为`7135a14`，审计文档提交为`e497600`。2026-11-09更新到G02标准CommandBar当前实现：240冻结输入哈希匹配、375套中英/fallback资源；Hidden/Search修正后Debug/Release完整构建成功，普通回归各1542 PASS / 0 FAIL / 4符号链接权限1314 SKIP。新增共26项检查：23项命令栏能力/选择模型、3项搜索生命周期。修正后Release完整18项GUI已18 PASS/0 FAIL/0 SKIP；只将实际覆盖的24条顶部入口升级，4个未点击排序叶仍`IMPLEMENTED_NEEDS_MANUAL`。

G01历史候选为235输入/369资源、两配置各1516/0/4、Release自有窗口15 PASS/0 FAIL/0 SKIP；下方保留这份有效证据。G02将查看host由DropDownButton换为AppBarButton，G01历史结果不自动证明新host、原生overflow或新增文件动作。

此前目录合并移动8项UI检查、普通目录21项与搜索18项菜单检查继续作为各自范围的历史证据，不冒充本轮重跑。这里仅修改菜单台账；本次文档更新未改产品、构建或运行窗口。

目标为Windows 11 24H2 x64；24H2菜单实测 **NOT RUN**。主模型本轮另取得25H2 `26200.9711`、zh-CN、DPI120（125%）自有Explorer窗口的 **View/Show UIA结构**，只核对这两层且未改系统开关。本轮Blink GUI报告为同一25H2版本、zh-CN、`window_dpi=96`；主题未记录，完整主题/DPI/语言矩阵未跑。不同窗口DPI与无popup截图不能当同条件视觉比对，不能据此推广到24H2、所有右键或第三方扩展。


## 读表约定

- `IMPLEMENTED_VERIFIED`：代码入口存在，且有表中明确的既有自动化/受控UI证据覆盖所写范围；仍未完成24H2全环境人工比对。
- `IMPLEMENTED_NEEDS_MANUAL`：可见入口与代码绑定存在，目标场景或完整动作尚缺对应UI验收。
- `PARTIAL`：已有部分模型/动作，但所列入口、位置、对象适配或完整交互缺失；绝不把“有后端”当可见可用。
- `DEFERRED`：目标条目未接入。矩阵保留缺口，产品不应放空可操作项。
- `UNSUPPORTED_BY_DESIGN`：当前明确边界，不是隐藏完成率。
- `OS_DEPENDENT`：已走系统/默认控件机制，具体条目、内容、可用性由版本/关联/注册/对象决定；不承诺某品牌或全量现代扩展。

“实现方式”仅使用Blink / Shell / 系统跳转。无可见入口时写预期归属并明确尚未接入，不代表已存在系统跳转。图标记录当前实际或“未接入”，不能拿建议图标冒充现状。行中的矩阵M-编号是审计定位号，非新增产品Command ID；`NOT RUN: ...` 是拟定验收ID，不是PASS。目标英文未实机比对；已有项文字直接取当前两套Resources.resw。Toggle勾选、分隔线、容器箭头记在树与条件中，不计作独立可执行命令。相同条件的尚缺命令族合并登记；共用本地菜单叶只在C01列全，其他场景引用这棵树并单列适用性差异，避免复制同一源码。

## 当前树与目标树

当前顶部为标准WinUI `CommandBar`：8个主AppBarButton，New/Sort/View真实菜单，单个系统原生MoreButton承载7个SecondaryCommands和窄宽动态溢出。命令行容器横跨左右窗格，bar本体按内容期望宽度左对齐；不宣称其bounds铺满窗口。旧视图/排序Combo、独立Hidden和方向按钮已移除。文件右键与背景右键不同；导航树、标签和列头仍按各自缺口登记。

~~~text
当前顶部（G02已实现，18项有限GUI通过；4个排序叶待验）
├─ 新建 #CommandNew > 新建文件夹 #TopNewFolder
├─ 剪切 / 复制 / 粘贴 / 重命名 / 删除（标准AppBarButton）
├─ 排序 #CommandSort > 名称 / 修改时间 / 类型 / 大小；升序 / 降序
├─ 查看 #FolderViewMode > 网格 / 列表 / 详细信息；显示 > 隐藏的项目
└─ 系统MoreButton“…” → #OverflowPopup（Popup，UIA Window）
   ├─ 全选 / 取消选择 / 反向选择；属性 / 刷新；复制完整路径 / 复制所在目录路径
   └─ 窄宽时动态移入的主命令（没有第二个More按钮）
当前普通文件/目录/多选右键
├─ 打开
├─ ─ 剪切 / 复制 / 粘贴（当前目录）
├─ ─ 复制完整路径 / 复制所在目录路径
├─ ─ 重命名 / 删除 / 属性（Blink只读）
├─ ─ 压缩与更多系统选项…（仅真实普通目录选择）
├─ ─ 刷新
├─ 查看 > 详细信息 / 列表 / 网格（互斥勾选）
└─ 排序 > 名称 / 类型 / 大小 / 修改时间；升序 / 降序（勾选）
当前文件列表背景：粘贴 / 新建文件夹；刷新；查看>3项；排序>6项
当前搜索结果：打开；剪切/复制/粘贴到所在目录；路径两项/定位；
              重命名/删除/系统属性；压缩与更多系统选项…
目标顶部（存在/缺口逐行见表，不是全部已显示）
├─ 新建 > 文件夹 / 快捷方式 / ShellNew动态注册模板
├─ 剪切 / 复制 / 粘贴 / 重命名 / 共享 / 删除
├─ 排序 > 名称/修改时间/类型/大小/升降序；其他属性
├─ 分组依据 > 无/名称/日期/类型/大小/组方向/展开折叠
├─ 查看 > 现有详细信息/列表/网格 + 尚缺图标尺寸/平铺/内容
│         详细信息窗格 / 预览窗格（25H2观测直属；24H2待核）
│         显示 > 导航窗格/紧凑视图/项目复选框/文件扩展名/隐藏项目
└─ 更多 > 全选/取消/反选/属性/选项/系统设置/上下文动态动作
~~~

G01曾对三视图、隐藏项与四个命令叶完成15项GUI，历史证据仍保留。G02保留三真实View模式及窗口级Hidden，新增标准栏与真实命令组织；策略按当前Folder/ThisPC/Search、选择和剪贴板推导，打开菜单/overflow捕获上下文并在动作时重验。Search粘贴改名“粘贴到所在目录”，ThisPC不继承旧目录目标。Share、ShellNew、分组、面板、列管理和未实现视图仍保留缺口，既有索引/导航/文件操作/ShellHost不为展示层另起模型。

## 场景与状态计数

统计单位为审计行/命令族，不是每个UI叶子个数；引用共用树的场景仍有自己的状态与证据，不能据此算功能百分比。动态行也不等于某次系统返回的命令数。移除已被G02排序菜单替换的5行旧排序控件记录，新增More中的刷新及两项路径复制；不重复统计已删除控件。

| 场景 | 行数 | IMPLEMENTED_VERIFIED | IMPLEMENTED_NEEDS_MANUAL | PARTIAL | DEFERRED | UNSUPPORTED_BY_DESIGN | OS_DEPENDENT |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| T-CORE 当前顶部常用命令区及共享缺口 | 6 | 5 | 0 | 0 | 1 | 0 | 0 |
| T-NEW 当前顶部新建及模板缺口 | 4 | 2 | 0 | 0 | 2 | 0 | 0 |
| T-SORT 当前排序及分组缺口 | 9 | 3 | 4 | 0 | 2 | 0 | 0 |
| T-VIEW 当前查看/显示及其余目标缺口 | 14 | 6 | 0 | 2 | 5 | 1 | 0 |
| T-MORE 当前原生更多及后续缺口 | 15 | 8 | 0 | 0 | 6 | 1 | 0 |
| C01 普通文件单选 | 26 | 16 | 4 | 0 | 0 | 0 | 6 |
| C02 普通目录单选 | 9 | 4 | 1 | 0 | 3 | 0 | 1 |
| C03 多文件、多目录、混合选择 | 11 | 6 | 5 | 0 | 0 | 0 | 0 |
| C04 文件列表真正空白 | 11 | 4 | 0 | 1 | 5 | 1 | 0 |
| C05 搜索结果 | 14 | 9 | 2 | 1 | 2 | 0 | 0 |
| C06 导航常用/固定项 | 6 | 0 | 0 | 3 | 3 | 0 | 0 |
| C07 导航普通目录 | 11 | 0 | 0 | 9 | 2 | 0 | 0 |
| C08 此电脑驱动器条目 | 11 | 2 | 0 | 2 | 7 | 0 | 0 |
| C09 此电脑背景与虚拟位置 | 9 | 1 | 0 | 3 | 5 | 0 | 0 |
| C10 回收站根与内容 | 6 | 0 | 0 | 0 | 6 | 0 | 0 |
| C11 UNC/SMB与远端Provider | 6 | 0 | 0 | 1 | 4 | 1 | 0 |
| C12 标签、地址、搜索输入、列头 | 24 | 3 | 4 | 0 | 12 | 0 | 5 |
| DYN 系统动态子菜单与执行机制 | 12 | 3 | 0 | 1 | 1 | 0 | 7 |
| 合计 | 204 | 72 | 20 | 23 | 66 | 4 | 19 |

## T-CORE 当前顶部常用命令与共享缺口

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T-CORE | 当前命令栏 > 剪切 | 剪切 | Cut | Fluent E8C6；标准AppBarButton | 当前Folder选择沿原policy；Search仅当前有效单结果；ThisPC禁用。模态/导航/目标/队列能力按动作重验 | ExplorerCommandBarAction::Cut；UIA CommandCut；复用FolderCommand/Search动作 | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [F-CMD] [CB-UI] | [CB-UI] #05真实单文件MOVE提交及留源/移除；#02混选启用态；#14 Search仅启用态；纯策略见[CB-TESTS] |
| T-CORE | 当前命令栏 > 复制 | 复制 | Copy | Fluent E8C8；标准AppBarButton | 当前Folder选择沿原policy；Search仅当前有效单结果；ThisPC禁用。模态/导航/目标/队列能力按动作重验 | ExplorerCommandBarAction::Copy；UIA CommandCopy；复用FolderCommand/Search动作 | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [F-CMD] [CB-UI] | [CB-UI] #04真实单文件COPY提交及字节；#02混选启用态；#14 Search仅启用态；纯策略见[CB-TESTS] |
| T-CORE | 当前命令栏 > 粘贴 | 粘贴（搜索时为粘贴到所在目录） | Paste (Paste to containing folder in Search) | Fluent E77F；标准AppBarButton | CF_HDROP及有效当前目标；Folder为当前目录，Search明确“粘贴到所在目录”；ThisPC/旧浏览目录不可作目标 | ExplorerCommandBarAction::Paste；UIA CommandPaste；复用FolderCommand/Search动作 | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [F-CMD] [CB-UI] | [CB-UI] #04/#05当前目录COPY/MOVE；#14真实Search结果PasteParent与字节，不用旧浏览目录；纯策略见[CB-TESTS] |
| T-CORE | 当前命令栏 > 重命名 | 重命名 | Rename | Fluent E8AC；标准AppBarButton | 当前Folder选择沿原policy；Search仅当前有效单结果；ThisPC禁用。模态/导航/目标/队列能力按动作重验；普通目录仅单选可改名 | ExplorerCommandBarAction::Rename；UIA CommandRename；复用FolderCommand/Search动作 | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [F-CMD] [CB-UI] | [CB-UI] #06普通目录单文件改名及字节；#14 Search仅启用态；纯策略见[CB-TESTS] |
| T-CORE | 当前命令栏 > 删除 | 删除 | Delete | Fluent E74D；标准AppBarButton | 当前Folder选择沿原policy；Search仅当前有效单结果；ThisPC禁用。模态/导航/目标/队列能力按动作重验；Shift走已有永久删除确认，不默默降级；G02仅Shift永久分支实测，普通回收站本轮NOT RUN | ExplorerCommandBarAction::Delete；UIA CommandDelete；复用FolderCommand/Search动作 | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [F-CMD] [CB-UI] | [CB-UI] #07仅Shift永久删除取消/明确确认分支；#14 Search仅启用态；普通回收站本轮NOT RUN；纯策略见[CB-TESTS] |
| T-CORE | 目标命令栏 > 共享 | 共享 | Share | 未接入 | 无静态共享按钮；系统扩展见DYN | —（尚未定义） | Blink | DEFERRED | [TOP] [HOST] | NOT RUN: M-T-CORE-06 |

## T-NEW 当前顶部新建与模板缺口

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T-NEW | 当前命令栏 > 新建 | 新建 | New | Fluent E710；AppBarButton下拉 | Opening按当前Folder上下文重建；仅真实NewFolder，Search/ThisPC及互斥状态禁用 | UIA CommandNew / TopNewMenu | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [F-CMD] [CB-UI] | [CB-UI] #03真实New菜单/唯一文件夹叶及原名称对话框，精确自有空目录；#13/#14虚拟/Search禁用；纯策略见[CB-TESTS] |
| T-NEW | 当前命令栏 > 新建 > 文件夹 | 新建文件夹 | New folder | 复用既有新建文件夹Fluent图标 | Opening按当前Folder上下文重建；仅真实NewFolder，Search/ThisPC及互斥状态禁用 | FolderCommand::NewFolder；UIA TopNewFolder | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [F-CMD] [CB-UI] | [CB-UI] #03真实New菜单/唯一文件夹叶及原名称对话框，精确自有空目录；#13/#14虚拟/Search禁用；纯策略见[CB-TESTS] |
| T-NEW | 目标命令栏 > 新建 > 快捷方式 | 快捷方式 | Shortcut | 未接入 | 无创建快捷方式UI或命令，不等同打开已有lnk | —（尚未定义） | Shell | DEFERRED | [TOP] [HOST] | NOT RUN: M-T-NEW-03 |
| T-NEW | 目标命令栏 > 新建 > 系统模板 > 文本及注册类型 | 系统注册的新建类型（动态） | Registered ShellNew templates (dynamic) | 系统注册图标；尚未接入 | 必须来自ShellNew/背景对象；当前host只获取选中项IContextMenu | —（尚无ShellNew枚举入口） | Shell | DEFERRED | [HOST] [TOP] | NOT RUN: M-T-NEW-04 |

## T-SORT 当前排序与分组缺口

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T-SORT | 当前命令栏 > 排序 | 排序 | Sort | Fluent E8CB；AppBarButton下拉 | 仅当前可排序Folder；按Tab权威sort勾选。Search/ThisPC禁用；Opening捕获上下文，调用重验 | UIA CommandSort / TopSortMenu | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [SORT] [CB-UI] | [CB-UI] #08仅Size/Descending实际顺序及唯一勾选；#13/#14 ThisPC/Search禁用；其余4排序叶未点击 |
| T-SORT | 当前命令栏 > 排序 > 名称 | 名称 | Name | 复用既有Fluent图标；Toggle勾选 | 仅当前可排序Folder；按Tab权威sort勾选。Search/ThisPC禁用；Opening捕获上下文，调用重验 | FolderCommand::SortName；UIA TopSortName | Blink | IMPLEMENTED_NEEDS_MANUAL | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [SORT] | command_bar_search_does_not_offer_fake_folder_projection_or_selection_modes；command_bar_this_pc_never_inherits_file_actions_from_old_folder_selection；本轮未点击该排序叶；保留GUI待验 [CB-GUI] |
| T-SORT | 当前命令栏 > 排序 > 修改时间 | 修改时间 | Modified | 复用既有Fluent图标；Toggle勾选 | 仅当前可排序Folder；按Tab权威sort勾选。Search/ThisPC禁用；Opening捕获上下文，调用重验 | FolderCommand::SortModified；UIA TopSortModified | Blink | IMPLEMENTED_NEEDS_MANUAL | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [SORT] | command_bar_search_does_not_offer_fake_folder_projection_or_selection_modes；command_bar_this_pc_never_inherits_file_actions_from_old_folder_selection；本轮未点击该排序叶；保留GUI待验 [CB-GUI] |
| T-SORT | 当前命令栏 > 排序 > 类型 | 类型 | Type | 复用既有Fluent图标；Toggle勾选 | 仅当前可排序Folder；按Tab权威sort勾选。Search/ThisPC禁用；Opening捕获上下文，调用重验 | FolderCommand::SortType；UIA TopSortType | Blink | IMPLEMENTED_NEEDS_MANUAL | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [SORT] | command_bar_search_does_not_offer_fake_folder_projection_or_selection_modes；command_bar_this_pc_never_inherits_file_actions_from_old_folder_selection；本轮未点击该排序叶；保留GUI待验 [CB-GUI] |
| T-SORT | 当前命令栏 > 排序 > 大小 | 大小 | Size | 复用既有Fluent图标；Toggle勾选 | 仅当前可排序Folder；按Tab权威sort勾选。Search/ThisPC禁用；Opening捕获上下文，调用重验 | FolderCommand::SortSize；UIA TopSortSize | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [SORT] [CB-UI] | [CB-UI] #08仅Size/Descending实际顺序及唯一勾选；#13/#14 ThisPC/Search禁用；其余4排序叶未点击 |
| T-SORT | 当前命令栏 > 排序 > 升序 | 升序 | Ascending | 复用既有Fluent图标；Toggle勾选 | 仅当前可排序Folder；按Tab权威sort勾选。Search/ThisPC禁用；Opening捕获上下文，调用重验 | FolderCommand::SortAscending；UIA TopSortAscending | Blink | IMPLEMENTED_NEEDS_MANUAL | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [SORT] | command_bar_search_does_not_offer_fake_folder_projection_or_selection_modes；command_bar_this_pc_never_inherits_file_actions_from_old_folder_selection；本轮未点击该排序叶；保留GUI待验 [CB-GUI] |
| T-SORT | 当前命令栏 > 排序 > 降序 | 降序 | Descending | 复用既有Fluent图标；Toggle勾选 | 仅当前可排序Folder；按Tab权威sort勾选。Search/ThisPC禁用；Opening捕获上下文，调用重验 | FolderCommand::SortDescending；UIA TopSortDescending | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [SORT] [CB-UI] | [CB-UI] #08仅Size/Descending实际顺序及唯一勾选；#13/#14 ThisPC/Search禁用；其余4排序叶未点击 |
| T-SORT | 目标命令栏 > 排序 > 其他属性 | 其他属性 | More properties | 未接入 | 当前只支持Name/Type/Size/Modified | —（尚未定义） | Blink | DEFERRED | [TOP] [SORT] | NOT RUN: M-T-SORT-08 |
| T-SORT | 目标命令栏 > 分组依据 > 字段/方向/组操作 | 无/名称/修改日期/类型/大小；升降序；展开折叠 | None / Name / Date modified / Type / Size; direction; expand/collapse | 见共用树；缺口未显示 | 完整目标子树在上方；当前没有分组模型/组标题，不能用Sort冒充 | —（尚未定义） | Blink | DEFERRED | [TOP] [SORT] | NOT RUN: M-TSORT-GROUP |

## T-VIEW 当前查看/显示及其余目标缺口

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T-VIEW | 当前命令栏 > 查看 | 查看 | View | Fluent E8A9；标准AppBarButton下拉 | AppBarButton每次Opening重建三真实视图及Show；Name=Command.View，HelpText=当前模式；Search禁用，ThisPC保留真实View | UIA FolderViewMode / TopViewMenu；PopulateFolderViewMenu | Blink | IMPLEMENTED_VERIFIED | [TOP] [TOP-VIEW] [P0-GUI] [CB-PRESENTATION] [CB-APP] [CB-UI] | G01历史PASS：[P0-GUI]（02 top View is the owned DropDownButton with real exclusive commands and Show；09 stale top View peer cannot change the next tab or its original tab；10 Space arrows Enter and Escape navigate Show using owned keyboard input and return focus）；G02当前AppBarButton host [CB-UI] #08三布局/所选项/排序保留、Hidden开关与SYSTEM排除；#11 overflow内真实View；跨Tab等仍限G01历史 |
| T-VIEW | 当前命令栏 > 查看 > 详细信息 | 详细信息 | Details | Fluent E9D5；Toggle互斥勾选 | 保留G01模型及菜单语义；每次打开按ActiveState勾选；原FolderCommand入口切换真实布局，保持路径/选择/顺序/排序，独立标签视图；空目录/ThisPC已测；当前AppBarButton host按G02 #08/#11限定验证 | FolderCommand::ViewDetails；UIA TopViewDetails；矩阵 M-TOP-VIEW-DETAILS | Blink | IMPLEMENTED_VERIFIED | [TOP-VIEW] [P0-GUI] [CB-PRESENTATION] [CB-APP] [CB-UI] | G01历史PASS：[P0-GUI]（03 Details List and Grid change actual layout while preserving path selection order and sort；04 ordinary ContextView command and top View selection stay synchronized；07 two tabs independently retain real Grid and List layouts and paths；11 empty local directory supports actual layouts and Show without stale rows；12 ThisPC offers real view choices and retains virtual location semantics）；G02当前AppBarButton host [CB-UI] #08三布局/所选项/排序保留、Hidden开关与SYSTEM排除；#11 overflow内真实View；跨Tab等仍限G01历史 |
| T-VIEW | 当前命令栏 > 查看 > 列表 | 列表 | List | Fluent EA37；Toggle互斥勾选 | 保留G01模型及菜单语义；每次打开按ActiveState勾选；原FolderCommand入口切换真实布局，保持路径/选择/顺序/排序，独立标签视图；空目录/ThisPC已测；当前AppBarButton host按G02 #08/#11限定验证 | FolderCommand::ViewList；UIA TopViewList；矩阵 M-TOP-VIEW-LIST | Blink | IMPLEMENTED_VERIFIED | [TOP-VIEW] [P0-GUI] [CB-PRESENTATION] [CB-APP] [CB-UI] | G01历史PASS：[P0-GUI]（03 Details List and Grid change actual layout while preserving path selection order and sort；04 ordinary ContextView command and top View selection stay synchronized；07 two tabs independently retain real Grid and List layouts and paths；11 empty local directory supports actual layouts and Show without stale rows；12 ThisPC offers real view choices and retains virtual location semantics）；G02当前AppBarButton host [CB-UI] #08三布局/所选项/排序保留、Hidden开关与SYSTEM排除；#11 overflow内真实View；跨Tab等仍限G01历史 |
| T-VIEW | 当前命令栏 > 查看 > 网格 | 网格 | Grid | Fluent E80A；Toggle互斥勾选 | 保留G01模型及菜单语义；每次打开按ActiveState勾选；原FolderCommand入口切换真实布局，保持路径/选择/顺序/排序，独立标签视图；空目录/ThisPC已测；当前AppBarButton host按G02 #08/#11限定验证 | FolderCommand::ViewGrid；UIA TopViewGrid；矩阵 M-TOP-VIEW-GRID | Blink | IMPLEMENTED_VERIFIED | [TOP-VIEW] [P0-GUI] [CB-PRESENTATION] [CB-APP] [CB-UI] | G01历史PASS：[P0-GUI]（03 Details List and Grid change actual layout while preserving path selection order and sort；04 ordinary ContextView command and top View selection stay synchronized；07 two tabs independently retain real Grid and List layouts and paths；11 empty local directory supports actual layouts and Show without stale rows；12 ThisPC offers real view choices and retains virtual location semantics）；G02当前AppBarButton host [CB-UI] #08三布局/所选项/排序保留、Hidden开关与SYSTEM排除；#11 overflow内真实View；跨Tab等仍限G01历史 |
| T-VIEW | 目标命令栏 > 查看 > 详细信息窗格 | 详细信息窗格 | Details pane | 未接入 | 25H2实测为查看直属项；24H2位置NOT RUN；当前没有面板 | —（尚未定义） | Blink | DEFERRED | [TOP] [VIEW] [EXP25] | NOT RUN: M-T-VIEW-11 |
| T-VIEW | 目标命令栏 > 查看 > 预览窗格 | 预览窗格 | Preview pane | 未接入 | 25H2实测为查看直属项；24H2位置NOT RUN；当前没有面板 | —（尚未定义） | Blink | DEFERRED | [TOP] [VIEW] [EXP25] | NOT RUN: M-T-VIEW-12 |
| T-VIEW | 当前命令栏 > 查看 > 显示 | 显示 | Show | Fluent E890；系统子菜单箭头 | 保留G01模型及菜单语义；当前仅包含Hidden items；没有导航窗格/紧凑视图/复选框/扩展名占位；Space/方向键/Enter/Escape有G01历史报告限定验证；当前AppBarButton host按G02 #08/#11限定验证 | UIA TopViewShow；MenuFlyoutSubItem | Blink | IMPLEMENTED_VERIFIED | [TOP-VIEW] [P0-GUI] [EXP25] [CB-PRESENTATION] [CB-APP] [CB-UI] | G01历史PASS：[P0-GUI]（02 top View is the owned DropDownButton with real exclusive commands and Show；05 Show Hidden reveals actual hidden file and directory but excludes both SYSTEM forms；10 Space arrows Enter and Escape navigate Show using owned keyboard input and return focus）；G02当前AppBarButton host [CB-UI] #08三布局/所选项/排序保留、Hidden开关与SYSTEM排除；#11 overflow内真实View；跨Tab等仍限G01历史 |
| T-VIEW | 目标命令栏 > 查看 > 显示 > 导航窗格 | 导航窗格 | Navigation pane | 未接入 | 现有导航窗格常驻，无显隐开关；25H2位于Show | —（尚未定义） | Blink | PARTIAL | [NAV] [EXP25] | NOT RUN: M-T-VIEW-14 |
| T-VIEW | 目标命令栏 > 查看 > 显示 > 紧凑视图 | 紧凑视图 | Compact view | 未接入 | 25H2位于Show；当前模板固定行高，无紧凑模式 | —（尚未定义） | Blink | DEFERRED | [VIEW] [EXP25] | NOT RUN: M-T-VIEW-15 |
| T-VIEW | 目标命令栏 > 查看 > 显示 > 项目复选框 | 项目复选框 | Item check boxes | 未接入 | 现用ListView选择模型，无项目复选框设置 | —（尚未定义） | Blink | DEFERRED | [VIEW] [EXP25] | NOT RUN: M-T-VIEW-16 |
| T-VIEW | 目标命令栏 > 查看 > 显示 > 文件扩展名 | 文件扩展名 | File name extensions | 未接入 | 当前始终显示完整entry.name，无可切换扩展名隐藏策略 | —（尚未定义） | Blink | PARTIAL | [VIEW] [EXP25] | NOT RUN: M-T-VIEW-17 |
| T-VIEW | 当前命令栏 > 查看 > 显示 > 隐藏项目 | 隐藏的项目 | Hidden items | Fluent E8A7；Toggle勾选 | 保留G01模型及菜单语义；窗口级VisibilityPolicy作用既有/新标签；隐藏时裁剪隐藏选择且不复活；SYSTEM两种形式继续排除；Hidden后Search可重启及PasteParent已在#08→#14串联通过；树展开/搜索隐藏结果显隐联动仍NOT RUN；当前AppBarButton host按G02 #08/#11限定验证 | SetShowHiddenItems；UIA TopViewShowHidden；矩阵 M-TOP-VIEW-SHOW-HIDDEN | Blink | IMPLEMENTED_VERIFIED | [TOP-VIEW] [HIDDEN] [P0-GUI] [CB-PRESENTATION] [CB-APP] [CB-UI] | G01历史PASS：[P0-GUI]（05 Show Hidden reveals actual hidden file and directory but excludes both SYSTEM forms；06 hiding items removes hidden selection without resurrecting it；08 Hidden is synchronized across existing tabs and inherited by a new tab；10 Space arrows Enter and Escape navigate Show using owned keyboard input and return focus）；G02当前AppBarButton host [CB-UI] #08三布局/所选项/排序保留、Hidden开关与SYSTEM排除；#11 overflow内真实View；跨Tab等仍限G01历史 |
| T-VIEW | 范围边界 > 受保护系统项目 | 受保护系统项目 | Protected operating system files | 不显示入口 | 现有策略始终排除SYSTEM属性；不是隐藏项开关的一部分 | ItemVisibilityPolicy::Includes | Blink | UNSUPPORTED_BY_DESIGN | [HIDDEN] | visibility_show_hidden_does_not_expose_system_rows |
| T-VIEW | 目标命令栏 > 查看 > 尚缺的视图模式 | 超大/大/中/小图标、平铺、内容 | Extra large / Large / Medium / Small icons; Tiles; Content | 见共用树；缺口未显示 | 25H2 UIA有六项；Blink Grid是固定136×112，不能映射为六个已实现模式 | —（尚未定义） | Blink | DEFERRED | [VIEW] [EXP25] | NOT RUN: M-TVIEW-MODES |

## T-MORE 当前原生更多与后续缺口

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| T-MORE | 当前命令栏 > … | 更多 | More | 系统原生MoreButton“…”；没有重复自建按钮 | CommandBar默认overflow；7个真实SecondaryCommands及窄宽主命令；捕获当前上下文，Closed按dispatcher+epoch清理 | 原生CommandBar overflow；实际UIA OverflowPopup / Popup / Window；TopMoreMenu仅源码Style ID未暴露；无AppBarButton(Action.More) | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-TESTS] [CB-UI] | [CB-UI] #09/#10实际More动作；#11 640宽原生动态overflow；#12旧peer；#15定向Space/Escape焦点及真实键盘全选；[CB-OVERFLOW]真实OverflowPopup |
| T-MORE | 当前命令栏 > 更多 > 全选 | 全选 | Select all | Fluent E8B3；Secondary AppBarButton | 仅当前非空Folder，权威可见行；Search/ThisPC禁用，不借用虚拟容器可见项 | ExplorerCommandBarAction::SelectAll；UIA CommandSelectAll | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-TESTS] [CB-UI] | [CB-UI] #09当前Folder全部可见行；#15原生More键盘真实全选；#14 Search禁用 |
| T-MORE | 当前命令栏 > 更多 > 取消选择 | 取消选择 | Clear selection | Fluent E8B4；Secondary AppBarButton | 当前Folder存在权威选择时启用；调用TabState.ClearSelection，不改文本选择 | ExplorerCommandBarAction::ClearSelection；UIA CommandClearSelection | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-TESTS] [CB-UI] | [CB-UI] #09清空当前Folder权威选择；#14 Search禁用 |
| T-MORE | 当前命令栏 > 更多 > 反向选择 | 反向选择 | Invert selection | Fluent E8B5；Secondary AppBarButton | 仅当前非空Folder可见有效行；TabState反选，隐藏/SYSTEM不选，硬链接名称独立；不改导航/视图/排序 | ExplorerCommandBarAction::InvertSelection；UIA CommandInvertSelection | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-TESTS] [CB-UI] | [CB-UI] #09反选补集且二次反选还原；#14 Search禁用；纯模型3相关检查见[CB-TESTS] |
| T-MORE | 当前命令栏 > 更多 > 属性 | 属性 | Properties | Fluent E946；Secondary AppBarButton | Folder沿既有只读属性；当前有效Search结果走系统属性；ThisPC禁用，不假称所有属性可编辑 | ExplorerCommandBarAction::Properties；UIA CommandProperties | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-TESTS] [CB-UI] | [CB-UI] #10普通目录选择的Blink只读属性；Search系统属性本轮未点击 |
| T-MORE | 当前命令栏 > 更多 > 刷新 | 刷新 | Refresh | Fluent E72C；Secondary AppBarButton | Folder/ThisPC沿原刷新；Search重新提交当前查询，无需旧选择；模态禁用 | ExplorerCommandBarAction::Refresh；UIA CommandRefresh | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-UI] | [CB-UI] #10同一已提交目录实际新增文件刷新；Search重新提交分支本轮未点击 |
| T-MORE | 当前命令栏 > 更多 > 复制完整路径 | 复制完整路径 | Copy full path | Fluent E71B；Secondary AppBarButton | 当前Folder权威选择或有效Search单结果；动作时重验上下文，ThisPC禁用 | ExplorerCommandBarAction::CopyFullPath；UIA CommandCopyFullPath | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-UI] | [CB-UI] #09 Folder真实路径；#14实际Search命中原路径复制 |
| T-MORE | 当前命令栏 > 更多 > 复制所在目录路径 | 复制所在目录路径 | Copy containing folder path | Fluent E8B7；Secondary AppBarButton | 复制当前所选项包含目录；不是选中文件夹内部，不使用背后浏览目录 | ExplorerCommandBarAction::CopyParentPath；UIA CommandCopyParentPath | Blink | IMPLEMENTED_VERIFIED | [CB-PRESENTATION] [CB-MODEL] [CB-APP] [CB-UI] | [CB-UI] #09 Folder包含目录真实路径；Search该分支本轮未点击 |
| T-MORE | 目标命令栏 > 更多 > 选项/文件夹选项 | 选项/文件夹选项 | Options / Folder options | 未接入 | 目标入口未实现；不能以文本框编辑撤销替代文件操作撤销 | —（尚未定义） | Blink | DEFERRED | [TOP] | NOT RUN: M-TMORE-OPTIONS |
| T-MORE | 目标命令栏 > 更多 > 打开Windows设置 | 打开Windows设置 | Open Windows Settings | 未接入 | 目标入口未实现；不能以文本框编辑撤销替代文件操作撤销 | —（尚未定义） | Blink | DEFERRED | [TOP] | NOT RUN: M-TMORE-SETTINGS |
| T-MORE | 目标命令栏 > 更多 > 在终端中打开 | 在终端中打开 | Open in Terminal | 未接入 | 目标入口未实现；不能以文本框编辑撤销替代文件操作撤销 | —（无Blink专属入口；动态项另见DYN） | Blink | DEFERRED | [HOST] | NOT RUN: M-TMORE-TERMINAL |
| T-MORE | 目标命令栏 > 更多 > 撤销文件操作 | 撤销文件操作 | Undo file operation | 未接入 | 目标入口未实现；不能以文本框编辑撤销替代文件操作撤销 | —（无可恢复文件操作日志） | Blink | DEFERRED | [F-CMD] | NOT RUN: M-TMORE-UNDO |
| T-MORE | 目标命令栏 > 更多 > 重做文件操作 | 重做文件操作 | Redo file operation | 未接入 | 目标入口未实现；不能以文本框编辑撤销替代文件操作撤销 | —（无可恢复文件操作日志） | Blink | DEFERRED | [F-CMD] | NOT RUN: M-TMORE-REDO |
| T-MORE | 范围边界 > 替换Explorer或修改Win+E | 替换系统文件管理器 | Replace system Explorer | 不显示入口 | 本次兼容任务不修改默认关联/Win+E/注册表；需要独立授权 | — | 系统跳转 | UNSUPPORTED_BY_DESIGN | [SCOPE] | NOT RUN: M-T-MORE-16 |
| T-MORE | 目标命令栏 > 更多 > 对象与系统相关动态动作 | 共享/全部提取/压缩/刻录/固定等 | Share / Extract all / Compress / Burn / Pin etc. | 见共用树；缺口未显示 | 无这些顶部入口；有的可经真实选中项系统菜单动态提供，详见DYN，不能承诺每台机都有 | —（无顶部命令ID）；Shell运行时verb | Shell | DEFERRED | [TOP] [HOST] | NOT RUN: M-TMORE-CONTEXTUAL |

## C01 普通文件单选

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C01 | Blink右键 > 打开 | 打开 | Open | Fluent E8E5 | 单选且目标可解析 | FolderCommand::Open；UIA ContextOpen | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | command_open_selected_committed_row；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 剪切 | 剪切 | Cut | Fluent E8C6 | 已提交普通目录选择；无互斥任务 | FolderCommand::Cut；UIA ContextCut | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | command_multi_copy_respects_owned_queue_busy_state；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 复制 | 复制 | Copy | Fluent E8C8 | 已提交普通目录选择；支持真实文件/目录集合 | FolderCommand::Copy；UIA ContextCopy | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | command_copy_accepts_multiple_selected_committed_rows；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 粘贴 | 粘贴 | Paste | Fluent E77F | CF_HDROP存在且当前目录身份有效；目标是当前目录 | FolderCommand::Paste；UIA ContextPaste | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_single_file_paste_ready_without_selection；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 复制完整路径 | 复制完整路径 | Copy full path | Fluent E71B | 选中完整路径；多选按提交顺序以CRLF拼接 | FolderCommand::CopyFullPath；UIA ContextCopyPath | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_multi_paths_keep_both_rows_without_enabling_multi_rename；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 复制所在目录路径 | 复制所在目录路径 | Copy containing folder path | Fluent E8B7 | 复制当前包含目录一次；非选中文件夹内部 | FolderCommand::CopyParentPath；UIA ContextCopyParentPath | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_path_clipboard_respects_existing_publication_busy_state；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 重命名 | 重命名 | Rename | Fluent E8AC | 仅单选、有效目录身份、无互斥任务 | FolderCommand::Rename；UIA ContextRename | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_single_rename_ready_with_valid_directory_and_target；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 删除 | 删除 | Delete | Fluent E74D | 真实选择；普通Delete入回收站，Shift走永久删除确认 | FolderCommand::Delete；UIA ContextDelete | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | RunDeleteOperationTests；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 属性 | 属性 | Properties | Fluent E946 | 支持文件/目录/混选；Blink只读聚合详情 | FolderCommand::Properties；UIA ContextProperties | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | RunItemPropertiesTests；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 刷新 | 刷新 | Refresh | Fluent E72C | 非呈现/导航忙碌；刷新当前已提交位置 | FolderCommand::Refresh；UIA ContextRefresh | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_this_pc_refresh_supported；H-FOLDER21（范围见证据说明） |
| C01 | Blink右键 > 更多系统选项 | 压缩与更多系统选项… | Compression and more system options… | Fluent E712 | 普通目录真实选择、同父目录；多选完整集合传host；背景/此电脑无入口 | UIA ContextSystemOptions；generic ShellContextMenuController::Show | Shell | IMPLEMENTED_VERIFIED | [F-SYSTEM] [HOST] [MENU-HANDOFF] | folder_shell_menu_accepts_file_and_directory_siblings_together；H-FOLDER21 |
| C01 | Blink右键 > 查看 > 详细信息 | 详细信息 | Details | Fluent E9D5 | 三个Toggle项，当前每标签视图勾选；无未来视图占位 | FolderCommand::ViewDetails；UIA ContextViewDetails | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [VIEW] | view_modes_are_isolated_when_switching_tabs；H-FOLDER21 |
| C01 | Blink右键 > 查看 > 列表 | 列表 | List | Fluent EA37 | 三个Toggle项，当前每标签视图勾选；无未来视图占位 | FolderCommand::ViewList；UIA ContextViewList | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [VIEW] | view_modes_are_isolated_when_switching_tabs；H-FOLDER21 |
| C01 | Blink右键 > 查看 > 网格 | 网格 | Grid | Fluent E80A | 三个Toggle项，当前每标签视图勾选；无未来视图占位 | FolderCommand::ViewGrid；UIA ContextViewGrid | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [VIEW] | view_modes_are_isolated_when_switching_tabs；H-FOLDER21 |
| C01 | Blink右键 > 排序 > 名称 | 名称 | Name | Fluent E8D2 | 普通目录；当前字段/方向Toggle勾选，文件夹优先策略 | FolderCommand::SortName；UIA ContextSortName | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [SORT] | command_folder_sort_supported；H-FOLDER21 |
| C01 | Blink右键 > 排序 > 类型 | 类型 | Type | Fluent E8A5 | 普通目录；当前字段/方向Toggle勾选，文件夹优先策略 | FolderCommand::SortType；UIA ContextSortType | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [SORT] | command_folder_sort_supported；H-FOLDER21 |
| C01 | Blink右键 > 排序 > 大小 | 大小 | Size | Fluent E8F1 | 普通目录；当前字段/方向Toggle勾选，文件夹优先策略 | FolderCommand::SortSize；UIA ContextSortSize | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [SORT] | command_folder_sort_supported；H-FOLDER21 |
| C01 | Blink右键 > 排序 > 修改时间 | 修改时间 | Modified | Fluent E787 | 普通目录；当前字段/方向Toggle勾选，文件夹优先策略 | FolderCommand::SortModified；UIA ContextSortModified | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [SORT] | command_folder_sort_supported；H-FOLDER21 |
| C01 | Blink右键 > 排序 > 升序 | 升序 | Ascending | Fluent E74A | 普通目录；当前字段/方向Toggle勾选，文件夹优先策略 | FolderCommand::SortAscending；UIA ContextSortAscending | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [SORT] | command_folder_sort_supported；H-FOLDER21 |
| C01 | Blink右键 > 排序 > 降序 | 降序 | Descending | Fluent E74B | 普通目录；当前字段/方向Toggle勾选，文件夹优先策略 | FolderCommand::SortDescending；UIA ContextSortDescending | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [SORT] | command_folder_sort_supported；H-FOLDER21 |
| C01 | 文件类型 exe > 更多系统选项 > 以管理员身份运行 | 以管理员身份运行 | Run as administrator | Shell返回图标/owner-draw | 仅选中相应真实文件；仅真实exe且Shell提供runas；Blink无自定义提权菜单 | Shell运行时command offset / canonical verb | Shell | OS_DEPENDENT | [F-SYSTEM] [HOST] | NOT RUN: M-C01-TYPE-EXE |
| C01 | 文件类型 document > 更多系统选项 > 打开方式/编辑/打印 | 打开方式/编辑/打印 | Open with / Edit / Print | Shell返回图标/owner-draw | 仅选中相应真实文件；只在文档关联/注册动词提供时出现 | Shell运行时command offset / canonical verb | Shell | OS_DEPENDENT | [F-SYSTEM] [HOST] | NOT RUN: M-C01-TYPE-DOCUMENT |
| C01 | 文件类型 image > 更多系统选项 > 旋转/设为桌面背景 | 旋转/设为桌面背景 | Rotate / Set as desktop background | Shell返回图标/owner-draw | 仅选中相应真实文件；仅Shell识别图像且该版本/处理程序提供 | Shell运行时command offset / canonical verb | Shell | OS_DEPENDENT | [F-SYSTEM] [HOST] | NOT RUN: M-C01-TYPE-IMAGE |
| C01 | 文件类型 media > 更多系统选项 > 播放及媒体关联动作 | 播放及媒体关联动作 | Play and media association actions | Shell返回图标/owner-draw | 仅选中相应真实文件；实际文件关联决定，不固定播放器 | Shell运行时command offset / canonical verb | Shell | OS_DEPENDENT | [F-SYSTEM] [HOST] | NOT RUN: M-C01-TYPE-MEDIA |
| C01 | 文件类型 script > 更多系统选项 > 运行/编辑 | 运行/编辑 | Run / Edit | Shell返回图标/owner-draw | 仅选中相应真实文件；脚本类型和策略决定；普通Open沿系统关联 | Shell运行时command offset / canonical verb | Shell | OS_DEPENDENT | [F-SYSTEM] [HOST] | NOT RUN: M-C01-TYPE-SCRIPT |
| C01 | 文件类型 shortcut > 更多系统选项 > 打开目标位置/快捷方式属性 | 打开目标位置/快捷方式属性 | Open file location / Shortcut properties | Shell返回图标/owner-draw | 仅选中相应真实文件；动态Shell处理，可能打开Explorer；Blink不自行解析为假目录 | Shell运行时command offset / canonical verb | Shell | OS_DEPENDENT | [F-SYSTEM] [HOST] | NOT RUN: M-C01-TYPE-SHORTCUT |

## C02 普通目录单选

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C02 | Blink右键 > 打开 | 打开 | Open | Fluent E8E5 | 单选且目标可解析 | FolderCommand::Open；UIA ContextOpen | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_open_selected_committed_row；H-FOLDER21（范围见证据说明） |
| C02 | Blink右键 > 重命名 | 重命名 | Rename | Fluent E8AC | 仅单选、有效目录身份、无互斥任务 | FolderCommand::Rename；UIA ContextRename | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_single_rename_ready_with_valid_directory_and_target；H-FOLDER21（范围见证据说明） |
| C02 | Blink右键 > 属性 | 属性 | Properties | Fluent E946 | 支持文件/目录/混选；Blink只读聚合详情 | FolderCommand::Properties；UIA ContextProperties | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | RunItemPropertiesTests；H-FOLDER21（范围见证据说明） |
| C02 | Blink右键 > 更多系统选项 | 压缩与更多系统选项… | Compression and more system options… | Fluent E712 | 普通目录真实选择、同父目录；多选完整集合传host；背景/此电脑无入口 | UIA ContextSystemOptions；generic ShellContextMenuController::Show | Shell | IMPLEMENTED_VERIFIED | [F-SYSTEM] [HOST] [MENU-HANDOFF] | folder_shell_menu_accepts_file_and_directory_siblings_together；H-FOLDER21 |
| C02 | Blink目录右键 > 在新标签页中打开 | 在新标签页中打开 | Open in new tab | 未接入 | 普通目录右键无此项；TabView创建能力不能等同此入口 | —（尚未定义） | Blink | DEFERRED | [F-MENU] [TABS] | NOT RUN: M-C02-21 |
| C02 | Blink目录右键 > 在Blink新窗口中打开 | 在Blink新窗口中打开 | Open in new Blink window | 未接入 | 普通目录右键无此项；TabView创建能力不能等同此入口 | —（尚未定义） | Blink | DEFERRED | [F-MENU] [TABS] | NOT RUN: M-C02-22 |
| C02 | Blink目录右键 > 固定/取消固定 | 固定/取消固定 | Pin / Unpin | 未接入 | 普通目录右键无此项；TabView创建能力不能等同此入口 | —（尚未定义） | Blink | DEFERRED | [F-MENU] [TABS] | NOT RUN: M-C02-23 |
| C02 | Blink目录右键 > 在终端中打开 | 在终端中打开 | Open in Terminal | 无静态项；系统可提供 | 静态菜单无此项；注册的终端扩展可经系统更多选项提供 | 动态Shell verb（非FolderCommand） | Shell | OS_DEPENDENT | [F-SYSTEM] [HOST] | NOT RUN: M-C02-24 |
| C02 | 目录右键共用树 > 剪贴板/路径/删除/刷新/查看/排序 | 共用本地菜单及子菜单 | Shared local commands and submenus | 见共用树；缺口未显示 | 剪切/复制/粘贴/路径两项/删除/刷新/View3/Sort6逐叶定义见C01；目录Open、Properties、Rename及新增入口单独列出 | 同C01对应FolderCommand/UIA ID | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] | H-FOLDER21；command_new_folder_ready_without_selection |

## C03 多文件、多目录、混合选择

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C03 | Blink右键 > 打开 | 打开 | Open | Fluent E8E5 | 多选时可见但禁用；不截取首条执行 | FolderCommand::Open；UIA ContextOpen | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_open_multi_selection_not_implicitly_dispatched；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 剪切 | 剪切 | Cut | Fluent E8C6 | 已提交普通目录选择；无互斥任务 | FolderCommand::Cut；UIA ContextCut | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | command_multi_copy_respects_owned_queue_busy_state；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 复制 | 复制 | Copy | Fluent E8C8 | 已提交普通目录选择；支持真实文件/目录集合 | FolderCommand::Copy；UIA ContextCopy | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | command_copy_accepts_multiple_selected_committed_rows；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 粘贴 | 粘贴 | Paste | Fluent E77F | CF_HDROP存在且当前目录身份有效；目标是当前目录 | FolderCommand::Paste；UIA ContextPaste | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | command_single_file_paste_ready_without_selection；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 复制完整路径 | 复制完整路径 | Copy full path | Fluent E71B | 选中完整路径；多选按提交顺序以CRLF拼接 | FolderCommand::CopyFullPath；UIA ContextCopyPath | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_multi_paths_keep_both_rows_without_enabling_multi_rename；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 复制所在目录路径 | 复制所在目录路径 | Copy containing folder path | Fluent E8B7 | 复制当前包含目录一次；非选中文件夹内部 | FolderCommand::CopyParentPath；UIA ContextCopyParentPath | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_path_clipboard_respects_existing_publication_busy_state；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 重命名 | 重命名 | Rename | Fluent E8AC | 多选时可见但禁用；不截取首条执行 | FolderCommand::Rename；UIA ContextRename | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] [F-POLICY] | command_rename_rejects_multiple_rows；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 删除 | 删除 | Delete | Fluent E74D | 真实选择；普通Delete入回收站，Shift走永久删除确认 | FolderCommand::Delete；UIA ContextDelete | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | RunDeleteOperationTests；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 属性 | 属性 | Properties | Fluent E946 | 支持文件/目录/混选；Blink只读聚合详情 | FolderCommand::Properties；UIA ContextProperties | Blink | IMPLEMENTED_NEEDS_MANUAL | [F-MENU] [F-CMD] [F-POLICY] | RunItemPropertiesTests；H-FOLDER21（范围见证据说明） |
| C03 | Blink右键 > 更多系统选项 | 压缩与更多系统选项… | Compression and more system options… | Fluent E712 | 普通目录真实选择、同父目录；多选完整集合传host；背景/此电脑无入口 | UIA ContextSystemOptions；generic ShellContextMenuController::Show | Shell | IMPLEMENTED_VERIFIED | [F-SYSTEM] [HOST] [MENU-HANDOFF] | folder_shell_menu_accepts_file_and_directory_siblings_together；H-FOLDER21 |
| C03 | 多选右键共用树 > 刷新/查看/排序 | 刷新、查看三项、排序六项 | Refresh / View (3) / Sort (6) | 见共用树；缺口未显示 | 与C01完全复用，不根据首个选中项派生；多选Open/Rename和操作集合在本场景逐行列出 | 同C01 Refresh/View/Sort命令ID | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-POLICY] | H-FOLDER21；command_open_multi_selection_not_implicitly_dispatched |

## C04 文件列表真正空白

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C04 | 背景右键 > 粘贴 | 粘贴 | Paste | Fluent E77F | CF_HDROP存在且当前目录身份有效；目标是当前目录 | FolderCommand::Paste；UIA ContextPaste | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-POLICY] | command_single_file_paste_ready_without_selection；H-FOLDER21 |
| C04 | 背景右键 > 新建文件夹 | 新建文件夹 | New folder | Fluent E8F4 | 有效当前目录身份，后台队列空闲 | FolderCommand::NewFolder；UIA ContextNewFolder | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-POLICY] | command_new_folder_ready_without_selection；H-FOLDER21 |
| C04 | 背景右键 > 刷新 | 刷新 | Refresh | Fluent E72C | 非呈现/导航忙碌；刷新当前已提交位置 | FolderCommand::Refresh；UIA ContextRefresh | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-POLICY] | command_this_pc_refresh_supported；H-FOLDER21 |
| C04 | 背景右键 > 新建 > 快捷方式/系统模板 | 新建 > 快捷方式/系统模板 | New > Shortcut / ShellNew templates | 未接入 | 当前背景仅Paste/NewFolder/Refresh/View/Sort；没有背景PIDL/CreateViewObject路径 | —（尚未定义） | Shell | DEFERRED | [F-MENU] [HOST] | NOT RUN: M-C04-13 |
| C04 | 背景右键 > 分组依据 > 无/名称/日期/类型/大小 | 分组依据 > 无/名称/日期/类型/大小 | Group by > None / Name / Date / Type / Size | 未接入 | 当前背景仅Paste/NewFolder/Refresh/View/Sort；没有背景PIDL/CreateViewObject路径 | —（尚未定义） | Blink | DEFERRED | [F-MENU] [HOST] | NOT RUN: M-C04-14 |
| C04 | 背景右键 > 粘贴快捷方式 | 粘贴快捷方式 | Paste shortcut | 未接入 | 当前背景仅Paste/NewFolder/Refresh/View/Sort；没有背景PIDL/CreateViewObject路径 | —（尚未定义） | Shell | DEFERRED | [F-MENU] [HOST] | NOT RUN: M-C04-15 |
| C04 | 背景右键 > 在终端中打开 | 在终端中打开 | Open in Terminal | 未接入 | 当前背景仅Paste/NewFolder/Refresh/View/Sort；没有背景PIDL/CreateViewObject路径 | —（尚未定义） | Shell | DEFERRED | [F-MENU] [HOST] | NOT RUN: M-C04-16 |
| C04 | 背景右键 > 当前文件夹属性 | 当前文件夹属性 | Current folder properties | 未接入 | 当前背景仅Paste/NewFolder/Refresh/View/Sort；没有背景PIDL/CreateViewObject路径 | —（尚未定义） | Blink | PARTIAL | [F-MENU] [HOST] | NOT RUN: M-C04-17 |
| C04 | 背景右键 > 完整系统背景菜单 | 完整系统背景菜单 | Full system background menu | 未接入 | 当前背景仅Paste/NewFolder/Refresh/View/Sort；没有背景PIDL/CreateViewObject路径 | —（尚未定义） | Shell | DEFERRED | [F-MENU] [HOST] | NOT RUN: M-C04-18 |
| C04 | 普通目录背景范围边界 > 显示设置/个性化 | 显示设置/个性化 | Display settings / Personalize | 不显示 | 桌面专属上下文不无条件放入普通目录 | — | 系统跳转 | UNSUPPORTED_BY_DESIGN | [F-MENU] [SCOPE] | NOT RUN: M-C04-19 |
| C04 | 背景右键共用树 > 查看/排序 | 查看三项、排序六项 | View (3) / Sort (6) | 见共用树；缺口未显示 | 逐叶与C01一致，背景捕获无row_index；Toggle勾选状态来自活动Tab | 同C01 View/Sort命令ID | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-CMD] | H-FOLDER21；command_folder_sort_supported |

## C05 搜索结果

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C05 | 搜索结果右键 > 打开 | 打开 | Open | Fluent E8E5 | 真实单条结果，目录在Blink导航、文件走关联 | ShellMenuCommand::Open；UIA SearchMenuOpen | Blink | IMPLEMENTED_NEEDS_MANUAL | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 剪切 | 剪切 | Cut | Fluent E8C6 | 真实结果重新核验；无互斥任务 | ShellMenuCommand::Cut；UIA SearchMenuCut | Blink | IMPLEMENTED_NEEDS_MANUAL | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 复制 | 复制 | Copy | Fluent E8C8 | eager CF_HDROP，目标不是背后浏览选择 | ShellMenuCommand::Copy；UIA SearchMenuCopy | Blink | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 粘贴到所在目录 | 粘贴到所在目录 | Paste into containing folder | Fluent E77F | CF_HDROP且无互斥任务；粘贴到命中项所在目录 | ShellMenuCommand::PasteParent；UIA SearchMenuPasteParent | Blink | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 复制完整路径 | 复制完整路径 | Copy full path | Fluent E71B | 真实命中完整路径 | ShellMenuCommand::CopyFullPath；UIA SearchMenuCopyPath | Blink | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 复制所在目录路径 | 复制所在目录路径 | Copy containing folder path | Fluent E8B7 | 真实命中所在目录 | ShellMenuCommand::CopyParentPath；UIA SearchMenuCopyParentPath | Blink | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 定位 | 定位 | Locate | Fluent E81D | 转向真实包含目录并定位命中行 | ShellMenuCommand::Locate；UIA SearchMenuLocate | Blink | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 重命名 | 重命名 | Rename | Fluent E8AC | 验证命中后复用原重命名队列 | ShellMenuCommand::Rename；UIA SearchMenuRename | Blink | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 删除 | 删除 | Delete | Fluent E74D | 验证后回收站/Shift永久删除与确认 | ShellMenuCommand::Delete；UIA SearchMenuDelete | Blink | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 属性 | 属性 | Properties | Fluent E946 | 原生完整属性；helper --verb properties，不是Blink只读统计 | --verb properties；UIA SearchMenuProperties | Shell | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果右键 > 压缩与更多系统选项… | 压缩与更多系统选项… | Compression and more system options… | Fluent E712 | 只有显式点击此项才启动原生helper；动态子菜单另见DYN | ShellContextMenuController::Show；UIA SearchMenuSystemOptions | Shell | IMPLEMENTED_VERIFIED | [SEARCH-MENU] [SEARCH-ACTION] | H-SEARCH18；search_context_current_published_single_result_is_actionable |
| C05 | 搜索结果 > 多选/混选右键 | 多选搜索结果菜单 | Multi-selection result menu | 未接入 | SearchPresentation当前单选；不能从普通目录multi-host推断搜索多选已接入 | —（尚未定义） | Blink | DEFERRED | [SEARCH-INPUT] [SEARCH-MENU] | NOT RUN: M-C05-12 |
| C05 | 搜索结果 > 真正空白右键 | 搜索背景菜单 | Search background context menu | 无菜单 | 当前清除选择并不借用旧结果；未实现搜索背景操作菜单 | SearchPresentation::ShowContextMenu(no row) | Blink | PARTIAL | [SEARCH-INPUT] | search_context_removed_result_invalidates_old_menu；NOT RUN: M-C05-BACKGROUND |
| C05 | 搜索结果 > 查看/排序/新建子菜单 | 结果视图/排序/新建 | Result view / sort / new | 未接入 | 没有给搜索surface复用背后普通目录菜单；新建需另行定义目录上下文 | —（尚未定义） | Blink | DEFERRED | [SEARCH-MENU] [TOP] | NOT RUN: M-C05-14 |

## C06 导航常用/固定项

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C06 | 导航常用/固定项右键 > 打开 | 打开 | Open | Fluent E8E5 | 实际KnownFolder或ThisPC独立目标，保持文件区选择 | TargetContextOpen | Blink | IMPLEMENTED_VERIFIED | [TARGET] | G06a UI07验证Quick Home Open；其它位置完整矩阵待验 |
| C06 | 导航常用/固定项右键 > 在新标签页中打开 | 在新标签页中打开 | Open in new tab | Fluent E8A7 | 捕获当前常用位置Location，复用原新标签入口 | TargetContextNewTab | Blink | IMPLEMENTED_NEEDS_MANUAL | [TARGET] | 入口已接；Quick实际NewTab组合待验 |
| C06 | 导航常用/固定项右键 > 固定/取消固定 | 固定/取消固定 | Pin / Unpin | 未接入 | 目前Home/桌面/下载/文档是KnownFolder清单，不是可编辑固定集合 | —（此上下文未定义） | Blink | DEFERRED | [NAV] [LOCATION] [F-SYSTEM] | NOT RUN: M-C06-03 |
| C06 | 导航常用/固定项右键 > 复制路径 | 复制路径 | Copy path | Fluent E71B | 实际目录目标复用文本Clipboard；虚拟ThisPC无路径动作 | TargetContextCopyPath | Blink | IMPLEMENTED_NEEDS_MANUAL | [TARGET] | 菜单适用性已接，真实写入待验 |
| C06 | 导航常用/固定项右键 > 属性 | 属性 | Properties | Fluent E946 | 后台捕获实际目录身份，调用既有进程外properties | TargetContextProperties | 系统跳转 | IMPLEMENTED_NEEDS_MANUAL | [TARGET] | 捕获/菜单已接，原生属性页待验 |
| C06 | 导航常用/固定项右键 > 更多系统选项 | 更多系统选项 | More system options | 未接入 | 选中项host没有连接到常用位置ListView | —（此上下文未定义） | Shell | DEFERRED | [NAV] [LOCATION] [F-SYSTEM] | NOT RUN: M-C06-06 |

## C07 导航普通目录

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C07 | 导航普通目录右键 > 打开 | 打开 | Open | Fluent E8E5 | 捕获当前树节点Location，保持文件列表选择，进入目标 | TargetContextOpen | Blink | IMPLEMENTED_VERIFIED | [TARGET] | G06a UI06；物理鼠标未验 |
| C07 | 导航普通目录右键 > 展开/折叠 | 展开/折叠 | Expand / Collapse | Fluent E8BB | 按当前节点真实展开状态，复用lazy loader；reparse不列此项 | TargetContextExpandCollapse | Blink | IMPLEMENTED_VERIFIED | [TARGET] | G06a UI04；不导航/不污染列表选择 |
| C07 | 导航普通目录右键 > 在新标签页中打开 | 在新标签页中打开 | Open in new tab | Fluent E8A7 | 新标签直接导航捕获Location，原标签和选择保留 | TargetContextNewTab | Blink | IMPLEMENTED_VERIFIED | [TARGET] | G06a UI05 |
| C07 | 导航普通目录右键 > 剪切 | 剪切 | Cut | Fluent E8C6 | 普通目录独立worker捕获，MOVE只发布意图；Search/pending/busy不可用 | TargetContextCut | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-TRANSFER] | 目录来源10项新增原生检查/菜单enabled通过；实际UI SKIP |
| C07 | 导航普通目录右键 > 复制 | 复制 | Copy | Fluent E8C8 | 单目录CF_HDROP/COPY，保持文件区选择 | TargetContextCopy | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-TRANSFER] | 菜单/真实目录来源模型通过；实际UI SKIP |
| C07 | 导航普通目录右键 > 粘贴 | 粘贴 | Paste | Fluent E77F | 读取点中目录真实identity，复用原CopyItems/MOVE队列与冲突/取消/重试 | TargetContextPaste | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-TRANSFER] | enablement=真实CF_HDROP；实际传输UI SKIP |
| C07 | 导航普通目录右键 > 新建文件夹 | 新建文件夹 | New folder | Fluent E8F4 | 捕获所点目录真实ID，原名称框与queue，CtrlShiftN限导航焦点，事件更新已加载树 | TargetContextNewFolder | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-MUTATION] | G06c原生已过，独立NewFolder7项脚本待运行 |
| C07 | 导航普通目录右键 > 重命名 | 重命名 | Rename | Fluent E8AC | 捕获目录与父ID，原whole-name/dialog/queue及其它Tab relocation，F2限导航，Retry保持实际目标 | TargetContextRename | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-MUTATION] | G06c原生已过，独立Rename8项脚本待运行 |
| C07 | 导航普通目录右键 > 删除 | 删除 | Delete | Fluent E74D | 普通Delete走回收站；ShiftDelete走默认Cancel确认，原queue/Retry及事件树刷新 | TargetContextDelete | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-MUTATION] | G06c原生已过，独立Delete5项脚本及普通回收站UI待运行 |
| C07 | 导航普通目录右键 > 属性 | 属性 | Properties | Fluent E946 | worker捕获点中目录的真实entry/parent，再走进程外properties-only | TargetContextProperties | 系统跳转 | IMPLEMENTED_NEEDS_MANUAL | [TARGET] | 10项原生捕获及菜单enabled已验；真实属性页NOT RUN |
| C07 | 导航普通目录右键 > 复制完整路径 | 复制完整路径 | Copy full path | Fluent E71B | 捕获点中目录Location，复用原后台文本剪贴板发布 | TargetContextCopyPath | Blink | IMPLEMENTED_NEEDS_MANUAL | [TARGET] | enabled已验；本轮实际剪贴板写入NOT RUN |
| C07 | 导航普通目录右键 > 更多系统选项 | 更多系统选项 | More system options | 未接入 | 无树节点host请求构建 | —（此上下文未定义） | Shell | DEFERRED | [NAV] [F-CMD] [F-SYSTEM] | NOT RUN: M-C07-11 |

## C08 此电脑驱动器条目

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C08 | 驱动器条目实际右键 > 打开 | 打开 | Open | Fluent E8E5 | ThisPC行捕获真实根Location，当前标签进入根 | TargetContextOpen | Blink | IMPLEMENTED_VERIFIED | [TARGET] | G06a UI01/02 |
| C08 | 驱动器条目实际右键 > 刷新 | 刷新 | Refresh | 此菜单不显示 | G06a仅列目标动作；全页刷新仍在顶部/背景 | FolderCommand::Refresh（顶部/背景） | Blink | UNSUPPORTED_BY_DESIGN | [TARGET] | 专用菜单不混入全页刷新 |
| C08 | 驱动器目标专用右键 > 在新标签页打开 | 在新标签页打开 | Open in new tab | Fluent E8A7 | 新标签直接进入捕获根，原ThisPC标签保持 | TargetContextNewTab | Blink | IMPLEMENTED_VERIFIED | [TARGET] | G06a UI02 |
| C08 | 驱动器目标专用右键 > 原生驱动器属性 | 属性 | Properties | Fluent E946 | 根entry和parent使用真实根身份；原有进程外properties协议 | TargetContextProperties | 系统跳转 | IMPLEMENTED_NEEDS_MANUAL | [TARGET] | 根捕获/协议原生检查及enabled已验，实际属性页NOT RUN |
| C08 | 驱动器目标专用右键 > 复制完整路径 | 复制完整路径 | Copy full path | Fluent E71B | 使用所点驱动器根Location，复用文本剪贴板发布 | TargetContextCopyPath | Blink | IMPLEMENTED_NEEDS_MANUAL | [TARGET] | enabled已验，实际写入NOT RUN |
| C08 | 驱动器目标专用右键 > 粘贴 | 粘贴 | Paste | Fluent E77F | 根是实际目录目标，仅在浏览owner可用且有CF_HDROP时启用；不提供根Cut/Copy | TargetContextPaste | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-TRANSFER] | readonly13核对适用性；真实盘根写入NOT RUN |
| C08 | 驱动器目标专用右键 > 新建文件夹 | 新建文件夹 | New folder | Fluent E8F4 | 真实盘根只提供适用新建，不提供根Rename/Delete | TargetContextNewFolder | Blink | IMPLEMENTED_NEEDS_MANUAL | [NAV-MUTATION] | G06c入口已接，实际盘根写入未验 |
| C08 | 驱动器目标专用右键 > 弹出 | 弹出 | Eject | 未接入 | 仅适用可弹出设备；当前无设备类型驱动的动作 | —（尚未定义） | Shell | DEFERRED | [LOCATION] [F-MENU] | NOT RUN: M-C08-22 |
| C08 | 驱动器目标专用右键 > 格式化 | 格式化 | Format | 未接入 | 需要设备适用性与显式确认；当前未接入 | —（尚未定义） | Shell | DEFERRED | [LOCATION] [F-MENU] | NOT RUN: M-C08-23 |
| C08 | 驱动器目标专用右键 > 管理 | 管理 | Manage | 未接入 | 未接Windows系统管理跳转 | —（尚未定义） | 系统跳转 | DEFERRED | [LOCATION] [F-MENU] | NOT RUN: M-C08-24 |
| C08 | 驱动器目标专用右键 > 光驱菜单 | 光驱菜单 | Optical drive context menu | 未接入 | LogicalDriveRoots只枚举FIXED/REMOVABLE，不列CDROM | —（尚未定义） | Shell | DEFERRED | [LOCATION] [F-MENU] | NOT RUN: M-C08-25 |
| C08 | 驱动器目标专用右键 > 映射网络驱动器菜单 | 映射网络驱动器菜单 | Mapped network drive menu | 未接入 | LogicalDriveRoots排除DRIVE_REMOTE，未接入 | —（尚未定义） | Shell | DEFERRED | [LOCATION] [F-MENU] | NOT RUN: M-C08-26 |
| C08 | 驱动器条目实际右键 > 普通文件操作误复用 | 剪切/改名/删除等 | Cut / Rename / Delete | 不显示 | G06a目标菜单移除不适用普通行操作，不给盘根套普通文件命令 | ShowTargetContextMenu | Blink | IMPLEMENTED_VERIFIED | [TARGET] | G06a UI01/02未出现这些入口 |
| C08 | 驱动器条目实际右键 > 查看/排序 | 查看/排序 | View / Sort | 此菜单不显示 | 查看/排序保留顶部/背景；G06a为目标菜单 | ShowTargetContextMenu | Blink | UNSUPPORTED_BY_DESIGN | [TARGET] | G06a UI01/02目标菜单 |

## C09 此电脑背景与虚拟位置

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C09 | 此电脑实际背景右键 > 粘贴 | 粘贴 | Paste | Fluent E77F | 只有Refresh可用；Paste/NewFolder禁用，不能伪装C盘目录 | FolderCommand::Paste；UIA ContextPaste | Blink | PARTIAL | [F-MENU] [F-POLICY] [LOCATION] | command_single_file_paste_ready_without_selection |
| C09 | 此电脑实际背景右键 > 新建文件夹 | 新建文件夹 | New folder | Fluent E8F4 | 只有Refresh可用；Paste/NewFolder禁用，不能伪装C盘目录 | FolderCommand::NewFolder；UIA ContextNewFolder | Blink | PARTIAL | [F-MENU] [F-POLICY] [LOCATION] | command_this_pc_rejects_filesystem_paste |
| C09 | 此电脑实际背景右键 > 刷新 | 刷新 | Refresh | Fluent E72C | 只有Refresh可用；Paste/NewFolder禁用，不能伪装C盘目录 | FolderCommand::Refresh；UIA ContextRefresh | Blink | IMPLEMENTED_VERIFIED | [F-MENU] [F-POLICY] [LOCATION] | command_this_pc_refresh_supported |
| C09 | 此电脑或虚拟位置目标入口 > 映射/断开网络驱动器 | 映射/断开网络驱动器 | Map / Disconnect network drive | 未接入 | Location目前只有ThisPc/Folder；Nav.Home为用户Profile路径，不是Explorer Home | —（尚未定义） | Blink | DEFERRED | [LOCATION] [NAV] | NOT RUN: M-C09-13 |
| C09 | 此电脑或虚拟位置目标入口 > 此电脑属性/系统信息 | 此电脑属性/系统信息 | This PC properties / System information | 未接入 | Location目前只有ThisPc/Folder；Nav.Home为用户Profile路径，不是Explorer Home | —（尚未定义） | 系统跳转 | DEFERRED | [LOCATION] [NAV] | NOT RUN: M-C09-14 |
| C09 | 此电脑或虚拟位置目标入口 > 主页/最近项目 | 主页/最近项目 | Home / Recent | 未接入 | Location目前只有ThisPc/Folder；Nav.Home为用户Profile路径，不是Explorer Home | —（尚未定义） | Blink | DEFERRED | [LOCATION] [NAV] | NOT RUN: M-C09-15 |
| C09 | 此电脑或虚拟位置目标入口 > 图库/库 | 图库/库 | Gallery / Libraries | 未接入 | Location目前只有ThisPc/Folder；Nav.Home为用户Profile路径，不是Explorer Home | —（尚未定义） | Blink | DEFERRED | [LOCATION] [NAV] | NOT RUN: M-C09-16 |
| C09 | 此电脑或虚拟位置目标入口 > OneDrive虚拟入口 | OneDrive虚拟入口 | OneDrive virtual entry | 未接入 | Location目前只有ThisPc/Folder；Nav.Home为用户Profile路径，不是Explorer Home | —（尚未定义） | Blink | DEFERRED | [LOCATION] [NAV] | NOT RUN: M-C09-17 |
| C09 | 此电脑实际背景右键 > 查看/排序 | 查看三项、排序六项 | View (3) / Sort (6) | 见共用树；缺口未显示 | 完整子树见C01；三视图可用、六排序禁用，不把ThisPc伪装成本地目录 | 同C01 View/Sort命令ID | Blink | PARTIAL | [F-MENU] [F-POLICY] [LOCATION] | command_this_pc_view_supported；command_this_pc_sort_disabled |

## C10 回收站根与内容

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C10 | 回收站 > 回收站浏览 | 回收站浏览 | Browse Recycle Bin | 未接入 | 只有普通目录删除到系统回收站后端；尚无回收站虚拟页/根菜单，不把系统失败降级永久删除 | —（尚未定义） | Shell | DEFERRED | [SCOPE] [DELETE] [LOCATION] | NOT RUN: M-C10-01 |
| C10 | 回收站 > 根 > 清空回收站 | 根 > 清空回收站 | Root > Empty Recycle Bin | 未接入 | 只有普通目录删除到系统回收站后端；尚无回收站虚拟页/根菜单，不把系统失败降级永久删除 | —（尚未定义） | Shell | DEFERRED | [SCOPE] [DELETE] [LOCATION] | NOT RUN: M-C10-02 |
| C10 | 回收站 > 根 > 属性 | 根 > 属性 | Root > Properties | 未接入 | 只有普通目录删除到系统回收站后端；尚无回收站虚拟页/根菜单，不把系统失败降级永久删除 | —（尚未定义） | Shell | DEFERRED | [SCOPE] [DELETE] [LOCATION] | NOT RUN: M-C10-03 |
| C10 | 回收站 > 内容 > 还原 | 内容 > 还原 | Content > Restore | 未接入 | 只有普通目录删除到系统回收站后端；尚无回收站虚拟页/根菜单，不把系统失败降级永久删除 | —（尚未定义） | Shell | DEFERRED | [SCOPE] [DELETE] [LOCATION] | NOT RUN: M-C10-04 |
| C10 | 回收站 > 内容 > 永久删除 | 内容 > 永久删除 | Content > Permanently delete | 未接入 | 只有普通目录删除到系统回收站后端；尚无回收站虚拟页/根菜单，不把系统失败降级永久删除 | —（尚未定义） | Shell | DEFERRED | [SCOPE] [DELETE] [LOCATION] | NOT RUN: M-C10-05 |
| C10 | 回收站 > 内容 > 原始位置/属性 | 内容 > 原始位置/属性 | Content > Original location / Properties | 未接入 | 只有普通目录删除到系统回收站后端；尚无回收站虚拟页/根菜单，不把系统失败降级永久删除 | —（尚未定义） | Shell | DEFERRED | [SCOPE] [DELETE] [LOCATION] | NOT RUN: M-C10-06 |

## C11 UNC/SMB与远端Provider

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C11 | UNC/SMB路径 > 浏览及本地样式右键 | UNC/SMB浏览及菜单 | UNC/SMB browsing and menu | 沿当前文件列表图标；未专项验证 | Folder路径可表示UNC；专门共享发现、离线和凭据菜单未接，写操作后端以本地盘为主 | Location::Folder / 既有FolderCommand（条件受限） | Blink | PARTIAL | [LOCATION] [LEASE] [F-MENU] | NOT RUN: M-C11-UNC-SMB |
| C11 | 网络与未来Provider > 网络共享发现 | 网络共享发现 | Discover network shares | 未接入 | Provider原型不是生产导航/菜单；远端需要独立协议状态与命令 | —（产品UI未定义） | Blink | DEFERRED | [SCOPE] [PROVIDER] | NOT RUN: M-C11-02 |
| C11 | 网络与未来Provider > 网络凭据/断线重连 | 网络凭据/断线重连 | Network credentials / Reconnect | 未接入 | Provider原型不是生产导航/菜单；远端需要独立协议状态与命令 | —（产品UI未定义） | Blink | DEFERRED | [SCOPE] [PROVIDER] | NOT RUN: M-C11-03 |
| C11 | 网络与未来Provider > SFTP连接/目录/上传下载菜单 | SFTP连接/目录/上传下载菜单 | SFTP connection / folder / transfer menus | 未接入 | Provider原型不是生产导航/菜单；远端需要独立协议状态与命令 | —（产品UI未定义） | Blink | DEFERRED | [SCOPE] [PROVIDER] | NOT RUN: M-C11-04 |
| C11 | 网络与未来Provider > WebDAV/自建网盘菜单 | WebDAV/自建网盘菜单 | WebDAV / self-hosted storage menus | 未接入 | Provider原型不是生产导航/菜单；远端需要独立协议状态与命令 | —（产品UI未定义） | Blink | DEFERRED | [SCOPE] [PROVIDER] | NOT RUN: M-C11-05 |
| C11 | 远端对象范围边界 > 直接套本地Shell菜单 | 远端直接调用本地Shell菜单 | Treat remote objects as local Shell items | 不提供 | 不伪造SFTP/WebDAV为本地绝对路径或任意FileIdentity | — | Shell | UNSUPPORTED_BY_DESIGN | [SCOPE] [PROVIDER] | NOT RUN: M-C11-06 |

## C12 标签、地址、搜索输入、列头

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| C12 | 标签栏 > 新建标签按钮 | 新建标签按钮 | New tab button | WinUI自有按钮 | 已有＋及Ctrl+T，不是标签右键项 | CreateAndActivateTab / Ctrl+T | Blink | IMPLEMENTED_VERIFIED | [TABS] | RunTabWindowStateTests |
| C12 | 标签栏 > 关闭按钮 | 关闭按钮 | Close tab button | WinUI自有按钮 | 已有TabView关闭按钮及Ctrl+W，不是右键项 | CloseTab / Ctrl+W | Blink | IMPLEMENTED_VERIFIED | [TABS] | RunTabWindowStateTests |
| C12 | 标签栏 > 右键 > 关闭 | 右键 > 关闭 | Context > Close | 未接入 | 未注册TabViewItem右键菜单 | —（尚未定义） | Blink | DEFERRED | [TABS] | NOT RUN: M-C12-03 |
| C12 | 标签栏 > 右键 > 关闭其他 | 右键 > 关闭其他 | Context > Close other tabs | 未接入 | 未定义批量关闭规则；Explorer目标版本行为待核 | —（尚未定义） | Blink | DEFERRED | [TABS] | NOT RUN: M-C12-04 |
| C12 | 标签栏 > 右键 > 关闭右侧 | 右键 > 关闭右侧 | Context > Close tabs to the right | 未接入 | 未定义批量关闭规则；Explorer目标版本行为待核 | —（尚未定义） | Blink | DEFERRED | [TABS] | NOT RUN: M-C12-05 |
| C12 | 标签栏 > 右键 > 复制标签/新窗口 | 右键 > 复制标签/新窗口 | Context > Duplicate / New window | 未接入 | 不能以TabView拖动或当前导航能力冒充已实现 | —（尚未定义） | Blink | DEFERRED | [TABS] | NOT RUN: M-C12-06 |
| C12 | 地址栏 > 层级段跳转/下级目录 | 层级段跳转/下级目录 | Breadcrumb segment / Child folders | 原生BreadcrumbBar + MenuFlyout | G03a层级跳转，G03b按需枚举直接子目录；真实隐藏/空/失败反馈，不含普通文件 | AddressBarPresentation / StartAddressChildren / NavigateToLocation | Blink | IMPLEMENTED_NEEDS_MANUAL | [ADDRESS] | G03b菜单15 + 地址20项；实物鼠标/完整矩阵待验 |
| C12 | 地址栏 > 收缩/路径菜单 | 收缩/路径菜单 | Collapsed breadcrumb / Path menu | 原生overflow | 保留原生收缩；collapsed项的独立子按钮支持真实Enter及后续菜单选择 | BreadcrumbBar / IElementFactory | Blink | IMPLEMENTED_NEEDS_MANUAL | [ADDRESS] | G03b#11及地址既有overflow通过；全环境待验 |
| C12 | 地址栏 > 地址历史/候选 | 地址历史/候选 | Address history / Suggestions | MenuFlyout历史 + Popup目录候选 | 已提交历史及最多12个直接目录候选；Tab补全、选中Enter/点击导航，未选Enter仍原文提交 | BuildAddressHistory / QueueSuggestions / StartAddressSuggestions | Blink | IMPLEMENTED_NEEDS_MANUAL | [ADDRESS] | G03c补全15+地址20通过，真实IME/完整矩阵待验 |
| C12 | 地址/搜索输入框默认文本右键 > 撤销 | 撤销 | Undo | WinUI默认文本菜单 | 当前两个TextBox均保留默认文本命令；按焦点与编辑状态决定，未在本轮UIA专项验证 | TextBox默认文本命令（非FolderCommand） | Blink | OS_DEPENDENT | [ADDRESS] | NOT RUN: M-C12-TEXT-地址编辑框-UNDO |
| C12 | 地址/搜索输入框默认文本右键 > 剪切 | 剪切 | Cut | WinUI默认文本菜单 | 当前两个TextBox均保留默认文本命令；按焦点与编辑状态决定，未在本轮UIA专项验证 | TextBox默认文本命令（非FolderCommand） | Blink | OS_DEPENDENT | [ADDRESS] | NOT RUN: M-C12-TEXT-地址编辑框-CUT |
| C12 | 地址/搜索输入框默认文本右键 > 复制 | 复制 | Copy | WinUI默认文本菜单 | 当前两个TextBox均保留默认文本命令；按焦点与编辑状态决定，未在本轮UIA专项验证 | TextBox默认文本命令（非FolderCommand） | Blink | OS_DEPENDENT | [ADDRESS] | NOT RUN: M-C12-TEXT-地址编辑框-COPY |
| C12 | 地址/搜索输入框默认文本右键 > 粘贴 | 粘贴 | Paste | WinUI默认文本菜单 | 当前两个TextBox均保留默认文本命令；按焦点与编辑状态决定，未在本轮UIA专项验证 | TextBox默认文本命令（非FolderCommand） | Blink | OS_DEPENDENT | [ADDRESS] | NOT RUN: M-C12-TEXT-地址编辑框-PASTE |
| C12 | 地址/搜索输入框默认文本右键 > 全选 | 全选 | Select all | WinUI默认文本菜单 | 当前两个TextBox均保留默认文本命令；按焦点与编辑状态决定，未在本轮UIA专项验证 | TextBox默认文本命令（非FolderCommand） | Blink | OS_DEPENDENT | [ADDRESS] | NOT RUN: M-C12-TEXT-地址编辑框-SELECTALL |
| C12 | 详细信息列头实际点击 > 名称 | 名称 | Name | 文字按钮；无右键图标 | 按钮切字段/升降序；固定列宽；不是列头右键菜单 | SortField::Name / MakeSortHeader | Blink | IMPLEMENTED_NEEDS_MANUAL | [COLUMNS] | type_sort_changes_projection_without_selection_history_or_listing_mutation |
| C12 | 详细信息列头实际点击 > 类型 | 类型 | Type | 文字按钮；无右键图标 | 按钮切字段/升降序；固定列宽；不是列头右键菜单 | SortField::Type / MakeSortHeader | Blink | IMPLEMENTED_NEEDS_MANUAL | [COLUMNS] | type_sort_changes_projection_without_selection_history_or_listing_mutation |
| C12 | 详细信息列头实际点击 > 大小 | 大小 | Size | 文字按钮；无右键图标 | 按钮切字段/升降序；固定列宽；不是列头右键菜单 | SortField::Size / MakeSortHeader | Blink | IMPLEMENTED_NEEDS_MANUAL | [COLUMNS] | type_sort_changes_projection_without_selection_history_or_listing_mutation |
| C12 | 详细信息列头实际点击 > 修改时间 | 修改时间 | Modified | 文字按钮；无右键图标 | 按钮切字段/升降序；固定列宽；不是列头右键菜单 | SortField::Modified / MakeSortHeader | Blink | IMPLEMENTED_NEEDS_MANUAL | [COLUMNS] | type_sort_changes_projection_without_selection_history_or_listing_mutation |
| C12 | 列头目标右键/列管理 > 名称/类型/大小/修改时间勾选 | 名称/类型/大小/修改时间勾选 | Name / Type / Size / Modified check boxes | 未接入 | 当前固定四列Button，未注册列头ContextRequested/列配置 | —（尚未定义） | Blink | DEFERRED | [COLUMNS] | NOT RUN: M-C12-24 |
| C12 | 列头目标右键/列管理 > 其他属性列 | 其他属性列 | More property columns | 未接入 | 当前固定四列Button，未注册列头ContextRequested/列配置 | —（尚未定义） | Blink | DEFERRED | [COLUMNS] | NOT RUN: M-C12-25 |
| C12 | 列头目标右键/列管理 > 调整列宽/自动适应 | 调整列宽/自动适应 | Resize / Size to fit | 未接入 | 当前固定四列Button，未注册列头ContextRequested/列配置 | —（尚未定义） | Blink | DEFERRED | [COLUMNS] | NOT RUN: M-C12-26 |
| C12 | 列头目标右键/列管理 > 列重排 | 列重排 | Reorder columns | 未接入 | 当前固定四列Button，未注册列头ContextRequested/列配置 | —（尚未定义） | Blink | DEFERRED | [COLUMNS] | NOT RUN: M-C12-27 |
| C12 | 列头目标右键/列管理 > 分组依据 | 分组依据 | Group by | 未接入 | 当前固定四列Button，未注册列头ContextRequested/列配置 | —（尚未定义） | Blink | DEFERRED | [COLUMNS] | NOT RUN: M-C12-28 |
| C12 | Shift+F10/菜单键 > 文件列表/搜索/导航目标 | 键盘上下文菜单 | Keyboard context menu | 对应WinUI菜单 | 焦点范围内捕获当前列表、搜索或导航目标，虚拟对象仅列适用动作 | ShowKeyboardFolderContextMenu/ShowKeyboardContextMenu/ShowNavigationContextMenu | Blink | IMPLEMENTED_VERIFIED | [TARGET] [F-KEY] | G06a10窗口检查；旧H-FOLDER21/H-SEARCH18保持历史证据 |

## DYN 系统动态子菜单与执行机制

| 场景 | 菜单路径 | 中文 | 英文 | 图标 | 条件 | 命令ID | 实现方式 | 状态 | 源码证据 | 测试ID |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| DYN | 系统更多选项 > 获取选中集合菜单 | 完整旧式系统菜单 | Full classic system menu | Shell返回图标/owner-draw；便利项为系统文字菜单 | 普通目录同父真实多选或单搜索结果；SHParseDisplayName→SHBindToParent→GetUIObjectOf(IContextMenu) | IContextMenu::QueryContextMenu | Shell | IMPLEMENTED_VERIFIED | [HOST] [SHELL-PROTOCOL] | folder_shell_menu_accepts_file_and_directory_siblings_together；H-FOLDER21 |
| DYN | 系统更多选项 > 动态 > 打开方式 > 注册应用 | 打开方式及应用列表（动态） | Open with / Registered applications (dynamic) | Shell返回图标/owner-draw；便利项为系统文字菜单 | 真实关联支持才显示；不固定应用名 | Shell canonical verb / command offset | Shell | OS_DEPENDENT | [HOST] [SHELL-PROTOCOL] | NOT RUN: M-DYN-OPENWITH |
| DYN | 系统更多选项 > 动态 > 发送到 > 已注册目标 | 发送到（动态） | Send to (dynamic) | Shell返回图标/owner-draw；便利项为系统文字菜单 | Shell返回级联；可能触发系统目标窗口 | Shell command offset | Shell | OS_DEPENDENT | [HOST] [SHELL-PROTOCOL] | NOT RUN: M-DYN-SENDTO |
| DYN | 系统更多选项 > 动态 > 压缩到/归档扩展 > 可用动作 | 压缩/归档动作（动态） | Archive/compression actions (dynamic) | Shell返回图标/owner-draw；便利项为系统文字菜单 | 取决于真实安装与注册；历史只验证一款已安装压缩扩展，不硬编码品牌 | Shell command offset / registered extension | Shell | OS_DEPENDENT | [HOST] [SHELL-PROTOCOL] | H-FOLDER21；H-SEARCH18（仅已安装扩展代表路径） |
| DYN | 系统更多选项 > 动态 > 共享 > 可用对象 | 共享（动态） | Share (dynamic) | Shell返回图标/owner-draw；便利项为系统文字菜单 | 仅当前对象/系统处理程序支持时；静态Blink Share仍未实现 | Shell registered verb | Shell | OS_DEPENDENT | [HOST] [SHELL-PROTOCOL] | NOT RUN: M-DYN-SHARE |
| DYN | 系统更多选项 > 动态 > 属性 > 系统属性页 | 系统属性页（动态） | System property pages (dynamic) | Shell返回图标/owner-draw；便利项为系统文字菜单 | 页签由对象、权限、扩展、版本决定；Properties native Invoke；搜索还支持--verb properties | IContextMenu::InvokeCommand(properties) | Shell | OS_DEPENDENT | [HOST] [SHELL-PROTOCOL] | H-SEARCH18（属性入口）；NOT RUN: M-DYN-ALL-PAGES |
| DYN | 系统更多选项 > 动态 > 打开/复制/剪切/粘贴/重命名/删除 | 回Blink的标准动词 | Canonical verbs routed back to Blink | Shell返回图标/owner-draw；便利项为系统文字菜单 | host截获open/copy/cut/paste/rename/delete；多选Open/Rename仍按Blink policy禁用 | ShellMenuCommand::Open/Copy/Cut/PasteParent/Rename/Delete/PermanentDelete | Blink | IMPLEMENTED_VERIFIED | [HOST] [SHELL-PROTOCOL] | shell_menu_reply_accepts_exactly_defined_blink_commands_one_through_ten；H-FOLDER21 |
| DYN | 系统更多选项 > 尾部 > 复制完整路径/所在目录/定位/粘贴到所在目录 | Blink便利命令 | Blink convenience commands | Shell返回图标/owner-draw；便利项为系统文字菜单 | Host菜单尾部四项；目录入口Locate仅滚动已选行，Paste落当前包含目录 | ShellMenuCommand::CopyFullPath/CopyParentPath/Locate/PasteParent | Blink | IMPLEMENTED_VERIFIED | [HOST] [SHELL-PROTOCOL] | shell_menu_reply_accepts_exactly_defined_blink_commands_one_through_ten；H-SEARCH18 |
| DYN | 系统更多选项 > 级联/owner-draw/键盘 | 动态级联与系统绘制 | Dynamic cascades / Owner-draw | Shell返回图标/owner-draw；便利项为系统文字菜单 | IContextMenu2/3转发消息；初始化动态子菜单，不把显示文字当ID | IContextMenu3::HandleMenuMsg2 / IContextMenu2::HandleMenuMsg | Shell | OS_DEPENDENT | [HOST] [SHELL-PROTOCOL] | --probe机制；NOT RUN: M-DYN-OWNERDRAW-ALL |
| DYN | 系统更多选项 > 现代扩展 | 现代Explorer扩展完整枚举 | Complete modern Explorer extensions | Shell返回图标/owner-draw；便利项为系统文字菜单 | 没有IExplorerCommand对接；传统IContextMenu不等于完整Win11现代菜单 | —（未实现IExplorerCommand） | Shell | PARTIAL | [HOST] [SHELL-PROTOCOL] | NOT RUN: M-DYN-MODERN |
| DYN | 系统更多选项 > 系统背景 > 新建 > ShellNew | 系统背景新建模板 | System background New / ShellNew | Shell返回图标/owner-draw；便利项为系统文字菜单 | Host目前只GetUIObjectOf选中项，未实现背景CreateViewObject/ShellNew | —（尚未定义） | Shell | DEFERRED | [HOST] [SHELL-PROTOCOL] | NOT RUN: M-DYN-BACKGROUND-NEW |
| DYN | 系统更多选项 > Shift扩展动词 | Shift扩展动词（动态） | Extended verbs (dynamic) | Shell返回图标/owner-draw；便利项为系统文字菜单 | 按实际Shift增加CMF_EXTENDEDVERBS；内容随系统注册变化 | CMF_EXTENDEDVERBS | Shell | OS_DEPENDENT | [HOST] [SHELL-PROTOCOL] | NOT RUN: M-DYN-EXTENDED-VERBS |

## 证据索引与验证边界

- [TOP]：`main.cpp::BuildFolderViewCommands`。G01历史是查看DropDownButton及旧排序控件；G02当前改为标准CommandBar/AppBarButton，新建/排序/查看Flyout和原生More，旧选择器/方向按钮已移除。
- [CB-OVERFLOW]：独立Release probe正常exit0，实际Bar类ApplicationBar；overflow真实UIA入口为OverflowPopup（Popup / Window），不是源码内部Presenter样式ID TopMoreMenu。
- [CB-PRESENTATION]/[CB-MODEL]/[CB-APP]：展示helper、纯派生能力和窗口绑定分别负责控件、状态判断和调用原命令；helper不执行I/O。菜单/overflow在Opening捕获上下文并在动作重验，切页/代次变更关闭旧菜单。
- [CB-TESTS]与[CB-LIFECYCLE]：新增26项检查（命令栏23项、搜索生命周期3项），修正后两配置各1542/0/4；不代表新建/传输/删除/溢出的GUI通过。[CB-GUI]为正式18项脚本入口，[CB-UI]为修正后最终18/0/0报告；交接归[CB-HANDOFF]。
- [F-MENU]：`ShowFolderContextMenu / MakeFolderMenuItem`。背景无row_index；单选/多选捕获已提交选择；右键已选项保持集合，未选项改为单选。分隔线和图标是WinUI样式，不代表支持所有Explorer现代动词。
- [F-POLICY]：`EvaluateFolderCommand`按位置、选择数、目标、busy判断。此电脑禁用文件系统操作与排序；多选Open/Rename禁用。普通文件夹右键Paste目标为当前目录，搜索为命中所在目录。
- [HOST]：helper的`SHParseDisplayName / GetUIObjectOf / QueryContextMenu`、IContextMenu2/3消息转发、TrackPopupMenuEx、InvokeCommand；单/多选身份核对、同父数组和取消。stdout选择回复不声称原生操作完成；选择后停止取消监听、STA按线程引用/可见窗口延寿，无无意义强杀操作超时。没有完整IExplorerCommand现代扩展枚举。
- [SHELL-PROTOCOL]：`ShellMenuCommand`枚举与CLI/回复解析；ID1–10为Blink便利/标准动词，系统ID为运行时offset。多选标准Open/Rename仍回Blink策略，不能把Shell允许动作当Blink已允许。
- H-FOLDER21 / H-SEARCH18：`FOLDER_MENU_SYNC_HANDOFF.md`与`SEARCH_THEMED_MENU_HANDOFF.md`记录的历史真实窗口21+18检查。覆盖文件/路径复制、文件夹打开、背景新建/刷新/改名、视图/排序、普通只读属性、搜索系统属性、同父多选系统菜单与取消，以及一款已安装压缩扩展的真实ZIP名称和字节。此历史不是所有文件类型、全部属性页、所有厂商扩展或24H2验收；本轮P0使用下面最终报告中的真实check名称，原拟定但未使用的Test ID不当作已跑。
- 本地保留证据`.build/folder-menu-sync/integration`、`.build/search-themed-menu/integration`；这些目录被忽略，不能假定公开clone包含原始运行文件。
- [EXP25]：本轮主模型只读采集`.build/explorer-ui-p0-view-menu/integration/explorer-reference.json`。View UIA有11项；Show数组含父层11项，实际新增5项，不能按16条当16个Show子项。View直属Details pane / Preview pane；Show为Navigation pane / Compact view / Item check boxes / File name extensions / Hidden items。仅zh-CN标签是实机记录，英文为目标语义。自有83字节fixture及窗口已清理/正常关闭，未改系统开关。
- 代码所示字体菜单图标统一为Segoe Fluent Icons；G02主/次AppBarButton使用实际Fluent图标；旧ComboBox图标事实仅属阶段A历史。Shell扩展owner-draw与图标不得转换成硬编码品牌静态WinUI菜单。
- 普通目录/搜索的Shift+F10仍按历史39项证据，不冒充本轮重跑。P0 check10仅验证自有窗口里Space、方向键、Enter、Escape导航Show并返回FolderViewMode焦点；check13仅覆盖地址文本焦点下Ctrl+A/Delete不触发文件动作。100/125/150/200%、高对比度、触摸、左右屏边展开、其它IME/语言与24H2焦点仍需独立矩阵。
- 回收站删除已实现，不等于回收站浏览/还原/清空；Provider原型存在，不等于SFTP/网盘菜单已接产品；普通路径可表示UNC，不等于网络共享的写操作、凭据、断线UI已完整验证。

## G01历史最终GUI报告（限定旧候选范围）

证据：[P0-GUI]；`status=PASS, passed=true, skipped=false`，15条check全部PASS；`process_exited=true, exit_code=0, forced_termination=false`。G01候选Release Blink.exe SHA256为 `940DAD8BEA687BDCC5FB6CAC71A2D19665845F048D90C3BCEEC98B4A35B342B2`。报告中`fixture_removed=true, fixture_bytes_removed=1015, fixture_retained=false, fixture_ownership_failures=[]`；`compiler_temp_removed=true`，其释放字节未记录，不虚构数值。

执行环境按报告为Windows 11 25H2 build26200.9711、zh-CN、普通用户、64位，`window_dpi=96`；`theme="not captured"`。输入仅为自有PID/HWND的InputSite键盘消息和WinUI UIA模式，没有全局输入、剪贴板或文件变更命令。实物人工验收、24H2、主题/DPI/语言矩阵、树展开、搜索Hidden、Narrator、菜单视觉截屏对比仍为NOT RUN。

| 报告实际check名称 | 结果 |
| --- | --- |
| 01 owned production window commits only the GUID fixture | PASS |
| 02 top View is the owned DropDownButton with real exclusive commands and Show | PASS |
| 03 Details List and Grid change actual layout while preserving path selection order and sort | PASS |
| 04 ordinary ContextView command and top View selection stay synchronized | PASS |
| 05 Show Hidden reveals actual hidden file and directory but excludes both SYSTEM forms | PASS |
| 06 hiding items removes hidden selection without resurrecting it | PASS |
| 07 two tabs independently retain real Grid and List layouts and paths | PASS |
| 08 Hidden is synchronized across existing tabs and inherited by a new tab | PASS |
| 09 stale top View peer cannot change the next tab or its original tab | PASS |
| 10 Space arrows Enter and Escape navigate Show using owned keyboard input and return focus | PASS |
| 11 empty local directory supports actual layouts and Show without stale rows | PASS |
| 12 ThisPC offers real view choices and retains virtual location semantics | PASS |
| 13 text-focused Ctrl A and Delete edit only address text and preserve selected file and View | PASS |
| 14 owned Blink closes normally | PASS |
| 15 exact owned marker bytes and paths verified before fixture cleanup | PASS |

字段级证据：`menu_snapshots`记录TopViewGrid/List/Details的ToggleMenuFlyoutItem与唯一勾选；`layout_snapshots`记录真实ListView/GridView、Details四列显隐、地址、行序和选中项；`tab_evidence`记录两标签Grid/List及路径隔离；`stale_peer.new_tab_unchanged/original_tab_unchanged=true`只证明被测过期peer未改两页。`keyboard_evidence`记录Space/Home/Down/Right/Enter及Escape序列、Hidden前后变化和返回焦点ID FolderViewMode。`not_run`与`notes`是限制来源，不能省略。

## G02当前验证状态

Hidden/Search生命周期修正后Debug/Release均完成完整构建与普通回归：240冻结输入哈希匹配、375套中英/fallback资源，各1542 PASS/0 FAIL/4符号链接权限1314 SKIP。启动结构probe已确认真实类ApplicationBar、8个AppBarButton和系统MoreButton。独立Release探针[CB-OVERFLOW]成功：原生overflow实际为AutomationId=OverflowPopup、ClassName=Popup、ControlType.Window；ControlView链为CommandSelectAll→OverflowPopup→ExplorerCommandBar，RawView另有无ID ScrollViewer。TopMoreMenu只是源码Style给Presenter设置的ID，本机未暴露为UIA入口，不能写成已验菜单定位ID。结构存在不等于窄宽动态overflow、菜单键盘或全部动作已验证。

`TabWindowState::SetVisibilityPolicy`原先先执行`LeaveSearch()`取消当前查询，又额外调用`SearchCoordinator::Shutdown()`，将所有已有标签的搜索状态永久置为Closed；随后`SearchState::Begin()`恒为0。独立只测搜索会成功，完整脚本执行#08 Hidden开关后，#14搜索连续失败两次，才暴露这一真实组合问题。修复仅删除visibility切换中的多余Shutdown，保留LeaveSearch取消/离开语义、CloseTab和全局Shutdown的永久关闭职责。新增3个模型回归覆盖现有前后台标签的旧代次取消、显示/隐藏后均可重启，以及关标签只永久关闭被关标签且不影响存活标签。

修正前正式GUI曾出现13个子项PASS，但在#08 Hidden之后#14连续失败两次；这份红尝试只用于定位问题，不能当最终验证。私有只搜索探针3 PASS同样仅属诊断，不能替代完整场景。修正后Release同一完整18项GUI最终18 PASS/0 FAIL/0 SKIP，报告归`ui-command-bar-report.json`；#08 Hidden后#14真实索引结果、PasteParent实际目标和字节核对串联通过。早期第09的harness旧TopMoreMenu定位问题已通过真实OverflowPopup校正，与本次搜索生命周期缺陷分开记录。 本页仅将实际覆盖的24条顶部入口列`IMPLEMENTED_VERIFIED`；Name/Modified/Type/Ascending4个排序叶尚无本轮点击证据，保留`IMPLEMENTED_NEEDS_MANUAL`，不由#08外推为PASS。各行已注明Search仅启用态或未点击的动态分支，普通回收站本轮仍NOT RUN。G01表内15条PASS保留历史范围，不升级为G02或全部P0通过。完整主题/DPI、24H2、IME/Narrator及所有右键仍遵循各自未验边界。

最终[CB-UI]运行时间为2026-11-09 05:41:20–05:45:02 UTC，候选SHA256 `5AFE3C46D519EF13C378FF7539C80821F313A860820949FBC5A5CF7F8E41662A`；环境为25H2/build26200.9711、zh-CN、非管理员，window_dpi=96、theme="not captured"。18 checks全部PASS；进程正常exit0、无强杀；自有fixture已删除3152字节，compiler-temp已清理但释放字节未记录，clipboard_restored=true。这不是24H2/全主题DPI/人工验收，默认发布状态见[CB-HANDOFF]，不由GUI结果推断默认EXE已替换。

## 尚未实机核验的Explorer行为

24H2全菜单、25H2的New/Sort/More与全部12类右键、设备插拔/光驱/映射盘、ShellNew模板、现代IExplorerCommand扩展、无关联/图片/媒体/脚本/lnk类型差异、系统属性全页、跨显示器展开与高对比度均为NOT RUN。导航“主页”真实对象、标签右键条目和地址层级行为不能从当前WinUI控件或任务书示例推定为Windows固定行为。本轮25H2 View/Show UIA结构只是观测事实，不宣称像素级一致。

## 复核行数的方法

在项目根目录只读执行下面Python；只计11列且第9列为合法状态的数据行，排除统计表、说明和表头。结果应与上方合计一致；更新状态时同步统计表与本页基线/验证说明。

~~~python
from pathlib import Path
from collections import Counter, defaultdict
allowed = {
    "IMPLEMENTED_VERIFIED", "IMPLEMENTED_NEEDS_MANUAL", "PARTIAL",
    "DEFERRED", "UNSUPPORTED_BY_DESIGN", "OS_DEPENDENT",
}
counts = defaultdict(Counter)
for line in Path("docs/EXPLORER_MENU_MATRIX.md").read_text(encoding="utf-8").splitlines():
    parts = [v.strip() for v in line.strip().strip("|").split("|")]
    if line.startswith("|") and len(parts) == 11 and parts[8] in allowed:
        counts[parts[0]][parts[8]] += 1
print({scene: dict(values) for scene, values in counts.items()})
print("rows", sum(sum(values.values()) for values in counts.values()))
~~~

[TOP]: ../src/Blink/src/main.cpp
[F-MENU]: ../src/Blink/src/main.cpp#L3203
[F-CMD]: ../src/Blink/src/main.cpp#L3021
[F-POLICY]: ../src/Blink/include/Blink/App/FolderCommandPolicy.h#L31
[F-SYSTEM]: ../src/Blink/src/main.cpp#L3151
[F-KEY]: ../src/Blink/src/main.cpp#L3454
[VIEW]: ../src/Blink/src/FolderViewPresentation.cpp#L8
[SORT]: ../src/Blink/src/TabState.cpp#L599
[HIDDEN]: ../src/Blink/include/Blink/App/ItemVisibilityPolicy.h#L8
[SEARCH-MENU]: ../src/Blink/src/SearchContextMenuPresentation.cpp#L72
[SEARCH-ACTION]: ../src/Blink/src/main.cpp#L5087
[SEARCH-INPUT]: ../src/Blink/src/SearchPresentation.cpp#L113
[NAV]: ../src/Blink/src/main.cpp#L959
[LOCATION]: ../src/Blink/src/Location.cpp#L53
[TABS]: ../src/Blink/src/main.cpp#L817
[ADDRESS]: ../src/Blink/src/main.cpp#L902
[COLUMNS]: ../src/Blink/src/main.cpp#L1069
[HOST]: ../src/Blink.ShellMenuHost/src/main.cpp#L474
[SHELL-PROTOCOL]: ../src/Blink/include/Blink/App/ShellContextMenu.h#L10
[DELETE]: ../src/Blink/src/DeleteOperation.cpp#L1
[LEASE]: ../src/Blink/src/DirectoryLease.cpp#L1
[PROVIDER]: ../src/Blink.Provider.Contracts/include/Blink/Provider/Contracts.h#L1
[SCOPE]: PRODUCT_SCOPE.md
[MENU-HANDOFF]: FOLDER_MENU_SYNC_HANDOFF.md
[EXP25]: VALIDATION_GUIDE.md
<!-- 原始报告路径: ../.build/explorer-ui-p0-view-menu/integration/explorer-reference.json；当前链接为交接摘要，旧目录已清理 -->

[TOP-VIEW]: ../src/Blink/src/main.cpp

[P0-GUI]: VALIDATION_GUIDE.md
<!-- 原始报告路径: ../.build/explorer-ui-p0-view-menu/integration/ui-view-menu-report.json；当前链接为交接摘要，旧目录已清理 -->

[CB-PRESENTATION]: ../src/Blink/src/CommandBarPresentation.cpp
[CB-MODEL]: ../src/Blink/include/Blink/App/ExplorerCommandBarModel.h
[CB-APP]: ../src/Blink/src/main.cpp
[CB-TESTS]: ../tests/Blink.Phase1.Tests/src/ExplorerCommandBarTests.cpp
[CB-HANDOFF]: EXPLORER_COMMAND_BAR_HANDOFF.md
[CB-GUI]: ../scripts/run-blink-command-bar-smoke.ps1
[CB-OVERFLOW]: EXPLORER_COMMAND_BAR_HANDOFF.md
<!-- 原始报告路径: ../.build/explorer-command-bar/integration/ui-overflow-probe.json；当前链接为交接摘要，旧目录已清理 -->
[CB-LIFECYCLE]: ../tests/Blink.Phase1.Tests/src/main.cpp
[CB-UI]: EXPLORER_COMMAND_BAR_HANDOFF.md
<!-- 原始报告路径: ../.build/explorer-command-bar/integration/ui-command-bar-report.json；当前链接为交接摘要，旧目录已清理 -->

- [TARGET]：main `TargetMenuContext/ShowTargetContextMenu/ShowNavigationContextMenu/ShowTargetProperties`，复用 `CaptureDirectoryShellMenuRequest`、原lazy loader与文本剪贴板发布；导航目标与当前文件列表选择独立。G06a只交付常用动作，树文件操作/其它对象菜单保持原缺口。

- [NAV-TRANSFER]：main CopyTargetDirectory/PasteTargetDirectory/PublishFileCopySources，CaptureDirectoryClipboardSource及FileDropOrigin，复用原CopyItems/MOVE/cache；已加载树按真实目录操作与F5串行刷新。真实Clipboard传输报告=SKIP，不可将未执行动作等同入口检查。

- [NAV-MUTATION]：main BeginTargetDirectoryOperation/CaptureNavigationTarget、CaptureDirectoryOperationTarget与原名称/删除对话框、唯一coordinator。源码已有实现，当前窗口独立功能仍待验证；见EXPLORER_NAVIGATION_MUTATION_HANDOFF。
