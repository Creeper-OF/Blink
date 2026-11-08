# Phase1D Slice8 — typed剪贴板来源捕获后台

状态（2026-10-15 21:38）：已实现，完整Debug/Release及各898项普通检查PASS、0 FAIL，各4项file-symlink权限1314 SKIP。仅worker metadata capture，不接目录UI/剪贴板读写，保留既有文件捕获接口行为；主模型统一串行验证，compiler1，3子代理本轮授权至22:00。

新增CaptureClipboardCopySources，输出variant<monostate, vector<FileCopyRequest>, DirectoryCopyRequest>。成功为普通本地文件批次或单个普通目录，互斥；任何错误/取消回到monostate。整批字符串共享现有1024路径/4MiB界限，先验结构再做I/O；任一目录混合/多目录整包Unsupported，不静默去重/返回前缀。

复用同一次no-follow source handle查询type/identity和parent DirectoryLease，保留实际parent/source IDs及name/path；destination字段空，复制时仍要重新核验所有身份。旧CaptureFileCopySource(s)继续strict files-only。新入口自己限制ordinary属性，不通过失败切换file/directory API。

目录ADS、后代junction/文件权限/库存均交DirectoryCopySafely后续预检；capture成功只代表root元数据可读取，不能宣称目录树可复制，不扫描整树也不触碰真实剪贴板。

新增29项普通检查覆盖互斥模式、完整source/parent身份、有序重复/硬链接、混合/多目录整包失败、旧输出与destination清空、结构限额先于I/O/取消、预取消、真实leaf/ancestor junction、扩展长路径和句柄释放。实际FSCTL_SET_SPARSE成功后，新typed入口Unsupported且输出为空，旧single/batch metadata API继续成功；普通root有目录ADS或后代junction仍能metadata capture，随后由engine决定内容支持范围。4项权限SKIP没有算作no-follow通过。

最终161项构建输入、10个产物摘要、228项资源对齐、冻结IPC/服务与既有二进制证据、完整日志见`.build/phase1d-slice8/integration-20261015-2120/native-validation.json`。两配置均无失败build/test尝试；此前失败记录保留。人工界面、真实Windows剪贴板互操作、IndexService及管理员/MFT验收均未执行。

独立owned reparse探针以WRITE_ATTRIBUTES-only实际把held目标root改成junction，核实同ID、mount-point tag与sentinel目的地。engine返回error50、copied=false、partial_tree_left=true、files=0、dirs=1；source/sentinel身份及字节/唯一子项保持，fixture已清理。31条观测独立保存，不增加898普通检查计数；仅是该hook时点的真实拒绝证据，不宣称冻结全部metadata/reparse变更或覆盖全部竞态。见`.build/phase1d-slice8/research/owned-reparse-probe/README.md`。

下一刀见[目录产品入口计划](PHASE1D_SLICE9_DEVELOPMENT_PLAN.md)。目录UI/名称重试/partial展示与刷新、Merge/KeepBoth、Move/Cut/拖放/Delete及后续远端阶段继续独立待做；人工验收暂缓。
