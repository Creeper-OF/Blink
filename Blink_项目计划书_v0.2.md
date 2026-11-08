# Blink｜新时代文件管理器 项目计划书 v0.2

> 产品名：**Blink**  
> 定位：**只管理文件，不管理 Windows 的历史包袱。**
>
> 目标：基于当前及后续 Windows 平台，构建一个采用现代 Windows 接口、Everything 式索引、Provider 统一抽象、网络与扩展进程隔离的高性能文件管理器。
>
> 不是 Explorer Shell 的替代品，而是一个专注于“文件”的新时代文件管理器。

---

## 1. 项目背景

Windows File Explorer 同时承担了文件系统浏览、Shell Namespace、网络位置、虚拟对象、第三方扩展等大量历史职责。

这带来几个长期存在的问题：

- 文件浏览和 Shell 资源管理耦合过深；
- 网络位置离线、响应过慢时可能影响整个 Explorer 交互；
- 第三方 Shell Extension、Preview Handler、Property Handler 等扩展一旦阻塞或崩溃，可能拖累 Explorer；
- 搜索体验受 Windows Search、目录枚举、索引范围等因素影响，无法稳定达到 Everything 式即时响应；
- 本地文件、远程文件、压缩包、云文件等数据源缺少真正统一的现代抽象；
- Explorer 承担了太多兼容性责任，因此很难激进重构。

本项目不试图重写 Windows Shell，而是绕开这些历史包袱，仅重新实现“文件管理”本身。

---

## 2. 产品定位

### 2.1 核心定位

构建一个：

- 保留 Windows Explorer 操作习惯；
- 采用现代 Windows API；
- 本地搜索接近 Everything；
- 本地与远程文件统一管理；
- 网络资源离线不影响主界面；
- 第三方扩展、预览、属性解析与主进程隔离；
- 对未来 Windows 版本优先适配；
- 不背负旧 Windows 兼容负担；

的现代高性能文件管理器。

### 2.2 明确不做

本项目**不负责**：

- 控制面板；
- 打印机；
- 蓝牙设备；
- 任务栏；
- 开始菜单；
- 系统服务；
- 注册表管理；
- 设备管理器；
- 网络适配器配置；
- Shell Namespace 中与文件无关的虚拟对象；
- 兼容所有旧 COM / CLSID Namespace Extension。

一句话：

> **凡是“这个文件/目录在哪里、是什么、怎么读写、怎么搜索、怎么移动”，归本项目。**
>
> **凡是“这个设备/系统组件应该怎么配置”，交给 Windows。**

---

## 3. 支持范围

### 3.1 文件类数据源

首期与后续可支持：

- NTFS；
- ReFS；
- FAT32 / exFAT；
- USB / 移动硬盘；
- SMB；
- SFTP；
- WebDAV；
- MTP；
- ZIP / 7z / RAR / TAR 等压缩包；
- OneDrive / Google Drive / Dropbox 等云文件；
- 未来第三方 Provider。

### 3.2 系统版本策略

建议最低系统：

> **Windows 11 25H2 或更高版本。**

原则：

- 不支持 Windows 7；
- 不支持 Windows 8 / 8.1；
- 不支持 Windows 10；
- 不为了旧系统添加核心 API fallback；
- 优先使用当前 Windows 新接口；
- 未来如果 Windows 12 / 后续版本提供更优接口，优先迁移；
- 只有新接口无法满足需求时，才使用仍受支持的经典 Win32 API；
- Stable Release 不依赖 Windows App SDK Experimental API。

这不是兼容性不足，而是项目定位的一部分：

> **以当前 Windows 文件平台为基线开发，不携带上一代兼容层。**

---

## 4. 技术栈建议

### 4.1 主技术栈

- 语言：C++20 / C++23
- Windows 封装：C++/WinRT
- UI：WinUI 3
- 平台：Windows App SDK Stable
- 打包：MSIX
- 首发架构：x64
- 后续：ARM64

### 4.2 为什么优先 C++

项目会大量接触：

- HANDLE；
- USN Journal；
- DeviceIoControl；
- File ID；
- Overlapped I/O；
- IOCP；
- ACL；
- COM；
- IPropertyStore；
- Cloud Files API；
- Reparse Point；
- SMB；
- 底层文件枚举与传输。

