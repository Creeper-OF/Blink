# Phase1D Slice8 — 已验证检查点与恢复入口

历史检查点；2026-10-16后续单目录产品入口进度见[Slice9恢复清单](PHASE1D_SLICE9_HANDOFF.md)。

更新时间：2026-10-15晚间。用户要求本轮22:00停止；实际停止时间及最终快照以`.build/development-session-20261015-1932/state.json`为准。整项目和Phase1D尚未完成，人工验收继续暂缓。

## 当前可以承接的基础

Slice5已把普通本地文件多选Copy/Paste接入窗口唯一coordinator：有界Windows CF_HDROP、逐项冲突、显式续队列、取消和真实partial历史。Slice6提供有界目录复制engine，Slice7接入同一个coordinator，Slice8提供普通文件批次或单目录互斥的worker metadata来源捕获。**目录仍未接入窗口命令或实际剪贴板流程。**

最新完整Debug和Release构建成功，各898项普通检查PASS、0 FAIL，各4项file-symlink因普通权限1314 SKIP。新增本轮检查按刀分别为74、44、27、29；从724项基线增至898项。228项中英文/fallback资源一致，161项输入和10个产物有SHA256证据，冻结IPC/IndexService三文件不变。6个旧Phase0/Phase05/Transfer二进制沿用相同摘要的既有证据，未冒充本刀重新执行；Transfer空入口只算smoke。

证据入口：

- 当前权威验证：`.build/phase1d-slice8/integration-20261015-2120/native-validation.json`，同目录保留Debug/Release build、tests日志及状态。
- Slice5/6/7检查点：`.build/phase1d-slice5/integration-20261015-1932/`、`.build/phase1d-slice6/integration-20261015-2030/`、`.build/phase1d-slice7/integration-20261015-2100/`。
- 同一源ID检查顺序问题的首次Debug 867 PASS/1 FAIL保留在Slice7 attempt1，修复后增加确定性1006/零写/源保留回归。目录创建share WRITE导致87的编译/链接/原生失败保留在Slice6研究目录，不覆盖历史。
- 独立真实重定向探针：`.build/phase1d-slice8/research/owned-reparse-probe/`。WRITE_ATTRIBUTES-only实际成功转换held目标root为junction；engine以50拒绝，copied=false、partial_tree_left=true、files=0、dirs=1，source/sentinel身份、字节及唯一子项不变，fixture清理完成。31条观测与898普通检查分开。这只证明该真实hook时点，不证明所有竞态或元数据变更都被冻结。
- 人工UI、真实剪贴板互操作、真实IndexService、管理员/MFT验收均未执行；[文件队列人工清单](PHASE1D_SLICE5_MANUAL_UI_CHECKLIST.md)保持全部待执行。

## 下一刀：单目录产品入口

按[Slice9计划](PHASE1D_SLICE9_DEVELOPMENT_PLAN.md)接线，不扩展Merge、Replace或Move。

| 接线位置 | 已有入口与必须保留的语义 |
| --- | --- |
| `ClipboardFileTransfer.h/.cpp` | `CaptureClipboardCopySources`只输出monostate、完整有序文件列表或单个DirectoryCopyRequest。成功不证明树内容可复制；目的地字段保持空，提交时重新核验身份。旧files-only入口不扩大行为。 |
| `main.cpp`来源准备与内部剪贴板状态 | 现仍调用`CaptureFileCopySources`。改为typed模式后，同样按实际发布有序路径及sequence保留最初source/parent ID；外部数据在worker捕获。结构错误、混合/多目录必须零提交，不返回可用前缀。 |
| `FileOperationCoordinator::CopyDirectory` | 沿用唯一Submit和owned native action。completed进度/terminal publication不等于action真实退出；窗口关闭合作取消，不能同步等待或开启第二个mutation。未决file batch tail也阻挡新目录任务。 |
| 目录状态与刷新 | 从独立directory request/progress/result取partial_tree_left、root/failure path、files/dirs。通用partial_file_left按设计为false，不能据此漏掉空partial root或谎称无写入。当前文件bytes与累计对象数量分开。 |
| 目录同名与重试 | 首版只新root，已有root明确要求新名称或取消。新名字不改source/parent身份；失败后重新命名是新任务，原partial仍须呈现，不能无声续写/接管/删除旧树。 |
| 异步对话/导航 | 现有origin只有tab/location identity，无navigation generation。先补A→B→A旧准备结果/continuation的可控延迟回归与世代保护；本轮仅静态审查，未UI复现或修复。旧回调仅允许必要的原目标刷新，不能打开当前页冲突或重新提交。 |

目录engine界限为10000后代、256目录、128深度、16MiB UTF-16路径预算；固定no-follow API、同handle身份核验、目录ADS/reparse/特殊存储明确Unsupported。失败后保留实际新建树，不按路径递归回滚。不增加IndexService写接口或后台复制引擎。

现有重试按钮只区分copy_request与通用新建/重命名；目录接入前必须独立分支，避免目录复制失败后误建空目录。新的目录operation会清除旧snapshot上下文，UI需另保留旧partial root并验证重试成功后仍可见。内部剪贴板cache也要保存typed模式，目录source同名替换后必须身份失败，不能按外部来源重新捕获后接受。

## 恢复与验证约束

先读项目README、PROGRESS、本文件和Slice9计划，再核对当前源码与最新验证的`source_sha256`。项目已有大量本轮及此前修改/未跟踪文件；保留原样，不执行reset/clean或以旧快照覆盖。快照用于恢复与核对，不能替代当前工作目录。

工具链留在`D:\DEV\C++`，所有编译/native检查由主模型串行执行，compiler并发1。新一刀建立自己的`.build/phase1d-slice9/`证据目录，冻结输入后再完整构建Debug/Release。现有Slice8的`run-build-cached.ps1`/`run-tests.ps1`会覆盖本目录同名证据，不能直接用来保存新一刀结果；先适配到新目录或给新运行独立路径。

没有源码变化时无需重复898项检查。只读复核可使用`git diff --check`、SHA256对照和附录B脚本；最终进程审计还要包含`owned_reparse_probe.exe`。旧工作区附录B既有Work根层5文件、project根层1文件、其它工程17个产物目录、缺失分区README0；只记录变化，不擅自整理其它资产。

本轮3子代理属于用户本次明确授权，22:00本轮停止后不得自动延长开发。下次开始应核对当时最新协作规则和新的时限；不要把旧goal的usageLimited状态伪改为complete/paused。

## 后续待做

单目录UI及partial/新名称重试完成后，继续处理目录冲突Merge/KeepBoth、多root队列、安全Replace、Move/Cut、拖放、Delete与回收站、完整元数据支持和后续SFTP/SMB/WebDAV等远端Provider。人工/真实服务验收保持独立；后台检查通过不等于这些功能已交付。
