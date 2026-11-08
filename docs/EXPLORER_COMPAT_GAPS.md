# Explorer 兼容差距与切片顺序

2026-11-08。接受用户提供的[任务书](EXPLORER_UI_MASTER_PROMPT.md)，以 `7135a14` 为阶段 A 的源码基线。该文档与[UI 规范](EXPLORER_UI_SPEC.md)、[菜单台账](EXPLORER_MENU_MATRIX.md)、[交互矩阵](EXPLORER_INTERACTION_TESTS.md)共同记录事实；下面的优先级是实施顺序，不是完成比例。

## 基线与证据边界

- 已有本地浏览/多标签/三视图/选择、异步目录监听、MFT/USN 常驻索引、多盘范围搜索、文件关联打开、复制/移动/目录合并、覆盖、回收站/永久删除、属性统计、剪贴板及拖放管线；以真实源码和最新 Slice21 为准，不能依据历史计划中的“尚未开始”否认现状。
- 最近成功记录为 Debug/Release 各1516 PASS、0 FAIL、4项符号链接权限1314 SKIP，235个原生输入、368个中英/fallback资源键；Slice21的8项GUI与此前菜单39项是历史已执行证据，不冒称本轮新跑。
- 本轮真实参考主机为 Windows 11 家庭版 **25H2 / 10.0.26200.9711 / zh-CN / 125%**，自有 Explorer 窗口呈现深色。任务书目标24H2、其他主题/倍率、多显示器和文本放大本轮 **NOT RUN**。
- Explorer 仅在新 HWND 的精确 GUID fixture 中观察“查看/显示”。主菜单11项，显示子菜单新增5项；完整Show树的16项包含父层11项，不能当作16个子命令。主菜单包括8种视图、详情/预览窗格和显示；显示包括导航窗格、紧凑视图、项目复选框、扩展名、隐藏项目。这是本机版本事实，不硬编码成所有版本的唯一列表。
- 实际主窗口截图仅用于布局参考，PrintWindow未捕获弹出菜单。菜单内容证据来自UIA；没有菜单像素对照或人工验收结论。未切换Explorer开关，测试窗口正常关闭，83字节fixture已清理。
- 原始证据归 `.build/explorer-ui-p0-view-menu/integration/explorer-reference.json` 和 `reference/explorer-owned-window.png`，不上传本机截图、窗口dump或索引数据。

## 差距台账

状态仅使用任务书约定的六类。已有后端但缺少前台入口标 PARTIAL；系统注册、设备和账户决定的能力标 OS_DEPENDENT；后续规划标 DEFERRED。具体每条菜单及测试编号在另外两份台账中维护。

