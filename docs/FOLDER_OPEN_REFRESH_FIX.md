# 打开目录二次刷新修复（2026-10-28）

## 现象与根因

用户反馈打开一个文件夹后似乎会再次刷新。`EnsureFolderWatcher` 将原生监听的 `Ready` 与 `Changed` 一样送入 `FolderChangeState.Notify`，200ms 合并后发起第二次 Browse。旧逻辑用这次补刷覆盖初始枚举与监听启用间的空隙，稳定目录也会重复加载。

## 修复约束与步骤

1. 先用共享生产事件路由与 BrowseController 的受控启动屏障证明旧行为：Ready 造成重复刷新、初次枚举抢在监听前执行。
2. 每个监听 Session 提供独立可等待的就绪事件；成功启用、失败和 Stop 均解除首次加载等待，不阻塞 UI 或占用线程等候。
3. Browse 异步等待就绪并再次核对 generation；普通导航不再将 Ready 视为内容变化。真实 Changed/Overflow 继续合并对账。
4. 导航失败恢复旧页面时没有新的首次枚举，须明确保留 Ready 补刷。关闭、快速切换、监听不可用均不能留下悬挂任务。
5. 完整 Debug/Release 单并发构建后串行普通测试，检查冻结输入、资源、既有功能与清理状态。人工 UI/剪贴板/服务验收继续暂缓。

## 最终结果与恢复入口

- 完整 Debug/Release 构建均 exit0；普通测试各1057 PASS、0 FAIL、4项符号链接权限1314 SKIP，严格在各自构建结束后串行运行。新增16项回归全部通过。
- 稳定中文目录使用真实 ReadDirectoryChangesExW/EnumerateDirectory 只执行首次加载，Ready 不再留下脏状态。首次加载期间真实创建文件仍触发对账，且忙碌期间通知保留；快速切换、取消、关闭、监听不可用、多个独立等待与16轮立即停止均验证。
- 177项构建输入冻结，7项旧输入修改、2项回归源码新增；248资源、工程引用、冻结三文件、10 EXE/两PRI摘要通过。六旧EXE未变，沿用明确标注的历史普通证据。
- 原生进程/ASCII别名0，WSL oom_kill0；附录B历史6/1/17/0未增减，无新建工作区根文件。工具链继续使用 D:/DEV/C++，无安装、无子代理，编译并发1。
- 证据目录 `.build/folder-open-refresh-fix/integration-20261028-0917/`，主记录 `native-validation.json`、`input-sha256.json`、`change-scope.json`、`review.md`。快照摘要/逐文件清单见`source-snapshot.json`与`.build/folder-open-refresh-fix/snapshots/`。
- 早期编译字段名笔误、旧全套二进制1041基线误选与scoped helper路径导致RED wrapper中断均在`red-assessment.json`区分；两项预期 RED 已实际观察，最终结论只使用完整 solution 产物的双配置1057结果。

本次没有启动产品窗口，用户实际视觉体验仍待确认；人工 UI/真实剪贴板/服务验收继续暂缓。Phase1D Slice12 完成点保留，[Slice13](PHASE1D_SLICE13_DEVELOPMENT_PLAN.md)仍为下一开发计划。未将单个问题修复描述为整个项目完成。
