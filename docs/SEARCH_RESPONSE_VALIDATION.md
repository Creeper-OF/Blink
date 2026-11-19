# 搜索正确性、响应与目录范围

本页覆盖 A01–A04 的原生功能测试、正式服务部署及真实自有夹具搜索，并记录索引恢复、启动载入、查询提速和结果交付。

## 多卷结果逐步交付 — 2026-11-19

搜索协调器在一次窗口搜索中最多并发查询两个卷。某一卷的响应完成范围、可见性和身份检查后可立即交付。后续卷按返回顺序追加，已显示的行、滚动位置、选中项和当前菜单保持原位。结果第一次加入列表时先生成最多 32 行，其余行分批加入。单卷只使用一个连接。

已显示的结果支持打开、定位和相同右键操作，执行时仍核对当前查询和实际文件。退出搜索、导航或换词会使旧回调失效。取消后，旧查询的两个连接都返回才轮到下一个窗口请求。重试继续保留已成功的卷，并报告目录已被替换等实际错误。

### 正式服务上的客户端对照

同一个普通用户测试程序使用正在运行的正式服务（PID 15836），交替运行串行最终交付及并发逐卷交付。固定 C/D 两卷，使用普通可见项过滤，上限为每卷 200 项。精确名称用 D 盘自有唯一文件。每种组合记录 1 次检查就绪及预热、6 次暖测，总计 42 个样本全部完成，每组分卷结果数在两种方式下相同。

| 查询 | 首批命中中位数，串行 → 并发 | 任务结束中位数，串行 → 并发 |
| --- | ---: | ---: |
| 双卷宽泛名称 | 36.5 → 25.1 ms | 36.5 → 25.4 ms |
| 双卷精确名称 | 111.7 → 66.9 ms | 111.7 → 66.9 ms |
| 单卷精确名称 | 57.8 → 61.5 ms | 57.8 → 61.5 ms |

宽泛查询的 6 次暖测都在最终完成前交付首批结果。精确名称只在 D 盘命中，这 6 次都随任务结束交付，由两卷重叠执行缩短等待。单卷测试两组都只使用一个连接，小样本未观察到提速。

计时从提交查询到协调器交付结果，包含服务请求与客户端校验。窗口脚本在锁屏预检时退出，未启动 Blink，也没有实测首屏数据。Everything 未在本机找到可用客户端，未进行同机比较。候选版保留在本机待窗口验证。

### 原生回归与候选

| 验证 | 结果 |
| --- | --- |
| Delivery 专项 | 10 PASS。覆盖慢卷、选择保留、重试、取消及两个连接的任务归属 |
| 搜索七组独立验证 | 164 PASS / 0 FAIL / 0 SKIP |
| 完整 Phase1 Release | 2001 PASS / 0 FAIL / 4 既有符号链接权限 SKIP（1314） |

首轮 Response 13 PASS / 2 FAIL。旧断言要求恢复的 C 盘回到已可用的 D 盘之前，现按保留 D 盘结果位置的行为核对，仍校验身份、请求顺序和 D 盘只查一次。仅重编测试，应用候选 SHA 保持为 `a38e3a41343fe6fe9a89c994533833cb082e396fe005c68ab08eea155f32b983`。正式服务的二进制与候选包随附的服务文件一致。原始记录见本机 `.build/search-first-result/integration/`。

收尾清理 1,633,228 字节，包含自有文件、被最终测试替代的日志、最旧校验源码包及 5 个空目录。保留两份最新源码恢复包和一个待窗口测量的候选。

## 保存期间的查询 — 2026-11-19

USN 每批变更应用时直接更新链接、目录、文件和内存估算，收尾无需再遍历全盘统计。BLK5 保存使用同一份只读视图，保持单任务写盘和临时文件替换。正常增量的保存阶段允许搜索。查询尽力获取共享读锁，同一视图提供健康状态、计数与结果，并校验卷序列号及 USN 检查点。写锁被占用时立即返回 Busy。日志断档或换卷仍拒绝提供旧索引结果。

正式服务更新至 `3e84c3aa742aa9dc334c4ebb80b238e65b49b4092383d2d7e4fe55e6a2e02ebe`，PID 15836。启动时两份现存 BLK5 均载入成功，C/D 分别通过只读就绪检查。

用相同的普通用户脚本在 D 盘依次创建自有文件，观测服务写入临时 BLK5。每项均核对查询前后临时文件仍存在，且进度属于同一个尚未完成的保存操作。旧版 3 个操作全部返回 Busy/997，新版 3 个操作都在保存尚未结束时返回准确的自有文件。文件身份、父身份和属性均通过核对。

同一个原生客户端在服务更新前后各发出 72 个只读请求，分别取得 47 与 71 次成功、25 与 1 次 Busy。C 盘成功 11→35，D 盘保持 36。每个请求只尝试一次。正常后台负荷会影响这组短时采样。WinUI 首屏和 Everything 对比另测。

Release 单并发构建服务和 Phase05 用时 15.763 秒。搜索六组 154 PASS，离线 Phase05 69 PASS，包含新增 Serving 10 项。真实服务夹具首次 NOT_READY，复用同一夹具后 23 PASS。原始报告归 `.build/index-serving/integration/`。实际清理旧产物 9,329,508 字节，测试夹具另清 3,362 字节。

## 启动载入与查询上限 — 2026-11-19

