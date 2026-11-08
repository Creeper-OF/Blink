# 索引服务修复与实机验证

本轮已完成代码修复、Debug/Release 原生验证和真实索引服务验证。默认启动位置仍为 `src/Blink/x64/Release/Blink.exe`；现有服务为自动启动，C、D 两盘索引保存在项目 `.blink-data/index`。实际 WinUI 鼠标键盘验收继续暂缓，整个项目尚未完成。

## 问题与修改

- “无法读取索引服务，本地浏览仍然可用”的现场原因是没有常驻服务注册及默认索引库。界面增加启用服务与所选卷首次建库入口；原安装器还存在可选 `TelemetryRoot` 被同名强类型变量转换为空字符串的问题，未指定日志路径时安装会提前失败，现已修正。
- USN 的 32 位卷序列号与原生 `FILE_ID_INFO` 的 64 位序列号不同。搜索后台按真实卷根补全身份后，再执行现有 Open、Locate 和当前目录范围验证，保持全局严格身份比较。真实 D 盘旧结果的错误 1006 已复现并通过修复后的路径验证。
- `FastIndex::Load` 先校验和构造候选快照，成功后才替换；服务按卷发布对应索引。缺失或损坏的 C 盘库不会污染已经加载的 D 盘结果与健康状态。
- 默认搜索遇到正常的冷加载 Busy/997 时，在同一搜索任务内等待，多个卷共用 120 秒预算；取消和过期结果仍由原任务生命周期处理。真实错误保留，不被末尾 Healthy 覆盖。
- BLK5 读取改为每文件 64 KiB 缓冲，避免按每个字段调用 `ReadFile`；磁盘格式、CRC、写入器和 IPC contract 未改。新增 5000 条记录、约 1.82 MB 的跨缓冲区回归，核对完整加载和搜索结果。

## 验证与边界

| 检查 | 结果 |
| --- | --- |
| 完整 solution Debug / Release 构建 | 均通过，编译并发 1 |
| Phase1 普通权限回归 | 每配置 1382 PASS、0 FAIL、4 项权限 1314 SKIP |
| Phase0、Phase05 搜索专项、Phase05 完整、Transfer | 两配置均重新运行并通过 |
| 新增专项 | 11 项真实卷身份、6 项启用/建库 fake RPC、4 项 Phase05 加载检查 |
| 输入和资源 | 219 项冻结输入，345 项中英/fallback 资源一致 |
| 真实服务 | 本项目 Release 服务，LocalSystem、Running、Automatic |
| 首次建库 | C、D 两盘 MFT 完成，共约 146 万条；没有借用旧验收库 |
| 当前产物与已有库 | 服务重启后直接加载已有库，健康卷没有重复重建 |
| 真实搜索与变化 | 生产 SearchCoordinator 搜索完整覆盖 C/D；自有文件创建、改名、删除后结果收敛，打开身份和定位行验证通过 |

旧 Phase05 完整 harness 固定使用 Debug console host；真实 Release 系统服务由独立 live probe 验证。C 盘的索引和完整搜索覆盖已验证，变化 fixture 位于项目 D 盘，因此不把 D 盘的创建/改名/删除试验冒充 C 盘试验。自动启动配置已核对，未执行整机重启。上述 Open/Locate 是实际生产模型路径验证，不等同于启动关联程序或实际 WinUI 鼠标键盘验收。

## 使用和恢复

启动默认 Release 程序即可使用当前索引。后续选中尚未建库的卷，可在索引状态入口执行首次建库；需要重新启用服务时，入口复用 `scripts/enable-index-service.ps1` 和原安装脚本。常规浏览、搜索及文件操作仍由普通用户界面发起。安装记录只保留最新 `.blink-data/logs/index-service-setup.log`，详细入口见 [INDEX_SERVICE_SETUP.md](INDEX_SERVICE_SETUP.md)。

本轮摘要、构建/测试日志、实机探针记录、失败原因及输入/产物摘要归入 `.build/index-repair/integration`。其中 `native-validation.json` 为最终验证结果，`source-snapshot.json` 为已逐项校验的成功源码恢复包。新包验证后保留本轮与 Slice20 两个成功恢复点，清除已替代的暂停 WIP 和 Slice19 压缩包；当前程序、有效依赖、真实索引数据和服务继续保留。

下一步回到原 Phase1 开发计划：目录移动合并、剪贴板外部完成反馈与剪切外观、组合场景及必要整理，再推进 SFTP/自建网盘。文件管理器本地功能不扩展额外防护平台。

后续搜索性能专项见 [SEARCH_PERFORMANCE_HANDOFF.md](SEARCH_PERFORMANCE_HANDOFF.md)，包括多卷驻留、名称预处理、有效limit及同程序实测。
