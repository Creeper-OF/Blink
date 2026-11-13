# 搜索正确性、响应与目录范围

本页覆盖A01–A04的原生功能测试、正式服务部署及真实自有夹具搜索。窗口首屏、Everything同机基准与完整环境验收仍需单独测量。

## 实现范围

A01：生产 `MakeSearchExchange` 每卷接收一次有界响应，保留 Connect 1秒、Send 5秒及请求前后取消/代次检查。Busy/IO_PENDING立即交给原协调器，继续查询后续卷，删除原来的120秒内部重试；最终呈现Partial/Unavailable和真实错误。用户明确重试时只补查未完成卷，保留已成功结果。阻塞RPC仍归原action所有，取消不会生成重复worker或发布过期结果。

A03：`FilterSearchFolderResults` 只在本次调用中复用目录链，key保留canonical路径的精确大小写拼写；每条结果仍比较scope和parent实际身份。批尾无缓存重读实际使用的scope拼写与唯一parent，真实错误清空该卷结果并保留错误码。`ValidateSearchResult`不共享缓存，Open/Locate继续鲜读。

A01/A03未改服务、只读协议或BLK5；查询仍逐卷串行、最终统一发布，没有加入并行RPC、逐卷流式发布、全局缓存或新watcher。

A02：查询携带当前窗口的显示策略，普通模式排Hidden与System，显示隐藏模式仍排System。索引在候选进入路径重建与limit之前过滤；名称、扩展、范围、直接子目录及硬链接仍沿原搜索实现。请求8/9、10/11复用原帧字段，分别表示两种显示策略，旧5/7保留全部包含语义；wire版本、pipe与BLK5格式不变。

新命令被旧服务明确拒绝时，只追加一次对应旧5/7调用；Busy、权限、传输、版本与响应关联错误仍如实反馈。旧返回达到候选上限时标记未完整，不当作确定空结果；显式Retry重查未完整卷并保留完整成功卷。窗口使用通用未完整提示和实际候选上限，重试按钮也允许此类卷。索引服务更新后，下次查询可直接使用新命令，不保留过期能力缓存。

