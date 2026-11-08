# Phase1D Slice10 — 单目录根冲突 Skip/KeepBoth/Cancel

状态（2026-10-16）：开发中，前置Slice9两配置各931 PASS/0FAIL/4权限SKIP。主模型执行、compiler1，人工UI/实际剪贴板/真实服务验收继续暂缓。本轮先保存165输入恢复包，再用真实owned fixture观察RED、实现及全量两配置验证。

## 已确定范围

DirectoryCopyRequest沿用FileCopyConflictPolicy，默认Fail保持既有行为；Skip明确跳过，Cancel明确取消，均不重新访问源或目标。Replace保持Unsupported，未知policy拒绝。DirectoryCopyResult新增独立skipped，coordinator只有零effects的合法Skip才显示Skipped，不能将已创建树伪报跳过。

KeepBoth只处理根名称占用：最多1000候选，原名称、完整名称加“ (2)”至“ (1000)”。目录名中的点不按扩展名拆分，长组件不能切开UTF-16代理对。复用同一次有界source plan与held source/parent guards，碰到原子创建冲突后，在任何写入前完整准备新的目录目的路径；路径和临时缓冲仍受现有16MiB预算/32767 native路径限制。不能逐候选重新遍历整树，也不能通过exists检查决定接管目标。

只在尚未创建任何自有目录时改根名称。新root一旦建立，子冲突、权限错误、取消、metadata或inventory错误均报告当前实际partial/count/path，绝不自动换名再复制。旧Fail模式的同源同名拒绝87保留；显式KeepBoth可复制到同父的另一个名称，真正源自身/后代目的父仍拒绝。

## 产品接线与验证

复用现有冲突dialog及一次性CopyConflictModel，目录文案/automation ID独立，默认取消、Replace/Merge不可用。只有真实native action结束、origin仍有效且当前operation ID匹配才接收决定；任何已有partial不得自动进入根冲突流程。手动名称重试重置为Fail，保留此前partial提示。

真实fixture覆盖Skip空目标/源已变、Cancel/Replace/未知policy、空闲原名、文件/目录/竞态占名、点/Unicode长名、同父复制、子冲突/取消不换root、源identity/后代保护及候选上限。coordinator验证Skip及KeepBoth独立结果和非法“有effects的Skipped”拒绝；保留此前全部普通回归。最终完整Debug/Release、资源对齐、冻结IPC、输入/产物摘要及workspace/native审计后更新进度，不将普通检查当人工接受。

目录Merge、多root队列、完整metadata、安全Replace、Move/Cut、DragDrop、Delete和远端Provider保持后续独立任务。

## 已完成验证 — 2026-10-27

上述实现已接入产品代码。Debug/Release完整构建成功，各949 PASS、0 FAIL、4权限SKIP，248资源对齐、165输入/冻结合同与工作区审计通过。18项新增检查覆盖根冲突、同父目录、长名称、竞态、取消partial及非法Skipped。人工验收继续暂缓，见同目录HANDOFF与MANUAL_UI_CHECKLIST。