因此 C++/WinRT 更适合本项目，而不是为了 UI 便利把底层核心全部做成 C#。

---

## 5. API 使用原则

### 5.1 基本原则

> **新接口优先，但“新接口”不等于“必须使用最高层 WinRT 抽象”。**

建议：

- UI：Windows App SDK / WinUI 3；
- Windows 集成：WinRT / Windows App SDK；
- 文件系统核心：当前 Windows 最新受支持 Win32 File API；
- 协议：独立 Provider；
- 第三方扩展：独立 Host 进程；
- 不为了“现代感”强行把所有底层文件系统操作包装成 StorageFile。

### 5.2 优先研究/采用的新文件接口

Windows 11 24H2 及后续可重点研究：

- `CreateFile3`
- `CreateDirectory2`
- `DeleteFile2`
- `RemoveDirectory2`

这些接口应优先于仅为旧系统兼容而使用的方案。

### 5.3 仍然重要的现代 Win32 能力

- `GetFileInformationByHandleEx`
- `SetFileInformationByHandle`
- `CopyFile2`
- `ReadDirectoryChangesExW`
- `FSCTL_QUERY_USN_JOURNAL`
- `FSCTL_ENUM_USN_DATA`
- `FSCTL_READ_USN_JOURNAL`
- `GetSecurityInfo`
- `GetNamedSecurityInfo`
- Windows Property System / `IPropertyStore`
- Cloud Files API / CFAPI

---

## 6. 总体架构

```text
FileManager.exe
│
├── UI / Navigation
├── Tab Manager
├── Address Bar
├── File View
└── ViewModel
     │
     ├───────────────┐
     │               │
     ▼               ▼
 FileCore        IndexService
     │               │
     │               ├─ Local Index
     │               ├─ Remote Cache
     │               └─ Search Engine
     │
     ├─ LocalProvider
     ├─ TransferService
     ├─ PropertyService
     └─ Provider Manager
             │
             ├─ SFTPProviderHost.exe
             ├─ SMBProviderHost.exe
             ├─ WebDAVProviderHost.exe
             ├─ ArchiveProviderHost.exe
             └─ CloudProviderHost.exe

Additional isolated processes:

MetadataHost.exe
PreviewHost.exe
ExtensionHost.exe
ShellIntegration.exe
```

核心原则：

> **UI 线程不等待磁盘，不等待网络，不等待第三方代码。**

---

## 7. 统一文件对象模型

所有本地/远程/压缩包/云端对象统一抽象。

示例：

```cpp
struct FileItem
{
    FileId Id;
    ProviderId ProviderId;

    std::wstring Name;
    std::wstring FullPath;

    FileItemType Type;
    std::wstring Extension;

    uint64_t Size;
    uint64_t AllocatedSize;

    FileTime CreatedTime;
    FileTime ModifiedTime;
    FileTime AccessedTime;

    FileAttributes Attributes;

    FileId ParentId;

    AvailabilityState Availability;
    FileCapabilities Capabilities;
};
```

逻辑 URI 示例：

```text
local://C:/Windows/notepad.exe
sftp://TokyoVPS/home/of/a.txt
smb://NAS/share/movie.mkv
archive://D:/abc.7z/test.txt
```

UI 不直接判断数据源类型，只读取能力：

```text
CanOpen
CanRead
CanWrite
CanRename
CanDelete
CanMove
CanCopy
CanQueryProperties
CanSetPermissions
```

---

## 8. 文件浏览功能

必须保留用户已经形成肌肉记忆的 Explorer 操作逻辑。

### 8.1 基础导航

- 文件夹层级浏览；
- 地址栏直接输入路径；
- 面包屑路径；
- 后退；
- 前进；
- 返回上一级；
- 刷新；
- 多标签页；
- 多窗口；
- 新标签打开；
- 导航历史；
- 左侧目录树；
- 收藏；
- 最近文件；
- 固定常用目录；
- 此电脑 / 磁盘入口；
- 远程位置入口。

### 8.2 快捷键

保持 Windows 习惯：

```text
Ctrl+C      复制
Ctrl+X      剪切
Ctrl+V      粘贴
Ctrl+A      全选
Ctrl+T      新标签
Ctrl+W      关闭标签
Ctrl+N      新窗口
Ctrl+L      地址栏
Alt+←       后退
Alt+→       前进
Alt+↑       上一级
F2          重命名
F5          刷新
Delete      删除
Shift+Delete 永久删除
```

