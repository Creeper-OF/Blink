# G04a 搜索快捷键与区域焦点

日期：2026-11-11。G04a已完成双配置构建/回归、最终同一Release完整23项窗口检查和默认版本发布。结果归`.build/explorer-focus/integration`，完整G04/P0仍为部分完成。

## 行为

F3与既有Ctrl+F/Ctrl+E共用搜索框聚焦和全选入口，只编辑当前标签的查询草稿，不自动提交搜索。F6依次在文件区、地址栏、搜索框、命令栏、导航栏、标签栏之间切换，Shift+F6反向循环。这是Blink明确的区域顺序，不将其称为所有Explorer版本一致的逐控件顺序；区域内部Tab、方向键和控件原快捷键保留。

区域由实际焦点的视觉祖先识别，不维护第二套焦点状态。搜索可见时，文件区对应搜索结果而非隐藏的普通列表；空列表保留键盘入口。命令栏用WinUI查找当前第一个可聚焦控件，包含原生“更多”，导航栏优先驱动器树，标签栏聚焦当前标签。地址区进入既有地址编辑器；离开不提交草稿，返回时仍按原编辑规则全选。切换不修改目录、文件选择、索引或操作队列。

操作对话框打开、菜单项、展开的原生ComboBox、其他输入框和命名输入保留自身键盘输入。地址已有IME组合状态可只读查询；搜索框记录原生组合开始/结束，组合期间不把Enter/Esc或本轮焦点快捷键当额外命令。真实输入法候选与全部菜单/ComboBox组合仍需实机矩阵，不由事件接线推断通过。

## 验证边界

本轮不新增与实现重复的纯策略测试；Debug/Release各1583 PASS、0 FAIL、4既有符号链接权限SKIP，冻结249项原生输入。窗口脚本`run-blink-focus-smoke.ps1`直接检查FocusedElement及祖先、真实选择、地址草稿、磁盘文件身份和字节。共23项，包括ThisPC/目录/空目录/实际搜索/零结果页的正反循环、F3共享入口、文本Delete、模态命名和编辑中正常关闭。

最终候选SHA256 `bd48b60ff238653a42714d99f4881eab771a6b4e4cb6553bb99bf28c86b40857`；双配置各1583 PASS/0 FAIL/4既有权限SKIP，249原生输入/378资源。完整23项在同一次Release运行中全部通过，PID30420正常退出0、44字节自有夹具已清，未拼接早期候选结果。

早期候选第18项未进入命令栏，诊断时Windows已经锁屏；解锁后独立补测确认了真实入口遗漏：零结果页无可用主命令，原生“更多”仍可用，只遍历PrimaryCommands会跳过命令栏。修正为`FocusManager::FindFirstFocusableElement`在真实范围内查找，沿用原生模板与能力判断。修正后重新构建两配置并完成全部23项；旧锁屏/失败记录只作诊断。公开脚本默认跑全组，`-Phase Remaining`只补后六项，不独立声明23项通过。

## 发布与清理

旧默认PID30576空闲核对后正常退出0；发布6个变化文件并核对四关键产物SHA，新默认PID32480可见，正式索引服务PID6848与在用服务二进制保持不变。249源码/11产物再次核对；不启动新索引构建。

清理34个归属路径的本轮夹具、重复候选运行与服务输出，共229695670字节、338文件/208目录和4个测试联接。保留当前有效增量缓存、15个包版本和最近两份成功恢复点；一次性过程材料先入成功恢复包再删，具体见`cleanup-result.json`与`recovery-retention.json`。

仅操作新PID/HWND与GUID自有夹具，不用全局输入、不改变剪贴板或正式服务、不强制结束进程。真实IME/UNC、完整主题/DPI/多屏/读屏及全部原生组合保持独立。

## 依据

- [微软键盘交互](https://learn.microsoft.com/en-us/windows/apps/develop/input/keyboard-interactions)：F6在主要区域间切换，Shift+F6反向。
- [微软键盘无障碍](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/keyboard-accessibility)：应用需显式实现F6；区域内仍使用标准键盘导航。
- [WinUI首个可聚焦控件](https://learn.microsoft.com/en-us/windows/windows-app-sdk/api/winrt/microsoft.ui.xaml.input.focusmanager.findfirstfocusableelement)：在指定真实控件范围内查找焦点目标。
- 产品原有窗口、命令、导航及搜索规则见`EXPLORER_UI_SPEC.md`；完整G04/P0仍按实际覆盖范围追踪。
