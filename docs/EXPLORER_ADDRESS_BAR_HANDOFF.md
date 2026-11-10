# G03a 地址栏、面包屑与已提交历史

日期：2026-11-10。完整 G03/P0 尚未完成，人工验收独立。

## 交付范围

- 默认标准 BreadcrumbBar；层级点击和原生长路径收缩均导航真实祖先。空白点击已接；Ctrl+L/Alt+D切换全选编辑，Enter同步读取当时文字并复用导航；Esc恢复当前已提交位置并返回实际可见的目录或搜索列表。
- F4/历史按钮只取当前标签已提交的位置、最近后退和前进历史，按Location去重，最多20项。输入草稿、pending与失败请求不加入；旧控件绑定自己的目标、标签和展示代次。ThisPC保持虚拟位置。
- 自动刷新保留同标签最新输入；切换标签取消未提交草稿。导航、索引和文件操作沿用原流程。

## 关闭问题与修正

首个Release检查发现编辑器已展开但系统焦点在其它窗口；同时编辑状态关窗出现0xC0000005。两问题分别处理，非零退出独立记录FAIL。输入前先取得自有窗口并核对实际前台和UIA焦点，未以SetFocus返回代替。

独立探针复现保留编辑后关窗AV。两份小转储经系统DbgHelp/StackWalk64真实展开64帧，顶层为Microsoft.UI.Xaml.dll+0xA0971→WinUIEdit→Xaml/CoreMessaging，空读0x1C0；本地Blink仅位于应用消息循环，未据此推断悬空指针。仅在Closed退役控件仍复现。

最终在既有顶层WM_CLOSE subclass转交WinUI之前，将焦点交可见列表，并用helper缓存的committed文本结束编辑、隐藏原生编辑器；不访问ActiveState或重建面包屑。唯一程序Window.Close入口同样先准备，Closed仅退役事件与逻辑shutdown。最终地址20项已通过，包含保留未提交草稿直接关闭及exit0；前一轮A/B/C均exit0，其中C当时为前景编辑关闭，D真实Ctrl+W也已正常退出，最终四项探针已通过，C在自有伴随窗口持有真实前台和UIA焦点时向后台SUT注入Ctrl+L，前后均真后台；D实际Ctrl+W关闭唯一标签，无WM_CLOSE回退。

## 验证

最终Release镜像SHA256：`cf1d8a2bae1a8b67ed2c99e53dff373369ee8362060d69b8dad3481a8e5fca9d`。双配置均1565 PASS / 0 FAIL / 4既有符号链接权限SKIP，23项新增地址模型；246项原生输入、378项中英与fallback键一致。

| 最终候选检查 | 结果 | 记录 |
| --- | --- | --- |
| Release地址栏 | 20 PASS / 0 FAIL / 0 SKIP | `integration/ui-address-bar-report.json` |
| Release命令栏兼容 | 18 PASS / 0 FAIL / 0 SKIP | `integration/ui-command-bar-compat-report.json` |
| Release查看菜单兼容 | 15 PASS / 0 FAIL / 0 SKIP | `integration/ui-view-compat-report.json` |
| 关闭探针A/B/C/D | 4 PASS，全部exit0；C后台COVERED，D实际Ctrl+W | `integration/window-close-probe/fixed/probe-report.json` |

以上integration相对`.build/explorer-address-bar/`。初轮Debug的19+18+15项属于早期候选预检，原始失败/修正记录独立留存，不混为最终Release通过数。原生收缩按钮经实际控件树确认：本机Name=更多、AutomationId空、BreadcrumbBarItem/Button。窄窗初轮已真实导航成功，脚本误等迁入More的排序主按钮；现按精确路径、完整两条目录项、字节、更新的面包屑与菜单关闭验证，恢复宽度后再要求完整Ready。

实机为25H2/26200.9711、zh-CN、普通用户、报告窗口DPI96，系统应用主题为暗，ActualTheme未采集。真实IME、UNC/拒绝访问、24H2、完整主题/DPI/多屏/读屏矩阵未验证。

## 构建与留存

完整Release首次因在用服务锁住root x64/Release/Blink.IndexService.exe出现LNK1168；可选BlinkIndexServiceOutputDirectory统一候选服务输出与应用复制来源。缺省规则保留，正式服务业务/IPC/存储不改、不重启。

每配置保留一份当前有效增量缓存，归原有固定中间目录。验证后清理重复runtime/服务候选、过期测试夹具和一次性过程文件；恢复点整体只保留最新两个成功包。下级目录菜单、自动完成建议与跨标签持久化草稿继续待做，F2主名选区和区域焦点为下一优先项。

默认运行目录已发布并核验相同镜像，当前可见PID10988，索引已连接；正式服务PID6848与原服务二进制保持。截图只捕获该自有窗口并已实际查看，不替代主题切换或实物输入验收。

本轮清理：38个明确目标，319,970,342字节（约305.15MiB）、371文件、274目录、7个测试reparse点，剩余0。保留当前有效增量缓存的路径/大小/源码freeze归`incremental-cache-retained.json`；原生测试夹具父目录与199MiB重复候选、旧90MiB诊断PDB已处理，固定运行TEMP重建为空。清理明细见`cleanup-result.json`，未触碰正式索引或用户数据。
