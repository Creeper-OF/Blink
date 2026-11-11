# 搜索响应与目录范围

本页覆盖 A01/A03 的 headless 功能测试，不代表实际界面或真实服务搜索性能验收。

## 实现范围

A01：生产 `MakeSearchExchange` 每卷接收一次有界响应，保留 Connect 1秒、Send 5秒及请求前后取消/代次检查。Busy/IO_PENDING立即交给原协调器，继续查询后续卷，删除原来的120秒内部重试；最终呈现Partial/Unavailable和真实错误。用户明确重试时只补查未完成卷，保留已成功结果。阻塞RPC仍归原action所有，取消不会生成重复worker或发布过期结果。

A03：`FilterSearchFolderResults` 只在本次调用中复用目录链，key保留canonical路径的精确大小写拼写；每条结果仍比较scope和parent实际身份。批尾无缓存重读实际使用的scope拼写与唯一parent，真实错误清空该卷结果并保留错误码。`ValidateSearchResult`不共享缓存，Open/Locate继续鲜读。

现有服务、只读协议与BLK5不变；查询仍逐卷串行、最终统一发布，没有加入并行RPC、逐卷流式发布、全局缓存或新watcher。

## 分功能入口

先单独构建包含新 CLI 的 `Blink.Phase1.Tests.exe`，再在普通用户 PowerShell 中从项目根执行：

```powershell
.\scripts\run-blink-search-smoke.ps1 -Feature Response
.\scripts\run-blink-search-smoke.ps1 -Feature Scope
.\scripts\run-blink-search-smoke.ps1 -Feature All
```

默认使用 `x64\Release\Blink.Phase1.Tests.exe`。可用 `-Configuration Debug` 选择 Debug 默认路径，或通过 `-TestBinaryPath` 指向已构建的明确候选。`-OutputDirectory` 可指定归类好的结果目录，默认是 `.build/search-response/integration/functional`；相对参数路径均相对项目根解析。

- `Response`：启动 `--suite search-response`，当前契约为 15 PASS、0 FAIL、0 SKIP。
- `Scope`：启动 `--suite search-scope`，当前契约为 25 PASS、0 FAIL、0 SKIP。
- `All`：先 Response，进程退出后再 Scope；不并行运行。

脚本不构建产品，不启动 GUI、不安装或修改服务，也不接触剪贴板。每个子进程保留各自标准输出、错误输出及简明 JSON；记录候选、脚本、日志 SHA256，进程退出码、PASS/FAIL/SKIP 数量和起止时间。文件名含 suite 与配置；重复运行更新该 suite/配置的当前记录。每次调用另有独立 Feature/config summary，按 run_id 和报告 SHA 关联。

非零退出、FAIL、0 PASS、数量不符或运行期间二进制变化都会失败；不能仅凭进程启动或未抛异常宣称通过。以 JSON 内实际结果为准。本入口编写阶段只做 AST 静态解析，未代替执行测试。

这些测试使用fake transport、可控状态与metadata调用计数，也包含自有真实Unicode目录替换和缺失错误夹具。调用次数减少不等于真实IPC耗时、UI首屏响应或Everything同机基准。后续仍需普通用户实际GUI验收与真实服务测量；完整无参数原生回归另跑，不由两个功能子集替代。

## 本轮结果 — 2026-11-12

| 验证 | 实际结果 |
| --- | --- |
| Response独立原生进程 | 15 PASS / 0 FAIL / 0 SKIP |
| Scope独立原生进程 | 25 PASS / 0 FAIL / 0 SKIP |
| 随后完整Release原生回归 | 1688 PASS / 0 FAIL / 4既有符号链接权限SKIP（1314） |
| 冻结与资源 | 249项原生输入一致；388项中英fallback资源一致 |
| 构建和原生耗时 | Release单并发365.200秒；完整原生31.329秒 |

新增40项已包含在1688中，不能与两个独立子集相加。Debug本轮未重建，前次G06c的1648结果仅作历史事实。6份相关PowerShell脚本AST通过，已完成的一次性执行器归清理记录；静态检查与功能运行分开。

200项同父目录夹具实测8次metadata读取（旧逻辑1002次）；20层共链46对4802，5父各40项24对1002。后两项和同父项均受检查预算约束；旧数是旧逻辑调用数，不是同机耗时。真实Unicode、批中替换和缺失错误夹具已通过，文件原内容保留后按自有范围清理。

当前未发布候选：`.build/search-response/runtime/Release/Blink.exe`，SHA256 `cfcdd314c00cc2bb2a28a66d9996cfa9dda2f794d42c80b0fdded73d6508a7b7`。它包含G06c侧栏实现；旧G06c候选已归并清理，侧栏NewFolder/Rename/Delete窗口7/8/5项仍待验。默认G06b及正式索引服务6848保持，本轮未操作前台或Clipboard，UI首屏/Everything实测仍未进行。服务候选中的CodeView路径因隔离OutDir变化而不同，与本批service-build副本一致；正式服务文件SHA未变，服务源码/协议/BLK5未变。

本地证据集中在`.build/search-response/integration`：source-freeze/native-validation、functional两个日志与JSON、完整tests-release.log/state、候选SHA清单及cleanup-summary。公开只同步源码与简明结果，不上传本地数据库/依赖/二进制/日志。

本轮清理可核算220683754字节（313文件/143目录/1测试链接），删除旧重复候选、已结束夹具、重复日志及完成的一次性脚本。保留当前候选及匹配服务符号、有效增量缓存/15包、当前运行版与两个已验证成功恢复点；本轮未将待GUI候选包装成新成功运行版本。用户要求本轮完成后休息，停止追加下一批。