A04：名称和扩展缓存使用Windows invariant uppercase，并以ordinal等价约束映射；ASCII有快路径，Load与USN变更统一重建派生键。扩展名读取原始后缀，不再间接依赖CRT locale；路径边界、内部排除与排序用ordinal比较，减少重复大小写映射。没有做拼写展开或Unicode规范化。依据微软[CompareStringOrdinal](https://learn.microsoft.com/en-us/windows/win32/api/stringapiset/nf-stringapiset-comparestringordinal)和[LCMapStringEx](https://learn.microsoft.com/en-us/windows/win32/api/winnls/nf-winnls-lcmapstringex)的序数比较及非语言学大小写规则。

## 当前部署与真实搜索结果 — 2026-11-12至13

23:04（北京时间）已正常停止原服务、等待原PID退出并校验旧EXE/PDB恢复副本，随后在原注册路径更新匹配的服务文件并启动。PID由6848变为29080，服务EXE SHA256为`5e921f86617437ceea6012e95b9a49b87ddd04e9b3c55407e1f6ce1305043073`；原启动命令、数据根、授权SID参数、LocalSystem账户与Auto启动方式保持，没有发生回滚。继续使用原BLK5库，未提交初建/重建；正常USN追赶可以更新库内容，不宣称库字节完全不变。随后C/D只读就绪检查均exit0，`index_ready`与`index_status_read_only`通过。

23:08的普通用户真实服务搜索为**23 PASS / 0 FAIL / 0 NOT_READY，19个只读v1请求**。五个自有文件分别覆盖普通、Hidden、System、Hidden+System与嵌套Unicode名称；实际核对旧5/7、新8/9/10/11的结果集合、各模式limit=1、嵌套范围、Unicode大小写与最终形sigma的Windows ordinal oracle，以及`.TXT`扩展筛选。请求未包含服务控制、初建或直接索引库写入，未操作GUI或Clipboard。

首次运行因自有夹具尚未通过USN完全收敛而返回`NOT_READY`：4 PASS / 0 FAIL / 1 NOT_READY，不计整组通过。稍后用`-ResumeFixture`复用同一GUID目录和manifest，脚本SHA相同，再完成上述23项；两次结果不相加。成功后核对内容与属性并精确删除五个样本及manifest，共6文件、2目录、2481字节，最新报告的`resume_fixture`已清空。

11月13日已补完Delete独立5项，连同11月12日同79f915修复版的NewFolder7、Rename8三组均完整PASS/exit0/精确清理。旧默认PID15036保存单个此电脑标签并正常退出，默认运行目录5文件更新、295文件/219148935字节逐项SHA核对；新默认PID39240可见，真实选中序号0和原地址由独立只读检查确认恢复。首次开启辅助流程因失前台标恢复未完，不能说其过程exit0；随后无选择/键盘动作的只读检查PASS，用户窗口未关闭或重复启动。C/D就绪检查耗时与这个五文件夹具均不能代表全盘搜索性能，UI首屏/Everything基准仍未做。原生、部署、就绪、真实夹具搜索及窗口验证分别记账，不合并为一个成功数。

发布后C/D只读就绪探针再通过，正式服务仍为PID29080/EXE5e921f86，未重启或重建索引。打包服务为当前引用工程增量链接的65099786：八个PE代码/数据节与正式5e逐字节一致，屏蔽时间戳/校验/CodeView后全文件相同；不是服务业务/协议变更。保留当前服务构建输出、匹配符号及`.ilk`，不把它们当无用缓存清空。真实服务23项仍为原报告，不把本次就绪时间当搜索或界面耗时。

原始部署和就绪事实位于本机`.build/search-rollout/integration`；`live-search/live-search-latest.json`按报告路径与SHA指向成功运行`1af9d3a626d74459862145f747269285`，首次`d9dcf0e52f824c398d8653647c5e56d9`的NOT_READY报告保留。公开文档只保留简明结论，不上传本机日志、索引、二进制或授权SID值。

实际入口为`.\scripts\run-blink-live-search-smoke.ps1`。exit3/NOT_READY保留现有manifest，稍后给`-ResumeFixture`传入最新指针中的manifest路径恢复同一夹具；exit0才表示完整通过，exit1表示失败。可执行示例及OutputDirectory约定见[验证指南](VALIDATION_GUIDE.md#真实服务搜索与夹具恢复)。

## 分功能入口

先单独构建包含新 CLI 的 `Blink.Phase1.Tests.exe`，再在普通用户 PowerShell 中从项目根执行：

```powershell
.\scripts\run-blink-search-smoke.ps1 -Feature Response
.\scripts\run-blink-search-smoke.ps1 -Feature Scope
.\scripts\run-blink-search-smoke.ps1 -Feature All
```

默认使用`x64\Release\Blink.Phase1.Tests.exe`及`Blink.Phase05.Tests.exe`，可通过`-TestBinaryPath`/`-IndexTestBinaryPath`分别指定明确二进制。`-Configuration Debug`选择另一配置，但本批需先实际构建，不能误用旧程序。`-OutputDirectory`默认`.build/search-correctness/integration/functional`；相对路径相对项目根解析。

- `Response`：启动 `--suite search-response`，当前契约为 15 PASS、0 FAIL、0 SKIP。
- `Scope`：启动 `--suite search-scope`，当前契约为 25 PASS、0 FAIL、0 SKIP。
- `Visibility`：启动`--suite search-visibility`，契约56 PASS，包含实际factory兼容与旧截断→服务升级→Retry。
- `Index`：Phase05离线启动`--search-index-only --suite search-correctness`，契约31 PASS。
- `Wire`：Phase05离线启动`--search-index-only --suite search-visibility-wire`，契约16 PASS。
- `All`：依次Response、Scope、Visibility、Index、Wire；每组独立进程，不并行，均要求0 FAIL/0 SKIP。

脚本不构建产品，不启动 GUI、不安装或修改服务，也不接触剪贴板。每个子进程保留各自标准输出、错误输出及简明 JSON；记录候选、脚本、日志 SHA256，进程退出码、PASS/FAIL/SKIP 数量和起止时间。文件名含 suite 与配置；重复运行更新该 suite/配置的当前记录。每次调用另有独立 Feature/config summary，按 run_id 和报告 SHA 关联。

非零退出、FAIL、0 PASS、数量不符或运行期间二进制变化都会失败；不能仅凭进程启动或未抛异常宣称通过。以 JSON 内实际结果为准。本入口编写阶段只做 AST 静态解析，未代替执行测试。

这些原生测试使用fake transport、可控状态与metadata调用计数，也包含自有真实Unicode目录替换和缺失错误夹具。调用次数减少不等于真实IPC耗时、UI首屏响应或Everything同机基准。后续仍需普通用户实际GUI验收与真实服务性能测量；完整Phase1与Phase05离线回归另跑，不由各功能子集替代。

## A02/A04原生检查点（正式部署前）— 2026-11-12

| 验证 | 实际结果 |
| --- | --- |
| Response / Scope（沿用本批已通过的同SHA结果） | 15 / 25 PASS，均0 FAIL/0 SKIP |
| Visibility（应用） | 56 PASS / 0 FAIL / 0 SKIP |
| Index / Wire（修正测试后只复测受影响程序） | 31 / 16 PASS，均0 FAIL/0 SKIP |
| 随后完整Phase1 | 1744 PASS / 0 FAIL / 4既有符号链接权限SKIP，32.462秒 |
| 随后Phase05离线索引 | 58 PASS / 0 FAIL / 0 SKIP，3.070秒；progress snapshot摘要另过 |
| 冻结与资源 | 251项输入；390项中英/fallback资源一致；Debug未重建 |

58已包含旧11、核心31、wire16；新增应用56已包含在1744中，各功能子集不能再加到全组。当前功能摘要按每个程序的实际SHA关联三个运行批次，原始30/1失败及受影响Index/Wire重测分别保留，不将首次All失败改作通过。6份PowerShell脚本AST通过，其中完成的一次性执行器已按记录清理。

首轮统一构建744.123秒，主程序、服务和Phase1已生成；Phase05测试夹具列表初始化需要显式DWORD转换。仅补该测试工程27.355秒后，核心组一项Greek断言发现把最终形sigma错误当成通用ordinal等价；真实Win32探针显示最终形与大写不等价、普通形等价。断言改为系统ordinal oracle，并仍要求普通形命中；仅再补Phase05工程8.031秒。生产搜索实现未为此放宽，两个修正均不重编产品。规范测试EXE/PDB逐项SHA归位，重复本地输出已清。

该原生检查点生成的候选：`.build/search-correctness/runtime/Release/Blink.exe`，SHA256 `b0091e56fad3300928e34fa879949acb72d80ba36c3e7bc339832c4dd00a22f4`。当时默认G06b与正式服务6848尚未替换，旧服务只有有限兼容及真实不完整反馈；随后正式服务部署和真实搜索结果见上方当前记录。该原生检查点未操作前台窗口或Clipboard；G06c侧栏7/8/5窗口、真实首屏/Everything与环境矩阵仍待验证。

本机证据归`.build/search-correctness/integration`：最终source-freeze/native-validation、functional/search-current-release-summary、两份完整合并日志、initial-build/before-greek-fix及greek-nls-probe摘要、candidate-files与cleanup-summary。清理可核算231313557字节、321文件/146目录/1链接；保留有效缓存/15包、一个当前候选及匹配服务符号、规范测试产物与原两个成功恢复点。原始日志、数据和二进制不进入公开仓库。

## 上批A01/A03结果 — 2026-11-12

| 验证 | 实际结果 |
| --- | --- |
| Response独立原生进程 | 15 PASS / 0 FAIL / 0 SKIP |
| Scope独立原生进程 | 25 PASS / 0 FAIL / 0 SKIP |
| 随后完整Release原生回归 | 1688 PASS / 0 FAIL / 4既有符号链接权限SKIP（1314） |
| 冻结与资源 | 249项原生输入一致；388项中英fallback资源一致 |
| 构建和原生耗时 | Release单并发365.200秒；完整原生31.329秒 |

新增40项已包含在1688中，不能与两个独立子集相加。Debug本轮未重建，前次G06c的1648结果仅作历史事实。6份相关PowerShell脚本AST通过，已完成的一次性执行器归清理记录；静态检查与功能运行分开。

200项同父目录夹具实测8次metadata读取（旧逻辑1002次）；20层共链46对4802，5父各40项24对1002。后两项和同父项均受检查预算约束；旧数是旧逻辑调用数，不是同机耗时。真实Unicode、批中替换和缺失错误夹具已通过，文件原内容保留后按自有范围清理。

该批候选（已被A02/A04批次替代）：`.build/search-response/runtime/Release/Blink.exe`，SHA256 `cfcdd314c00cc2bb2a28a66d9996cfa9dda2f794d42c80b0fdded73d6508a7b7`。它包含G06c侧栏实现；旧G06c候选已归并清理，侧栏NewFolder/Rename/Delete窗口7/8/5项仍待验。该批结束时默认G06b及正式索引服务6848保持，未操作前台或Clipboard，UI首屏/Everything实测尚未进行。该批服务候选中的CodeView路径因隔离OutDir变化而不同，与其service-build副本一致；当时正式服务文件SHA未变，服务源码/协议/BLK5未变。

本地证据集中在`.build/search-response/integration`：source-freeze/native-validation、functional两个日志与JSON、完整tests-release.log/state、候选SHA清单及cleanup-summary。公开只同步源码与简明结果，不上传本地数据库/依赖/二进制/日志。

该批清理可核算220683754字节（313文件/143目录/1测试链接），删除旧重复候选、已结束夹具、重复日志及完成的一次性脚本。保留当时候选及匹配服务符号、有效增量缓存/15包、当前运行版与两个已验证成功恢复点；未将待GUI候选包装成新成功运行版本。该批按当时用户要求结束后暂停，后续部署与验证见上方当前记录。
