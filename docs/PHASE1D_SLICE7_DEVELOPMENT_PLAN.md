# Phase1D Slice7 — 目录复制任务协调器

状态（2026-10-15 21:20）：已实现，完整Debug/Release及各869项普通检查PASS、0 FAIL，各3项权限1314 SKIP；目录UI/剪贴板未接入。用户授权本轮3子代理，主模型串行编译/测试，compiler并发1，22:00停止报告。

## 范围

- 新增app-private FileOperationKind::DirectoryCopy和CopyDirectory入口，沿用窗口唯一Submit、operation ID、cancellation和owned native action。目录engine只有CopyDirectorySafely，不另开worker，不将目录伪装成普通file batch。
- Snapshot独立保存directory request、progress和returned result。partial_tree_left、created-directory/committed-file数量、root path、failure path保留完整；顶层partial_file_left保持false，不把树残留说成单文件残留。
- 100ms合并进度，每个当前文件路径切换时允许bytes归零；completed进度仅为观察事实，不能让尚未return的action提前可被替换。真实returned copied胜过late cancel，failed不因Cancel被改成假成功，error0/copiedfalse归一化失败。
- Shutdown合作取消并立即关闭发布，不在UI等待；native action自持state/request/executor到真实退出。旧operation/kind/closed回调不能修改新任务，终态发布仍不等于action已返回。
- 普通CopyBatch未决tail继续阻挡新目录任务。目录终态本刀无resume/mixed batch/冲突决策，不扩展Merge/Replace/Move；既有files队列功能保留。

## 验证

新增薄适配fixture使用真实engine覆盖成功、取消、partial与实际冲突/源oracle，另以确定gate验证提交前后、terminal snapshot与action真实结束、Shutdown/destructor寿命、旧回调跨kind、当前file bytes重置、原file batch门禁。错误注入只用于验证adapter状态，不能冒充真实复制。两配置全量普通回归包含既有842项，完整build及source/output摘要后才更新本刀通过状态。

本轮还修复现有file batch刷新：同item多次失败各留下新path时，历史partial item数量可能不变，UI必须同时处理本次实际committed/partial事实，不能只看聚合数量增长。

## 未开放范围

目录UI菜单、Windows剪贴板目录捕获、混合多文件/目录队列、Merge/KeepBoth、目录ADS/reparse复制、整树回滚、完整EA/security备份、Move/Cut、Drag & Drop、Delete及后续Provider继续独立待做。人工UI、真实剪贴板/服务及高权限验收仍暂缓；没有本刀后台验证可以代替它们。

最终证据：`.build/phase1d-slice7/integration-20261015-2100/native-validation.json`。首轮867/1 RED保留，源identity优先级修复及确定性回归见PROGRESS。
