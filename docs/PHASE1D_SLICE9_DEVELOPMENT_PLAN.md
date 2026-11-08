# Phase1D Slice9 — 目录复制产品入口（下一刀计划）

状态（2026-10-16）：单目录产品接线已实现，完整Debug/Release与各931项普通检查PASS、0 FAIL，各4项符号链接权限1314 SKIP；165输入摘要、242资源对齐及冻结IPC/服务核对通过。目录Merge/KeepBoth和多目录队列仍未开放。

## 接线前先处理的审查项

原FileOperationOrigin只保存tab/location/parent identity。确定性延迟回归已复现A→B→A等5种旧intent复活，现由TabRecord.file_operation_generation和共享FileOperationOrigin门禁保护；显式导航/失败、切出标签、搜索及Shutdown均失效，自动同ID刷新保留intent。窗口全部新建/重命名/文件与目录复制、对话continuation使用此门禁；12项ordinary gate检查已通过，产品UI人工未复现/验收。

重试按钮已加入独立DirectoryCopy分支，目录失败/取消不再落入通用新建/重命名。目录终态发布后异步等待真实action退出，再开放新名称；新任务保留旧partial历史，最多展示32条路径/65536字符，计数仍含全部尝试。仅显式新的复制任务重置本条重试历史，不删除磁盘树。

## 最小用户闭环

1. 首次只允许单个普通本地目录Copy/Paste；普通文件多选维持既有队列。混合目录/多目录选择明确整体拒绝，不悄悄遗漏。Ctrl+C和右键入口复用同一source捕获，worker读取parent+leaf身份/type/reparse，不在UI遍历整树。
2. 标准Unicode CF_HDROP仍发布COPY意图。内部实际有序路径+sequence匹配时保留最初身份；外部目录由worker捕获。准备取消、窗口关闭和origin失效后不得迟到提交，不能把目录engine的Unsupported当成文件fallback。
3. 粘贴到source自身后代按engine identity检查拒绝；同父同名/已有root只可选择明确的新root名或取消，直到独立证明root KeepBoth和Merge。新名称不得改变源ID或目标父ID；不能通过改名重试接管已经留下的partial tree。
4. 使用唯一coordinator.CopyDirectory；目录专用状态显示扫描/创建/已完成文件/当前文件字节、真实partial root及failure path。不能套用Operation.Created、普通CopyBatch百分比或partial_file_left提示。Cancel保持cooperative，native真实结束前不允许另一个mutation。
5. 目录发布刷新必须检查独立directory_copy_result的copied或partial_tree_left，不能只依赖通用partial_file_left（后者按设计为false）。根据目标父identity刷新；旧opid只允许必要的原目标刷新，不能打开新页冲突或重绑目的地。
6. 失败后的名称重试是新的单目录任务，保留source identity，明确先前partial仍在；本阶段不自动续传/合并/整树删除。关闭或导航使对话continuation失效，不同步等待worker。

## 必要验证与门禁

保留当前所有文件队列/剪贴板/目录backend/coordinator及typed source capture回归。新增typed capture结果与内部clipboard identity接线、非法混合输入零提交、origin切换/准备取消、同名root名称重试、真实partial状态与刷新、late conflict回调、Cancel/关闭和当前文件字节语义检查。两配置完整构建、资源键对齐、冻结IPC/服务摘要及进程/工作区审计后，另列全部待执行的目录人工清单；未运行界面或真实剪贴板时不得标为人工接受。

[预备人工清单](PHASE1D_SLICE9_MANUAL_UI_CHECKLIST.md)已列出，但全部待执行，不能视为Slice9已实现或已验收。

目录Merge/Skip/KeepBoth/Cancel完整冲突语义、多root队列、目录ADS/reparse对象、完整metadata备份、安全Replace、Move/Cut、拖放、Delete和远端Provider继续独立推进，不随单目录产品入口完成而提前结项Phase1D。

最终证据：`.build/phase1d-slice9/integration-20261016-2110/native-validation.json`。12项origin及21项transfer/partial检查加入原898项回归；两轮RED日志保留。当前恢复入口见[Slice9清单](PHASE1D_SLICE9_HANDOFF.md)。没有产品窗口/真实剪贴板或真实服务验收，本刀不结项Phase1D。
