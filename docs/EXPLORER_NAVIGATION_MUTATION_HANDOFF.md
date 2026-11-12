# G06c 导航目录新建、重命名与删除

侧栏普通目录及此电脑中的实际目录目标新增“新建文件夹”“重命名”“删除”，沿用 Blink 的名称输入、系统回收站、永久删除确认和唯一文件操作队列。盘根只提供适用的新建入口；虚拟此电脑不提供这三项文件系统操作。文件区原有选择保持不变。

新建在所点目录内部创建；重命名与删除作用于该目录本身。后台捕获真实目标或目标父目录、项目身份，再绑定到发起页面的 owner/代次，不将当前文件列表的目录或选中项当成侧栏目标。`CaptureDirectoryOperationTarget`复用已有单目录来源读取，不读取剪贴板或遍历子树。来源已变化时沿原后端反馈真实失败。

导航树和常用位置控件内的 Ctrl+Shift+N、F2、Delete、Shift+Delete 走同一目标入口；文件区和文本框原有作用域保留。普通删除使用系统回收站；Shift+Delete 沿原默认取消的永久删除确认。名称编辑和永久删除对话框都在实际关闭后更新按钮，并优先恢复原侧栏控件，控件退役时返回原区域。

失败后的修改名称/重试、删除重试与跳过按当前 operation_id 保留独立目标，而非重新读取文件区。新建、目录改名、删除和恢复删除批次均接入既有树刷新：只后台重读相关已加载分支，保留未变节点；可恢复删除失败保留范围直至该批次结束。目录改名继续沿已有 relocation 更新其它正在浏览该目录的标签。

Debug/Release已各1648 PASS/0 FAIL/4既有符号链接权限SKIP，249输入/388中英fallback资源一致。窗口功能仍待完整通过：5/1标题假设失败和0/1前台中断分档，两进程均exit0/夹具已清，不能拼作成功。用户转为整体审计及功能优先工作流，默认G06b未替换；后续逐功能脚本验证，不重新编译未变源码。原始记录归本轮integration，审计原生事实见 `.build/maintenance/code-audit/native-validation.json`。

```powershell
$blinkBinary = '.build\search-correctness\runtime\Release\Blink.exe'
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature NewFolder -Binary $blinkBinary
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature Rename -Binary $blinkBinary
.\scripts\run-blink-navigation-mutation-smoke.ps1 -Configuration Release -Feature Delete -Binary $blinkBinary
```

分功能入口预期分别7/8/5项，报告独立保存，未运行的功能块不计SKIP；All保留原14项兼容流。拆分已通过AST，尚未实机运行。窗口脚本使用新建的自有 PID/HWND 和 GUID 夹具，覆盖真实新建/改名、冲突与重试、另一标签路径跟随、取消和默认按钮、作用域、树自动更新、未提交草稿正常关窗及精确清理。实际确认永久删除只对身份和字节已核对的自有非空目录；不操作用户资料、图片剪贴板或回收站，不用 F5 代替操作后的自动刷新。

普通回收站删除的导航UI、真实鼠标、常用位置及盘根实际写入、IME/UNC与完整主题/DPI/多屏/读屏矩阵仍需分别验证。其它对象菜单、外部剪贴板完成反馈及完整 G06/P0 继续按 README 推进。当前正式索引和服务不在本轮修改范围；生成物集中在本轮 stage，保留有效缓存、当前运行版本和最近两个成功源码恢复点。

2026-11-12搜索批次已将原G06c候选归并到上述search-response候选，旧runtime/service-build已核对归属后清理，原GUI分档记录及Git源码保留。新候选Release原生1688/0/4通过，但本页7/8/5项窗口功能仍未完整运行；不能用搜索子集或完整原生通过替代。见[当前候选与搜索验证](SEARCH_RESPONSE_VALIDATION.md)。

当前候选继续归并到A02/A04的search-correctness批次，Release应用原生1744/0/4通过，原侧栏7/8/5项窗口事实保持待验；上批A01/A03 runtime/service-build已校验后清理。详情仍见[搜索验证](SEARCH_RESPONSE_VALIDATION.md)。
