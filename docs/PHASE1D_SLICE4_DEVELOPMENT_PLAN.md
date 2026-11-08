# Phase1D Slice4 — 完善文件复制兼容与冲突策略

状态（2026-10-15）：Skip/KeepBoth/Cancel、独立冲突弹窗/Skipped结果及默认+命名$DATA流复制已实现，完整Debug/Release构建与各724项普通检查PASS、0 FAIL、exit0；各2项file-symlink权限1314明确SKIP。人工验收继续暂缓，历史RED和Slice3证据保留。Replace独立审计未通过，保持显式Unsupported/禁用。

## 继续沿用的边界

FileOperationCoordinator保持窗口单worker、owned native生命周期、真实committed/partial、immutable进度及目的父identity刷新。ClipboardFileTransfer继续只是标准Windows文件数据输入输出，不产生第二个copy engine。普通用户权限；不加服务mutation、提权或Shell Namespace。

## 下一阶段顺序

1. 先审计并测试普通文件复制的metadata限制。下载文件可能有Zone.Identifier等附加数据流，不能静默丢弃安全元数据或绕过系统提示；Slice3明确Unsupported，本刀扩展到所有被支持的$DATA streams，保持同handle读取、原子占名和owned cleanup，保留旧source。EA/ACL/特殊属性支持范围逐项记录，不宣称完整备份。
2. 增加显式File Copy冲突模型与UI，按冻结计划提供Replace/Skip/KeepBoth/Cancel。KeepBoth采用确定性序号、保留extension、Unicode/长路径边界，最终仍用CREATE_NEW解决名字竞态，有限重试。Skip与Cancel不能显示copied或写目标；命名/存储错误继续可见。
3. Replace必须在隔离owned fixture证明目标身份绑定、提交期间占用、reparse/硬链接、并发替换、取消及权限失败的数据保留语义；先研究适用native API再选实现，不能以exists→overwrite或check identity→path truncate充当原子保护，不能在失败后偷偷切换API。不能可靠证明时保持不开放Replace，并明确门禁未通过。
4. 新增独立模型与native fixtures，串行完整Debug/Release及普通回归，补人工清单；保留所有跳过项/失败日志和冻结契约摘要。根据实测决定下一刀多文件队列与递归目录复制。

## 本刀之后

目录递归与reparse/循环/partial策略、同/跨卷Move、Cut/DropEffect确认、Drag & Drop、回收站Delete/确认永久删除、Phase1E收口和Provider/远端能力仍是独立阶段，不能由单文件复制通过而标记完成。

## 当前实现与门禁

- `FileCopyConflictPolicy`区分默认Fail与显式Skip/KeepBoth/Cancel/Replace。Skip是跳过本次文件，零文件I/O/零progress，即使原目标已经消失也不重新复制；Cancel直接1223。Skip保存独立终态，不能伪造committed或100%。KeepBoth最多1000次CREATE_NEW，只有明确选择后才给leaf增加(2)、(3)等确定序号，保留扩展名并避免截断UTF16高代理项。
- source default handle枚举所有$DATA streams，最多256个/1MiB枚举信息；逐流重新打开核验同file identity、size、tag，并持有读保护直到提交。目标默认流CREATE_NEW且exclusive，ADS同样CREATE_NEW，分享DELETE以兼容本操作default handle，但不分享读写。取消/异常先关owned ADS，再用default owned handle disposition清理，绝不按名字删除。
- source/target最后再枚举流，拒绝漏流/变更；总进度包含所有流。各目标写句柄关闭前的自动访问/写入时间更新被抑制，Flush及basic metadata完成后才committed与最后100%。ACL仍继承目标父目录；EA/security/object ID等完整备份和特殊存储仍未实现，不宣称原子metadata快照。
- 最后逐个核验owned目标流的identity、size、type及DeletePending。Windows的共享状态按流维护，目标ADS为兼容本操作DELETE句柄必须分享DELETE，不能声称它阻止了单独删除流。真实回归发现删除中的流仍可出现在FileStreamInfo；增加逐流FileStandardInfo核验，发现后清理整个owned副本并失败，不能报告copied/最终100%。源流继续deny-write/delete，目标流继续deny-write；无竞争时晚取消仍保留真实成功。
- 自有ContentDialog以独立model拥有一次choice，默认取消；Replace禁用有说明。UI只在对应native IAsyncAction实际结束后开放冲突选择，await不阻塞UI；窗口关闭/导航失效continuation。失败/取消后仍可用名称重试。重试按同destination identity显式绑定当前已打开路径，避免目录改名后继续用旧目标path。

