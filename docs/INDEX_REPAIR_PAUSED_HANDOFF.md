# 索引修复暂停续接 — 2026-11-06 11:55

用户要求北京时间11:55暂停，转移工作环境后继续；不是结束索引修复。当前全部修改为WIP，未编译、未运行新增测试、未安装或启停真实服务。当前可运行Release仍为Slice20，两配置历史各1365 PASS/0 FAIL/4权限SKIP，不能拿它证明本轮修复已通过。

## 用户症状与现场

用户明确提示：“无法读取索引服务，本地浏览仍然可用”。只读CIM检查确认系统没有Blink.IndexService注册/进程，Release Blink正在运行（PID27620，恢复时须重新确认），项目默认.blink-data/index不存在。旧2026-08验收使用专用DataRoot，结束后移除了服务，不等于当前有常驻索引。现有界面此前只有刷新，没有启用和首次建库入口。

当前D盘真实GetVolumeInformation序列号0x286936AA，FILE_ID_INFO为0xE8286968286936AA，证据integration/volume-serial.json。旧验收盘序列不同，不应把旧验收索引当当前有效库。

## 已写入但尚未验证的修复

1. SearchCoordinator在worker按真实卷根补全USN32到App64身份，child/parent同步处理，全局FileIdentity严格比较不变。补11项真实文件Open/Locate/当前目录过滤及卷不匹配检查。
2. FastIndex::Load完整读/CRC/卷检查及派生索引完成后才提交；已有D索引不再被缺失/损坏C库污染。服务用候选shared_ptr，成功后同时发布index和active_volume，Query/Search/Scheduler固定同一实例。Phase05 --search-index-only补3项回归。
3. 根install-index-service.ps1仅修正High integrity英文组名检测为数字SID。新scripts/enable-index-service.ps1接收原用户AuthorizedClientSid和Configuration，复用已验收LocalSystem配置、保留本repo DataRoot，设自动启动并等待Running；不自动删库。服务操作尚未执行。
4. IndexServiceCoordinator增加EnableService/BuildIndex/CurrentAction，启用时worker通过Windows UAC调用上述脚本；成功且所选盘缺库才用既有StartInitialIndex RPC建立索引。新6项fake setup测试不会弹UAC。
5. 状态浮层新增启用/建立所选盘索引及管理员确认/取消文案，345中英/fallback键静态匹配。main共用发布回调，搜索忙时不抢操作。
6. Phase1测试新增显式 --live-index-probe D:\ [--enable|--build]；无flag仅读状态，明确flag才执行设置/建库和项目data/index-repair-fixtures内自有sentinel创建/改名/删除USN验证。D完整验证，C明确跳过跨卷文件fixture。未运行此模式。

## 恢复顺序

1. 先核对本目录input-sha256.json/static-checks.json与用户后续修改，不回退覆盖。三个模块代理均已冻结，不需要恢复旧代理。
2. 本轮触及此前冻结的FastIndex.cpp和IndexService main.cpp，这是用户明确要求修索引的功能修改；旧冻结摘要保留作历史，不能继续断言这两文件未变。公开IPC版本与BLK5格式未改。
3. 主模型单并发完成Debug全solution构建和Phase1原生测试，再做Release及对应测试。调用本目录run-build-cached.ps1/run-tests.ps1；Release主程序被输出到.build/index-repair/runtime/Release候选目录，避免覆盖用户正在运行的旧Release。该可选BlinkOutputDirectory属性已加进vcxproj，默认启动路径不变。
4. 两配置至少重新执行Phase05 --search-index-only（新增3项）；按二进制摘要决定其它旧suite是否必须重跑，不能复用已经变化的旧二进制证据。普通Phase05全套会启动console测试服务，必须在真实常驻服务安装之前跑，避免共用pipe冲突。
5. 构建/测试通过以后，再用Release Phase1.Tests显式 --live-index-probe D:\ --enable验证真正启用服务、首次MFT和USN收敛。这会出现Windows管理员确认，当前token非管理员；不要绕过UAC或把拒绝当成功。新wrapper参数见docs/INDEX_SERVICE_SETUP.md。
6. 实际服务可用后，部署候选UI需要先确认用户已关闭旧Blink，不擅自结束其文件操作。随后核对真实打开/定位、当前目录范围、另一未建库卷状态不污染D。人工其它功能验收仍暂缓。
7. 最终才更新为已修复，保存新的成功恢复点；本轮WIP包不替代Slice19/20两个成功恢复点。清理本轮候选重复运行时、空fixture及重试生成物，保留简明失败记录和当前可运行版本。

## 证据与归档

.build/index-repair/integration保存environment.json、volume-serial.json、静态检查、输入摘要、暂停session及source-snapshot.json。恢复包只有源码/文档/简明证据，不重复打包工具链、NuGet或当前二进制。没有在项目根或主目录散放临时文件。本轮未启动构建、原生测试或后台服务任务。
