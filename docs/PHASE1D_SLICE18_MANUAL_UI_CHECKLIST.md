# Slice18 文件拖放人工验收（暂缓）

2026-11-06：按用户此前要求，当前不执行人工验收。以下是后续恢复验收时的操作清单，未勾选不表示失败，也不能作为已通过证据。使用普通权限、自有可丢弃文件夹和Release版，不操作用户实际文件。

1. 详细信息、列表和网格中拖动单个文件、多选40个文件、中文长路径、目录及混合选择，确认来源与目标项完整。
2. 从Explorer拖入Blink文件区空白、文件夹行、侧栏目录和This PC驱动器；同盘默认移动、跨盘默认复制，Ctrl复制、Shift移动；Alt及Ctrl+Shift不执行未支持的链接操作。
3. Blink文件区拖到另一标签；悬停约500毫秒切换标签，投放位置与显示提示一致；目标为子文件夹或侧栏时不强制跳转页面。
4. Blink拖出到Explorer，同盘和跨盘分别验证复制/移动及来源自动更新。取消拖动不得删除来源；完成反馈不得引起来源二次删除。
5. 同目录默认移动不执行；Ctrl复制在同名冲突中显式选择保留两者，现有文件内容不变。
6. 文件/目录同名时验证跳过、保留两者、取消和新名称重试；来源异步状态随真实队列及重试完成结束。合并和覆盖替换仍未开放。
7. 较大目录传输中取消、关闭窗口或切换标签，确认已完成前缀与真实部分结果有反馈；无崩溃、UI冻结或残留拖动光标。
8. 连续开始两次不同拖动，快速越过无效位置并返回、移出窗口、关闭目标标签，确认旧预览或回调不提交旧来源。
9. 传输准备期间更改目录，确认没有将旧投放准备应用到新页面；操作进行中的进度、取消及后续正常文件命令可用。
10. 从不提供异步COM能力的文件来源投放，检查Drop deferral期间实际完成/冲突窗口/取消交互。原生非窗口检查无法证明这一UI时序。

API依据：[WinUI拖放](https://learn.microsoft.com/en-us/windows/apps/design/input/drag-and-drop)、[IDataObjectProvider](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nn-shobjidl_core-idataobjectprovider)、[Shell异步与optimized move](https://learn.microsoft.com/en-us/windows/win32/shell/datascenarios)。产品使用标准完整CF_HDROP；已留存的40-vs16窗口外观察只说明本机API行为。
