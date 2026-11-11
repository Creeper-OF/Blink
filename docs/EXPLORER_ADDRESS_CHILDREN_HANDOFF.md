# G03b 面包屑下级目录菜单

日期：2026-11-11。本轮补齐逐级目录菜单，自动完成建议仍待开发，完整 G03/P0 未完成。

地址栏每一级有独立下拉按钮，原有层级点击与 WinUI 原生收缩保留。打开某一级菜单时才后台枚举该目录的直接子目录，复用 `EnumerateDirectory` 和导航树的目录排序、隐藏项规则；不把普通文件或递归后代混进菜单。“此电脑”复用现有常用位置和可用本地驱动器，不映射为 C 盘。

`AddressBarPresentation` 只拥有菜单、草稿和一次请求快照；主窗口拥有一个 ThreadPool action，后台只枚举、产生值，回到 Dispatcher 后才能展示。选择项目沿既有 `NavigateToLocation` 入口。菜单关闭、切换标签、编辑地址、可见性策略变化和窗口退出都会退役旧请求；同标签、同已提交位置的自动刷新保留已打开菜单。请求取消是合作式取消，真正阻塞的文件系统枚举仍须等系统返回。

菜单使用原有 WinUI MenuFlyout 外观，提供加载中、无下级目录和真实失败文本。加载期间如出现文件操作对话框，完成回调先收起菜单，不抢对话框焦点。没有新增服务、索引格式、保护框架或路径权限预检。

`BreadcrumbBar.ItemTemplate` 使用标准 `IElementFactory` 为 inline/overflow 创建独立的原生 `BreadcrumbBarItem`，数据仍是每轮不可变的 PropertySet。原生项的 Name/HelpText 显式使用目录名和路径；子按钮的 Command 绑定同一个值目标。新容器在原生模板挂接 PreviewKeyDown 前注册按键：聚焦层级后 Down/Alt+Down 展开；聚焦子按钮后 Enter/Space 展开。普通层级 Enter/Space 仍导航，避免在 overflow 先关闭再触发 ItemClicked 时从已恢复的焦点猜测用户意图。 本机原生BreadcrumbBar从外部获得焦点时先进入“更多”或首可见项；测试用真实Right逐级到达目标后再Down，不把UIA对末级的SetFocus请求当作已获得焦点。

最终 Debug/Release 单并发构建，各1583 PASS/0 FAIL/4既有符号链接权限SKIP；249项冻结原生输入、382个en/zh/fallback资源一致。相同最终Release `3dc3f922779070363fe9df566b6ea4110ea2c0759122f244bdb11dee1c12a7d5` 完成下级菜单15项及既有地址栏20项GUI检查，新菜单两段的PID为[23716, 44876]，地址回归PID为46212，均正常exit0，无强杀，自有夹具已清。新菜单原始报告为 `ui-address-children-initial-report.json` 与 `ui-address-children-remaining-report.json`，合并索引为 `ui-address-children-report.json`；既有地址回归原始报告为 `ui-address-bar-report.json`，输入/输出摘要与普通回归见 `native-validation.json`。

15项覆盖ThisPC实际C/D、直接目录及文件排除、实际Enter/Space/Down和菜单选择、祖先菜单、空目录、Esc及Ctrl+L、隐藏项、标签旧peer、watcher刷新保留菜单、真实overflow子按钮Enter、删除自有空目录的真实失败、菜单打开时关窗和夹具清理。20项既有回归继续覆盖编辑/草稿、历史、层级导航、原生overflow及编辑中关闭。初轮帮助文本覆盖问题保留失败记录，不纳入最终通过数。

默认版本已发布并核对关键SHA，正式服务PID6848不重启，在用服务二进制不变。真实鼠标命中、IME/UNC/拒绝访问、真正阻塞枚举取消及完整主题/DPI/多屏/Narrator矩阵仍未验收。

复现菜单检查：

```powershell
.\scripts\run-blink-address-children-smoke.ps1 -Configuration Release -NativeOverflowName "更多"
```

默认验证保留的 `src/Blink/x64/Release/Blink.exe`；构建候选验证时可用 `-Binary` 指向本轮 runtime。发布后重复候选会清理。

`更多` 是本机先前实际 UIA 探针确认的原生收缩项名称，其他语言/版本须先读取真实 peer，不能猜 ID。脚本只操作新启动 PID/HWND、InputSite 键盘消息与 GUID 自有夹具，正常关窗，不用全局输入、剪贴板或修改服务。

构建保持单并发并保留每配置有效增量缓存；发布后清理重复 runtime/service-build、完成的测试夹具和归档后的过程文件，仅留最近两份成功源码恢复点。源码、用户数据、正式索引、配置与有效依赖均保留。

## 后续G03c — 2026-11-11

目录自动完成已接入，保留本页逐级目录菜单。当前实现、限定验证与边界见[自动完成交接](EXPLORER_ADDRESS_COMPLETION_HANDOFF.md)。