## Replace普通原生探测结果

证据：`.build/phase1d-slice4/replace-probe/run-20261015-101841-ff91e85f116c400f9d18ee0aa4c9e649/state.json`、同级`assessment.json`和`README.md`。compiler/native exit0且owned fixture已清理。35条为观测记录，其中33实际执行、2条文件symlink因普通用户1314明确SKIP，不把成功rename或跳过当作安全通过。

metadata-only/no-share-delete和read/share-delete guard的POSIX替换允许竞争者换入新目标，held target ID仍等于旧ID；随后的“ID复核+rename”仍成功覆盖rival。真正带read且deny-delete的guard能阻止竞争者，却也让自己的Replace commit返回32。没有得到满足expected-target identity及失败保留旧目标的可接方案；`FILE_RENAME_INFO`没有expected-target-ID字段，不能以旧handle ID未变证明目录项未变。ReplaceFileW文档的1176无backup分支也不满足失败时保留旧目标，不改用路径truncate或静默fallback。

依据：[File Streams](https://learn.microsoft.com/en-us/windows/win32/fileio/file-streams)、[FileStreamInfo](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_stream_info)、[SetFileTime](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-setfiletime)、[FILE_RENAME_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info)、[ReplaceFileW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew)。

普通native验证结论仍见PROGRESS；本刀不标Phase1D或整项目完成。人工清单见`PHASE1D_SLICE4_MANUAL_UI_CHECKLIST.md`。

## CreateFile3兼容诊断

首轮Debug普通检查714 PASS/9 FAIL，修复参数组合后721 PASS/2 FAIL，随后定位长路径测试fixture与上述目标ADS删除缺陷。全部RED日志保留，不计作最终通过。

隔离probe的V1/V2保持独立源码及hash：ADS为1355/488条观测，目录为127/61条观测；各compiler/native exit0、保留oracle及自有fixture清理通过。这些是诊断观测数，不合并到回归PASS。V2在固定access/flags后只变share，普通文件/目录/junction/ADS的READ_ATTRIBUTES及GENERIC_READ均表现为：DISALLOW配share0/1/3成功，配share4/5/7返回160；移除DISALLOW的对照成功。结论仅适用于实测环境，不能归因为metadata-only或推广为平台契约。

最终固定参数：源默认/ADS保留DISALLOW及share READ，目标默认保留DISALLOW、DELETE及share0；目标ADS固定OPEN_REPARSE_POINT/share DELETE/CREATE_NEW；exists-only探测固定OPEN_REPARSE|BACKUP、READ_ATTRIBUTES/share RWD。后两者依赖已持有的非reparse父链、独占默认目标和流identity核验，不做失败后动态降级。目录原CREATE_NEW返回5时仅在exists-only明确看见叶子后才归一化80。证据见`.build/phase1d-slice4/{ads-probe,directory-probe}/assessment.json`；[CreateFile3扩展参数](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/ns-fileapi-createfile3_extended_parameters)描述各flag，组合限制以本机矩阵为证。

最终证据：`.build/phase1d-slice4/integration-20261015-1813/native-validation.json`，保存两配置完整build/tests状态、当前源码/产物SHA-256、冻结契约核对及工作区审计。下一刀见[多文件复制队列](PHASE1D_SLICE5_DEVELOPMENT_PLAN.md)。
