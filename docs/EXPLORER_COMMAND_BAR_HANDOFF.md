# Explorer G02 顶部命令栏与隐藏项搜索修复

2026-11-09，承接5687170。主模型协调策略/模型回归、WinUI展示、受控UI脚本三个代理，统一集成和验证；构建单并发。工具沿用D:\DEV现有环境，全部本轮产物归`.build/explorer-command-bar`。

## 交付范围

导航下方独立命令行采用标准WinUI CommandBar：8个主入口（新建、剪切、复制、粘贴、重命名、删除、排序、查看）和原生More/动态溢出。更多提供全选、取消选择、反向选择、属性、刷新、复制完整路径和复制所在目录路径。新建仅提供已有新建文件夹；分享、ShellNew模板、分组等继续按台账待做。

展示helper不读文件、不判定能力；纯能力模型从TabState、Search及剪贴板格式派生，点击才捕获目标。目录和搜索仍调用原命令、剪贴板及唯一操作队列；Search Paste明确所选结果父目录，ThisPC不继承普通目录权限。排序/查看复用标签状态，Hidden复用窗口visibility。更多及菜单上下文随导航、标签、选择代次失效；More关闭延后一轮清理以适配原生Click时序。反选仅对可见有效RowIdentity取补集。

命令行横跨左右窗格，bar本体按内容宽度左对齐。真实UIA为ApplicationBar、主按钮AppBarButton；原生菜单为`OverflowPopup`（Popup/Window），位于本Bar子树，根IsOffscreen=true而真实子按钮可见。TopMoreMenu仅为未暴露的Presenter样式ID，测试不依赖它。名称/删除对话框实际peer也为Popup，按真实编辑框或确认/取消按钮和目标文本定位。

## 组合检查找到并修复的搜索缺陷

完整流程两次在Hidden切换后无法开始搜索，非空query读回正确，但快照为空；独立只跑搜索成功。根因位于[TabWindowState.cpp](../src/Blink/src/TabWindowState.cpp)：SetVisibilityPolicy先LeaveSearch取消，又调用SearchCoordinator::Shutdown，令全部旧tab的SearchState永久Closed，之后Begin恒返回0。

最小修正只删除可见性切换中的额外Shutdown。LeaveSearch保留原取消和代次失效；关闭标签/窗口仍永久退役相应状态。新增3项模型回归覆盖活动和后台旧查询失效、show/hide后旧tab重新搜索、关闭另一个tab不影响survivor且被关闭状态不可重开。原函数及修正前两配置各1539项的结果、完整失败/独立成功证据保存在`integration/attempts/visibility-search-shutdown`，不重建索引或修改服务。

## 实际验证

- 当前240个原生输入与构建字节一致，375个中英/fallback资源键一致。Debug、Release完整构建成功，各 **1542 PASS / 0 FAIL / 4权限1314 SKIP**；新增26项为23个命令栏/反选检查和3个搜索生命周期回归。
- 新Release同一完整流程 **18 PASS / 0 FAIL / 0 SKIP**（16功能检查及2项退出/清理）。覆盖0/1/多选能力、真实新建/复制/剪切移动/改名、Shift永久删除取消再确认、Size/Descending实际顺序、三视图与Hidden、更多选择/反选/路径/属性/刷新、旧菜单失效、ThisPC及键盘作用域。
- 640像素宽时Sort/View真实进入OverflowPopup；从该弹层切换List后为ListView且0列头，路径和选择保持；恢复宽度后重取View按钮，HelpText及唯一List勾选一致。
- **同一流程先执行Hidden开/关，再以真实高级范围查询本轮fixture，取得真实索引命中并粘贴到结果父目录。** 原浏览目录与结果父目录不同，验证实际目标及源/目标字节；随后再次真实搜索、复制完整路径。未注入结果，query逐步读回无丢字。
- 测试窗口正常退出0，无强杀；按精确路径/属性/字节清除3152字节fixture，临时编译目录清理完成（空集合字节为null，按0计）；原文本剪贴板已恢复。
- 实际主机Windows11 25H2/26200.9711、zh-CN、普通用户，自动化窗口DPI96，theme未采集。已观察真实发布主窗口截图为深色且命令行/左右窗格布局完整；无弹出菜单像素比对。24H2、完整主题/DPI/语言/IME/Narrator、多显示器、设备/UNC及本轮未运行的系统扩展/回收站分支仍NOT RUN。Sort其余4叶本轮未点击，不能据此升级整个P0或项目。

## 失败记录与收尾

首轮构建缺Controls.Primitives完整投影头而C3779，补头后通过；另修目录busy沿原拖放/冲突边界、More整体可用态及反选失败准确文案。测试迭代修正了typed参数Surface同名覆盖、对话框类名假设、未暴露More样式ID及script缓存被Check子作用域null遮蔽。失败报告及紧凑原因分别归attempts，不为每次重试复制完整源码或二进制。最终严格断言保留，并增强了窄窗真实列头/勾选和搜索逐步状态证据。

默认旧窗已关闭，发布6个变化文件，EXE/PRI/helper/service哈希一致；新默认窗口PID7052可见并留给用户。发布主窗口图归`integration/visuals/command-bar-window.png`，只证明当前主布局。索引服务PID6848持续运行，服务/IPC/BLK5及真实C/D索引不改。重复候选、冗余日志和空fixture由cleanup-runtime处理；两份恢复点、清理字节、公开提交及工作区索引以本轮integration的最终记录为准。

复现入口：[命令栏脚本](../scripts/run-blink-command-bar-smoke.ps1)。兼容旧View脚本本轮仅静态校验，不冒称重跑历史15项。下一切片按G03推进地址编辑闭环/面包屑，再处理F2选区和剩余焦点/菜单场景；完整项目与人工矩阵继续待做。