原则：

> 不教育用户重新学习文件管理。

---

## 9. 文件选择

必须支持：

- 单击选择；
- Ctrl 多选；
- Shift 连续选择；
- Ctrl+A 全选；
- 鼠标框选；
- 复选框选择；
- 键盘方向键选择；
- 多文件拖拽；
- 反选（可增强）；
- 大批量文件选择时保持 UI 流畅。

---

## 10. 文件操作

### 10.1 基础功能

- 打开文件；
- 打开方式；
- 当前标签 / 新标签 / 新窗口打开文件夹；
- 新建文件夹；
- 新建文件；
- 复制；
- 剪切；
- 粘贴；
- 移动；
- 重命名；
- 删除到回收站；
- 永久删除；
- 恢复；
- 创建快捷方式；
- 复制路径；
- 复制文件名；
- 撤销；
- 重做；
- UAC 提权处理；
- 权限错误处理；
- 冲突处理。

### 10.2 拖放语义

兼容 Windows 习惯：

```text
同盘拖动        Move
跨盘拖动        Copy
Ctrl + 拖动     Copy
Shift + 拖动    Move
Alt + 拖动      Create Shortcut
```

---

## 11. TransferService

文件复制与移动必须独立于 UI。

建议独立：

```text
TransferService.exe
```

负责：

- 本地复制；
- 本地移动；
- 跨 Provider 传输；
- 上传；
- 下载；
- 暂停；
- 恢复；
- 取消；
- 重试；
- 限速；
- 进度；
- ETA；
- 冲突处理；
- 断线续传；
- 后台传输。

目标：

> 即使关闭主 FileManager 窗口，传输任务也可以继续。

### 11.1 本地复制

第一版优先基于 `CopyFile2`。

后续实测后再决定是否值得自建 Overlapped / IOCP Pipeline。

---

## 12. 文件显示

支持：

- 超大图标；
- 大图标；
- 中图标；
- 小图标；
- 列表；
- 详细信息；
- 平铺；
- 内容视图；
- 紧凑视图；
- 缩略图；
- 文件类型图标；
- 文件夹缩略图；
- 预览窗格；
- 详细信息窗格；
- 状态栏。

后续可加入 Gallery。

---

## 13. 排序 / 分组 / 列

至少支持：

### 排序字段

- 名称；
- 修改日期；
- 创建日期；
- 访问日期；
- 类型；
- 扩展名；
- 大小；
- 路径；
- 属性。

### 排序方式

- 升序；
- 降序；
- 文件夹优先；
- 混排；
- 自然数字排序。

自然排序示例：

```text
file1
file2
file9
file10
```

而不是：

```text
file1
file10
file2
file9
```

### 详细信息列

- 拖动列宽；
- 调整列顺序；
- 添加/隐藏列；
- 自动适应宽度；
- 保存不同目录的显示习惯。

---

## 14. 本地索引系统

这是核心卖点之一。

### 14.1 目标

> 搜索速度达到 Everything 式即时响应。

### 14.2 NTFS / ReFS

优先利用 USN Change Journal：

```text
第一次建立索引
↓
Index Snapshot
↓
记录当前 USN
↓
读取 USN 增量
↓
实时更新索引
```

不是每次启动重新扫描整盘。

### 14.3 索引层设计

建议双层结构：

```text
FastIndex
├─ Name
├─ Path
├─ Extension
├─ FileId
├─ ParentId
├─ Size
└─ Time

MetadataStore (SQLite)
├─ 收藏
├─ 历史
├─ 远程缓存
├─ 扩展属性
├─ 设置
└─ Provider 配置
```

FastIndex 可考虑：

- 内存结构；
- memory mapped file；
- 自定义紧凑索引。

搜索热路径不要全部依赖 SQLite。

---

## 15. 搜索系统

目标：

> 用户输入字符时结果同步出现，而不是“开始搜索磁盘”。

支持语法：

```text
abc
*.exe
ext:exe
size:>1GB
modified:today
path:D:\Game
minecraft ext:jar
```

目录内搜索：

