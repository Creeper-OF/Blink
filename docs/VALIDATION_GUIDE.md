# 功能验证指南

当前流程调整为：同一批功能集中实现 → 代码稳定后统一构建 → 每功能独立验证 → 仅重测失败及受影响项。全应用流程脚本放到本批功能稳定后补齐；当前不重新串跑所有历史 UI 脚本。

## 构建与原生回归

日常迭代优先一种配置，交付节点再集中验证另一配置。编译并发固定为 1，保留有效增量缓存。普通用户原生回归入口为：

```powershell
& .\x64\Debug\Blink.Phase1.Tests.exe
& .\x64\Release\Blink.Phase1.Tests.exe
```

本次原生回归全部检查约 33–54 秒，产品编译约 15–16 分钟。源码没变而脚本修正时，直接复测对应脚本，不重编产品。开发台构建使用本批 candidate OutDir，避免覆盖正在运行的正式索引服务；具体环境见 LOCAL_ENVIRONMENT。

## 侧栏系统菜单

真实普通目录及指向真实文件夹的常用位置可打开经典 Windows 菜单。路径取自点中的侧栏项目。系统注册的菜单项在原 ShellMenuHost 中运行。打开、复制、剪切、粘贴、改名和删除使用 Blink 的导航及文件操作流程，其中粘贴进入所点中的目录。虚拟位置和驱动器根使用各自的目标菜单。

运行 `Blink.Phase1.Tests.exe --suite shell-context-menu` 检查目标参数、真实身份、同父目录及旧普通项和属性请求。实际窗口检查通过已知路径和所属进程确认弹出的系统菜单，分别记录取消、内置命令和扩展的效果。菜单里出现压缩项时，可以在自有目录内调用并核对归档内容。

2026-11-20 原生菜单专项 54 项、详情专项 36 项通过，完整 Phase1 2006 PASS / 0 FAIL / 4 既有权限 SKIP。独立的宿主探针从 Unicode 夹具目录读到 33 个菜单项。Windows 11 实机从侧栏调用已安装的 Bandizip，将所点自有目录压成 ZIP，核对归档中唯一文件的名称和字节。原有文件和所选项通过校验，已清除自有测试归档。窗口工具从实际菜单窗口读取可见命令，再按该窗口的目标项操作。见[Win32 菜单信息接口](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmenubarinfo)。

同一 Release 的分项窗口报告覆盖树目录、常用主文件夹、虚拟此电脑与侧栏盘根的适用性、菜单取消、原生改名、原生打开、侧栏属性和普通文件菜单回归。关闭原生属性页后核对列表选中项。实际剪贴板操作、此电脑列表的设备命令及其它扩展继续独立验证。原始报告集中在本机 `.build/directory-system-menu/integration/`。

## 主文件区键盘菜单

```powershell
.\scripts\run-blink-object-menu-smoke.ps1 -KeyboardOnly
```

Apps 和 Shift+F10 使用当前列表焦点行。有其他盘符选中时，菜单仍对应实际聚焦的盘符。普通文件也可只聚焦后打开菜单，焦点位于已选中的文件时保留原有多选。空目录仍打开背景菜单。

专项脚本覆盖聚焦且未选中的驱动器、与焦点不同的选中项、当前标签及新标签打开确切盘根、普通文件项、多选和空目录。候选测试可用 `-Binary` 指定 Release 路径。窗口检查使用所属进程的 UIA 和定向键盘消息。物理右键、剪贴板和设备操作分别验证。

## 标签页菜单

```powershell
& .\x64\Release\Blink.Phase1.Tests.exe --suite tab-window
.\scripts\run-blink-object-menu-smoke.ps1 -TabMenuOnly
```

右键、菜单键或 Shift+F10 可打开标签菜单。复制将所点标签已打开的位置、视图和排序带入新标签。新标签独立维护历史、选中项和搜索。关闭其他会保留所点标签，关闭右侧只处理其右边的标签。当没有相应的标签可关时，菜单项显示为禁用。关闭最后一页沿用窗口已有的正常关闭流程。

