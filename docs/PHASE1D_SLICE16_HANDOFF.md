# Slice16 删除与系统回收站恢复清单

状态（2026-11-05）：已接入并完成完整Debug/Release构建与各1240 PASS、0 FAIL、4权限1314 SKIP，新增49项删除检查，194冻结输入/291资源/冻结合同/六旧EXE核对通过。人工UI、真实剪贴板和服务验收继续暂缓，整体项目active，主模型/compiler1，无子代理。遵守[实用文件管理范围](PRODUCT_SCOPE.md)，不扩展额外防护平台。

## 当前行为

Delete和右键删除进入Windows系统回收站；Shift+Delete/永久删除菜单先单次确认，默认取消。捕获原行名称、父/源身份与页面上下文后再执行，不把确认时的新选择当成旧请求。永久模式在队列和后端核对确认事实，普通回收不重复确认；确认构造异常、原页面过期分别反馈。

文件、目录、多选顺序队列沿用FileOperationCoordinator，owned action等待一个临时STA线程执行IFileOperation，监听Stop prerequisites先排空，不阻塞UI。普通回收显式RECYCLEONDELETE，非回收PreDelete否决；没有应用内永久fallback、自定义垃圾目录、事务/防护服务或提权文件操作通道。真实无法回收卷当前未验证，此处不承诺所有文件系统/配置的行为。

实际删除根项、回收/永久数量、跳过、失败、取消、未开始尾项与警告分别显示；目录中途失败保留可能部分删除，不误用普通文件partial字段。失败后重试原剩余项或跳过继续尾项，已完成前缀不重做；取消不承诺回滚。最后一次真实完成不会因晚取消或observer异常反转。

已完整删除的文件夹，其打开根/子目录标签和历史中的失效位置退回源父目录，重新枚举取得新身份，保留视图/排序而不误选父目录同ID硬链接。正在去其它位置的用户导航保持，旧请求结果不能覆盖新页。根仍存在的部分删除保留当前位置并刷新。监听暂停/恢复和归属沿用Slice15路径。

## 验证事实

- 普通用户真实fixture涵盖文件和非空目录回收/恢复、ADS、永久删除及未确认拒绝、只读、硬链接只删除所选名称、junction目标内容保留、长中文路径、占用失败、busy子项partial后retry、子项取消、真实后台多项回收、已监听根/子目录删除后父目录重新枚举和监听。确定性executor用于队列和关闭/旧回调检查，不能代替真实文件删除。
- 首轮Debug1234 PASS/4 FAIL/4权限SKIP在attempts/first-debug-1234-4保留。非空目录永久删除的根PostDelete实际先返回COPYENGINE_S_NOT_HANDLED，子项随后处理；现只有Perform结束后核对原根链接消失才记根完成，不把提前回调当作移除。另修正测试对第一STA为MAINSTA的假设，产品原已支持两者。新加空目录完成及子项途中取消两项，最终49项新增，原1191项继续通过。
- 独立recycle-probe四项确认及delete-callback-probe顺序观察均不计普通PASS。前者的非回收标志veto不是“真实无回收站卷”验收。只按本次返回的Shell项恢复/清理自有样本，没有枚举或清空用户回收站；callback初次stderr处理异常及初次standalone编译include错误保留。
- 完整Debug/Release各自构建exit0后再串行普通测试，各1240/0/4。四项symlink权限1314明确跳过。194输入、291 en/zh/fallback键/工程引用、冻结三文件、10 EXE/两PRI已核对；六旧EXE与Slice15摘要相同，沿用既有普通证据，没有重跑旧suite。Transfer空入口不当作复制行为验收。
- native进程/临时ASCII别名0，自有C/D PID样本目录0，WSL swap0/OOM0；附录B历史6根散落文件/1 project散落/17其它项目产物目录/0缺README未增，未清理其它项目资产。工具链仍在D:\DEV\C++，未另装工具。

证据在`.build/phase1d-slice16/integration-20261105-2116/`：native-validation.json、input-sha256.json、expected-checks.json、build/test状态与日志、change-scope.json、slice16-delete.patch、review.md、初始失败和工作区审计。源码/文档/证据快照见source-snapshot.json和`.build/phase1d-slice16/snapshots/`，索引#4同步v4.25。恢复先核对输入/产物摘要和在途状态，不reset/clean、不盲目重启，不自动解压覆盖未提交代码。

## 后续

人工[删除清单](PHASE1D_SLICE16_MANUAL_UI_CHECKLIST.md)继续暂缓；真实无回收站卷、磁盘故障、UI/clipboard/真实IndexService与管理员综合验收未执行。下一步推进属性查看、拖放、同名Merge/Replace及外部performed-effect/剪切外观反馈，随后继续SFTP/自建网盘Provider；独立RaiDrive式挂载工具不在主线。完整metadata、无限目录树和整体回滚没有因此成为本轮承诺。用户未新设停止时间，整体目标仍active，本模块通过不等于整项目完成。

API参考：[PostDeleteItem](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperationprogresssink-postdeleteitem)、[SetOperationFlags](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ifileoperation-setoperationflags)。NOT_HANDLED的本机回调顺序来自自有实际探针，不当作跨平台API保证。
