# Slice17 只读属性与目录大小统计恢复清单

状态（2026-11-05）：完整Debug/Release各自构建exit0后普通用户串行测试，各1276 PASS、0 FAIL、4权限1314 SKIP。新增36项，201冻结输入/329资源、冻结合同和六旧EXE核对通过。主模型/compiler1，无子代理，人工UI/真实剪贴板/服务验收暂缓，整体目标active。以[实用文件管理范围](PRODUCT_SCOPE.md)为主，不增加额外防护平台。

## 当前功能

右键属性和Alt+Enter打开Blink自有只读窗口，捕获已提交的原选择，显示名称、位置、类型、逻辑大小/精确字节、磁盘占用、创建/修改/访问本地时间和常见属性。时间独立精确到秒并使用动态时区转换，未知0时间显示不可用；多选不同类型/时间显示多个值，共有属性按所有已读根项的交集展示，不用首项冒充整体。最多1024项，超限有明确提示。

普通用户backend读取FileBasic/Standard/AttributeTag/IdInfo，核对原父/源身份，同名替换不被当成原选择；单项无有效身份只报该项不可用，继续其它项。未修改旧Properties原型、Core或Index/协议，不加载完整Shell元数据/ACL编辑，不写文件内容、ADS或属性。

文件夹和混合多选在后台复用Core枚举统计，包含空/隐藏子项。已知目录junction/链接不递归，硬链接按名称分别累计；稀疏/压缩文件查询FileCompressionInfo占用。大小不含额外数据流，不承诺原子快照。取消在枚举/项目之间检查，可能等待当前目录I/O完成；失败保留已统计数据、错误位置和不完整提示。

worker不持强UI引用，只持纯请求、取消标志、inbox和weak UI；150ms节流并合并到最多一个待发布提示。关闭窗口、主动导航、切/关标签和退出使旧Ui关闭并取消，晚回调不更新新窗口；自动目录刷新不直接关闭属性。写队列繁忙时可读取稳定页面的属性，导航/呈现忙时遵循原统一命令策略。以上UI接线已编译，实际交互仍待人工验收。

## 验证与过程记录

- 真实fixture覆盖当前元数据而非旧行大小、时间/占用和内容/ADS不变、嵌套/空目录、多选混合、hardlink计名、junction不计目标、稀疏实际占用、长中文路径、原父/源替换、缺失/占用partial、初始/途中取消、callback异常和后台无COM读取。稀疏占用与GetCompressedFileSizeW一致；实际SetFileTime到56秒后读取/显示保留秒，多选实际不同类型/时间显示多个值。
- 初次Debug1272 PASS/1 FAIL/4权限SKIP保留。占用子目录fixture补足FILE_LIST_DIRECTORY后，INFO实测opened1/error32/3 bytes/1 file/1 unavailable，保留原strict partial断言；未删除失败或改成skip。复核修正无source ID不整批丢弃其它项并新增回归，双配置1274/0/4保留pre-review。最终显示细节后重冻并完成两配置1276/0/4，原1240检查继续通过。
- `.build/phase1d-slice17/integration-20261105-2230/`包含native-validation.json、201输入SHA、expected-checks、完整build/test状态和日志、attempts、initial-failures-review.json、change-scope、slice17-properties.patch及review.md。36项新增不含独立探针；旧Delete/reparse/API观察明确作为历史证据沿用。
- 329 en/zh/fallback键和工程引用、冻结IPC三文件、10 EXE/两PRI已核对。六旧EXE与Slice16摘要相同，明确复用原普通证据，未重新运行旧suite。四项既有symlink权限1314跳过不计通过；Transfer空入口不当作文件复制行为。
- native进程/临时ASCII别名0，自有测试PID目录0，WSL swap0/OOM0。附录B已有6根散落文件/1 project散落/17其它项目产物目录/0缺README未增加，未清理其它项目资产。工具链仍在D:\DEV\C++，未安装新工具。

源码/文档/证据快照见source-snapshot.json和`.build/phase1d-slice17/snapshots/`，工作区索引#4同步v4.26。恢复先核对输入/产物摘要、真实进程与在途任务；不reset/clean，不盲目重跑，不解压覆盖未提交工作。此前过程文档中的“待构建/Release进行中”保留为历史，最终状态以native-validation为准。

## 继续推进

[人工清单](PHASE1D_SLICE17_MANUAL_UI_CHECKLIST.md)依用户要求暂缓。真实UI、Clipboard、IndexService/管理员、非NTFS/远端文件系统、大树压力、完整Shell metadata/ACL编辑和属性写入未验收；普通native回归不替代这些范围。

接着推进拖放、同名Merge/Replace、外部performed-effect/剪切外观反馈，并按既有计划推进后续SFTP/自建网盘Provider。独立RaiDrive式挂载工具不纳入主线。用户未指定新的停止时间，整体goal active，不将本模块通过当作整项目完成。

API参考：[FILE_COMPRESSION_INFO](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_compression_info)、[SystemTimeToTzSpecificLocalTimeEx](https://learn.microsoft.com/en-us/windows/win32/api/timezoneapi/nf-timezoneapi-systemtimetotzspecificlocaltimeex)。占用错误32、稀疏物理占用与秒级显示的具体结果来自自有实际普通测试。
