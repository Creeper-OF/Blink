# Phase1D Slice5 — 多文件复制队列

状态（2026-10-15）：已实现，完整Debug/Release构建及各798项普通检查PASS、0 FAIL、exit0，各2项symlink权限1314明确SKIP。用户本轮授权3子代理，主模型集成且compiler并发1，继续推进至今晚22:00。人工UI与真实服务继续暂缓。

## 范围与顺序

1. 在既有窗口FileOperationCoordinator外观中增加有界、串行的普通本地文件队列。复用唯一CopyFileSafely，不新建第二个copy engine，不开多个native worker抢同一目的目录。
2. 队列保存每项source Parent+Name+FileIdentity、目的父identity、实际结果路径及copied/skipped/failed/cancelled/partial。成功项不得因后续取消被报告回滚，未开始项不得伪造成已复制或已跳过。
3. 对当前项同名冲突暂停提交，待本项native action实际退出后复用Skip/KeepBoth/Cancel对话框；队列取消停止后续项。Replace继续显式Unavailable，除非另有完整安全提交证据。最初不增加“全部应用”或隐式失败跳过。
4. 进度分别呈现已完成项和当前项字节，处理总大小溢出/未知大小，不在source捕获或单项完成时提前报告全队列100%。刷新仅由真实committed/partial项触发，仍按目的父identity。
5. 扩展现有CF_HDROP有界解析与内部复制快照，接入本地普通文件多选。外部source身份仍在worker捕获；混合目录/特殊存储明确不可提交，先验证整批输入，不能悄悄遗漏不支持项。保持COPY意图，不发布Cut，不删除源。

## 必须处理的生命周期

关闭窗口/切换页面后旧回调不能推进新队列或修改新位置；合作取消保留在途action所有权，不在UI等待；一次只拥有一个真实native操作。冲突弹窗、剪贴板capture、队列调度及当前项retry必须统一busy/代际约束。目的目录改名后的重试只显式绑定仍为同identity的当前路径。

## 普通验证与交付

覆盖两项以上的顺序、重复同一source/硬链接输入、Skip与KeepBoth、第二项失败、首项提交后取消、最后一项晚取消、关闭/导航/过期回调、真实partial计数及总进度边界。保留Slice4的ADS内容/删除pending/取消、Unicode/长路径和非覆盖检查。完整Debug/Release串行构建和普通检查通过后更新源码/产物摘要与人工清单，不启动产品窗口或真实服务验收。

后续独立阶段：目录递归及自身/后代目标/循环保护、同跨卷Move/Cut、Drag & Drop、回收站及确认永久删除、Phase1E整理。SFTP/WebDAV/远端Provider继续按冻结路线推进，不由单文件或多文件复制通过而提前宣称可用。

## 本轮实现约束

- Clipboard批量路径上限1024、payload4MiB，完整Unicode双NUL列表；全部输入字符串与元数据先通过再交付请求，任一失败清空整批。内部匹配保存实际发布的有序路径及sequence，不能因extended DOS规范化丢失原source身份。
- Copy(single)/CopyBatch共用窗口coordinator唯一Submit/native action，队列串行调用原CopyFileSafely。首写前整批结构/type/identity/名称/目标父校验；捕获后的真实对象变化仍由各项native重验，批次不承诺事务回滚。
- Snapshot保留每项request/result、当前索引、copied/skipped/failed/cancelled/remaining及partial报告计数。失败停当前项、保留tail；显式Resume新operation id，实际action退出前拒绝，旧opid/item progress不影响新状态。Retry历史保留实际partial路径，不将旧残留伪装清理。
- 只有文件冲突自动显示Skip/KeepBoth/Cancel；其它失败可显式名称重试。已完成项不重做，未开始队列不可被新mutation无声覆盖，可明确Cancel。普通运行中导航不重定向目标；导航关闭冲突选择时取消pending continuation。
- 当前文件字节不作为全批字节或100%。状态栏分别显示批次数量和当前文件，late cancel不推翻末项真实提交；准备阶段也可合作取消，并在UI提交复制前复查。
- 完整Debug/Release构建与各798项普通检查通过；最终结论见PROGRESS及`.build/phase1d-slice5/integration-20261015-1932/native-validation.json`。人工清单：[Slice5](PHASE1D_SLICE5_MANUAL_UI_CHECKLIST.md)，全部待执行。
