# Phase 1B Slice 6 隐藏项目验收

当前状态：Debug/Release 构建、各 349 项普通检查、各 26 项生产隐藏项目 UI 检查 **PASS**；Release 原视图 35 项及窗口消息滚轮 15 项回归 **PASS**。测试窗口正常退出 0、无强制终止。人工验收 **PENDING**。

本 Slice 添加窗口级“显示隐藏项目”开关。自动化和人工验收各自记录实际结果；人工项不由自动化预勾。当前构建/检查结果见 `PROGRESS.md` 和 `.build/phase1b-slice6`。

使用当前源码构建的 `src\Blink\x64\Release\Blink.exe` 和脚本生成的专用 fixture，无需管理员权限或 IndexService。

- [ ] 默认文件区和导航树不显示 Hidden/System 项目；开启后显示 Hidden 文件与目录，System 和 Hidden+System 项目仍隐藏。
- [ ] 在详细信息、列表、网格视图中切换开关，条目数及空目录状态正确，当前目录、视图和排序保持。
- [ ] 选择两个硬链接及一个隐藏项目后关闭开关，两个可见硬链接仍分别选中；隐藏项目不再选中，再开启不自动恢复它的选择。
- [ ] Ctrl+A 只选择当前策略允许的项目；隐藏行不能通过旧选择或旧焦点触发打开。
- [ ] 在两个标签中切换开关，已有标签共同更新；新增标签继承当前窗口策略。
- [ ] 编辑但未提交地址栏时切换开关，草稿保持；Back/Forward 历史不增加。
- [ ] 当前目录只有隐藏文件时，关闭开关显示空目录，开启后显示文件，不需手动刷新。
- [ ] 连续切换、浏览/树加载期间切换及关闭窗口，无旧行回填、崩溃或残留工作进程。
- [ ] 开关变更后导航树按新策略重新惰性加载，并定位当前目录；其它手动展开状态可能重置。
- [ ] 在现有 10k/50k 可控 fixture 中观察开关后的响应、滚动和 resize，记录残余停顿，不预填性能结论。

自动化入口：

```powershell
.\scripts\run-blink-visibility-smoke.ps1 -Configuration Release
```

默认 fixture 含 600 个普通文件、真实硬链接以及 Hidden/System 属性文件和目录，覆盖渐进呈现。脚本只控制自己启动的 Blink PID/HWND，通过 UI Automation 与该窗口的原生输入站点发送按键，不向其它应用发送全局键盘。报告与 fixture 保存在 `.build\phase1b-slice6\ui-<runid>`；正常关闭与退出码也纳入结果。此规模不替代 10k/50k 人工响应性检查。

人工记录需包含时间、配置/二进制标识、fixture、实际覆盖项和遗留问题。Slice 4/5 门禁保持独立，本 Slice 不代表完成 Phase 1B。