```text
D:\Project 下搜索 *.cpp
```

逻辑上仍然是查全局索引：

```text
WHERE Path STARTS WITH 'D:\Project'
AND Extension = '.cpp'
```

不重新扫描目录。

后续可扩展：

- 内容索引；
- OCR；
- 语义搜索；
- Windows App Content Search 等稳定 API。

但基础文件名搜索不得依赖 Experimental API。

---

## 16. 本地文件身份

路径不能被当作文件的唯一身份。

本地优先使用：

- Volume ID；
- File ID；
- Parent File ID。

通过 `GetFileInformationByHandleEx` 等接口获取。

目标：

- 文件重命名后仍能追踪；
- 文件在同卷移动后仍可识别；
- 索引稳定性更好；
- 减少纯字符串路径带来的错误。

---

# 17. 属性系统（一级核心模块）

> **属性不是一个附加窗口，而是整个文件管理器的基础能力。**

必须从第一阶段设计 `PropertyService`。

原因：

- 索引需要属性；
- 排序需要属性；
- 搜索需要属性；
- 详细信息视图需要属性；
- 权限需要属性；
- Provider 需要属性；
- 预览和扩展元数据需要属性。

---

## 18. 属性第一层：基础文件系统属性

应支持：

- 名称；
- 完整路径；
- 文件类型；
- 扩展名；
- 文件大小；
- 占用空间；
- 创建时间；
- 修改时间；
- 访问时间；
- 只读；
- 隐藏；
- 系统；
- 归档；
- File ID；
- Volume ID；
- 硬链接数量；
- 压缩状态；
- Reparse Point；
- 符号链接；
- 链接目标；
- Alternate Data Streams；
- 文件系统类型。

---

## 19. 属性第二层：Windows Property System

通过：

```text
IPropertyStore
```

统一读取：

### 图片

- 分辨率；
- EXIF；
- 拍摄时间；
- 相机；
- ISO；
- GPS。

### 音乐

- 标题；
- 艺术家；
- 专辑；
- 时长；
- 编码。

### 视频

- 分辨率；
- 帧率；
- 时长；
- 编码。

### EXE / DLL

- 产品名称；
- 公司；
- 文件版本；
- 产品版本；
- 数字签名信息。

### Office / 文档

- 作者；
- 标题；
- 标签；
- 创建时间；
- 修改者等。

---

## 20. MetadataHost 隔离

主进程禁止直接加载第三方 Property Handler。

结构：

```text
FileManager.exe
      │
      │ IPC
      ▼
MetadataHost.exe
      │
      ├─ IPropertyStore
      └─ 第三方 Property Handler
```

如果第三方 Handler：

- 卡死；
- 崩溃；
- 内存泄漏；

结果应为：

```text
MetadataHost.exe 被杀死 / 重启
FileManager 继续运行
UI 显示“扩展属性读取失败”
```

---

## 21. 属性第三层：Security

支持完整 NTFS Security Descriptor。

包括：

- Owner；
- DACL；
- ACE；
- 继承；
- 完全控制；
- 修改；
- 读取和执行；
- 列出目录；
- 读取；
- 写入；
- 高级权限；
- 有效访问。

重点 API：

- `GetSecurityInfo`
- `GetNamedSecurityInfo`
- 对应 Set API

不是简单显示“可读 / 可写”。

---

## 22. 属性窗口建议结构

```text
属性

[常规]
名称
类型
位置
大小
占用空间
包含内容
创建
修改
访问
文件属性

[详细信息]
Windows Property System
EXIF / Media / Version / Document Metadata

[安全]
Owner
DACL
Inheritance
Effective Access

[高级]
File ID
Volume ID
Hard Links
Streams
Reparse Point
Compression
Encryption

[签名]
Digital Signature
Certificate
Timestamp
```

远程文件根据 Provider 动态变化。

例如 SFTP：

```text
[常规]

[远程]
Server
Remote Path
UID
GID
Unix Mode
Owner
Group
Symlink
Last Sync
```

---

## 23. 文件变化监听

本地索引：

- USN Journal 为主；
- 打开目录可配合 `ReadDirectoryChangesExW`；
- 所有接口通过能力检测使用。

建议：

