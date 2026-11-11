# G06a 导航与位置目标菜单

2026-11-11：侧栏常用位置、目录树和此电脑目录/驱动器条目补常用目标菜单。功能切片不代表全部G06/P0完成。

菜单使用现有WinUI外观，支持右键事件、Apps和Shift+F10。目录/盘根提供打开、新标签、复制完整路径、系统属性；树节点按实际可展开状态增加展开/折叠。虚拟ThisPC仅提供打开、新标签及展开/折叠，不伪装成C盘，不提供文件系统路径或目录属性。ThisPC条目不再显示普通文件行的剪切/改名/删除等不适用操作。

动作捕获实际node/quick/ThisPC条目的Location，不导航或改变文件列表选择后再执行。新标签直接向捕获目标发起一次原导航，不先枚举默认ThisPC。树展开进入原lazy loader，保持原滚轮与隐藏项语义。复制路径复用原有后台文本剪贴板发布；系统属性在worker读取点中目录/盘根的真实entry及parent identity，沿用原properties-only进程外Shell host，不加载系统扩展到UI线程。

新标签默认的ThisPC没有已提交内容，PresentActiveTab仅在showing_this_pc且真实committedLocation为ThisPC时同步窗口共享导航，避免新标签/待加载标签的空快照清除目录树和常用位置。

仅做菜单代次、活动标签、显示源与节点目标核对；过期peer不能操作后来显示的另一个目标。关窗取消本轮在途元数据任务；真实系统读取仍须待调用返回，不扩展本地防护平台。

最终双配置构建/普通用户回归各1618 PASS/0 FAIL/4既有权限SKIP，249输入哈希和386个中英/fallback资源一致。同一最终Release `ffea2fed03203f20b7cecae6c45a3394b091a62107ae0583f203cf3cc878d434` 的10项窗口检查通过，PID10400正常exit0、无强杀、自有夹具已清。默认新版PID5736已发布并核对11关键输出，正式服务6848未重启。另以相同镜像完整重跑10项、选中标签实际获焦后正常关窗，PID44688亦exit0/夹具清除，不重复算不同场景。原始报告、脚本SHA与输入/输出哈希见本轮integration/native-validation.json、ui-object-menu-report.json与ui-object-tab-close-report.json。

未覆盖：实际物理鼠标命中、真正剪贴板写入、原生属性页交互、真实IME/UNC/拒绝访问及完整主题/DPI/多屏/读屏矩阵。更多系统选项和树目标的剪切/复制/粘贴/新建/改名/删除、其它对象菜单仍待独立切片，不借当前列表选择冒充。

```powershell
.\scripts\run-blink-object-menu-smoke.ps1 -Configuration Release
```

主模型单并发构建和GUI验证，子代理分别处理Shell单目标捕获/10原生检查及10项窗口脚本。生成物分类归.build/explorer-object-menu；结束清除重复候选/自有夹具、归档后清理过程文件，保留有效缓存和最近两个成功源码恢复点。