| ID | 优先级 | 现状/差距 | 状态 | 真实入口与复用边界 | 取舍/风险与验收 |
| --- | --- | --- | --- | --- | --- |
| G01 | P0，本轮唯一切片已交付 | 顶部已改真实查看/显示菜单：Grid/List/Details及Hidden | IMPLEMENTED_VERIFIED | main `BuildFolderViewCommands/SetFolderViewMode/SetShowHiddenItems`，`FolderCommandPolicy`、`ContextMenuAppearance` | 只收拢三个已实现视图与隐藏项；复用每标签视图、窗口级visibility、既有命令；用实际布局、勾选、双标签和stale peer验证，不塞预览等占位 |
| G02 | P0，后续 | 完整New/Cut/Copy/Paste/Rename/Delete/Sort/View/More CommandBar及窄宽溢出缺失；排序仍ComboBox | PARTIAL | main现有文件区命令与 `ExecuteFolderCommand`、`FolderCommandPolicy`、搜索意图 | 另一个独立界面切片，按位置/选择计算能力；不得建立第二套文件操作或把搜索目标当普通目录 |
| G03 | P0，后续 | 地址为常驻TextBox；面包屑、Alt+D、Esc恢复/返回列表、F4候选未接 | PARTIAL | main `BuildUi/RestoreNavigationUi/BrowseAsync`；`TabState/NavigationModel/Location` | 先补编辑焦点闭环再面包屑；ThisPC保持虚拟Location；历史候选有真实数据后才开放 |
| G04 | P0，后续 | F3/F6焦点循环等未显式接线，TabView和ListView部分快捷键由控件提供 | IMPLEMENTED_NEEDS_MANUAL | main `AddAccelerator/CreateFolderViewControl`、WinUI TabView/Extended Selection | 不以未手写CtrlTab认定功能完全缺失；逐一实测标题/地址/搜索/列表焦点，避免重复accelerator，文本编辑优先 |
| G05 | P0，后续 | F2对话框固定SelectAll，普通文件扩展名也全选 | PARTIAL | `FolderNameDialog.cpp`、普通/搜索重命名共同入口 | 独立补主名选区；目录、dotfile、多点名、IME、Enter/Esc与文本Ctrl+A/Delete覆盖，不改变rename引擎 |
| G06 | P0，后续 | 文件/目录/多选/搜索有Blink主菜单，导航树/驱动器/虚拟背景的专属菜单未完整接入 | PARTIAL | main `ShowFolderContextMenu/ShowSearchContextMenu`、`SearchContextMenu`、`NavigationTree` | 按12场景补菜单；盘根不套普通文件Replace/Delete，设备动作须适用且有真实接口，不能加空按钮 |
| G07 | P0，后续 | Shell菜单单/同父多选与2/3消息转发已做；host无wall-clock deadline，现代IExplorerCommand未对接 | PARTIAL | `ShellContextMenuController::Dispatch`、`Blink.ShellMenuHost` | 进程隔离不等于超时已覆盖；先验证实际挂起/取消边界再做必要处理；动态扩展按系统返回，不硬编码软件名，不进Blink主进程 |
| G08 | P0，后续 | 标准COPY/MOVE剪贴板有Preferred；外部performed完成回传与剪切淡化闭环不足 | PARTIAL | `ClipboardFileTransfer`、main剪贴板缓存及唯一coordinator；OLE拖放现有完成反馈另计 | 保留真实sequence/源身份与部分完成；Explorer双向互操作必须实测，不能把“已发布剪贴板”写成已移动 |
| G09 | P0，后续 | 同/跨卷拖放、Ctrl/Shift与OLE完成已有；link、右键拖放选择菜单、虚拟文件未接 | PARTIAL | `FileDragDropController/Model/Data`与 `FileDragDropTests` | 现有操作如实验证；未支持的effect返回NONE。FileDescriptor/FileContents不是CF_HDROP的既有能力 |
| G10 | P0，后续验证 | 文件操作真实partial/取消/冲突已有，组合状态和更直观进度仍需验收 | IMPLEMENTED_NEEDS_MANUAL | `FileOperationCoordinator/MoveTransferPresentation`及既有对话框 | 继续唯一任务队列，不增加假暂停/剩余时间；失败重试不能重放成功前缀，回收站失败不降级永久删除 |
| G11 | P1 | 普通目录Properties为Blink只读统计，搜索Properties走系统页；盘根和多选系统页未统一 | PARTIAL | `PropertiesDialog/ItemProperties`及ShellHost properties命令 | 优先按对象调用真实系统属性；Blink只读详情作为补充，不能假称ACL/共享可编辑 |
| G12 | P1 | 三视图已有，其余图标尺寸/平铺/内容、分组和列宽/重排/选择列缺失 | PARTIAL | `FolderViewPresentation/Formatting`、`SortDescriptor`、详情列头 | 分组不是排序，不显示尚未实现视图；排序状态继续每标签持有，逐个功能单独验证 |
| G13 | P1 | 导航单滚动、lazy目录树已有；图片/音乐/视频、固定管理/主页、导航菜单不完整 | PARTIAL | `NavigationPane/Tree/Model`、main单ScrollViewer | 不重复遍历整盘、不造假的Shell文件夹；只扩充实际KnownFolder或明确系统跳转，个人固定位置需自己的数据模型 |
| G14 | P1 | 详情窗格/预览/缩略图未产品化，属性库/Provider原型不能代替前台功能 | DEFERRED | `Blink.Properties`、`PropertiesDialog`；未来独立metadata/preview边界 | 不提前开放无处理能力菜单，第三方预览不在主UI无限等待；格式与故障处理按实际后端落实 |
| G15 | P1 | 标题/Mica/浅深色与基础UIA已做，激活、高对比、全部缩放/多屏/文本放大未完成矩阵 | IMPLEMENTED_NEEDS_MANUAL | main `BuildWindowFrame/SyncWindowFrameTheme`、App.xaml、现有框架检查 | 保留系统caption按钮/Snap/命中测试，不用自绘替换；本轮125%观察不能代替所有倍率和读屏验收 |
| G16 | P1 | 每标签有路径/历史/排序/视图/选择/search；地址草稿、滚动offset与完整会话持久化不完整 | PARTIAL | `TabRecord/TabUiState/TabWindowState`、`PresentActiveTab` | 不把UI控件变成唯一真相源；用户切标签/关闭不能复活异步结果；会话落盘另立切片 |
| G17 | P1 | New仅新建目录，ShellNew模板/快捷方式、新更多菜单及Blink设置页未接 | PARTIAL | `NewFolderOperation/FolderNameDialog/FolderCommandPolicy` | 系统Office等注册项动态获取；无实际模板不伪造Word/ZIP；设置仅承诺Blink自身可兑现部分 |
| G18 | P1 | 本地索引搜索可用；同卷Busy/保存时等待与UI首屏仍待专项 | PARTIAL | `SearchState/Coordinator/Presentation`、`Blink.Index*` | UI任务不改索引模型/IPC/BLK5，不用全盘DFS掩盖服务离线；性能另给量化证据 |
| G19 | P2 | 回收站浏览/恢复/清空、操作Undo/Redo未产品化 | DEFERRED | 当前仅 `DeleteOperation` 回收站提交，非回收站浏览器 | 不绑定假Ctrl+Z/Y，输入框自身编辑撤销仍由文本控件处理；不可把清空放普通目录菜单 |
| G20 | P2 | 特殊Shell Namespace、MTP、图库、库、网络发现、云盘状态未原生管理 | DEFERRED | `Location`当前ThisPC/本地folder，Provider为原型 | 逐项区分系统跳转/有限操作/暂缓；路径不存在时不能造“文件夹”，不承诺替代完整Shell Namespace |
| G21 | P2 | SFTP/WebDAV/自建网盘仍属既有后续Provider路线 | DEFERRED | `Blink.Provider.Contracts/Client/Host/Fake` | 保留原架构，协议权限/断线/上传状态按远端事实；系统已挂载盘不等于Blink已有SFTP连接管理 |
| G22 | P2，非默认路线 | 系统默认管理器/Win+E/注册表Shell接管不在此次UI任务 | UNSUPPORTED_BY_DESIGN | 无对应安装或关联改动 | 需另立明确任务才考虑，不以外观兼容为由更改系统配置 |
| G23 | 环境条件 | UNC/网络映射、可移动/光驱、云占位、注册动词及第三方菜单 | OS_DEPENDENT | 普通本地API及Shell返回能力；相应网络/设备实测未做 | 不把synthetic路径测试写成真实SMB/设备兼容，不静默提权或强行使用本地专属操作 |