```text
NTFS
USN Journal
+
ReadDirectoryChangesExW

ReFS
USN Journal / 能力检测

FAT32 / exFAT
目录枚举 + 普通变化监听

Remote
Provider 自己的 Watch / Refresh 能力
```

---

## 24. SFTP Provider

第一版远程协议优先实现 SFTP。

### 24.1 原则

所有网络请求必须：

- 异步；
- 可取消；
- 有 Deadline；
- 有超时；
- 有失败状态；
- 有重试策略；
- 与主 UI 隔离。

### 24.2 进程

```text
SFTPProviderHost.exe
```

Provider 卡死时：

```text
Tokyo VPS ● 离线 / 响应超时
```

但用户仍然能正常浏览：

```text
C:\
D:\
E:\
```

### 24.3 远程缓存

缓存：

- 目录树；
- 文件名；
- 路径；
- 大小；
- 修改时间；
- 类型；
- Unix 权限；
- Last Sync。

离线状态仍可：

- 查看上次目录结构；
- 搜索上次索引；
- 查看上次属性；
- 明确显示缓存时间。

---

## 25. SMB Provider

第二阶段实现。

不重新实现 SMB 协议。

使用 Windows SMB Redirector / UNC：

```text
\\server\share
```

但是：

> 即使底层是 Windows 文件 API，也不允许 SMB 请求直接阻塞主 UI。

建议放入：

```text
SMBProviderHost.exe
```

实现：

- 超时；
- 断线；
- 重连；
- 凭据；
- 离线状态；
- 缓存；
- 网络搜索。

---

## 26. WebDAV / Cloud / MTP / Archive

后续 Provider：

```text
ProviderManager
├─ LocalProvider
├─ SFTPProvider
├─ SMBProvider
├─ WebDAVProvider
├─ ArchiveProvider
├─ MTPProvider
└─ CloudProvider
```

### ArchiveProvider

把压缩包表现为目录：

```text
abc.7z
  ↓
archive://D:/abc.7z/
```

支持：

- 浏览；
- 拖出；
- 拖入；
- 复制；
- 删除；
- 解压；
- 压缩。

### CloudProvider

后续研究 Windows Cloud Files API / CFAPI：

- Placeholder；
- Hydration；
- 同步状态；
- 按需下载；
- 云端文件统一展示。

### MTPProvider

把手机内容视为文件 Provider，而不是系统设备管理。

---

## 27. 回收站

回收站属于文件生命周期，应支持。

功能：

- 删除到回收站；
- 查看删除时间；
- 查看原路径；
- 恢复；
- 永久删除；
- 清空。

实现上不要因此引入整个 Shell Namespace。

远程 Provider 根据自身能力提供：

- Trash；
- `.Trash`；
- 或明确提示永久删除。

---

## 28. PreviewHost

预览必须隔离。

```text
PreviewHost.exe
```

建议：

- 图片：WIC；
- 音频/视频：Media Foundation；
- 文本：内建；
- PDF / Office：第三方 Preview Handler 时放在独立进程。

原则：

> 预览器死，不等于文件管理器死。

---

## 29. ExtensionHost

右键菜单分两层：

### 核心菜单

始终由程序自己实现：

- 打开；
- 新标签打开；
- 打开方式；
- 剪切；
- 复制；
- 粘贴；
- 重命名；
- 删除；
- 复制路径；
- 属性。

### 扩展菜单

第三方：

- VS Code；
- Git；
- 7-Zip；
- 其他工具。

放入：

```text
ExtensionHost.exe
```

目标：

> 某个右键插件卡死，不允许拖死 FileManager。

本项目自身集成 Windows Explorer 时，可以优先研究当前 Windows 11 推荐的 `IExplorerCommand`。

---

## 30. 文件夹选项

### 常规

```text
启动时打开
○ 主页
○ 此电脑
○ 上次位置

打开文件夹
○ 当前标签
○ 新标签
○ 新窗口

文件打开方式
○ 双击
○ 单击
```

### 显示

- 显示扩展名；
- 显示隐藏文件；
- 显示系统文件；
- 显示受保护系统文件；
- 缩略图；
- 文件夹缩略图；
- 复选框；
- 完整路径；
- 状态栏；
- 紧凑模式；
- 文件夹优先排序。

### 导航

- 自动展开当前目录；
- 显示所有磁盘；
- 显示网络位置；
- 显示远程位置；
- 显示收藏。

