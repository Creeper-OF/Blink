# G06b 导航目录剪切、复制和粘贴

2026-11-11。普通目录目标菜单接入Cut/Copy/Paste；盘根仅提供适用的Paste，虚拟ThisPC不提供文件系统传输。沿原导航拖放/操作队列限制，Search表面、导航pending、操作忙或剪贴板准备中不启用三项。不代表完整G06/P0完成。

Cut/Copy捕获菜单点中的目录路径，在worker复用原CaptureClipboardCopySources并只接受单DirectoryCopyRequest；CF_HDROP只发布该目录本身，Preferred DropEffect区分COPY/MOVE。Cut本身不移动目录。真实source/parent ID和原始sequence进入已有clipboard cache，未借当前文件区选中项。

Paste保留原页面owner、独立解析点中目标的真实identity；源读取与目标metadata均在worker。COPY进入原CopyItems队列，MOVE进入原MoveEntries并保留move_submitted/原始source IDs，继续使用原冲突、取消、部分完成及重试。目标metadata失败先保留真实错误，再核对成功identity，不误报旧页面。取消单目录复制后按名称重试仍保留目录类型和原limits。

目录复制/移动结束按实际父目录及受影响目录内容范围刷新已加载树缓存；仅后台重枚举已加载分支，串行推进，无全盘扫描/轮询/新watcher。F5也更新当前已加载树分支。model刷新保留旧snapshot直到新数据提交，同Location复用ID及已加载孙节点；UI按ID复用TreeViewNode，保留展开/折叠。旧结果/失败不覆盖新代次。重试改名、尾分隔符和失效在途节点都保留正确刷新语义。

最终双配置各1636 PASS/0 FAIL/4既有权限SKIP，249输入/386资源核对；最终Release `fd13f367c9bc6708e677e4689ab9518582d93eb604e40d1e73534c8218660907` 的13项不写Clipboard窗口检查通过，PID31692正常exit0、夹具清除。实际剪贴板传输因原图片/OLE等内容无法完整恢复，在启动SUT/任何写入前SKIP，0项传输UI通过，原内容未改。默认PID15036已发布，正式服务6848未重启。原始事实/脚本及输入输出SHA以integration/native-validation.json、ui-navigation-readonly-report.json、ui-navigation-transfer-report.json及clipboard-preflight-report.json为准，不能将SKIP计入通过。

完整剪贴板脚本只接受可完整恢复的空/有界文本/HTML/RTF原格式；未知图片/OLE/GDI/文件CUT等在任何写入前SKIP。原内容仅驻内存、不写报告。每次Paste核对ownPID/前台、GUID路径/identity及真实sequence/effect，结束只在仍是本轮own sequence/owner时还原全部原格式/字节；用户复制新内容不覆盖。

```powershell
.\scripts\run-blink-navigation-transfer-smoke.ps1 -Configuration Release
.\scripts\run-blink-navigation-transfer-smoke.ps1 -ClipboardPreflightOnly
```

真实鼠标、系统属性页、SFTP/UNC、外部Explorer组合、剪切淡化和完整环境矩阵保持独立；本轮没有修改索引/服务或新增本地防护平台。源码、用户资料、正式索引、配置及密钥不在清理范围；构建保持单并发，生成物归.build/explorer-navigation-transfer，保留有效缓存与最近两成功源码恢复点。
