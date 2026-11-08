# Slice18 文件拖放续接记录

2026-11-06用户恢复后完成本轮拖放开发与双配置原生验证。**Debug和Release完整构建均exit0；各1320 PASS、0 FAIL、4权限1314 SKIP。真实鼠标/Explorer和WinUI交互仍暂缓，整项目未完成。** 主模型执行，compiler1，无子代理。

## 当前功能与边界

- 文件区空白、目录行、This PC位置/驱动器、侧栏目录及标签目标接入同一控制器。列表/网格多选发布原始行身份；目标与所属页面独立，子目录投放不跳转页面，冲突/改名继续使用原目标；标签悬停500ms激活已编译。
- 标准IDataObject/CF_HDROP与DataPackage IDataObjectProvider保留完整多选，来源准备和目标元数据在worker完成。普通检查真实传输40项中文路径并验证跨apartment读取；高层GetStorageItemsAsync在旧探针的40项fixture只返回16，因此不用它读取文件拖放。
- 同卷默认Move，异卷默认Copy，Ctrl复制、Shift移动；Alt或Ctrl+Shift链接未开放。无效目标、同目录默认Move、目录自身/后代不提交；Ctrl同目录Copy仍交现有同名处理。
- External→Blink和Blink→Blink实际传输由唯一FileOperationCoordinator拥有，复用已实现的Copy/Move、Skip/KeepBoth/Cancel和改名重试；Blink→External只发布路径和反馈，由接收方执行文件操作。来源发布与反馈没有文件删除动作。
- 接收方Move已完全执行，因此通知performed=None/logical=Move。异步来源Begin幂等，真正队列终态结束；可重试冲突保持。非异步来源持Drop deferral，真实UI时序仍待人工验收。内部刷新仍由操作发布负责，外部逻辑Move触发来源刷新。

## 验证与证据

- 本轮目录：`.build/phase1d-slice18/integration-resume-20261106-083946/`。`native-validation.json`汇总最终构建/测试、209输入、338资源、冻结三文件、10 EXE/两PRI摘要及审计；`source-snapshot.json`给出源码/文档/证据包位置和SHA。Release启动：`src/Blink/x64/Release/Blink.exe`。
- 新增44项普通检查：真实COM数据对象、标准40项/独立HGLOBAL、DataPackage包与当前运行时视图互操作、内部原始身份、跨apartment准备、同异卷效果/修饰键、实际混合复制和外部移动、async反馈/取消、冲突终态策略及所属页面/导航失效。原有1276项继续通过，各4个file-symlink权限1314跳过未冒充成功。
- 原有六个Phase0/Phase05/Transfer二进制与Slice17一致，明确沿用历史普通证据，未在本轮重跑。旧独立40-vs16 API观察贡献0普通PASS。未启动产品窗口、全局clipboard写入、真实服务或管理员验收。
- `attempts/attempt1-compile`与`attempt2-compile`保留编译失败、原输入和相关源文件；`initial-failures-review.json`说明修复。第二次失败构建后的测试诊断1320通过不算完整产品验证；之后完整成功Debug/Release分别结束才运行最终suite。

## 后续开发

1. 继续本地文件管理主线：目录Merge和文件Replace明确同名策略，复用现有队列和部分完成反馈；不增加事务文件系统、oplock或额外防护平台。
2. clipboard外部performed-effect、剪切外观及剩余Phase1稳定化仍待做；SFTP/自建网盘按既有后续Provider规划，独立RaiDrive工具不纳入当前主线。
3. 人工验收仍暂缓，见[清单](PHASE1D_SLICE18_MANUAL_UI_CHECKLIST.md)。重点待验：Explorer双向拖放、悬停标签、非异步来源的Drop deferral和同名对话框、取消/关闭、已打开源子目录监听对向外Move的占用。普通原生检查不证明这些UI时序已通过。
4. 恢复工作先核对本轮`input-sha256.json`与用户后续修改，沿用主模型/compiler1，不用旧207输入草稿覆盖当前已修正源码。

## 历史暂停证据

- 暂停时稳定Slice17：`.build/phase1d-slice17/snapshots/slice17-properties-verified-source-docs-evidence-20261105-231316.zip`，SHA256 `70e41405abed8b41f3ebcf99b35212b844cf98b1e6a595c0ac477eb827e6430f`。
- 暂停时未验证草稿：`.build/phase1d-slice18/snapshots/slice18-drag-drop-unverified-wip-20261106-003427.zip`，SHA256 `6f26941cd763395f3a1080269c206545889b4561bd072dac34f10e850de075a1`。原8项隔离副本/patch与pause记录未覆盖；本轮确认201输入未变后只恢复8项，并在其上修复与补齐。
- 工作区索引`D:\Work\README.md`附录A.1 #4更新v4.28。
