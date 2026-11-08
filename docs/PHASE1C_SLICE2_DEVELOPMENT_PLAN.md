# Phase 1C Slice 2 — 搜索请求与结果基础

**目标**：实现可独立验证的标签级搜索状态与后台请求层，为下一刀搜索 UI 与 Locate/Open 接线提供唯一数据模型。规范来源为 PHASE1_PLAN 第 8 节及 1C；人工验收暂缓，主模型自行执行。

**结构**：SearchState 保存 query/scope、generation、带 source volume 与完整 link identity 的结果及逐卷错误；SearchCoordinator 用既有只读 Search 命令在受控 worker 内串行查询卷。浏览快照、history 与 selection 不被搜索覆盖。失败重试只查询未成功的卷，保留同一次查询已经得到的结果，避免单活动索引的服务在 C/D 之间反复加载。

**约束**：不改变 IPC、ACL、Search timeout、FastIndex/BLK5；不启动索引、不做 DFS fallback。Busy 是未确认的服务级操作，不能将全局 Rebuilding 标到请求卷。已有协议提供 substring/prefix/extension；不以截断的 substring 结果伪装 exact。Current Folder 必须是本地绝对路径，并再次检查路径边界，拒绝 sibling prefix、无效 identity/path 与 scope 外结果。

**步骤**：

1. 新建 SearchState.h/.cpp 和状态/映射测试，使用 stub 观察 RED，再实现 generation、link identity、scope 与错误映射。结果上限显式可见。
2. 新建 SearchCoordinator.h/.cpp，注入 fake exchange/roots；覆盖 readonly commands、known-hit/miss、逐卷失败、只重试失败卷、断开、关闭与 stale completion。一个 controller 同时只持有一个后台任务。
3. Debug/Release 构建与普通检查，主模型审查 worker 所有权与关闭路径，保存源码/产物摘要。按北京时间 23:00 停止，不因本刀完成而声称 Phase 1C 完成。

**后续**：搜索框、scope/mode UI、虚拟化结果列表、Locate/Open 的当前文件和父目录身份验证、标签生命周期接线单独实施。当前刀不将未接线的搜索描述为可供用户使用的完整功能。
