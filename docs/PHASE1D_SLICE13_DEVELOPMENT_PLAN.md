# Phase1D Slice13 — 多项移动与剪切产品接线计划

状态（2026-10-28）：本计划的有序同卷移动、标准COPY/MOVE剪贴板意图与Ctrl+X/Paste产品接线已完成开发及原生验证，完整Debug/Release各1110 PASS、0 FAIL、4权限SKIP，182输入/268资源核对通过。人工UI/实际剪贴板互操作暂缓。恢复入口见[Slice13清单](PHASE1D_SLICE13_HANDOFF.md)，跨卷移动仍按[Slice14计划](PHASE1D_SLICE14_DEVELOPMENT_PLAN.md)继续；整项目未完成。

**目标：** 在Slice12单项同卷Move和Slice11混合COPY的基础上，完成有序多项移动队列，再接入标准Cut/Paste和移动专用状态/冲突。继续遵守PHASE1_PLAN完整目标；跨卷Move、目录Merge/Replace、拖放与删除独立跟进，不能以单项同卷实现替代完整Move。

**执行约束：** 主模型、compiler1、native串行、人工UI/真实剪贴板/服务验收暂缓。保持唯一FileOperationCoordinator与唯一native action；既有NewFolder/Rename/Copy与已完成来源身份保护不回退，不创建第二个复制/移动engine。

## 1. 有序移动队列

- 顶层最多1024项/4MiB来源路径，与现有metadata捕获一致；保留选择顺序和重复项。每项调用同一MoveEntrySafely，目录原子rename保留子树，不递归copy/delete。
- snapshot逐项保存原请求、实际MoveResult、Pending/Running/Moved/Unchanged/Skipped/Failed/Cancelled，明确moved/no-op/skipped数量。已成功前缀不重做，未开始尾项保持Pending。
- 整包结构与当前支持范围预检后再开始；source身份在每项实际移动前重新核验。重叠或重复来源不擅自重新捕获/改路径，第一次已移动后再次引用旧路径应明确失败，可由用户跳过。
- 当前同卷范围内，预检已知跨卷项时先拒绝整包并保留来源；后续跨卷实现仍须完成Copy并持有可证明的来源提交/删除保护，不能只看路径仍存在或ID未变就删除可能已修改的源数据。
- operationID与item索引绑定进度，等待实际action结束才能续队列。最后实际移动完成后的晚取消不反转提交，目录post_commit_error保存为warning而不伪报无改动。保护失败队列的pending尾项。

## 2. 剪贴板意图与身份

- ClipboardFileTransfer增加显式Copy/Move意图，CF_HDROP codec继续纯有界解析；Preferred DropEffect明确区分COPY/MOVE，链接/虚拟项或不支持的组合给出明确错误。发布Cut只发布标准数据，不自行删除来源。
- internal cache匹配实际发布payload顺序/数量/sequence及意图，保留原始source/parent IDs。外部数据整包metadata捕获；Source同名替换不得被当作新外部来源接管。
- 准备阶段与导航origin generation共享取消/过期门禁；A→B→A、切换标签、失败导航、搜索和关闭后，迟到结果不能再次提交。
- Blink→External由外部目标执行真正Move；Blink→Blink和External→Blink由唯一coordinator执行。部分完成后的重复粘贴/缓存处理需要明确规则，不自动对已移动前缀再操作或静默把MOVE降成COPY。

## 3. 产品状态与冲突

- Ctrl+X/右键剪切、Ctrl+V/背景粘贴复用现有命令入口。状态、名称重试及冲突提供Move文案，复制文案不能误标移动。中英文/fallback及PRI一致。
- 文件/目录同名可Skip/KeepBoth/Cancel，Replace及尚未实现的目录Merge保持禁用。原地成功no-op单独显示，实际moved才触发来源和目的父身份刷新。
- 已移动目录所在其它标签/地址跟随仍按独立计划处理，不能假装已完成。导航离开不能重定向正在执行的原目的地，旧dialog决定须再次检查origin/current operation。

## 4. 验证顺序

先写并观察RED：真实文件/目录/ADS/硬链接多项Move、同名续尾/KeepBoth、重复/重叠来源、后项身份变化、跨卷整包拒绝、取消/晚取消、warning、Shutdown、旧item/operation回调、action退出，以及COPY/MOVE纯codec/cache/目的绑定和origin检查。实现后冻结输入，完整Debug/Release构建和普通检查、资源/冻结合同/旧EXE证据与工作区审计，最后更新恢复说明/索引并逐文件SHA256归档。人工清单保持待执行，不启动产品窗口或修改用户实际剪贴板。