普通目录标签显示文件夹名，盘根显示盘符。原生回归覆盖关闭范围、失效目标、相邻标签选择及关闭后的取消。窗口脚本用自有 PID/HWND 的菜单键和 Shift+F10 验证后台标签、复制后的视图与排序以及最后一页关闭。候选版可用 `-Binary` 指定。实物鼠标单独验证。复制标签的参考见[微软 Windows 11 24H2 Dev 记录](https://blogs.windows.com/windows-insider/2024/07/26/announcing-windows-11-insider-preview-build-26120-1330-dev-channel/)。

## 详情视图选列

```powershell
& .\x64\Release\Blink.Phase1.Tests.exe --suite folder-view
.\scripts\run-blink-object-menu-smoke.ps1 -ColumnsOnly
```

普通目录的详情列头菜单可以显示或隐藏类型、大小和修改时间，也可恢复默认四列。名称列固定可见。用右键或聚焦列头后按菜单键或 Shift+F10 打开。列选择按标签保存在当前会话中。普通新标签从默认列开始，复制标签继承源标签的列设置。

窗口脚本使用自有目录和定向键盘输入，核对可见表头和实际文件单元格、释放的列宽、原有排序和选中项、导航、视图切换和标签隔离。候选版可用 `-Binary` 指定。鼠标实物操作单独验证。

## Windows 属性与 Blink 详情

```powershell
& .\x64\Release\Blink.Phase1.Tests.exe --suite shell-context-menu
& .\x64\Release\Blink.Phase1.Tests.exe --suite item-properties
```

第一个入口验证普通目录的单选、同父混选、过期和越限请求，以及搜索、系统菜单协议和目录身份。第二个入口验证 Blink 自有详情的只读统计、取消和部分失败。两组包含在无参完整 Phase1 中。

普通目录的右键「属性」、命令栏「更多」中的「属性」和 Alt+Enter 复用进程外 Windows 属性页。Blink 右键中的「Blink 详情」单独打开只读统计。实际页签由 Windows 按对象类型、权限和扩展提供。窗口验证应确认真实属性页、目标对应所选项、正常关闭和选中状态。只记录收到系统命令回复时，仍需观察属性页是否真正出现。

2026-11-20 同一 Release 通过系统菜单 53 项、只读详情 36 项和完整 Phase1 2005 PASS / 0 FAIL / 4 既有权限 SKIP。实机用自有目录完成文件右键、目录 Alt+Enter、命令栏混选、右键混选、Blink 详情、快捷方式六项。原生页由自有的 ShellMenuHost 子进程显示，核对混选实际包含的文件和文件夹，以及快捷方式的目标路径。关闭后核对原目录与原多选。先前失败的窗口探针分别遇到系统标签角色、WinUI 可见文字和快捷方式初始标签页假设，修正后原候选重测。操作不修改属性或权限。原始记录归本机 `.build/shell-properties/integration/`。

## Clipboard结果、呈现与原对象接收

```powershell
.\scripts\run-blink-clipboard-smoke.ps1 -Configuration Release -Feature All -OutputDirectory '.build\clipboard-send\integration'
```

| Feature | 独立原生CLI | 预期唯一正常检查数 |
| --- | --- | --- |
| Presentation | clipboard-transfer-presentation | 64 |
| DataObject | clipboard-data-object | 60 |
| Session | clipboard-paste-session | 33 |
| Controller | clipboard-paste-controller | 23 |
| Source | clipboard-source-data | 38 |
| Sender | clipboard-sender | 29 |

All 顺序启动六个独立进程，核对每组精确标签、完成标记、退出码及相同测试EXE SHA。也可只选某个Feature复测；默认Presentation及旧默认输出目录保持兼容。脚本不构建、不读写真实Clipboard、不操作GUI或服务。六组也包含在无参完整Phase1中，数量不能相加为更多覆盖。

DataObject验证格式/medium所有权与完成协议；Session验证真实STA消息泵、原对象捕获/释放、取消和序号变化；Controller验证实际任务ID/续接、部分完成、单目录根冲突的处理与放弃。Source另验证自有真实Shell文件/目录及替换路径，Sender注入Clipboard API验证STA/flush/重试与消息泵；均未使用系统Clipboard传输。最终247/0/0、完整1991/0/4，不等于真实Explorer/透明度/实际flush与WinUI关闭验收，见[发送交接](CLIPBOARD_SEND_HANDOFF.md)。原生结果、失败修复及发布边界见[接收交接](CLIPBOARD_RECEIVE_HANDOFF.md)，上一批本机呈现记录见[呈现交接](CLIPBOARD_TRANSFER_HANDOFF.md)。实际Clipboard无法完整恢复时，必须在任何写入前SKIP。

## 搜索响应与目录范围

```powershell
.\scripts\run-blink-search-smoke.ps1 -Configuration Release -Feature All
```

`All` 顺序启动九个独立进程，也可用 `-Feature Delivery`、`-Feature NamePrefix` 或 `-Feature NameSubstring` 等选项单独复测。

| Feature | 原生组 | 预期检查数 |
| --- | --- | --- |
| Response | search-response | 15 |
| Scope | search-scope | 25 |
| Visibility | search-visibility | 56 |
| Delivery | search-delivery | 10 |
| Index | search-correctness | 32 |
| Wire | search-visibility-wire | 16 |
| Serving | search-serving | 10 |
| NamePrefix | search-name-prefix | 14 |
| NameSubstring | search-name-substring | 14 |

前四组使用 Phase1 测试程序，后五组使用 Phase05 并带 `--search-index-only --suite` 离线前缀。`-TestBinaryPath` 和 `-IndexTestBinaryPath` 可分别指定二进制。Delivery 用可控的慢卷验证提前交付、可见结果命令、追加顺序、失败卷重试和取消。Serving 使用可控保存阶段验证并发行为和 BLK5 回读。NamePrefix 和 NameSubstring 对照原扫描的条目、顺序和候选数，并分别验证 BLK5 加载、硬链接和增量更新。

每项保存日志、退出码、精确检查数及二进制SHA，同一程序的跨组SHA必须一致。脚本不编译、不操作前台或正式服务；完整Phase1与受影响的离线索引回归另跑：

```powershell
& .\x64\Release\Blink.Phase1.Tests.exe
& .\x64\Release\Blink.Phase05.Tests.exe --search-index-only
```

Phase05无参数是会启动服务原型的历史入口，不作为本批普通回归。详细约定及实际结果见[搜索功能验证](SEARCH_RESPONSE_VALIDATION.md)。调用次数和假IPC行为不能当成真实窗口首屏或Everything对比。

正式服务 C/D 均就绪且 D 盘自有夹具已进入索引后，可在普通用户会话运行客户端对照。参数使用已存在的唯一文本文件路径。

```powershell
& .\x64\Release\Blink.Phase1.Tests.exe --measure-search-delivery $FixturePath
```

同一测试程序交替运行串行最终交付和并发逐卷交付，覆盖双卷宽泛名称、双卷精确名称和单卷精确名称。第 0 轮检查就绪并预热，后续 6 轮记录首批与完成时间。探针只读正式服务，计时终点在客户端回调或搜索任务完成，窗口呈现另测。

用同一份现有 BLK5 的只读副本对照新旧引擎路径时，提供卷根、已收录的文件名或中间片段，以及对应的常用前缀或常见词。前缀探针使用四字符常用前缀。

```powershell
& .\x64\Release\Blink.Phase05.Tests.exe --benchmark-name-prefix $StoreCopy $VolumeRoot $KnownFileName $CommonPrefix
& .\x64\Release\Blink.Phase05.Tests.exe --benchmark-name-substring $StoreCopy $VolumeRoot $KnownInteriorFragment $CommonText
```

探针在同一次载入后交替运行查询，逐项核对路径、身份、属性、USN 和顺序。第 0 轮为预热，后续 8 轮用于比较引擎耗时和候选检查次数。子串探针使用已收录文件的中间片段，另记录顺序筛查的名称摘要数和短词、常见词对照。

## 真实服务搜索与夹具恢复

2026-11-19 已部署保存期间可查询的正式服务（PID 15836）。启动载入、C/D 就绪和 23 个真实服务检查均通过。保存阶段实测与 Busy 采样见[搜索实测](SEARCH_RESPONSE_VALIDATION.md)。真实服务入口在普通用户 PowerShell、项目根运行：

```powershell
.\scripts\run-blink-live-search-smoke.ps1
```

脚本在`.build/search-rollout/integration/live-search`的自有GUID目录创建五个样本和manifest，只向正式服务发送19个只读v1请求。完整契约为23 PASS，包含旧5/7和新8/9/10/11、显示策略、limit=1、范围、Unicode ordinal oracle、扩展名和精确清理；不编译、不管理服务、不操作GUI或Clipboard，不直接写索引库。成功后删除该夹具的五个样本、manifest及两个目录，保留报告与日志。

退出码`0`表示完整通过；`3`表示脚本已识别服务或夹具尚未就绪（`NOT_READY`），保留同一manifest，不能记作通过；`1`表示失败。exit3后稍后恢复相同夹具，避免反复新建GUID：

```powershell
$latestPath = '.build\search-rollout\integration\live-search\live-search-latest.json'
$pending = Get-Content -LiteralPath $latestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($pending.status -eq 'NOT_READY') {
    .\scripts\run-blink-live-search-smoke.ps1 -ResumeFixture $pending.resume_fixture
}
```

恢复时沿用相同`-OutputDirectory`（如曾自定义），`-ResumeFixture`与`-FixtureRoot`不能同时使用。`-TimeoutMs`默认5000、范围1000–15000，限制单次IPC预算，不会自动等待USN收敛。最新指针包含报告路径和SHA；PASS后`resume_fixture`清空，不能再使用已清理的历史manifest。

2026-11-12实际首次4 PASS/0 FAIL/1 NOT_READY，复用同一manifest后23 PASS/0 FAIL/0 NOT_READY、19请求通过，精确清理2481字节；两次不相加。小夹具与就绪检查耗时不代表全盘性能，UI首屏/Everything仍待单独测量，完整事实见[当前搜索验证](SEARCH_RESPONSE_VALIDATION.md)。

## 当前侧栏目录功能

```powershell
$blinkBinary = 'src\Blink\x64\Release\Blink.exe'
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature NewFolder -Binary $blinkBinary
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature Rename -Binary $blinkBinary
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature Delete -Binary $blinkBinary
```

三个入口分别预期 7、8、5 项检查，包含各自真实操作、取消/冲突或目标核对及正常关闭/精确清理。报告独立保存；共同检查不能重复当成新增功能数。现有 `-Feature All` 保留侧栏批次的 14 项检查兼容入口，不等于后续的全应用流程验收。

当前默认79f915修复版已发布，NewFolder7/Rename8/Delete5三组独立完整通过、正常exit0、夹具清理；7/8于11月12日、5于11月13日运行，同二进制与脚本SHA，分组原始报告分别保存。未来候选未发布时才通过`-Binary`指向当批明确路径。历史片段/前台中断不计成功；本轮复用旧通过结果，只补Delete，没有重新编译或重复无关整组。用户使用电脑时不争抢前台。

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
