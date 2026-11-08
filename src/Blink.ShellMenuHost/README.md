# Blink.ShellMenuHost

普通用户独立Win32 STA进程，通过文件系统IContextMenu承载Windows完整经典右键菜单及已注册的第三方Shell扩展。扩展DLL只加载到此helper，主窗口使用共享的轻量ShellContextMenu协议和原文件操作入口。

## 构建与CLI

由主模型统一构建Blink.sln，输出`x64/<Configuration>/Blink.ShellMenuHost.exe`，再随Blink发布；项目GUID末尾为`B110`。本目录不安装或注册任何扩展。

```text
Blink.ShellMenuHost.exe --path <full-path> --identity <FileIdentity.ToString> --parent-identity <parent.ToString> --x <screen-X> --y <screen-Y> --owner <HWND-as-uintptr> --locale zh-CN|en-US [--extended] [--cancel-handle <inherited-event>] [--verb properties] [--additional-path <full-path> --additional-identity <FileIdentity.ToString>]...
```

普通目录的多选通过成对 `--additional-path` / `--additional-identity` 传入；全部条目必须属于同一父目录，保留不同硬链接名称，通过同一个 IContextMenu 提供真实多选系统扩展。单选及搜索调用仍沿用原参数。路径与父目录身份在创建菜单前及选择命令后核对。坐标支持负值；owner仅作为前台提示，helper窗口不依附Blink窗口的生命周期。`--verb properties`不弹菜单，直接交给Windows完整属性实现。

普通模式只输出一条ASCII回复：

```text
BLINK_SHELL_MENU_V1 <cancel|blink|native|error> <decimal-command> <8-hex-HRESULT>
```

`blink`的command值与共享`blink::app::ShellMenuCommand`一致：1完整路径、2所在目录路径、3定位、4粘贴到所在目录、5打开、6复制、7剪切、8重命名、9删除、10永久删除。其它kind的command为0；cancel/native和成功blink回包为S_OK。原生canonical open/copy/cut/paste/rename/delete回到Blink，clipboard使用已有eager CF_HDROP，delete在选择时按Shift区分。原生paste和尾部便利项统一为“粘贴到所在目录”，没有CF_HDROP时禁用。

native回复在InvokeCommand之前写出，仅表示已选择交给Windows，不表示完成。父进程收到回复即返回，不等待EOF或helper退出；stdout保留至进程自然结束，避免第三方CRT缓存的标准句柄被提前关闭。Invoke失败由helper显示真实HRESULT。父进程关闭或换标签通过继承的cancel event取消尚未选择的菜单，选择后注销监视并关闭事件副本，不终止已开始的操作。

## Shell生命周期

QueryContextMenu使用正常item菜单，Shift增加扩展verbs。自有窗口转发IContextMenu3/2菜单消息，原生调用使用合法独立HWND、Unicode、调用坐标及ASYNCOK。线程通过SHSetThreadRef注册带事件通知的IUnknown，Invoke返回后先销毁菜单并释放本次菜单/文件夹引用，再保持STA消息泵，等待Shell线程引用和helper进程的可见操作窗口都结束。modeless对话框继续处理键盘消息；跨线程窗口可见时使用普通窗口timer观察关闭。不设置操作强杀超时，也不把子应用放进随Blink退出而终止的job。

## 非交互探针

在上述路径和身份参数后加`--probe`，只枚举原生命令/label，不弹菜单、不调用Invoke。每项输出独立JSONL：

```text
BLINK_SHELL_MENU_PROBE_V1 {"event":"item","label":"...","verb":"...","id":1,"depth":0,"submenu":false,"owner_draw":false,"enabled":true}
BLINK_SHELL_MENU_PROBE_V1 {"event":"done","count":42,"error":"00000000"}
```

探针会初始化可用的级联子菜单以列出动态项。owner-drawn扩展可能不公开字符串label，仍保留该标记、命令ID与可用canonical verb。探针成功不能代替真实点击、第三方压缩和属性窗口的人工验收。

## 微软依据

- [IContextMenu::QueryContextMenu](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-querycontextmenu)：正常菜单可包含扩展，提供Shell命令ID范围。
- [IContextMenu3::HandleMenuMsg2](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu3-handlemenumsg2)：转发owner-draw与级联菜单消息。
- [IContextMenu::InvokeCommand](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icontextmenu-invokecommand)及[CMINVOKECOMMANDINFOEX](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/ns-shobjidl_core-cminvokecommandinfoex)：Unicode、有效owner窗口及异步调用要求。
- [Managing Thread References](https://learn.microsoft.com/en-us/windows/win32/shell/managing-thread-references)：Shell线程引用用于保留异步工作需要的调用线程；只设置ASYNCOK不足以管理其生命周期。