### 搜索

- 索引磁盘；
- 索引远程位置；
- 是否索引隐藏文件；
- 是否索引系统文件；
- 是否索引内容。

### 隐私

- 显示最近文件；
- 显示最近目录；
- 记录历史；
- 清除历史。

### 远程

- 连接超时；
- 重试次数；
- 后台刷新；
- 离线缓存；
- 缓存大小；
- 自动重连。

### 性能

- 缩略图缓存；
- Metadata 缓存；
- 索引内存上限；
- 后台索引优先级；
- Provider 并发数；
- 传输并发数。

---

## 31. UI 原则

UI 可以现代，但操作逻辑不要为了创新而创新。

建议：

```text
┌──────────────────────────────────────────┐
│ ← → ↑  地址栏                     搜索 │
├──────────────────────────────────────────┤
│ 新建 剪切 复制 粘贴 重命名 删除 视图 … │
├──────────────────────────────────────────┤
│ C:\Users\OF | D:\Game | Tokyo VPS     │
├──────────────┬───────────────────────────┤
│ 收藏         │ 文件列表                  │
│ 此电脑       │                           │
│ C:           │                           │
│ D:           │                           │
│ 远程         │                           │
│ Tokyo VPS    │                           │
└──────────────┴───────────────────────────┘
```

目标：

- Explorer 用户零学习成本；
- UI 响应必须始终优先；
- 所有慢任务后台化；
- 失败明确显示状态，而不是卡死。

---

## 32. 第一版 MVP

### 系统

- Windows 11 25H2+
- x64
- WinUI 3
- Windows App SDK Stable

### LocalProvider

- NTFS 浏览；
- 标签页；
- 地址栏；
- 导航树；
- 图标 / 详细列表；
- 排序；
- 多选；
- 拖放；
- 新建；
- 复制；
- 移动；
- 删除；
- 重命名；
- 回收站。

### PropertyService

- General；
- Windows Property System；
- Security；
- Advanced。

### IndexService

- USN；
- FastIndex；
- 即时文件名搜索；
- 目录内索引过滤；
- 基础搜索语法。

### TransferService

- 复制；
- 暂停；
- 恢复；
- 取消；
- 冲突处理；
- 进度与 ETA。

### SFTPProvider

- 浏览；
- 上传；
- 下载；
- 新建；
- 重命名；
- 删除；
- 属性；
- 缓存；
- 离线状态；
- Provider 独立进程。

---

## 33. 开发阶段

### Phase 0 — 技术验证

先不追求漂亮 UI。

验证：

- WinUI 3 窗口；
- NTFS 快速目录枚举；
- `GetFileInformationByHandleEx`；
- File ID；
- USN Journal；
- 10 万 / 100 万 / 500 万文件搜索；
- `IPropertyStore`；
- ACL；
- `CopyFile2`；
- `ReadDirectoryChangesExW`；
- SFTP Provider 进程隔离；
- Provider 超时；
- Provider 崩溃；
- Provider 永远不返回时主 UI 是否仍流畅。

### Phase 1 — Local Explorer

实现可日用的本地文件管理器。

### Phase 2 — Property System

完成属性窗口、MetadataHost、安全属性与高级属性。

### Phase 3 — Index

实现 Everything 式常驻索引。

### Phase 4 — Transfer

实现独立传输服务。

### Phase 5 — SFTP

完成远程 Provider。

### Phase 6 — SMB / WebDAV

扩展远程协议。

### Phase 7 — Archive / Cloud / MTP

扩展数据源。

### Phase 8 — Plugin API

设计第三方 Provider / Metadata / Command 扩展接口。

---

## 34. 从第一天开始禁止的事情

### 34.1 UI 线程

禁止：

- UI 线程执行网络 I/O；
- UI 线程等待网络；
- UI 线程执行长时间磁盘 I/O；
- UI 线程加载第三方扩展；
- UI 线程解析复杂 Metadata；
- UI 线程等待 Preview Handler。

### 34.2 主进程

禁止 FileManager 主进程直接加载：

- 第三方 Shell Extension；
- 第三方 Property Handler；
- 第三方 Preview Handler；
- 未受信任 Provider。

### 34.3 Provider

禁止：

