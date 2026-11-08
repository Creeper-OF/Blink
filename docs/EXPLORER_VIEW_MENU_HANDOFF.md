# Explorer UI 阶段A与首个P0：查看/显示菜单

用户任务书已原样归档到EXPLORER_UI_MASTER_PROMPT.md；先完成五份纯文档提交e497600（含UI/菜单/差距/交互四份规范），然后才改业务。审计基线7135a14及235原生输入已核对。

## 当前实现与验证

顶部视图Combo与独立隐藏CheckBox替换为WinUI DropDownButton/MenuFlyout，三个真实Grid/List/Details用原FolderCommand路径；显示子菜单只含已有Hidden功能。窗口visibility仍是所有标签共用，视图/排序/选择仍按标签独立。菜单打开时读取模型，复用现有菜单主题/样式和上下文代次；无新增视图/预览假入口、加速键、索引或文件操作引擎。

4个原生文件改动（main及两套resw/fallback），235输入不增，369资源键一致。最终Debug/Release完整构建及各1516 PASS/0 FAIL/4权限1314 SKIP通过；新公开入口scripts/run-blink-explorer-view-menu-smoke.ps1完成15 PASS/0 FAIL/0 SKIP，4份脚本静态解析/BOM检查通过。旧三份脚本适配新控件，原断言未放宽；早期context脚本的Copy尚未开放断言是历史限制，未拿它作为本轮菜单回归。

GUI实际核对真实三布局、唯一勾选、path/selection/order/sort保留、普通右键同步、Hidden/SYSTEM、两现有及新标签、旧peer、Space/箭头/Enter/Esc及焦点、空目录/ThisPC、地址文本Ctrl+A/Delete、正常退出0与精确归属清理。最终运行的Blink窗口DPI96、主题未捕获；不能当作125%或主题矩阵通过。树展开、搜索Hidden、IME、更多文本对话框、其他语言、24H2与完整人工矩阵仍NOT RUN。

原默认预览空闲检查后正常关闭，5个变化文件发布并核对EXE/PRI/helper/service哈希，新默认窗口PID3344可见并留给用户。IndexService与ShellHost输出相对Slice21未变，未启停服务、改协议或重建数据库。206条当前菜单台账为功能追踪，不是全面Explorer兼容声明。

本轮实际Explorer参考为25H2 build26200.9711、zh-CN、125%与深色主窗口；View/Show由自有窗口UIA确认11+5项，仅根窗口图可用，popup没有截图证据。24H2、其余主题/DPI/文本缩放、真实SMB/设备、完整人工体验NOT RUN。状态以integration最终记录和四份规范的实测更新为准，不升级全P0/整项目验收。

## 入口与产物

FolderViewMode现在是DropDownButton（AutomationName复用Command.View“查看”），HelpText为当前模型视图；TopViewMenu及TopViewGrid/List/Details、TopViewShow、TopViewShowHidden为真实菜单ID。Menu动作复用ExecuteFolderCommand/SetFolderViewMode/SetShowHiddenItems，排序入口保留。旧菜单在换标签/导航后隐藏并通过原上下文检查失效。

源码归src、测试脚本归scripts、规范归docs；本轮构建/UI/参考/发布/清理记录归.build/explorer-ui-p0-view-menu/integration。D:\DEV\C++工具链与缓存继续复用，构建compiler1、独立PowerShell隔离构建TEMP与跨盘测试。当前程序/依赖/真实索引及最近两成功恢复点保留。

## 排障与中间候选

首个GUI1PASS/1FAIL，唯一失败是harness假定短ClassName。只读自有窗口probe确认实际为Microsoft.UI.Xaml.Controls.DropDownButton、Button角色、Invoke及ExpandCollapse；随后改为精确完整类名并增加角色/接口断言，未放宽动作验证。第二轮旧文案候选已10PASS（含真实布局/Hidden/双标签/旧peer/键盘菜单），为更新最终查看文案而正常关闭；后续FAIL系主动结束旧候选，保留superseded-normal-close证据，不归因为产品功能失败。两轮fixture与Add-Type临时内容均已清理，未强杀。

自动化查找从全桌面UIA根枚举改为本窗口优先、同PID可见顶层HWND回退；所有可见/权限/真实菜单判据保持，避免其他窗口provider开销。最终结果必须来自当前字节的完整新运行。

## 复现与收尾

Windows PowerShell入口：`scripts/run-blink-explorer-view-menu-smoke.ps1 -Configuration Release`，`-Binary`可指定独立候选；原生构建/测试使用本轮integration中的run-config-checks分别执行Debug/Release，保持/m1。最新验收以native-validation.json与ui-view-menu-report.json的源/二进制哈希为准。归属清理、原始失败/主动结束证据、工作区自检、索引同步、公开发布及最近两个恢复点均归同一integration；源码、当前程序、有效依赖和真实索引保留。
