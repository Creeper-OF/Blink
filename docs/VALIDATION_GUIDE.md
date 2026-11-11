# 功能验证指南

当前流程调整为：同一批功能集中实现 → 代码稳定后统一构建 → 每功能独立验证 → 仅重测失败及受影响项。全应用流程脚本放到本批功能稳定后补齐；当前不重新串跑所有历史 UI 脚本。

## 构建与原生回归

日常迭代优先一种配置，交付节点再集中验证另一配置。编译并发固定为 1，保留有效增量缓存。普通用户原生回归入口为：

```powershell
& .\x64\Debug\Blink.Phase1.Tests.exe
& .\x64\Release\Blink.Phase1.Tests.exe
```

本次原生回归全部检查约 33–54 秒，产品编译约 15–16 分钟。源码没变而脚本修正时，直接复测对应脚本，不重编产品。开发台构建使用本批 candidate OutDir，避免覆盖正在运行的正式索引服务；具体环境见 LOCAL_ENVIRONMENT。

## 当前侧栏目录功能

```powershell
$blinkBinary = '.build\explorer-navigation-mutation\runtime\Release\Blink.exe'
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature NewFolder -Binary $blinkBinary
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature Rename -Binary $blinkBinary
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature Delete -Binary $blinkBinary
```

三个入口分别预期 7、8、5 项检查，包含各自真实操作、取消/冲突或目标核对及正常关闭/精确清理。报告独立保存；共同检查不能重复当成新增功能数。现有 `-Feature All` 保留侧栏批次的 14 项检查兼容入口，不等于后续的全应用流程验收。

候选尚未发布时通过 `-Binary` 指向本批 runtime。G06c 当前原生双配置已过，窗口功能尚未完整通过；两次片段/前台中断不计作成功。用户使用电脑时不争抢前台，窗口检查另择时间。

## 其它维护入口

- 查看/隐藏：`run-blink-explorer-view-menu-smoke.ps1`。
- 地址：`run-blink-address-bar-smoke.ps1`、`run-blink-address-children-smoke.ps1`、`run-blink-address-completion-smoke.ps1`。
- 焦点与改名选择：`run-blink-focus-smoke.ps1`、`run-blink-rename-selection-smoke.ps1`。
- 目标菜单：`run-blink-object-menu-smoke.ps1`。
- 实际剪贴板传输：`run-blink-navigation-transfer-smoke.ps1`；原格式无法完整恢复时在启动/写入前 SKIP，不能清空用户图片来凑通过。

只运行本次改动涉及的入口。所有当前窗口检查须保留自有 PID/HWND、GUID 夹具、真实文件结果、正常退出及清理证据。性能测量另记首屏/服务 IPC/首次加载/重复查询，测试通过数量不是性能指标。

## 历史记录

旧 `run-blink-context-menu-smoke.ps1` 仍假设复制等功能未开放，旧 `run-blink-command-bar-smoke.ps1` 不能完整恢复图片/OLE剪贴板；二者已停止作为当前可执行回归入口，参数后直接说明并退出，历史主体只供对应提交追溯。

旧 View/Visibility/Wheel 等切片脚本和手工清单是各阶段历史材料，不自动代表当前控件树可用。当前验证优先上方维护入口；需要重现历史时使用对应提交，不在现版本直接跑旧预期。

旧构建目录按保留策略清理后，历史报告位置保留为文字溯源；公开阅读使用相应 `docs/*HANDOFF.md` 交接摘要。必要的最近原始报告位于本机恢复包或 `.build/maintenance/validation-history.json`，公开仓库不包含本机 `.build`。