- 无限等待；
- 无法取消的长请求；
- 将网络阻塞传回 UI；
- Provider 失败导致整个程序退出。

### 34.4 文件身份

禁止把：

```text
Path == File Identity
```

作为核心假设。

本地文件优先：

```text
Volume ID + File ID
```

### 34.5 旧系统兼容

禁止为了 Windows 7 / 8 / 10：

- 降低核心 API；
- 增加大量 fallback；
- 破坏现代架构；
- 把旧兼容代码混入核心路径。

---

## 35. 性能目标

初步目标：

### 启动

- 冷启动可接受；
- 热启动目标接近瞬开；
- 索引服务可常驻或独立启动。

### 文件浏览

- 打开普通本地目录无明显等待；
- 大目录采用虚拟化列表；
- 目录枚举不能卡 UI。

### 搜索

目标测试规模：

- 100,000 文件；
- 1,000,000 文件；
- 5,000,000 文件。

文件名索引搜索目标：

> 人体感知上接近即时出现。

### 网络

- 网络位置不可达时主 UI 仍可操作；
- Provider 超时后明确显示 Offline；
- 离线缓存可继续搜索。

---

## 36. 核心演示场景

### Demo A：搜索

磁盘拥有数百万文件。

输入：

```text
server.properties
```

目标：

> 输入完成前，结果已经出现。

### Demo B：网络故障

左侧：

```text
Tokyo VPS
NAS
Server A
Server B
```

直接拔网线。

预期：

```text
Tokyo VPS  ● 离线
NAS        ● 离线
Server A   ● 离线
Server B   ● 离线
```

与此同时：

```text
C:\Users
D:\SteamLibrary
E:\Projects
```

仍可无感浏览。

### Demo C：扩展崩溃

故意让某个 Metadata / Preview / Extension Host 崩溃。

预期：

- FileManager 不崩溃；
- 对应区域显示失败；
- Host 可自动重启。

---

## 37. 产品宣传定位

可以考虑：

> **新时代文件管理器抢先体验。**

> **只管理文件，不管理 Windows 的历史包袱。**

> **Everything 的搜索速度 + Explorer 的操作习惯 + 原生远程文件能力。**

> **NAS 掉线，文件管理器不陪葬。**

项目不是“重写 Explorer”，而是：

> **重新定义 Windows 上文件管理应该是什么体验。**

---

## 38. 给 Codex 的执行原则

Codex 在实现时必须遵守：

1. 优先采用 Windows 11 当前稳定新接口；
2. 不为 Windows 10 及更早系统提供兼容分支；
3. UI 与 I/O 强制解耦；
4. 网络 Provider 强制进程隔离；
5. Property / Preview / Extension 第三方代码强制隔离；
6. 本地文件身份优先使用 File ID；
7. 搜索优先建立本地索引，而不是实时扫描；
8. NTFS / ReFS 优先利用 USN Journal；
9. 核心右键菜单自行实现；
10. 第三方扩展不得影响主进程稳定性；
11. 不在 MVP 阶段过度设计所有 Provider；
12. 第一版只需证明 Local + Index + Property + Transfer + SFTP 架构成立；
13. 避免重复造 Windows 已有且可靠的底层能力；
14. 不依赖 Experimental API 作为 Stable Release 核心路径；
15. 所有架构设计优先考虑“卡死时能否隔离和杀掉”。

---

## 39. 建议的仓库文档结构

后续可以从本文档继续拆分：

```text
/docs
├─ PROJECT_PLAN.md
├─ ARCHITECTURE.md
├─ API_SELECTION.md
├─ PROPERTY_SYSTEM.md
├─ INDEX_DESIGN.md
├─ PROVIDER_SPEC.md
├─ TRANSFER_SERVICE.md
├─ UI_UX_GUIDE.md
└─ CODING_RULES.md
```

当前文档可直接作为 Codex 的总体项目上下文。

---

---

## 39.5 项目命名

### 产品名称

**Blink**

含义：眨眼。对应项目强调的即时搜索、快速打开与“文件就在眨眼之间”的体验。

建议统一使用：

```text
产品名：Blink
仓库名：BlinkFileManager
主程序：Blink.exe

Blink.Core
Blink.Index
Blink.Transfer
Blink.Properties
Blink.Provider.Local
Blink.Provider.SFTP
Blink.Provider.SMB
```

