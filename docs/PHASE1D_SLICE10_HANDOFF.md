# Phase1D Slice10 — 目录根冲突恢复清单

状态（2026-10-27）：Debug/Release完整构建及各949普通检查PASS、0 FAIL、4权限1314 SKIP。中断恢复已核对165项输入并补齐Release检查及验证归档。最终证据见PROGRESS和`.build/phase1d-slice10/integration-20261016-2150/native-validation.json`。人工UI/真实剪贴板/真实服务及高权限/MFT验收继续暂缓，Phase1D与整项目未完成。

DirectoryCopyRequest新增默认Fail的conflict_policy，沿用FileCopyConflictPolicy；DirectoryCopyResult新增独立skipped。Skip/Cancel是冲突决定后的无文件系统访问动作，不重新读取源或目标；Replace明确Unsupported，未知policy拒绝。只有请求确为Skip、结果无错误且无任何files/dirs/partial时，coordinator接受Skipped；非法“带effects的Skipped”保留数量和partial事实并报告失败。

KeepBoth最多1000候选，原名及完整目录名“ (2)”至“ (1000)”。不把点后部分当扩展名，组件截断保护UTF-16代理对；保留同一次held source plan及parent/source guards，不对每个候选重新枚举整树。原子新建root确认占名，不能以exists后写入接管竞争者。

更换根名之前，RebindRoot完整分配所有新目的目录路径，限制old+new路径峰值及更大progress/native scratch；不超过现有16MiB路径预算和native路径上限。根一旦创建，所有子项冲突、取消、权限/metadata/inventory错误都停止于该根，保留真实partial/count/path，绝不自动换名重做。旧Fail同源同名错误87保留；明确KeepBoth可复制到同父的兄弟名称，真正源自身/后代目的父仍拒绝。

目录专用冲突dialog复用一次性CopyConflictModel，默认取消，Merge/Replace不可用，即使误传enable_replace也不会对目录启用。实际action结束、origin generation及operation ID仍匹配才接收决定；旧页面仅允许必要的原目的刷新。已有partial/count不能自动进入根冲突对话。手动名称重试重置Fail，不将用户准确名称继续自动编号。

## 验证与证据

- 前置Slice9完整165输入摘要匹配，恢复包在`.build/phase1d-slice10/snapshots/slice9-verified-inputs-20261016-2150.zip`。
- 新增18项：15个native root-policy fixture及3个coordinator fixture，包括真实占名竞态、不同类型占名、完整内容/ADS/source oracle、同父与后代、长组件、取消/child rival后不换root、1000候选上限、Skip及非法Skip effects。
- 首轮长组件fixture没用扩展路径，在调用新feature前失败；日志完整保留。修正为扩展路径后保留所有内容/ADS/source断言，完整RED为935 PASS/14预期FAIL/4权限SKIP。实现后Debug/Release各949 PASS/0FAIL/4权限SKIP；Release构建上次已通过，本轮165输入匹配后补跑Release测试。两配置证据与最终恢复包均已保存。
- 248项中英文/fallback资源对齐。最终input/output SHA256、build/test状态和workspace/native审计在同一integration目录；不要覆盖旧日志或将RED回填通过。
- S8实际reparse重定向探针为历史能力证据，S10没有重跑；不能把它当本刀所有新竞态的实测证明。既有真实junction/no-follow普通检查继续执行，权限不足的4项symlink不当作通过。
- [人工清单](PHASE1D_SLICE10_MANUAL_UI_CHECKLIST.md)全部待执行，亦保留Slice9来源/导航/外部剪贴板人工门禁。无产品窗口、用户实际剪贴板或真实服务验收。

## 下一轮

继续目录Merge、多目录队列、安全Replace、Move/Cut、拖放、删除/回收站、完整metadata及后续远端Provider。Merge需要独立的已有目录身份/冲突/部分结果设计，不能把本刀root改名循环当成Merge或断点续传。

保持当前工作目录及未提交/未跟踪文件；主模型执行，compiler1、native串行，工具链D:\DEV\C++。先读取session及实际build/test状态，遇中断不重复启动。新的工作使用新证据目录与快照，不自动回滚旧包、不reset/clean。