## 执行顺序与本轮边界

A. 三代理只读审计UI、菜单、交互，主模型复核源模型/历史、采集自有Explorer参考，先提交四份规范与原始任务书。此阶段不修改业务代码。

B. **只实施 G01**：顶部查看MenuFlyout含现有三视图，显示子菜单含隐藏项目；文案/图标/勾选/Tooltip/UIA共用WinUI和已有资源。开菜单时从TabState/窗口visibility读取，调用原命令。旧peer不能修改新标签或新目录，搜索面板仍按既有边界隐藏目录视图工具条。

C. 本切片Debug/Release及真实UI检查已通过（各1516/0/4、GUI15/0/0），G01仅按这些动作范围验证，再按G02/G03等逐个推进。后续P1/P2只登记，不在本提交里顺手实现。

### G01 交付前必须取得的证据

- 三个菜单项的唯一勾选与实际Details/List/Grid控件/列头一致；普通右键切换后再开顶部菜单，同一权威view状态。
- 显示子菜单真实展开；隐藏项的当前/其他标签、目录树策略一致，系统属性项保持原策略，外部搜索visibility入口能同步。
- 切标签、导航、关闭之后旧菜单不作用于新位置；菜单Esc关闭和焦点返回在自有窗口中验证；排序仍使用既有入口，不误改其默认值。
- 本轮限定环境的真实窗口自动化和Debug/Release回归通过，资源键一致；24H2/其余DPI/主题/IMEs/网络等按交互矩阵仍NOT RUN，不升级整项目或整个P0验收。

风险限于WinUI菜单生成、既有命令绑定和展示状态；索引服务、协议、存储、文件操作引擎与ShellHost均不在此切片修改范围。失败修原因，不通过关闭测试或添加无动作按钮掩盖问题。

## 本轮P0实测更新

最终235输入/369资源、Debug/Release各1516 PASS/0 FAIL/4权限SKIP；15项自有窗口检查通过，涵盖新菜单、实际三布局、selection/order/sort保持、Hidden/SYSTEM、双现有及新Tab、旧peer、纯键盘菜单和焦点、空目录/ThisPC、地址Ctrl+A/Delete作用域，以及正常关闭/归属清理。原始报告ui-view-menu-report.json记录Blink窗口DPI96、主题未捕获；Explorer参考窗口DPI120，两者不能混同。树展开与搜索Hidden联动、本轮未覆盖的其他焦点/IME/主题/DPI/24H2仍NOT RUN。G01功能已验证不代表全部P0或全部视觉/人工矩阵完成。
