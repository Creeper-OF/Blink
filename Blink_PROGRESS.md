# Blink 项目进度报告

> 项目：Blink  
> 定位：新时代 Windows 文件管理器  
> 当前阶段：Phase 0 — 技术验证准备  
> 最后更新：2026-08-09

---

## 1. 项目当前状态

Blink 已完成第一轮产品定位、功能边界和总体技术路线设计，当前尚未进入完整功能开发阶段。

当前核心目标不是立即实现完整文件管理器，而是先验证计划中的关键 Windows API、索引方案、属性系统、进程隔离和远程 Provider 架构是否可行。

当前状态：

- [x] 确定项目名称：Blink
- [x] 确定产品定位：新时代 Windows 文件管理器
- [x] 确定“不重写 Windows Shell，只重新做文件管理”的边界
- [x] 完成第一版项目计划书
- [x] 建立 `IDEAS.md` 想法池机制
- [x] 明确 Windows 11 新接口优先策略
- [x] 明确放弃 Win7 / Win8 / Win10 兼容包袱
- [x] 明确属性系统为一级核心模块
- [x] 明确 Everything 式索引为核心能力
- [x] 明确本地与远程文件统一 Provider 架构
- [x] 明确网络、预览、扩展等高风险组件需要隔离
- [ ] 建立正式代码仓库
- [ ] 完成 Phase 0 技术验证
- [ ] 开始 Phase 1 本地文件管理器开发

---

## 2. 当前产品定义

Blink 不是 Windows Explorer / Windows Shell 的完整替代品。

Blink 只负责具有明确“文件/目录语义”的内容：

- 本地 NTFS / ReFS 文件
- USB / 移动硬盘
- SMB
- SFTP
- WebDAV
- 压缩包
- 云文件
- MTP 设备文件
- 其他后续 File Provider

Blink 不负责：

- 控制面板
- 打印机
- 任务栏
- 开始菜单
- 系统设置
- 注册表管理
- 设备管理
- Windows Shell Namespace 中与文件无关的历史对象

核心原则：

> 只管理文件，不管理 Windows 的祖传家业。

---

## 3. 当前核心技术路线

### UI

计划采用：

- C++20/23
- C++/WinRT
- WinUI 3
- Windows App SDK
- MSIX

### 本地文件系统

计划重点验证：

- Windows 11 当前文件 API
- `CreateFile3`
- `CreateDirectory2`
- `DeleteFile2`
- `RemoveDirectory2`
- `GetFileInformationByHandleEx`
- `SetFileInformationByHandle`
- File ID / Volume ID
- Reparse Point
- Alternate Data Streams
- NTFS ACL

### 索引

目标：

> 文件系统负责保存文件，索引负责让用户找到文件。

计划：

- NTFS / ReFS USN Change Journal
- 初始索引
- 增量更新
- 文件名 / 路径 / 扩展名 / 大小 / 时间等快速检索
- 热路径尽量不依赖每次实时扫描
- 远程 Provider 使用独立缓存索引

### 属性系统

属性功能已经确定为一级核心能力，而不是后期附属窗口。

计划包含：

- 常规属性
- 文件系统属性
- Windows Property System
- EXIF / 媒体 / 版本信息
- NTFS ACL / Owner / DACL / 继承
- File ID
- Volume ID
- Streams
- Reparse Point
- Hard Links
- Compression
- Encryption
- Digital Signature

### Provider

统一 Provider 模型：

- Local Provider
- SFTP Provider
- SMB Provider
- WebDAV Provider
- Archive Provider
- Cloud Provider
- MTP Provider

UI 不直接关心文件来自哪种协议，只通过统一文件能力模型工作。

### 隔离

计划将以下高风险或高延迟模块从主 UI 进程隔离：

- Remote Provider
- Preview Host
- Metadata Host
- Extension Host
- Thumbnail Host（后续视实现决定）

目标：

