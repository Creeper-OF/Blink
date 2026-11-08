# Phase1D Slice11 — 混合复制队列恢复清单

状态（2026-10-27）：完整Debug/Release构建及各983 PASS、0 FAIL、4权限SKIP；248资源/167输入与冻结合同核对通过，人工UI/实际剪贴板/真实服务继续暂缓，Phase1D和整项目未完成。

多目录及文件/目录混合选择已接入Ctrl+C/右键复制和粘贴，仍为标准Unicode CF_HDROP/COPY。旧文件列表和单目录variant保留，多目录或混合使用CopyItemRequests（有序variant），保留重复项和原始身份。整包metadata捕获成功后才提交；目录子树执行时再有界预检。内部缓存匹配实际发布有序payload及sequence，不能把同名新对象当原来源。

唯一FileOperationCoordinator::CopyItems扩展现有队列，逐项调用同一文件/目录engine，唯一native action。CopyBatch文件专用入口继续拒绝目录。mixed顶层kind为Copy，当前目录有独立request/progress/result及每项limits；公共request只投影公共字段，类型和attributes必须一致。目录字节是当前文件字节，counts按目录独立；旧operation/item回调不能推进下一项。

根冲突只在零effects同名时自动打开目录dialog，等待真实action退出，仍检查origin generation和operation ID。Skip/KeepBoth/Cancel或手动名称通过ResumeCopyBatch继续尾项，已提交前缀不重做。partial后仅显式重试，不接管旧树；每项最早32份partial结果、65536字符及所有partial次数保留，UI显示路径/数量并刷新原目的身份。tree partial不等于file partial。

## 验证与证据

- integration目录：`.build/phase1d-slice11/integration-20261027-1500/`，入口session.json、progress.md、native-validation.json；run-build-cached.ps1/run-tests.ps1按配置运行，compiler1。
- 34项新增检查及4项旧限制转正；完整RED945/4/4、950/24/4、982/1/4保留。最后RED证明zero directory limit应在整队列写入前拒绝，不仅在后项engine才拒绝。修复后两配置983/0/4。
- 真实owned fixture验证序列/重复项/ADS/空树、占名与源身份、取消partial、晚取消、Skip/KeepBoth续尾、来源替换、旧item回调、Shutdown、实际action完成和40次partial历史。异常executor创建真实新root后抛出，独立树事实和实际路径不丢。
- 167源码输入、10产物、两PRI摘要及248资源对齐；六个旧EXE摘要相同，复用Slice10链上的历史普通证据。S8重定向探针仍是历史证据，未在Slice11重跑；4权限跳过不算no-follow通过。
- 提交后的摘要复制改为primitive事实保底，避免内存分配异常反转commit；没有故障注入实际allocator耗尽，不宣称已覆盖所有低内存路径。
- native进程/临时别名0，附录B6/1/17/0既有资产不变，OOM0；主模型完成代码审查，未启用子代理。
- [人工清单](PHASE1D_SLICE11_MANUAL_UI_CHECKLIST.md)全部待执行，更早人工门禁仍独立保留。

## 后续

目录Merge、安全Replace、同卷/跨卷Move与Cut、拖放、删除/回收站、完整metadata和远端Provider仍待做。本轮接着研究同卷Move的源句柄与目标父句柄绑定，跨卷Copy→Delete不得在来源变化后误删原资料。任何新功能使用新的证据目录，当前983源码快照作为已验证恢复点；禁止自动reset/clean/覆盖未提交工作。

用户新窗口从2026-10-27 14:32:37至16:32:37北京时间，主模型执行、compiler1、native串行。恢复先读该轮state与实际任务状态，中断不重复启动。
