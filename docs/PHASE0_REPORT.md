# Blink Phase 0 验收报告

日期：2026-08-10  
目标系统：Windows build 26200  
Windows SDK：10.0.26100.0  
构建工具：Visual C++ Build Tools 18，MSVC 14.51，PlatformToolset v145

## 结论

Phase 0 核心路线已完成验证并停在 Phase 0。没有进入 Phase 1，也没有实现完整 Explorer UI、缩略图、网络 Provider 或插件系统。

## 构建

使用的构建命令：

~~~text
MSBuild Blink.sln /m /nr:false /p:Configuration=Debug /p:Platform=x64
~~~

已验证的产物包括：

- Blink.exe：WinUI 3 + C++/WinRT self-contained 主程序
- Blink.ProviderHost.exe：隔离 Provider 进程
- Blink.Phase0.Tests.exe：核心运行探针
- Blink.Core.lib、Blink.Index.lib、Blink.Properties.lib、Blink.Transfer.lib、Blink.Provider.Contracts.lib、Blink.Provider.Fake.lib

主工程通过 Windows App SDK 2.3.1 stable 和 Microsoft.Windows.CppWinRT 2.0.250303.1 NuGet 还原构建。

## 运行验证

### 本地文件与身份

- CreateDirectory2W 创建测试目录成功。
- CreateFile3 创建测试文件成功。
- RenameFileModern 后 File ID + Volume ID 保持一致：identity_same_after_rename=1。
- 规范化相对路径后，索引搜索命中 identity-renamed.bin：search_results=1。

### 索引与变化观察

- 首次 DFS 索引完成；Search 不重新扫描磁盘。
- 目录观察器使用 ReadDirectoryChangesExW，并成功捕获 observer-trigger.bin：directory_observer_changes=1，saw_trigger=1。
- USN 查询路径已执行，但当前非管理员环境返回 ERROR_ACCESS_DENIED（5）。这不是代码静默降级；该状态会向上层暴露。

### 属性

- IPropertyStore 读取路径可用。
- 文件系统属性、ACL、硬链接和 Alternate Data Streams 探针成功。
- 示例输出：properties_available=1，acl_entries=4，hard_links=1，streams=1。
- 属性读取 API 对 UI 提供后台调用入口；正式第三方 Handler 隔离仍需 MetadataHost。

### CopyFile2

- 小文件复制成功，进度回调到达 100%。
- 64 MiB 取消探针返回取消状态并报告 canceled=1：copy_cancel_status=0x800704d3。
- 未把暂停/恢复列为已验证功能。

### Provider 隔离

ProviderClient 启动独立 Blink.ProviderHost.exe，并使用消息模式命名管道。运行探针覆盖：

| 模式 | 结果 |
| --- | --- |
| normal | 收到 FakeProvider 正常响应 |
| delay | 本地 Browse 继续完成；请求可取消 |
| hang | 本地 Browse 继续完成；请求可取消 |
| error | 收到显式错误响应 |
| disconnect | 收到断开错误 |
| crash | 收到主机断开错误，主机不拖垮测试进程 |

### WinUI 3 启动

Blink.exe 已通过 Windows App SDK self-contained 构建；启动探针观察到 OnLaunched、Window.Activate 和主窗口句柄。UI 中提供路径浏览、后台枚举、内存索引搜索、属性查看、CopyFile2 探针和 Provider 故障模式按钮。

## 性能观测

本次小型探针不是正式基准，只用于验证路线和量级：

- 单文件临时目录首次索引约 1–3 ms。
- 内存索引搜索约 0.1 ms。
- 目录观察事件在 3 秒等待窗口内返回。
- 本地目录 Browse 在 Provider delay/hang 期间仍约 0.4–0.8 ms 完成。
- 测试进程工作集约 15 MiB；该数字受 Debug 构建和测试规模影响，不能作为产品预算。

## 失败路径与已知问题

1. 全卷 USN：非管理员打开卷或执行 Journal 控制码返回 ERROR_ACCESS_DENIED。需要在管理员/具备相应卷访问权限的环境中补测真正的 USN 增量消费。
2. USN reset、wraparound、Journal ID 变化、卷离线后的重建策略只完成数据结构和错误边界，尚未做长时间压力测试。
3. Property Handler 仍在 Blink 主进程的后台线程中执行；第三方 Handler 的崩溃/阻塞隔离要到 MetadataHost。
4. Provider 协议是 Phase 0 原型，尚未加入正式版本协商、调用者身份认证、速率限制和消息大小配额。
5. 索引持久化格式是版本化二进制原型，尚未做崩溃一致性、压缩和大规模数据集基准。
6. Build 输出包含 Windows App SDK self-contained 运行库；尚未做安装器、签名和发布流程。

## 架构变更

- 初始计划中的 Provider 命令行直接传完整命名管道路径会遇到反斜杠转义和 argv 边界问题；实现改为传递不含路径分隔符的 pipe token，由 ProviderHost 重建标准命名管道路径。
- 目录变更路线从“仅记录选择”落实为 ReadDirectoryChangesExW Extended information 的异步一次性观察器。
- CreateFile3 的 SDK 符号审计修正为无 W 后缀的 CreateFile3。

## 技术债务

MetadataHost、生产级索引存储、USN 长期增量重建、Provider 安全协议、安装发布和正式 UI/可访问性均留待后续阶段。

## 下一阶段

下一阶段只有在用户明确确认后才开始。建议的 Phase 1 入口是：在授权测试环境补做 USN 增量长跑和大规模索引基准，然后确定 MetadataHost 与 Provider 正式协议；在此之前不扩展功能面。