> 网络死了，文件管理器不能跟着死。  
> 插件死了，文件管理器不能跟着死。  
> 预览器死了，文件管理器不能跟着死。

---

## 4. 当前优先级

### P0 — 必须验证

- [ ] WinUI 3 基础窗口可正常运行
- [ ] 本地目录快速枚举
- [ ] File ID 获取与文件身份跟踪
- [ ] USN Journal 初始索引
- [ ] USN Journal 增量更新
- [ ] 10 万文件搜索性能
- [ ] 100 万文件搜索性能
- [ ] 500 万文件搜索性能
- [ ] `IPropertyStore` 属性读取
- [ ] NTFS ACL 读取
- [ ] 文件复制与进度控制
- [ ] Provider 独立进程 IPC
- [ ] SFTP Provider 故意卡死时主 UI 不受影响
- [ ] SFTP Provider 断网时能够快速返回离线状态

### P1 — 第一版可用文件管理器

- [ ] 文件夹浏览
- [ ] 地址栏
- [ ] 面包屑路径
- [ ] 标签页
- [ ] 左侧导航树
- [ ] 图标视图
- [ ] 详细信息视图
- [ ] 多选 / 框选
- [ ] 复制 / 剪切 / 粘贴
- [ ] 移动
- [ ] 重命名
- [ ] 删除
- [ ] 回收站
- [ ] 拖放
- [ ] 排序
- [ ] 基础属性窗口
- [ ] 即时文件名搜索

### P2 — 核心差异化能力

- [ ] 完整属性系统
- [ ] Metadata Host 隔离
- [ ] Transfer Service
- [ ] 复制暂停 / 取消 / 重试
- [ ] SFTP 浏览
- [ ] SFTP 上传 / 下载
- [ ] SFTP 属性
- [ ] SFTP 离线缓存
- [ ] SFTP 搜索缓存
- [ ] 网络 Provider 强制超时与取消机制

### P3 — 后续扩展

- [ ] SMB Provider
- [ ] WebDAV Provider
- [ ] Archive Provider
- [ ] Cloud Provider
- [ ] MTP Provider
- [ ] Preview Host
- [ ] Extension Host
- [ ] 插件 API
- [ ] ARM64

---

## 5. 当前已确认的设计决策

### 决策 001：不追求旧 Windows 兼容

Blink 面向当前及未来 Windows。

原则：

- 不为了 Windows 7 增加 fallback
- 不为了 Windows 8 / 8.1 增加 fallback
- 不为了 Windows 10 降低核心架构
- 新接口存在时优先使用当前 Windows 推荐接口
- 只有当前 Windows 没有对应新能力时，才采用仍然受支持的传统 Win32 API

---

### 决策 002：文件属性不是附属功能

属性系统必须从项目早期建立。

原因：

- 索引依赖属性
- 排序依赖属性
- 搜索依赖属性
- 权限依赖属性
- 文件类型元数据依赖属性
- Provider 需要统一属性模型

---

### 决策 003：不允许网络资源阻塞主 UI

任何网络 Provider 必须：

- 异步执行
- 支持取消
- 支持 Deadline / Timeout
- 可独立崩溃
- 可重新启动
- 不允许 UI 线程同步等待远程服务器

---

### 决策 004：不直接继承完整 Shell Namespace

Blink 保留用户已经习惯的 Windows 文件操作逻辑，但不继承与文件无关的 Windows Shell 历史对象。

---

### 决策 005：Everything 式索引是核心架构，不是附加搜索框

目录浏览、搜索和文件定位应尽可能利用索引，而不是每次重新扫描文件系统。

---

## 6. 当前风险

### 风险 A：索引复杂度

需要确认：

- USN Journal 在目标文件系统上的可靠性
- Journal Reset / Wraparound 处理
- 离线磁盘重新接入后的索引恢复
- 大规模文件数量下内存占用
- File ID 与路径变化同步

状态：待 Phase 0 验证。

### 风险 B：Windows Property Handler

