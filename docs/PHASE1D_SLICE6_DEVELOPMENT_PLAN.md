# Phase1D Slice6 — 有界目录复制后台

状态（2026-10-15 20:30）：后台已实现，完整Debug/Release构建和各842项普通检查PASS、0 FAIL、各3项权限1314 SKIP；未接入目录UI/剪贴板/批量队列。目录后台检查点见`.build/phase1d-slice6/integration-20261015-2030/native-validation.json`，产品目录入口仍未开放；本轮继续至北京时间22:00，用户授权3子代理，主模型统一串行native构建与检查，compiler并发1。

## 本刀可审查范围

- 新增app-private `CopyDirectorySafely`，只接受普通本地目录，目标必须是原子创建的全新root。文件仍由唯一`CopyFileSafely`复制，复用ADS/Zone.Identifier、取消、身份与owned目标清理，不新增第二个字节复制引擎。
- 执行前以同一源目录handle枚举并生成有界计划，默认最多10000个后代、256个目录（含root）、128层深度（root=0），计划/临时库存/路径scratch的UTF-16字符串预算16MiB。可下调限制，不能突破hard cap。限制不等于日常大目录产品能力，本刀先验证安全边界；拒绝超限和不支持项时尚未创建目的树。
- 持有source目录保护并保存FileIdentity；每个文件执行时重验原身份。目的父链任一ancestor与source root同identity即拒绝，防止复制到自身/后代；拒绝reparse、循环和未支持属性。禁止改用路径枚举作为失败fallback。
- `CreateDirectory2W`直接取得新对象handle，禁止先create再按路径open；创建handle固定LIST_DIRECTORY|READ_ATTRIBUTES|SYNCHRONIZE、share READ、不申请DELETE，保持其后另以READ_ATTRIBUTES|WRITE_ATTRIBUTES、share READ|WRITE、完整no-follow flags取得同ID metadata handle。197条独立矩阵观测确认嵌套DirectoryLease/CopyFileSafely含ADS兼容；CreateDirectory2W加share WRITE在本机全部返回87，生产不按错误自动切换参数。share READ不能阻止所有reparse修改，因此各实际创建仍用DISALLOW、逐层复核identity/type/reparse/DeletePending。
- 已创建的树在失败/取消后保留，result明确partial_tree_left、实际path及已完成数量；不通过路径递归删除，不把保留树描述为回滚。当前未提交文件的清理由既有file engine负责。目录metadata在后序通过持有handle提交；ACL继承目标，不承诺完整EA/security备份。
- 目录自身的命名数据流尚不复制，有目录ADS时明确拒绝；普通文件ADS继续保留。未知stream-info行为应失败，不自动把API错误当空流。源目录内容不是原子快照，持续外部变化需明确失败或有限捕获语义，不能承诺时间点备份。
- 进度只报告已提交文件/已创建目录与当前文件字节；completed仅在所有计划对象和目录metadata实际完成后置true。最终通知的晚取消或observer异常不撤销成功。

## 必要验证

独立owned probe先核实API签名、创建占名、共享兼容、目录guard及释放、同handle枚举、目录metadata关闭后保留和已有file engine嵌套复制；诊断记录不混入普通PASS数。新普通fixture覆盖嵌套/空/Unicode/长路径、文件ADS、基础目录metadata、源和目的identity、既有目标、自身/后代、reparse、目录ADS、bounds、合作取消、partial事实、晚取消和异常。所有修改只在新建fixture下，不触碰用户文件、真实剪贴板、产品窗口或服务。

两配置完整构建/普通检查后保存source/output SHA256、冻结IPC/IndexService摘要及只读工作区审计。失败尝试保留，截止时未完成检查如实记录。

本轮另将既有多文件复制的UI派发改为只保留标量刷新事实及当前冲突request，避免每个待处理通知持有完整batch快照；冲突派发前复查live stage/resumable，取消后的旧失败通知不会重新弹出冲突。此项随当前完整构建验证，人工体验仍待执行。

## 后续门禁

本刀即使通过也不直接打开目录Copy菜单。后续需独立接入目录source捕获、队列条目类型、冲突语义与Merge/KeepBoth、跨目录partial展示和人工清单。安全Replace、Move/Cut、Drag & Drop、回收站/永久删除、远端Provider仍为独立任务；不得宣称Phase1D或整项目完成。