建议宣传语：

> **Blink — 文件，就在眨眼之间。**

或：

> **Blink — 只管理文件。**

由于“Blink”本身在软件领域存在大量同名项目，代码仓库、包名、命名空间和发布标识尽量避免仅使用裸 `Blink`，优先使用 `BlinkFileManager` 或带组织前缀的唯一命名。

---

## 39.6 项目执行中的新想法与变更管理

项目执行过程中允许持续产生新想法，但**禁止想到一个功能就立即打断当前阶段重构主线**。采用“先记录、再评估、再合并”的方式。

### 新想法处理流程

```text
出现新想法
   ↓
记录到 docs/IDEAS.md
   ↓
标记类型与优先级
   ↓
判断是否影响当前架构
   ├─ 不影响 → 进入 Backlog，等对应阶段再做
   ├─ 小幅影响 → 更新相关设计文档后实施
   └─ 架构级影响 → 暂停编码，先形成设计决策记录
```

### 不要直接修改主计划的情况

下面这些内容先进入 `IDEAS.md`：

- 临时想到的新功能；
- UI 小改动；
- 某个 Provider 的扩展想法；
- 尚未验证可行性的 Windows API；
- “以后也许可以做”的功能；
- 目前阶段不会实现的增强项。

### 应立即更新主文档的情况

下面这些属于已经确定的项目事实，需要直接回写主文档：

- 产品名称与定位发生变化；
- 最低 Windows 版本变化；
- 核心架构变化；
- Provider、Index、Property、Transfer 等模块职责变化；
- 已验证原技术路线不可行，需要更换 API 或实现方式；
- MVP 范围正式增加或删除功能；
- 安全边界或进程隔离规则发生变化。

### 建议新增的项目文档

```text
/docs
├─ PROJECT_PLAN.md        # 当前有效的总计划
├─ ARCHITECTURE.md        # 架构
├─ API_SELECTION.md       # Windows API 选择
├─ PROPERTY_SYSTEM.md     # 属性系统
├─ INDEX_DESIGN.md        # 索引设计
├─ PROVIDER_SPEC.md       # Provider 规范
├─ TRANSFER_SERVICE.md    # 复制/移动服务
├─ UI_UX_GUIDE.md         # UI 与 Explorer 操作兼容原则
├─ CODING_RULES.md        # Codex 必须遵守的编码规则
├─ IDEAS.md               # 尚未确认的新想法池
├─ DECISIONS.md           # 已确认的重大设计决策
└─ CHANGELOG.md           # 已落地变更记录
```

### 给 Codex 的变更规则

Codex 在执行项目时应遵守：

1. `PROJECT_PLAN.md` 与各专项设计文档是当前事实来源。
2. `IDEAS.md` 中的内容默认**不是开发任务**，除非明确标记为 Accepted。
3. 新想法如果会改变核心架构，先停止相关实现并输出影响分析。
4. 一个新功能只有在明确“接受”后，才允许从 `IDEAS.md` 合并到正式设计文档。
5. 已经进入实现阶段的模块，不因无关新想法随意重写。
6. 每次架构级变更都在 `DECISIONS.md` 记录“为什么改、改了什么、放弃了什么”。

推荐状态：

```text
Idea       只是想法
Review     正在评估
Accepted   已接受，等待进入计划
Planned    已进入正式开发计划
Doing      正在实现
Done       已完成
Rejected   不做
Deferred   延后
```

这样可以允许项目不断长出新想法，同时防止开发过程被“突然想到一个东西”反复打断。

## 40. 当前项目结论

第一阶段不追求“替代 Explorer 全部功能”。

第一阶段只需要证明五件事：

```text
1. 本地文件管理可以比 Explorer 更干净
2. 文件名搜索可以做到 Everything 式即时响应
3. 属性系统可以完整且独立
4. 网络 Provider 可以掉线而不拖死 UI
5. 第三方扩展可以崩溃而不拖死主进程
```

只要这五件事成立，项目方向就成立。

---

**项目名称：Blink**  
**仓库建议：BlinkFileManager**  
**主程序：Blink.exe**  
**当前版本：v0.2 计划书**  
**目标平台：Windows 11 25H2+ / Future Windows**
