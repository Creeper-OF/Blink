# Phase 1C Slice 1 — 索引服务连接与状态

目标：接入既有 IndexService，使服务连接、卷状态与处理进度成为可见的产品状态；本地浏览保持独立。用户授权持续推进开发至北京时间 2026-10-14 23:00，人工验收暂缓，所有任务由主模型执行。Phase 1B 开发已完成当前 native 门禁，人工门禁不提前升级。

复用 Blink.Index.Contracts.Client，保持 IPC ABI、命令权限、ACL、数据库与既有 timeout 不变。只使用 QueryIndexProgress、QueryServiceStatus、QueryVolumeStatus，不启动、停止或重新建立索引，不运行管理员/全卷 MFT 验收。

组件：IndexServiceState 保存窗口级连接/卷/全局操作快照和 generation；IndexServiceCoordinator 在单个受控后台任务中发现现有本地卷并查询，拒绝重复在途刷新，关闭时逻辑取消并让 bounded IPC 在后台退出。IndexServicePresentation 构建紧凑状态入口与状态浮层，使用本地化文案、重试按钮和真实计数；主窗口只接生命周期和意图。

协议限制：Client 自身分配并验证 wire request ID；adapter 不虚构 caller ID。活动操作进度没有卷根，单独作为服务级进度展示；Busy 卷响应保持“状态待确认”，不把全局 Rebuilding/CatchingUp 归到所有卷。查询先走独立 progress snapshot；活动操作期间不读取可能被索引工作占用的 service stats。卷错误独立映射，失败或未知枚举不显示 Healthy。既有服务一次只装载一个卷的索引，状态面板每轮只查询选定卷，用户可切换卷；不自动依次切换所有卷造成反复加载。

步骤：

1. 为状态、reply 验证、busy/unknown、重复刷新、generation 与关闭增加行为测试；观察 RED 后实现状态映射。
2. 实现后台 coordinator 和可控 RPC/卷发现注入，测试不可用、重新连接、请求/操作 ID、单卷失败、stale/close 与禁止 mutation；全程不连接真实服务做验收。
3. 独立 presentation 模块接状态入口/重试与定时后台状态刷新；关闭不等待 IPC 或持有 Client mutex。
4. Debug/Release 构建及普通检查，主模型检查所有权、锁与错误映射；更新 PROGRESS/计划/README 与 Work 索引。

本刀暂不接搜索结果与 Locate/Open，它们为下一 1C slice。生产 UI 与真实服务人工验收暂缓，整个项目和 Phase 1C 不标为完成。