第三方 Property Handler 可能出现：

- 卡死
- 崩溃
- 非线程安全
- 读取超慢

处理方向：

> Metadata Host 独立进程隔离。

状态：架构已确定，待验证。

### 风险 C：远程协议

SFTP / SMB / WebDAV 在断网、DNS 故障、认证失败、服务器半连接状态下可能出现长时间等待。

处理方向：

- Provider Host
- Timeout
- Cancellation
- Last Known State
- Offline Cache

状态：待验证。

### 风险 D：AI 生成代码偏离架构

Codex 可能为了快速实现而出现：

- UI 线程直接做 I/O
- 把所有模块塞进一个进程
- 用路径字符串代替稳定文件身份
- 绕过 Provider
- 引入不必要的旧系统 fallback
- 为“快速跑起来”破坏隔离

处理方向：

> Codex 在修改核心架构前必须先读取项目计划和设计决策，不允许自行降低架构要求。

---

## 7. Phase 0 完成标准

只有满足以下条件，才进入正式 UI / 功能开发：

- [ ] Windows 文件 API 技术路线确认可行
- [ ] USN 索引 Prototype 可工作
- [ ] 百万级文件搜索性能达到可接受水平
- [ ] 属性系统 Prototype 可工作
- [ ] ACL Prototype 可工作
- [ ] File ID 跟踪方案确认
- [ ] 文件复制 Prototype 可暂停 / 取消
- [ ] Provider IPC Prototype 可工作
- [ ] SFTP Provider 卡死不会冻结 UI
- [ ] SFTP 离线能够及时返回
- [ ] 已记录 Phase 0 中发现的 API 限制
- [ ] 已修订正式 `PROJECT_PLAN.md`
- [ ] 已建立第一版 `ARCHITECTURE.md`
- [ ] 已建立第一版 `API_SELECTION.md`

---

## 8. Codex 每次完成任务后的报告格式

Codex 每完成一个阶段或明显功能后，需要更新本文件。

使用以下格式：

```md
## YYYY-MM-DD — 任务名称

### 完成内容

- 完成了什么
- 修改了哪些模块
- 新增了哪些文件

### 当前结果

- 是否可编译
- 是否可运行
- 已测试什么
- 性能数据

### 未解决问题

- Bug
- 技术限制
- 临时实现
- TODO

### 架构变化

如果没有：

无。

如果有：

- 原方案
- 新方案
- 为什么修改
- 影响范围

### 下一步

- 下一项任务
```

---

## 9. 进度报告维护规则

本文件只记录：

- 已完成内容
- 当前正在做的内容
- 当前阻塞
- 已确认风险
- 下一步

不要把未经确认的脑洞直接写入正式进度。

新想法：

> 写入 `IDEAS.md`

架构级变更：

> 写入 `DECISIONS.md`

确定进入开发计划：

> 更新 `PROJECT_PLAN.md`

已经实际完成：

> 更新 `PROGRESS.md`

因此四类文档职责为：

```text
PROJECT_PLAN.md
    ↓
我们准备做什么

IDEAS.md
    ↓
我们可能想做什么

DECISIONS.md
    ↓
为什么这么做

PROGRESS.md
    ↓
实际上已经做到哪了
```

---

## 10. 当前下一步

当前推荐执行顺序：

1. 创建 Blink 正式仓库。
2. 将项目计划书、IDEAS 和本进度报告放入 `docs/`。
3. 让 Codex 先审查技术路线，不立即写完整产品。
4. 建立 Phase 0 Prototype。
5. 优先验证：
   - WinUI 3
   - File ID
   - USN Journal
   - Property System
   - ACL
   - Provider IPC
   - SFTP 故障隔离
6. 根据验证结果修订架构。
7. Phase 0 通过后正式进入 Blink 本地文件管理器开发。

---

## 11. 一句话状态

> **Blink 当前已经完成“想清楚要造什么”，下一阶段开始验证“这套东西到底应该怎么造”。**
