# Blink 本地运行环境

项目根目录是：

```text
D:\Work\project\Blink_文件管理器
```

## 使用方式

在项目根目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

发布构建：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release
```

只还原依赖：

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1 -Action Restore
```

历史记录（2026-10-14）：当时功能开发采用单个轻量子代理、Windows 原生构建且每次传入 `-MaxCpuCount 1`，构建、普通回归与 UI 验证逐项运行，结果落在 `.build/phase1b-slice5`。

构建入口默认将项目和编译器并行数各限制为 1，避免 WinUI 生成头文件在 16 核、约 14 GiB 内存的本机并行编译时触发 C1060。需要调整时可显式使用 `-MaxCpuCount`；本项目当前开发保持 1；这个参数同时控制 MSBuild `/m` 与 C++ `CL_MPCount`，不修改系统环境。

`Directory.Build.props` 显式使用 `/utf-8`，确保 UTF-8 源码中的中文字符串不受本机 ACP 936（GBK）影响。主程序的 `App` 实现在 `src/main.cpp` 内定义；生成的 `XamlTypeInfo.g.cpp` 包含 App 启动工厂，因此两个 `XamlTypeInfo*.g.cpp` 都在 main.cpp 中包含并编译一次，生成列表在 MarkupCompilePass2 后去除这两个独立编译项，避免重复编译/链接。不要手改忽略目录中的生成文件。

构建使用 `/restore /t:Rebuild`，让 NuGet 还原后重新评估生成的 props/targets，再编译。不要在同一评估中串联 `Restore;Rebuild`；临时 ASCII junction 每轮不同，旧 `Pkg*` 路径可能造成 WMC1006。大型 XAML metadata 头只强制包含在 App 和生成工厂等实际需要的编译项中，普通 model/controller 源文件使用自己的头。依据：[NuGet 的还原/构建评估说明](https://learn.microsoft.com/en-us/nuget/reference/msbuild-targets#restoring-and-building-with-one-msbuild-command)。

## 目录归属

构建脚本会把可由项目控制的目录放在仓库内：

```text
.nuget\packages          NuGet 全局包目录
.nuget\http-cache        NuGet HTTP 缓存
.nuget\scratch           NuGet 临时协调目录
.nuget\plugins-cache     NuGet 插件缓存
.build\tmp               本次构建进程的 TEMP/TMP
.blink-data              Blink 运行时数据与应用缓存预留目录
.vs                      Visual Studio 解决方案缓存
src\*\obj                C++/MSBuild 中间文件
src\Blink\x64\Debug / Release 主程序与 self-contained 运行库
x64\Debug / x64\Release       库、测试与隔离 helper 构建产物
```

这些目录都已加入 `.gitignore`，不会进入版本库。`build.ps1` 设置的环境变量只对子进程有效，不会改变系统全局环境变量。测试程序不再默认使用系统 TEMP，而是使用项目内 `.build\phase0-test`。

## 清理

项目提供了安全的本地清理脚本：

```powershell
powershell -ExecutionPolicy Bypass -File .\clean-local.ps1
```

默认清理构建产物、IDE 状态、运行时缓存和 NuGet HTTP/临时缓存；不会删除 NuGet 包。确认需要重新下载依赖时，再使用：

```powershell
powershell -ExecutionPolicy Bypass -File .\clean-local.ps1 -IncludePackages
```

清理这些目录不会影响源码；代价只是下次构建需要重新编译，使用 `-IncludePackages` 后还需要重新下载 NuGet 包。

## 仍然属于系统环境的部分

2026-10-13 已按用户授权恢复工具链，官方安装器的 Microsoft Authenticode 签名验证通过，安装退出码均为 0，无须重启：

- `D:\DEV\C++\BuildTools`：Visual Studio Build Tools 2026 `18.10.3`，提供 MSBuild `18.10.1.42706`。
- `D:\DEV\C++\BuildTools2022`：完整的 MSVC v143 构建规则和 `14.44.35207` 工具目录。2026 组件提供旧编译器但缺少 v143 的 `Toolset.props`，首轮构建报 MSB8020，故并行安装 2022 工具集；使用 2026 MSBuild 的官方工具集发现机制解析 v143。
- `D:\DEV\C++\Cache` / `Shared` / `installers` / `logs`：安装缓存、共享组件、原始安装器与持久日志。详细目录说明见 `D:\DEV\C++\README.md`。
- Windows SDK `10.0.26100.0` 的实际注册根目录为 `C:\Program Files (x86)\Windows Kits\10`。SDK 和安装器的部分组件使用系统规定的路径；主编译工具与安装缓存留在 D 盘。

`build.ps1` 优先识别新的 D 盘 MSBuild 路径。首次缺工具链的日志保留在 `.build/phase1b-slice4/build-debug.log`，2026 单独安装后缺 v143 规则的日志在 `build-debug-installed.log`。安装结果不代替源码验收；当前构建、测试和 UI 验收状态仍以 `PROGRESS.md` 为准。

Visual Studio Build Tools、MSVC、Windows SDK 和 Windows App SDK 所需的系统组件通过正式安装提供，不能仅靠项目文件搬到仓库内。主程序已启用 Windows App SDK self-contained，运行时文件会随主程序构建产物落在 `src\Blink\x64\<Configuration>` 下。

原来的 `%USERPROFILE%\.nuget\packages` 没有自动删除，因为它可能被其他项目使用。Blink 后续通过 `build.ps1` 使用 D 盘自己的包目录；不建议直接在 Visual Studio 中执行还原来替代这个入口，因为 NuGet 的 HTTP/临时缓存需要由脚本的进程环境重定向。
## Phase 0.5 service data and installation

`Blink.IndexService` stores its checkpoint under `.blink-data\index` when installed with `install-index-service.ps1`. The installer receives the project path explicitly, so the service index and its temporary files remain on D:. Installing/registering the Windows service is the only UAC-required operation.

Build and run the non-privileged console harness without changing the machine service table:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
& .\x64\Debug\Blink.Phase05.Tests.exe
```

The product entry now uses `scripts/enable-index-service.ps1` through the Blink index-status controls. It selects the previously accepted LocalSystem configuration, retains this project's DataRoot, and sets automatic startup. The original installer's default LocalService account remains a historical prototype candidate; it cannot read this machine's USN journal. Actual C/D service validation and normal startup are described in [INDEX_REPAIR_HANDOFF.md](INDEX_REPAIR_HANDOFF.md).

历史限制（2026-10-15）：当时禁止子代理，全部由主模型执行，构建入口默认并发改为1。用户已于2026-11-06重新允许按独立模块使用子代理；主模型集成验证，构建仍保持单并发。联网还原停滞时，开发记录中的cached wrapper先核对锁定版本与现有assets/cache，再临时保留两个已核验ASCII别名供旧NuGet绝对路径使用，结束验证目标后移除；没有新增依赖或全局修改NuGet环境。早期证据在`.build/phase1d-slice1/cached-package-validation.json`和`native-validation.json`，本轮在`.build/index-repair/integration`。
