# G03c 地址栏目录自动完成

日期：2026-11-11。补齐地址编辑的目录候选，不代表完整G03/P0或全部环境矩阵完成。

输入停顿180ms后后台读取直接子目录，按名称前缀匹配，大小写不敏感，最多12项，复用既有排序及隐藏项规则。普通文件不列入候选；Unicode、盘符根、完整UNC share及长路径按已有路径语义处理，不展开不完整UNC server、URI、shell或环境变量表达式。相对候选以当前已提交Folder为基准，不使用进程或每盘工作目录；ThisPC仍是虚拟位置，沿用真实常用位置和可用本地驱动器。

方向键只移动候选选中项，保留地址框实际焦点和原文；Tab填入绝对路径及尾分隔符，不提交导航。Enter只有显式选中候选时才进入该目录；没有选中时同步读取并提交原输入，沿用现有地址导航规则。点击候选进入目录，Esc维持取消编辑并恢复已提交地址的原有行为，F4保留已提交历史。候选不会自动改变目录、历史、搜索范围或文件选择。

保留原TextBox，候选用原生Popup/FlyoutPresenter/ListView承载。PreviewKeyDown只处理非组合输入的方向键和Tab；Enter/Escape仍留在原KeyDown阶段。Popup不主动取得焦点，离开编辑框或窗口、切换标签、取消编辑、导航和关闭会取消定时请求并丢弃过期结果。主窗口只持一个当前自动完成action，worker不持XAML对象，返回Dispatcher后再核对owner、request、实际Text与焦点。真实阻塞的目录I/O仍须等系统返回，取消不承诺中断正在进行的系统调用。

已选候选也必须匹配当前Text快照，避免TextChanged尚未派发时接受旧输入的结果；Enter不匹配时回到同步原文提交。按住方向键时复用同owner和draft的在途请求，只更新完成后的高亮方向。旧container的点击还要匹配当前Items里的对象身份，不能因索引相同导航到新标签的其他目录。

候选使用自有ListViewItem容器，原生ItemClick返回值可能是其Content；点击时按当前item或Content的对象身份查找，避免只转换container而漏掉真实点击。主窗口收到Deactivated后延后核对前台进程，区分本应用InputSite等窗口间的激活和真正离开应用，避免编辑焦点仍在本应用时误关候选。

最终双配置构建与普通用户回归各1608 PASS/0 FAIL/4既有权限SKIP；249项原生输入哈希、383个en/zh/fallback资源一致。相同最终Release `816d18b85436447383da5b34cab4ab81786602b6eae616839cffce7365ea481f` 通过自动完成15项及既有地址20项；两个SUT进程PID45132/39864均正常exit0，无强杀，自有夹具已清。原始报告、脚本/输入/输出SHA见本轮integration的`native-validation.json`与两份GUI报告。

实际覆盖大小写/Unicode/相对候选、最多12项的完整键盘遍历、原文与真实焦点保留、Tab只补全、选中Enter及真实Invoke、未选Enter提交原地址、Esc、隐藏项、标签旧peer、watcher和编辑中关窗。默认新版PID28152已发布并核对关键产物，正式服务6848未重启。

真实物理鼠标、IME组合与候选选择、UNC/拒绝访问、真正阻塞的目录IO取消和完整主题/DPI/多屏/Narrator矩阵未验收；不能用普通回归或UIA代替这些检查。完整G03/P0和项目仍未全部完成。

复现当前已发布版本：

```powershell
.\scripts\run-blink-address-completion-smoke.ps1 -Configuration Release
```

开发候选可以通过`-Binary`指向本轮runtime；发布后重复runtime会按分类清理。脚本只操作新启动PID/HWND与GUID自有夹具，使用InputSite键盘消息和真实UIA，正常关闭，不使用全局输入、剪贴板、服务修改或强杀。

保持唯一编译进程、每配置有效增量缓存、15个有效依赖版本与最近两份成功源码恢复点。源码、用户数据、正式索引、配置、密钥及其他任务文件不属于清理目标。
