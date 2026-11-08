# 索引服务启用入口

`scripts/enable-index-service.ps1`供普通Blink窗口申请UAC后调用。界面必须传入原普通用户的`AuthorizedClientSid`，不能以提权账户的SID替代。默认使用Release服务产物；未显式指定配置且Release不存在时才尝试Debug。

启用入口复用根目录安装脚本，显式选择已经验收过的LocalSystem配置。新安装的数据放在本项目`.blink-data`；已有服务的可执行文件和DataRoot均属于本项目时保留原DataRoot，需要修改配置才停止并重新启动服务。另一个项目位置注册的同名服务会明确失败，保留其注册和数据。原安装脚本的高完整性判断使用数字SID，避免系统语言导致误判。

用户主动启用后，本项目索引服务设为随系统自动启动，避免重启后每次打开Blink都重复申请UAC。关闭主窗口后服务继续处理已加载卷的增量更新；用户没有初始化的卷不会被脚本自动建立索引。

脚本只安装、配置和启动服务，不生成、删除或重建索引。服务就绪后，所选卷的首次索引由界面通过现有`StartInitialIndex` IPC命令提交；索引仍存放在服务DataRoot的`index/<drive>.blink5`。

进程退出0表示服务已到Running，非零表示失败。常见退出码：5表示未获提权，2表示缺少产物或安装脚本，87表示输入无效，183表示同名服务归属无法确认，1460表示服务状态等待超时；其它失败保留原生错误码或返回1。UAC取消由调用方接收，脚本自身不再次申请提权。语法检查和普通权限失败路径不能代替实际服务运行验证。

安装过程将最新记录写入项目`.blink-data/logs/index-service-setup.log`，重复启用覆盖旧记录。可选`TelemetryRoot`使用独立的未限定类型变量解析；省略此参数时不再把空字符串传给`GetFullPath`。界面使用系统正常提权入口，是否出现确认窗口由Windows当前UAC设置决定。

2026-11-07收尾：已通过生产入口启用本项目自动启动服务，并完成C/D首次建库、服务重启后复用已有库、真实联合搜索及D盘创建/改名/删除后的结果收敛验证。当前Release产物已更新到默认启动目录；实际WinUI操作验收仍暂缓。见[修复记录](INDEX_REPAIR_HANDOFF.md)和`.build/index-repair/integration/native-validation.json`。
