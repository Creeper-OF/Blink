# Slice18 文件拖放

状态（2026-11-06恢复后）：完整Debug/Release构建及各1320 PASS/0 FAIL/4权限1314 SKIP，新增44项回归，209输入/338资源冻结。本轮拖放已完成开发与原生验证；真实UI/Explorer、悬停标签和Drop deferral交互仍按用户要求暂缓。主模型/compiler1，无子代理。最终证据见本轮续接记录。

External→Blink和Blink→Blink的实际复制/移动均交既有FileOperationCoordinator；Blink→External只发布标准文件系统数据，实际操作由外部目标拥有，来源发布和反馈从不自行删文件。按同卷Move/异卷Copy及Ctrl/Shift/Alt规则显示效果，链接操作不支持时明确反馈；复制/移动/冲突/部分完成复用现有引擎。

使用DataPackage的IDataObjectProvider与完整CF_HDROP，不使用会截断多文件的高层读取。自有40项普通interop探针实测GetStorageItemsAsync返回16，标准对象/视图raw/后台marshal均40，独立观察不计普通PASS。最初缺少头文件的编译失败保留，无产品窗口或全局clipboard操作。

文件区背景、文件夹行、This PC位置/驱动器、导航树文件夹和标签目标接入共同拖放控制器。目标不同于当前页时保留所属页面scope，用于冲突/重试，不因投放到子文件夹而强制导航；标签悬停激活及投放激活另按正常标签流程。批量多选和长/中文路径沿用有界codec和metadata捕获。

移动由目标完全执行，完成时反馈optimized move（performed=None、logical=Move），避免来源重复删。支持IDataObjectAsyncCapability的来源在后台持有operation直到真实完成/取消，失败待用户重试时保留；不支持的来源持Drop deferral等待，UI不做阻塞式等待。身份、旧callback、关闭/取消、重试lineage和Native动作先结束等沿用现有模型。

交付边界：完整Debug/Release构建和原生回归必须匹配最终209项输入；真实WinUI鼠标、Explorer双向拖放、悬停标签和Drop deferral交互仍列入暂缓人工验收，不能由windowless探针或原生测试替代。Merge/Replace、clipboard外部效果/剪切外观和后续远端不纳入本轮。

普通回归覆盖标准CF_HDROP完整40项中文路径、调用方拥有的独立HGLOBAL、DataPackage documented provider及当前运行时视图读取、跨apartment准备、内部原始身份、按键/同异卷效果选择、混合目录真实复制、外部非异步来源真实移动、optimized move反馈、异步开始/完成/取消、冲突保持、This PC/子目录目标所属页面和过期导航。来源发布及反馈不删除任何项目。
