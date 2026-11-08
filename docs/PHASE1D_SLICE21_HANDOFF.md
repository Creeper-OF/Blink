# Slice21 目录移动合并 — 实现与验证

本轮在Slice20目录复制合并和已有同/跨盘Move之上，复用原目录计划、文件操作及唯一coordinator。UI外观由用户另行准备提示词，本轮只接功能、冲突选择和真实状态。

## 行为

- 同名目标根及已有普通子目录合并，保留目标目录身份和独有项；内部文件仍逐项Replace/Skip/KeepBoth/Cancel，类型不匹配不提供不可执行选项。
- 同卷叶文件使用原生移动，保留文件身份与数据流；跨卷复用复制后移除来源。仅源目录确实移空后删除该目录。
- Skip保留来源文件或整棵跳过子树，非空祖先保留。正常遍历含Skip时不宣称整个来源根已移动，也不把正常跳过当失败；计数、目标变化和真实来源移除分别报告。
- 取消或失败保留已完成前缀和剩余来源，部分完成项只允许Skip或Cancel续尾，不整树重放。原worker用operation/conflict/item ID等待用户，关闭、取消或UI发布失败会唤醒。
- 每个实际移空的源目录记录原路径/身份与真实目标路径，按最深路径优先更新标签和历史；KeepBoth子目录使用实际新名称，跳过留源目录保持原位置。
- 根冲突的目标类型在worker读取；同文件才Replace、同目录才Merge，UI线程不读文件元数据。

## 验证与交付

235构建输入、368中英/fallback资源键一致；最终Debug和Release完整构建通过，各1516 PASS / 0 FAIL / 4符号链接权限1314 SKIP，新增54项目录MoveMerge/类型检查及5项标签跟随全部通过。

自有生产窗口8 PASS / 0 FAIL / 0 SKIP，实际执行剪切、粘贴、根Merge与子项Replace/Skip，核对目标字节、来源保留、目标独有项与真实状态；完整合并则确认来源根与空目录移除。标签跟随本轮仅由native回归验证，不声称已做该组合GUI或人工体验验收。测试窗口正常退出0，fixture清理并恢复仍属于本轮的剪贴板文本。

默认Release发布5个变化文件，核对Blink.exe、PRI、ShellMenuHost及IndexService哈希，新默认窗口PID38136可见并留给用户。helper及索引服务输出与上次菜单同步相同；未启停服务、改协议或重建C/D数据库。本轮只接功能，UI外观等用户另行提供提示词。

本轮产物归入`.build/phase1d-slice21`，最终日志、235输入摘要、发布与分类清理记录位于integration。构建和测试使用独立PowerShell进程，单并发，保持跨盘fixture的真实第二卷环境。当前程序、有效依赖、健康索引库及最近两个成功源码恢复点保留；具体清理范围和释放空间见cleanup-runtime、snapshot-retention及recovery-cleanup记录。

## 排障证据

首次GUI脚本误用生产程序不读取的BLINK_INITIAL_PATH，停在ThisPC而未发生剪切；已改为地址栏明确提交并核对唯一marker，随后8项全部通过。失败窗口正常退出0、626字节fixture已清理，报告单独保留，产品输入未变。

排障记录保留：首次及诊断轮1515/1/4，定位新建目标目录只读共享与同盘叶原生移动冲突（32）；直接向CreateDirectory2W加入WRITE共享的尝试为1463/3/4并因fixture后续元数据不存在而提前结束，真实错误87。项目API_SELECTION.md:196已有该API本机参数限制，本轮已补读，不把未支持创建形状当通用方案。最终恢复固定创建方式，旧LIST句柄仍持有时先打开RW-sharing只读LIST，核对同一File ID后再交换释放旧LIST。metadata-only句柄不被当成独立命名guard。全部源数据变更边界和原断言保留，未通过放宽测试达成。
