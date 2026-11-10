# G05a 重命名初始选区

日期：2026-11-10起，2026-11-11完成本切片验证与默认版本发布。完整G05/P0仍未完成。

## 实现范围

普通文件重命名默认选中最后一个扩展名前的主名；目录（包括含点目录）全选。无扩展名、leading-only dotfile、尾点与空字符串保持全选；多点文件只保留最后后缀，`.config.json`选中`.config`。这是明确的Blink选区规则，未代替完整Explorer版本对照。

纯策略只计算字符串范围，不修改名称、不做文件I/O；WinUI对话框在Opened中按实际Text设置选区。只有统一ShowFileNameDialog入口按真实rename_source.is_directory选择FileStem，NewFolder/Copy/Move名称对话框保留WholeName。Ctrl+A仍可显式全选并编辑扩展名，未增加扩展名变更确认或新重命名管线。

## 验证

新增13项纯策略回归，最终Debug/Release各1578 PASS/0 FAIL/4既有符号链接权限SKIP；249项原生输入SHA冻结，378项中英/fallback资源一致。最终同一Release（SHA256 `ef4986100848f3656dcaa0f2d8a645a96e9a40b09f44c5e86c51f16abb67434c`）的15项GUI全PASS；进程正常退出0、无强制终止，自有夹具425字节已清理。记录见`.build/explorer-rename-selection/integration/native-validation.json`与`ui-rename-selection-report.json`。

受控GUI只操作新PID/HWND与GUID夹具，覆盖真实TextPattern选区、键入替换和Enter、文件身份/UTF8字节、Esc后的能力及回焦、工具栏与新建入口、真实索引搜索F2及编辑中关窗。包括多点、无扩展名/dotfile、中文/emoji、含点目录与子项；Ctrl+A可改全名，Delete只作用于编辑文本。搜索列表对实际唯一结果行Select/SetFocus，严格核对前台PID、焦点行身份及列表祖先。

## 真实路由与边界

搜索结果F2先导航至已验证父目录、定位原文件再进入统一Rename；取消后回当前FolderEntries，不声称保留SearchResults。实机发现取消后New/Sort仍disabled，以及工具栏Rename取消回左侧NavigationPane的两个原有问题；修正在AwaitDialog结束、closed=true及submit/failed回调处理后通知owner重算能力，无其它对话框时将焦点交实际可见文件列表，不改变模型选择。最终完整GUI的F2、工具栏及搜索路由均通过。

关闭主窗口时，名称对话框在既有更早PrepareWindowClose中先Close取消，再移交列表焦点。最终检查保留真实聚焦Rename编辑器和未提交草稿直接关窗，正常退出0；外部Close已置closed，晚到Await不再执行完成回调。

已有成功重命名后刷新按fileID+旧name保留选择的限制仍在：本轮验证实际改名与列表可用，不声称已保持新项选中。成功新项定位/选择、F3/F6区域焦点环、真实IME及完整主题/DPI/多屏/读屏矩阵后续独立推进；完整G05/P0尚未完成。

## 工作台

主模型集成、三个代理分策略/文档/GUI脚本，实际构建与GUI均由主模型串行执行。默认版本已发布7个变化文件，四关键产物SHA一致，旧窗口PID10988正常退出0，新窗口PID7108保留供用户使用；正式索引服务PID6848及其在用二进制未变。独立主窗口截图已查看：深色布局、标题标签同排和索引已连接可见，不将其当完整视觉矩阵。

按34个明确路径清理229,819,404字节（约219.17MiB）、358文件/250目录、6个测试联接，包含本轮夹具、重复候选运行与服务输出。保留当前每配置有效增量缓存、15个有效依赖包、正式索引/配置/日志；一次性过程证据先归成功源码恢复包再清除，整个项目仅保留最近两份成功恢复点。清理范围与实际释放空间见`cleanup-result.json`和`recovery-retention.json`。

字符注入验证：早期脚本未显式绑定SendMessageTimeoutW，ASCII路线通过但Unicode替换失败；修正测试接口为Unicode W后，真实中文/emoji选区、逐UTF16码元键入、实际rename后的fileID与字节均已通过。原文件结果断言未改，真实IME仍未测；微软接口语义见[WM_CHAR](https://learn.microsoft.com/en-us/windows/win32/inputdev/wm-char)与[SendMessageTimeoutW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessagetimeoutw)。
