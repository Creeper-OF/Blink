# Blink Phase 0 项目计划

当前执行状态（2026-11-08）：位置跟随与高级搜索已完成：此电脑搜所有本地盘，目录内默认搜所在整盘；支持指定多盘、当前/指定目录及子目录开关，设置按标签独立、按搜索/回车提交。Debug/Release各1416 PASS/0 FAIL/4权限SKIP，新增34项与4项Phase05检查；359资源/220输入一致，真实C/D范围和目录深度、19项自有窗口检查通过，默认Release已更新。人工体验仍暂缓，完成本任务后停止，项目其余功能待续。

本仓库按附件《Blink Phase 0：核心架构与技术路线验证》执行。Phase 0 的目标是验证核心架构、Windows API 选择、索引/属性/传输原型和 Provider 隔离，不追求完整文件管理器产品。

## Phase 0 范围

- WinUI 3 + C++/WinRT 最小可运行壳子
- 本地目录浏览与后台枚举
- File ID + Volume ID 稳定身份验证
- USN Journal 查询/读取 Prototype 与内存索引 Prototype
- ReadDirectoryChangesExW 目录级变化观察 Prototype
- Windows Property System、文件系统属性与只读 ACL 查看
- CopyFile2 复制、进度、取消验证
- Blink.ProviderHost.exe + FakeProvider 进程隔离与故障模式验证
- 每次运行维护 API 决策、进度、想法、架构和 Phase 0 报告

Phase 0 完成后停止扩展功能，等待用户验收；不自动进入 Phase 1。

## 明确不在本阶段

完整 Explorer UI、缩略图、Preview Handler、SMB/WebDAV/Cloud Provider、插件 API、完整设置页和正式视觉设计均不在本阶段。

## Phase 0.5 scope

- Separate normal-user Blink.exe from Blink.IndexService through Blink.Index.Contracts.
- Use privileged MFT enumeration, USN catch-up, parent-linked identity/path storage, and explicit rebuild states.
- Keep the service surface read-only and limited to status, index, progress, cancellation, and search; UAC is installation-only.
- Keep the BLK5 checkpoint and service data under the project `.blink-data` root on D:.

Phase 0.5 stops before Phase 1 UI/product features. Missing service, unavailable USN, and recovery states remain visible and may use the Phase 0 DFS fallback.

## 核心架构后续实现阶段划分 (Roadmap Phases)

按照 D-0021 / D-0022 架构规划，后续存储与索引实现划分为以下阶段：

### Phase 0.5 (已完成验收)
- 验证单 Volume 的 MFT 扫描、USN 捕获、CatchUp 逻辑、持久化正确性与写盘性能。
- 保证测试集及开发阶段数据隔离与自索引基类支持。

### Phase 1 (核心文件管理器阶段)
- 本地文件管理器基本 UI 与功能可用。
- 逐步将 IndexStore 抽象调整为按卷隔离的基础能力。

### Phase 1.x Storage Architecture (存储架构落地阶段)
- 实现 `DataRoot` 解析器与 `Install-volume-following` 策略。
- 物理文件按卷隔离存储 (`Index\Volumes\Volume-{GUID}\`)。
- 接入 `InternalExcludedRoots` 机制，彻底阻断自索引无限写盘。
- 实现单卷级重建 (`Rebuild Volume Index`) 与一致性低频 Scrubbing (`Index Consistency Scrub`)。
- 提供磁盘占用统计 UI（Index、Cache、Logs、Dumps 可视化）。

### Installer Phase (安装与系统恢复阶段)
- 实现安装器默认 DataRoot 判定（默认跟随安装卷）。
- 实现系统重装后的服务修复工具 (`Repair Installation`)，恢复 SCM 注册并重用旧数据卷索引。
- 提供 DataRoot 迁移工具 (`Move Blink Data`) 与卸载选择（保留或删除数据）。
