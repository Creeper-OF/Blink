# Phase 1B Slice 8 — 本地目录变化监听

依据：PHASE1_PLAN 的 1B watcher 范围、D-0027 的 debounce/reconcile、有效选择保留与生命周期约束。用户要求继续功能开发，人工验收暂缓；本计划在指定工作目录由主模型执行，构建并发 1，不建立子代理或提交其它在途修改。

实现目录标签的非递归监听：使用 ReadDirectoryChangesExW 的 overlapped 通知作为失效提示，复用现有 BrowseController/TabState.Refresh 重新枚举。采用事件驱动的 coroutine 等待，空闲监听不占用一个常驻工作线程。通知不直接编辑行或索引。缓冲区溢出重新枚举；不支持或失效时报告“自动更新不可用”，手动刷新仍可用。

所有权：TabRecord 拥有 native watcher 和 UI 线程使用的 FolderChangeState。路径与递增 generation 保护过期通知；停止仅发取消信号，后台 CancelIoEx 后等待 I/O 真正完成，再释放 OVERLAPPED/缓冲区/句柄。每标签一个短周期 DispatcherQueueTimer 仅在有待处理变化时运行，200 ms 静默或 1 s 上限触发，browse/projection 忙时保留 dirty；刷新期间的新通知留给下一次刷新。初次监听 Ready 触发一次对账，覆盖监听建立与首次枚举之间的变化。

自动刷新保留地址草稿、历史、视图与排序；选择继续按有效 RowIdentity 收敛，删除项和已改名的旧行不会误选其它硬链接。非活动标签只更新自己的状态。导航失败恢复原 committed 目录监听，This PC 不监听；关闭标签/窗口取消 watcher、timer 与回调。树保持原惰性模型，本刀的变化更新文件区，不递归监听其它目录。

执行步骤：

1. 为合并、忙时延迟、上限、在途新变化、路径/generation 和关闭保护增加行为测试，观察失败后实现 FolderChangeState。
2. 实现 FolderWatcher，增加真实 Unicode 临时目录的新增/重命名/删除、属性、子目录隔离、替换监听、待机取消和失败测试。
3. 接入 TabRecord 生命周期、生产导航/刷新及状态文案。以源码约束检查自动刷新保留草稿，构建生产入口；用户桌面不做鼠标/键盘验收。
4. Debug/Release 串行构建并运行普通回归，检查失败、资源退出与差异；主模型自行审查取消竞态、复用 OVERLAPPED 的时机、token、busy 和 inactive 路径。
5. 更新当前计划/进度/README 与 Work 索引，保持所有原人工门禁待验收。

限制：ReadDirectoryChangesExW 支持受限文件系统，当前不增加旧 API 或轮询 fallback；当前目录本身被移动/删除后，后台失败或手动刷新反馈处理，跨目录重定位不在本刀。目录树缓存的 watcher 驱动对账、网络 Provider、Search 与 mutation 不在本刀。