FastIndex 将候选匹配、路径确认和结果构造合并为一次遍历。隐藏项、系统项、扩展名、直接子项及内部排除规则均在结果计数前生效。上限以内保持原结果顺序，未达上限仍按原路径顺序排序。离线基准把合并后的阶段记录为 `candidate_selection_median_ms`，并记录实际检查的链接数。仍可分别读取等锁、搜索和排序耗时。

服务启动后会枚举已挂载固定盘对应的现有 BLK5，逐盘用同一个加载任务载入并加入既有 USN 轮转。断档、索引损坏或没有索引时，仍通过状态页进入明确的重建流程。

### 同一索引文件上的引擎对照

使用项目自己的两份真实 BLK5，C 盘 703,023 条、D 盘 717,593 条。每个用例记录 1 次首轮和 11 次暖查询，下表为暖查询的中位数，单位 ms。原版和修正版均只读打开原文件，前后 SHA 一致。144 组结果条数和顺序、路径、身份及属性摘要一致。首版 D 盘稀疏和空结果样本回退，调整整段扫描循环后才通过最终对照。

| 每盘上限 200 | C 盘原版 → 修正版 | D 盘原版 → 修正版 |
| --- | ---: | ---: |
| 宽泛名称 `a` | 93.99 → 1.12 | 88.17 → 1.09 |
| `.txt` 扩展名 | 9.33 → 1.13 | 12.20 → 0.93 |
| 稀疏名称 `Blink.Phase1` | 90.08 → 81.78 | 78.44 → 71.22 |
| 无命中 | 50.87 → 45.30 | 45.77 → 41.81 |
| 目录递归 `a` | 103.38 → 12.20 | 92.91 → 2.40 |
| 直接子项 `.exe` | 12.95 → 10.26 | 4.86 → 3.93 |

### 实机恢复、更新与真实搜索

C、D 的原索引起初均返回 `1181 / ERROR_JOURNAL_ENTRY_DELETED`，已有断档需重建。先在原服务上逐盘执行 Blink 的现有重建动作，随后两盘分别通过只读就绪检查。D 盘重建后已找到新建的探针文件，组合搜索随即撞到 C 盘后台追赶，返回 997 并使探针退出 84。该次组合失败单独保留。

正式服务更新为 SHA `68b9a41a9852d19e4e51069b6aa3940de030f5b93cc2fa1ca449c2cbf3192b53`（PID 24932）。沿用原注册路径、启动方式、账户、权限和数据根，复用刚恢复的索引。重启后 Blink 窗口未打开，两个只读操作查询已返回成功的装载记录，MFT 扫描数为 0。此后真实服务夹具首次在后台追赶期间返回 NOT_READY，复用同一夹具后 **23 PASS / 0 FAIL**，19 个只读请求，覆盖范围、属性、Unicode 与结果上限。

重建完成后，用同一原生客户端在正式服务更新前后各执行 72 次单请求。旧版 41 次成功、31 次 Busy，新版 47 次成功、25 次 Busy。成功返回的后五轮宽泛查询中位数，C 盘 141.8→15.8 ms（3→2 个成功样本），D 盘 205.1→16.1 ms（3→5 个成功样本）。服务负荷与正常 USN 变化不同。WinUI 首屏、Everything 同机对比和持续追赶时的查询响应仍待测。

单并发 Release 只构建索引服务和 Phase05 两个目标，耗时 14.674 秒和 5.491 秒。最终 Index 32 PASS，完整 Phase05 离线 59 PASS，内含 16 项 Wire。未变化的应用测试程序单独通过 Response 15、Scope 25、Visibility 56。默认应用未重新构建。

原始指标、首次回退、恢复与发布记录见本机 `.build/search-latency/integration/`。实际清理 15,919,305 字节、7 文件和 3 空目录，另拆除真实服务夹具 2,481 字节。保留最近两份源码恢复包、上一正式服务 EXE/PDB、独立的应用恢复点和当前有效缓存。

## 实现范围

A01：生产 `MakeSearchExchange` 每卷接收一次有界响应，保留 Connect 1秒、Send 5秒及请求前后取消/代次检查。Busy/IO_PENDING立即交给原协调器，继续查询后续卷，删除原来的120秒内部重试；最终呈现Partial/Unavailable和真实错误。用户明确重试时只补查未完成卷，保留已成功结果。阻塞RPC仍归原action所有，取消不会生成重复worker或发布过期结果。

A03：`FilterSearchFolderResults` 只在本次调用中复用目录链，key保留canonical路径的精确大小写拼写；每条结果仍比较scope和parent实际身份。批尾无缓存重读实际使用的scope拼写与唯一parent，真实错误清空该卷结果并保留错误码。`ValidateSearchResult`不共享缓存，Open/Locate继续鲜读。

A01/A03 初版沿用串行查询与最终交付。后续在同一只读协议上实现多卷并发和逐卷交付，详见本页首节。

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
- `Index`：Phase05 离线启动 `--search-index-only --suite search-correctness`，当前契约 32 PASS。
- `Wire`：Phase05离线启动`--search-index-only --suite search-visibility-wire`，契约16 PASS。
- `Serving`：Phase05 离线启动 `--search-index-only --suite search-serving`，验证真实保存锁、并发查询、写入互斥和断档等 10 项。
- `All`：顺序运行 Response、Scope、Visibility、Index、Wire、Serving，各组独立进程。

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
