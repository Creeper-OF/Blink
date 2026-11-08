# Blink Phase 1A Manual UI Checklist

本清单用于普通用户权限的人工 UI smoke。不会启动 IndexService，也不执行 elevated acceptance。
资源语言应跟随系统设置；本清单只记录行为，不把资源加载失败误判为功能成功。

## Startup and ordinary browse

- [ ] 启动 Release Blink，窗口标题为 `Blink`，窗口正常出现，初始目录完成本地加载。
- [ ] 启动后空闲观察至少 30 秒，进程不自然退出、窗口不变黑、不出现启动崩溃。
- [ ] Resize 窗口：缩小时地址栏仍可用，左侧 pane 保持可读，Details view 填充剩余空间。
- [ ] 点击 Places：Home、Desktop、Downloads、Documents。
- [ ] 点击 Drives：至少验证一个真实卷根目录（例如 `D:\`）。
- [ ] 在 Address Bar 输入正常目录并按 Enter，确认进入且列表正常。
- [ ] 在 Details view 确认 Name、Type、Size、Modified 四列；固定文案显示为当前系统语言。
- [ ] 点击 Name、Size、Modified 列头，确认排序改变显示顺序且目录仍优先。
- [ ] 双击目录，确认进入该目录；本轮不测试双击打开文件。

## Navigation state and error semantics

- [ ] Back、Forward、Up、Refresh 均正常，且 Refresh 失败时保留原 committed 页面。
- [ ] Back 到旧目录后再 Navigate 到新目录，确认 Forward history 被截断。
- [ ] 输入不存在路径（例如 `Z:\BlinkDefinitelyDoesNotExist_114514`），显示可理解的错误，地址栏恢复旧目录，旧列表不消失、不变空。
- [ ] 输入一个当前用户无权限的目录（若机器上有安全、非重要的测试目标），确认同样保留旧页面并显示 access-denied 文案；不得修改权限或真实用户文件。
- [ ] 快速执行 A → B → C 导航，确认最终页面只来自最后一次请求，不跳回旧结果。
- [ ] 使用 Ctrl 单项追加选择和 Shift 范围选择，确认底部 selected count 正确；若当前构建尚未承诺 Ctrl+A，则按 Deferred 处理。
- [ ] 切换目录后确认上一目录的 selection 不泄漏到新目录。

## Large-directory fixture

- [ ] 在 `entries-10000` 和 `entries-50000` 普通用户 fixture 中进入目录，分别记录首次可操作时间、进入期间窗口响应、滚动、拖动滚动条、Name/Size/Modified 排序、Shift/Ctrl 多选、resize 和大概内存；不设定未经批准的固定毫秒阈值。
- [ ] 大目录中回收容器重新绑定后，Name/Type/Size/Modified 与 FileIdentity 选择仍对应同一行；列表不出现跳行或错误复用。

## Boundary and shutdown

- [ ] 验证中文路径与中文文件名显示正常。
- [ ] IndexService 停止或未运行时，local browse 仍可用；底部显示本地化的未连接占位，不把浏览失败伪装成索引失败。
- [ ] 枚举期间关闭窗口，窗口不等待磁盘、不崩溃；记录是否有残留进程。
- [ ] 记录发现的问题、复现路径和截图；在真实运行前不预填 PASS、耗时、内存或“正常”。

## Deferred by scope

- Ctrl+A 全选（除非当前构建明确承诺并单独验收）
- 真正多标签、Tab close/switch
- Search 产品集成
- Rename/Delete/Copy/Paste 产品流程
- thumbnail、Preview、Shell Namespace 图标
- 运行时语言切换和翻译管理工具

## Final manual acceptance record — 2026-08-14

本节记录用户实际完成的最终 Phase 1A 手工验收，不是自动测试推断：

- **Back / Forward history：PASS**。A → B → C → Back → B → Back → A →
  Forward → B → Forward → C，顺序正确。
- **Forward-history truncation：PASS**。从 C 返回 B 后直接导航到新目录 D，
  旧 C 不再可由 Forward 到达。
- **50k large-directory responsiveness：PASS WITH ACCEPTED RESIDUAL LATENCY**。
  进入/Back、拖动窗口、resize、scrolling 通过；仍可能有短暂交互停顿，
  但不再出现此前的 prolonged freeze / `Not Responding`，恢复后窗口立即跟随当前鼠标输入。
  该结果不表示零延迟或完全平滑。
- **Close during large-directory presentation：PASS**。窗口正常关闭，无 crash/error
  popup，无残留 `Blink.exe`。

结合本清单前面的既有手工记录（启动稳定、本地导航、地址栏、Up、Refresh、排序、
Ctrl/Shift selection、目录双击、错误路径保留旧页面、Unicode、IndexService 未运行时
local browse、generation cancellation 和 selection/identity 语义），Phase 1A manual
acceptance gate 已满足。

最终状态：**Phase 1A = ACCEPTED / COMPLETE；Phase 1B = NOT STARTED；Phase 1
Product Surface = FROZEN（D-0027）**。Ctrl+A、真实多标签、Search 产品化、文件 mutation、
watcher 等仍按本清单和 `PHASE1_PLAN.md` 的范围延期。
