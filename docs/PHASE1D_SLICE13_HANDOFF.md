# Slice13 多项同卷移动与剪切粘贴恢复清单

状态（2026-10-28）：**IMPLEMENTED / DEBUG AND RELEASE FULL BUILD AND ORDINARY TEST PASS / MANUAL UI AND REAL CLIPBOARD DEFERRED**。完整两配置各1110 PASS、0 FAIL、4权限1314 SKIP，182输入与268资源核对通过。Phase1D和整项目未完成。

## 已实现行为

文件与目录混合MoveEntries有序执行，每项仍为Slice12同卷same-handle rename，不递归copy/delete。整包最多1024项/请求路径合计4MiB，统一目标、来源/父身份、类型/属性、名称、策略及已知同卷预检先于写入；每项执行时再次检查实际来源与父链。重复项/父子重叠不去重、不改写来源路径；已移动后再次引用旧路径明确失败，由用户显式跳过再续尾项。

MoveBatchSnapshot独立保留每项request/result与Pending/Running/Moved/Unchanged/Skipped/Failed/Cancelled、moved/unchanged/skipped/failed/cancelled/remaining/warning数量。唯一FileOperationCoordinator与owned native action保持，既有COPY尾项和MOVE尾项互相保护。ResumeMoveBatch按原operation ID和目标身份，显式Skip/KeepBoth/新名称只重试失败项，已提交前缀不重做；Replace和跨卷仍明确拒绝。

进度检查operation与item index、kind、stage、closed，Completed进度不替代结果；terminal publication不提前释放action。合作取消/Shutdown不逆转已移动事实，最后实际提交后的晚取消仍完成；post_commit_error保存在对应项，不被后续项成功清掉。全量NoOp提示仅在全部请求unchanged时显示，混合skip使用普通队列完成提示和真实计数。

Ctrl+X/右键剪切发布标准CF_HDROP与Preferred DropEffect MOVE，Ctrl+V/背景Paste读取COPY或MOVE并进入对应原协调器路径；剪切发布本身不删除任何来源。缺失DropEffect按COPY；Link/含糊/未知意图拒绝，不静默降为COPY。旧只读Copy兼容API仍拒绝MOVE。

Cache以原始有序payload、sequence、意图与source/parent IDs绑定。内部同名替换不能当新外部来源重新接受；MOVE序列一旦提交，通过现有队列恢复/取消，再次Paste不重抓已移动前缀。COPY可重复Paste。外部MOVE首次被Blink接受后也保存该缓存；Blink发布至外部程序时实际移动归外部目标所有。origin代数、准备取消和窗口/标签门禁复用既有路径。

移动有独立状态、错误、名称与同名冲突文案，复用已有对话框。只由真实moved生成来源/目标父identity刷新，同父去重；NoOp/Skip不伪报修改。用户当前打开的被移动目录自身地址跟随、external performed-effect反馈/剪切淡化仍留后续。

## 验证与恢复

- 入口`.build/phase1d-slice13/integration-20261028-1015/`：session.json、native-validation.json、input-sha256.json、change-scope.json、slice13.patch、review.md；源码归档/逐文件摘要在source-snapshot.json及`.build/phase1d-slice13/snapshots/`。
- 当前182项构建输入已冻结；最终完整Debug/Release各1110 PASS、0 FAIL、4项symlink权限1314 SKIP，严格构建后串行测试，native exit0。资源en-US/zh-CN/fallback共268键、工程引用、冻结三文件、10 EXE/两PRI摘要核对通过。
- 新增53项有效回归：真实混合file/directory/ADS/hardlink、冲突/KeepBoth/Skip续尾、重复/重叠、source身份变化、结构与known跨卷整包拒绝、取消/晚取消/Shutdown、旧item进度、warning和真实action退出；共享clipboard准备/意图/cache/能力/刷新事实与summary决策。原同卷backend的真实异卷拒绝和reparse保护回归继续执行，不把伪造volume ID预检当跨卷成功。
- 保留14项RED与15项RED的完整原生记录。前轮1108/0/4和Release构建标pre-review-attempt；最终两项summary检查加入后重新冻结输入、完整双配置构建与测试。catch-only FAIL诊断不计入有效PASS；没有覆盖旧失败日志。
- 六旧EXE摘要与目录刷新修复基线一致，沿用明确记录的历史Phase0/Phase05/Transfer普通证据；Transfer空入口不等于复制行为验证。当前主程序/Phase1普通测试为新构建。保留旧编译warning，无工具链改动。
- 最终native进程与临时ASCII别名0、WSL swap0/OOM0；附录B既有6根散落文件/1 project散落文件/17其它项目产物目录/0缺README，与本轮接手基线相同。主模型直接执行/复核，compiler1，无子代理。
- 人工UI/真实剪贴板/Explorer互操作/真实服务验收按用户要求暂缓，[人工清单](PHASE1D_SLICE13_MANUAL_UI_CHECKLIST.md)保持未执行。不因编译或纯模型/原生fixture通过而宣称真实界面已验收。

## 继续开发

先读[Slice14计划](PHASE1D_SLICE14_DEVELOPMENT_PLAN.md)：跨卷file copy后安全移除来源必须先证明版本/链接与父链保护，目标已创建但来源保留须独立表示，不能按路径直接删除。单文件通过后再审计目录与混合队列；当前跨卷仍Unsupported。Merge、安全Replace、DragDrop、Delete/RecycleBin、属性和远端Provider仍待做。

保留当前大量未提交/未跟踪代码，不reset/clean、自动回滚解压或提交。恢复先核对输入摘要、实际进程和状态文件，原生任务有session时恢复等待，不盲目重跑。用户本轮只要求继续，未新设截止时间；不恢复旧子代理授权或旧两小时窗口。
