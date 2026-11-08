# Phase 1C Slice 3 — 搜索界面与结果动作

**目标**：把 Slice 2 已实现的搜索模型和后台请求层接入标签，不覆盖浏览快照，完成搜索框、范围/模式、结果列表与 Open/Locate。此文是下一刀实施计划，尚未实施或验收。依据 PHASE1_PLAN 第 8 节和 1C 冻结范围。

**模块与接口**：

- TabRecord 持有 shared SearchState / owned SearchCoordinator；MakeTab 创建，CloseTab/Shutdown 先关闭搜索状态。UI 标签记录保存搜索草稿、显示模式与稳定结果选择，使用 SearchResult 的 source_volume + identity + parent_identity + name/full_path 区分硬链接。
- 新建 SearchPresentation.h/.cpp，构造搜索设置和虚拟化结果 ListView；每行显示文件名、完整路径、源卷，显示逐卷状态、错误、上限与部分结果。主窗口只转发 query、retry、cancel、open/locate 意图，所有 RPC 和文件验证留在后台。
- 新建 SearchResultValidation.h/.cpp，接收 SearchResult，后台用现有 metadata handle 重新验证结果 FileIdentity、父目录 FileIdentity、name/path 和 directory 类型；失败显示“结果已变化/无法定位”。File Open 继续用 FileActivationController/helper 再验证文件身份后关联派发；Directory Open 先验证再导航；Locate 在父目录新 browse commit 后按当前真实行 identity 选择。

**产品行为**：

1. Enter 提交搜索；提供 All indexed local volumes 和 Current Folder 两个范围，substring/prefix/extension 模式。This PC 的 Current Folder 不可用，UNC/Provider 当前明确不支持；不做 DFS 搜索。已有协议没有 exact 开关，不能把受限 substring 结果过滤成看似完整的 exact 查询。
2. 搜索和 folder 内容用两个独立 presentation surface。进入搜索只改变该标签的显示模式，folder watcher 继续对账其 committed listing。切换标签恢复各自查询/结果/视图；无新的文件枚举或自动重复搜索。
3. Back 在搜索中退出搜索，保留目录历史；F5 在搜索中重新查询，显式 retry 仅补齐同一次查询中未成功卷。明确新 query / refresh 必须丢弃之前已完成卷结果。地址导航、侧栏导航与 Up 明确退出搜索并使旧请求 generation 失效；自动 watcher refresh 保持搜索显示。
4. 全局状态面板的定时 QueryVolumeStatus 在任一搜索 worker 在途时延后，避免后台状态轮询切换服务的单活动索引；用户仍可读已有状态快照。多个标签请求需要窗口级调度，最新 pending intent 受 TabId/generation 管理，避免不同标签同时改变活动卷；不新增常驻 detached worker。
5. 搜索期间浏览仍可用；单卷 busy/unavailable 呈现部分结果与重试，Busy 的全局操作不标为目标卷 Healthy/Rebuilding。保留的成功结果注明来自同一次查询，真实操作前重新验证。

**实施与测试**：

- 为 search mode、tab 隔离、显式/自动导航、关闭和 generation 写行为测试；RED 后接 TabWindowState。避免仅检索源码字符串作为主要证据。
- 临时 Unicode fixture 验证正常文件/目录、改名、删除、同名替换、父目录替换以及两条硬链接的动作目标；stub validator 观察 RED 后实现 metadata 验证。
- Current Folder 的现有字符串边界过滤遵循 IPC 的大小写不敏感路径语义；产品接线前补当前目录及候选祖先的真实目录身份验证，覆盖大小写敏感目录、reparse/目录链接和长路径，不能把只有大小写不同的其它目录混入范围。索引表达不了的 alias/reparse scope 明确显示不支持。
- 可控 fake service 覆盖多卷 cold-load→busy→只重试未完成卷、其它标签并发、disconnect→retry、上限和无结果，不执行真实服务安装或 MFT 验收。
- Debug/Release 普通 native 检查和主模型审查；按用户既有要求保留界面/真实服务人工门禁。完成产品接线后才将搜索描述为可用，Phase 1C 整体验收仍独立。

**审查重点**：旧菜单/快捷键不能以 folder selection 操作 search result；显示状态更新不能抢回已退出的搜索界面；Locate 的迟到 commit 不能改变另一个标签；移动/删除后不能借同名路径打开其它对象；关闭不在 UI 等待 Client mutex 或 native work。
