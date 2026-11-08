[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release', [string]$Label='fixed', [ValidateSet('SendInput','WindowMessage')][string]$InputMode='WindowMessage')
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$OutputEncoding=[Console]::OutputEncoding
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Ordinary user required'}
$project=Split-Path -Parent $PSScriptRoot
$runId=(Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')+'-'+$PID
$runRoot=Join-Path $project ('.build\navigation-wheel\'+$Label+'-'+$Configuration.ToLowerInvariant()+'-'+$runId)
$fixtureName='wheel-fixture-'+$runId
$fixture=Join-Path $runRoot $fixtureName
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
foreach($n in 1..80){New-Item -ItemType Directory -Path (Join-Path $fixture ('folder-{0:D3}' -f $n)) | Out-Null}
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BlinkNavigationWheelInput {
 [StructLayout(LayoutKind.Sequential)] public struct Point {public int X,Y;}
 [StructLayout(LayoutKind.Sequential)] struct Rect {public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")] static extern bool SystemParametersInfo(uint action,uint param,out Rect rect,uint flags);
 [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr window,int command);
 [DllImport("user32.dll")] static extern bool MoveWindow(IntPtr window,int x,int y,int width,int height,bool repaint);
 public static void PlaceWindow(IntPtr window){ShowWindow(window,9);Rect work;if(!SystemParametersInfo(0x0030,0,out work,0))throw new Exception("Work area unavailable");if(!MoveWindow(window,work.Left+16,work.Top+16,Math.Min(1200,work.Right-work.Left-32),Math.Min(850,work.Bottom-work.Top-32),true))throw new Exception("Window placement failed");}
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
 [DllImport("user32.dll")] public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")] static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point p);
 public static string HitWindow(){Point point;GetCursorPos(out point);uint pid;var window=WindowFromPoint(point);var thread=GetWindowThreadProcessId(window,out pid);return window+"/thread="+thread+"/pid="+pid;}
 [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hWnd,out uint pid);
 [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string className,string title);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,System.Text.StringBuilder value,int size);
 static IntPtr InputSite(IntPtr parent){IntPtr child=IntPtr.Zero;while((child=FindWindowEx(parent,child,null,null))!=IntPtr.Zero){var name=new System.Text.StringBuilder(128);GetClassName(child,name,128);if(name.ToString()=="InputSiteWindowClass")return child;var nested=InputSite(child);if(nested!=IntPtr.Zero)return nested;}return IntPtr.Zero;}
 public static void EnterMessage(int pid){var window=InputSite(OwnedWindow);if(window==IntPtr.Zero)throw new Exception("Owned input site unavailable");uint owner;GetWindowThreadProcessId(window,out owner);if(owner!=(uint)pid)throw new Exception("Owned input site required");UIntPtr result;if(SendMessageTimeout(window,0x100,new UIntPtr(13),new IntPtr(1),2,3000,out result)==IntPtr.Zero)throw new Exception("Owned Enter down failed");if(SendMessageTimeout(window,0x101,new UIntPtr(13),new IntPtr(unchecked((int)0xC0000001)),2,3000,out result)==IntPtr.Zero)throw new Exception("Owned Enter up failed");}
 [DllImport("user32.dll")] static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
 [StructLayout(LayoutKind.Sequential)] struct MouseInput { public int dx,dy;public uint data,flags,time;public UIntPtr extra; }
 [StructLayout(LayoutKind.Sequential)] struct Input { public uint type;public MouseInput mouse; }
 [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint count,Input[] inputs,int size);
 [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);
 static void Mouse(uint flags,int delta,int x=0,int y=0){var input=new Input{type=0,mouse=new MouseInput{flags=flags,data=unchecked((uint)delta),dx=x,dy=y}};if(SendInput(1,new[]{input},Marshal.SizeOf(typeof(Input)))!=1)throw new Exception("SendInput failed: "+Marshal.GetLastWin32Error());}
 public static bool UseWindowMessage;
 public static IntPtr OwnedWindow;
 static Point messagePoint;
 public static Point TargetPoint(){if(UseWindowMessage)return messagePoint;Point point;GetCursorPos(out point);return point;}
 [DllImport("user32.dll",SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,UIntPtr wParam,IntPtr lParam,uint flags,uint timeout,out UIntPtr result);
 public static bool OwnsForeground(int pid){uint owner;GetWindowThreadProcessId(GetForegroundWindow(),out owner);return owner==(uint)pid;}
 public static void Enter(int pid){if(!OwnsForeground(pid))throw new Exception("Owned foreground window required");keybd_event(13,0,0,UIntPtr.Zero);keybd_event(13,0,2,UIntPtr.Zero);}
 public static void Move(int pid,int x,int y,bool foreground=true){
  if(UseWindowMessage){uint messageOwner;GetWindowThreadProcessId(OwnedWindow,out messageOwner);if(messageOwner!=(uint)pid)throw new Exception("Owned message window required");messagePoint=new Point{X=x,Y=y};return;}
  if(foreground && !OwnsForeground(pid))throw new Exception("Owned foreground window required");
  int left=GetSystemMetrics(76),top=GetSystemMetrics(77),width=GetSystemMetrics(78),height=GetSystemMetrics(79);
  if(!UseWindowMessage)Mouse(0xC001,0,(int)Math.Round((x-left)*65535.0/(width-1)),(int)Math.Round((y-top)*65535.0/(height-1)));
  Point point;var deadline=DateTime.UtcNow.AddMilliseconds(500);
  do{GetCursorPos(out point);if(Math.Abs(point.X-x)<=2 && Math.Abs(point.Y-y)<=2)break;System.Threading.Thread.Sleep(20);}while(DateTime.UtcNow<deadline);
  uint owner;GetWindowThreadProcessId(WindowFromPoint(point),out owner);
  if(owner!=(uint)pid || Math.Abs(point.X-x)>2 || Math.Abs(point.Y-y)>2)throw new Exception("Owned mouse target required; actual="+point.X+","+point.Y+" expected="+x+","+y+" pid="+owner+" virtual="+left+","+top+","+width+","+height);
 }
 public static void SendWheel(int pid,int delta){
  if(!UseWindowMessage && !OwnsForeground(pid))throw new Exception("Owned foreground window required");
  if(UseWindowMessage){
   Point point=messagePoint;var window=OwnedWindow;uint owner;GetWindowThreadProcessId(window,out owner);
   if(owner!=(uint)pid)throw new Exception("Owned wheel target required");
   UIntPtr result;var wParam=new UIntPtr(unchecked((uint)(delta<<16))|0u);var lParam=new IntPtr(unchecked((point.Y<<16)|(point.X&0xffff)));
   if(SendMessageTimeout(window,0x020Au,wParam,lParam,2,3000,out result)==IntPtr.Zero)throw new Exception("Window wheel message failed: "+Marshal.GetLastWin32Error());
  }else{Mouse(0x0800,delta);}
 }
 public static void ClickBlank(int pid,int x,int y){Move(pid,x,y);Mouse(0x0002,0);Mouse(0x0004,0);}
 public static void Restore(Point p){SetCursorPos(p.X,p.Y);}
}
'@
[BlinkNavigationWheelInput]::UseWindowMessage=($InputMode -ne 'SendInput')
$originalCursor=New-Object BlinkNavigationWheelInput+Point
[BlinkNavigationWheelInput]::GetCursorPos([ref]$originalCursor) | Out-Null
$dpiContext=[BlinkNavigationWheelInput]::SetThreadDpiAwarenessContext([IntPtr](-4))
$process=$null;$failure=$null;$forced=$false;$exitCode=$null
$script:outerPattern=$null;$script:outerElement=$null
$actions=[Collections.Generic.List[object]]::new()
$observations=[Collections.Generic.List[object]]::new()
function Window {
 $process.Refresh();if($process.HasExited){throw ('Blink exited: '+$process.ExitCode)}
 if($process.MainWindowHandle -eq [IntPtr]::Zero){return $null}
 return [Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)
}
function Find($parent,$property,$value){
 if($null -eq $parent){return $null}
 return $parent.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new($property,$value))
}
function OfType($parent,$type){
 if($null -eq $parent){return @()}
 return @($parent.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,$type)))
}
function WaitFor([string]$name,[scriptblock]$predicate,[int]$seconds=25){
 $deadline=[DateTime]::UtcNow.AddSeconds($seconds);$last=''
 do {
  $process.Refresh();if($process.HasExited){throw ('Blink exited during '+$name)}
  try{if(& $predicate){$actions.Add([pscustomobject]@{action=$name;passed=$true});return}}catch{$last=$_.Exception.Message}
  [Threading.Thread]::Sleep(150)
 }while([DateTime]::UtcNow -lt $deadline)
 throw ('Timed out: '+$name+'; '+$last)
}
function Address {return @(OfType (Window) ([Windows.Automation.ControlType]::Edit) | Where-Object {$_.Current.IsEnabled})[0]}
function OuterScroll {
 if($script:outerElement){return $script:outerElement}
 $window=Window
 $named=Find $window ([Windows.Automation.AutomationElement]::AutomationIdProperty) 'NavigationPane'
 if($named){return $named}
 $candidates=@($window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ClassNameProperty,'ScrollViewer')))
 foreach($candidate in $candidates){
  $pattern=$null
  if($candidate.TryGetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern,[ref]$pattern) -and $pattern.Current.VerticallyScrollable -and $candidate.Current.BoundingRectangle.Width -lt 400){return $candidate}
 }
 return $null
}
function ScrollSnapshot {
 $window=Window
 $result=@()
 $candidates=@($window.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ClassNameProperty,'ScrollViewer')))
 foreach($candidate in $candidates){
  $pattern=$null
  if($candidate.TryGetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern,[ref]$pattern)){
   $rect=$candidate.Current.BoundingRectangle
   $result+=[pscustomobject]@{id=$candidate.Current.AutomationId;left=$rect.Left;top=$rect.Top;width=$rect.Width;height=$rect.Height;vertical=$pattern.Current.VerticallyScrollable;percent=$pattern.Current.VerticalScrollPercent;view_size=$pattern.Current.VerticalViewSize}
  }
 }
 return $result
}
function Percent {return $script:outerPattern.Current.VerticalScrollPercent}
function WheelCheck([string]$name,[int]$delta,[double]$relativeY){
 $pane=OuterScroll;$rect=$pane.Current.BoundingRectangle
 $before=Percent
 [BlinkNavigationWheelInput]::Move($process.Id,[int]($rect.Left+$rect.Width*0.60),[int]($rect.Top+$rect.Height*$relativeY))
 $point=[BlinkNavigationWheelInput]::TargetPoint()
 $observations.Add([pscustomobject]@{action='wheel target';x=$point.X;y=$point.Y;native_window=$process.MainWindowHandle.ToInt64();control=[BlinkNavigationWheelInput]::GetAsyncKeyState(17);shift=[BlinkNavigationWheelInput]::GetAsyncKeyState(16);alt=[BlinkNavigationWheelInput]::GetAsyncKeyState(18)})
 $timer=[Diagnostics.Stopwatch]::StartNew()
 [BlinkNavigationWheelInput]::SendWheel($process.Id,$delta)
 WaitFor $name { $after=Percent; $(if($delta -lt 0){$after -gt $before+0.05}else{$after -lt $before-0.05}) } 3
 $observations.Add([pscustomobject]@{action=$name;before=$before;after=(Percent);wheel_delta=$delta;relative_y=$relativeY;response_observed_ms=$timer.Elapsed.TotalMilliseconds})
}
try {
 $psi=[Diagnostics.ProcessStartInfo]::new()
 $psi.FileName=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')
 $psi.WorkingDirectory=Split-Path -Parent $psi.FileName;$psi.UseShellExecute=$false
 $psi.EnvironmentVariables['BLINK_STARTUP_TELEMETRY']=Join-Path $runRoot 'startup.log'
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi
 if(-not $process.Start()){throw 'Blink start failed'}
 WaitFor 'startup ready' { $null -ne (Address) -and @(OfType (Window) ([Windows.Automation.ControlType]::Text) | Where-Object {$_.Current.Name -match '(?:Ready|就绪).*locations|(?:就绪).*位置'}).Count -gt 0 }
 [BlinkNavigationWheelInput]::OwnedWindow=$process.MainWindowHandle
 [BlinkNavigationWheelInput]::PlaceWindow($process.MainWindowHandle)
 [BlinkNavigationWheelInput]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
 $address=Address;$address.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($fixture);$address.SetFocus()
 if($InputMode -eq 'SendInput'){
  WaitFor 'owned foreground acquired for fixture navigation' {
   $address.SetFocus()
   [BlinkNavigationWheelInput]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
   [BlinkNavigationWheelInput]::OwnsForeground($process.Id)
  } 5
  [BlinkNavigationWheelInput]::Enter($process.Id)
 }else{[BlinkNavigationWheelInput]::EnterMessage($process.Id)}
 WaitFor 'fixture appears in navigation tree' { $null -ne (Find (Window) ([Windows.Automation.AutomationElement]::NameProperty) $fixtureName) }
 $text=Find (Window) ([Windows.Automation.AutomationElement]::NameProperty) $fixtureName
 $treeItem=$text
 while($treeItem -and $treeItem.Current.ControlType -ne [Windows.Automation.ControlType]::TreeItem){$treeItem=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($treeItem)}
 if(-not $treeItem){throw 'Fixture tree item not found'}
 $treeItem.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand()
 WaitFor 'expanded tree overflows navigation viewport' { $null -ne (OuterScroll) -and (OuterScroll).GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern).Current.VerticallyScrollable -and $null -ne (Find (Window) ([Windows.Automation.AutomationElement]::NameProperty) 'folder-080') }
 $script:outerElement=OuterScroll
 $script:outerPattern=$script:outerElement.GetCurrentPattern([Windows.Automation.ScrollPattern]::Pattern)
 $scroll=$script:outerPattern
 $observations.Add([pscustomobject]@{action='scroll viewers before wheel';scrolls=@(ScrollSnapshot)})
 (Address).SetFocus()
 $scroll.SetScrollPercent(-1,0)
 WaitFor 'outer navigation at top' { (Percent) -lt 0.05 }
 $paneRect=(OuterScroll).Current.BoundingRectangle
 if($InputMode -eq 'SendInput'){[BlinkNavigationWheelInput]::ClickBlank($process.Id,[int]($paneRect.Left+12),[int]($paneRect.Top+4))}
 WheelCheck 'wheel down over tree rows scrolls outer pane' -120 0.75
 $oneTick=(Percent)
 WheelCheck 'wheel up over tree rows scrolls outer pane' 120 0.75
 WheelCheck 'wheel down over quick locations scrolls outer pane' -120 0.10
 WheelCheck 'wheel up over quick locations scrolls outer pane' 120 0.10
 $before=Percent;$rect=(OuterScroll).Current.BoundingRectangle
 [BlinkNavigationWheelInput]::Move($process.Id,[int]($rect.Left+$rect.Width*0.60),[int]($rect.Top+$rect.Height*0.75))
 foreach($n in 1..8){[BlinkNavigationWheelInput]::SendWheel($process.Id,-120)}
 WaitFor 'rapid wheel burst accumulates all eight ticks' { (Percent) -ge $before+$oneTick*7.9 -and (Percent) -le $before+$oneTick*8.1 } 3
 $observations.Add([pscustomobject]@{action='rapid wheel burst';before=$before;after=(Percent);ticks=8;expected=$before+$oneTick*8})
 $before=Percent
 foreach($n in 1..4){[BlinkNavigationWheelInput]::SendWheel($process.Id,-30)}
 WaitFor 'four fractional ticks equal one full tick' { (Percent) -ge $before+$oneTick*0.95 -and (Percent) -le $before+$oneTick*1.05 } 3
 $scroll.SetScrollPercent(-1,0)
 WaitFor 'scrollbar can reset navigation to top after wheel' { (Percent) -lt 0.05 }
 [BlinkNavigationWheelInput]::SendWheel($process.Id,120)
 WaitFor 'wheel clamps at top' { (Percent) -lt 0.05 }
 $scroll.SetScrollPercent(-1,100)
 WaitFor 'scrollbar can reach bottom' { (Percent) -gt 99.95 }
 [BlinkNavigationWheelInput]::SendWheel($process.Id,-120)
 WaitFor 'wheel clamps at bottom' { (Percent) -gt 99.95 }
 WaitFor 'wheel keeps committed location unchanged' { (Address).GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -eq $fixture }
}catch{$failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{$observations.Add([pscustomobject]@{action='scroll viewers at failure';scrolls=@(ScrollSnapshot)})}catch{}}
finally {
 if($process){
  $process.Refresh();if(-not $process.HasExited){$process.CloseMainWindow() | Out-Null;if(-not $process.WaitForExit(5000)){$forced=$true;$process.Kill();$process.WaitForExit(5000)|Out-Null}}
  $process.Refresh();if($process.HasExited){$exitCode=$process.ExitCode}
  if($null -eq $failure -and ($forced -or $exitCode -ne 0)){$failure='Blink did not close normally'}
 }
 [BlinkNavigationWheelInput]::Restore($originalCursor)
 [BlinkNavigationWheelInput]::SetThreadDpiAwarenessContext($dpiContext) | Out-Null
 $report=[ordered]@{configuration=$Configuration;label=$Label;input_mode=$InputMode;fixture=$fixture;ordinary_user=$true;actions=@($actions.ToArray());scroll_observations=@($observations.ToArray());passed=($null -eq $failure);failure=$failure;exit_code=$exitCode;forced_termination=$forced;process_id=$(if($process){$process.Id}else{$null});binary_sha256=(Get-FileHash -LiteralPath $psi.FileName -Algorithm SHA256).Hash}
 $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $runRoot 'report.json') -Encoding UTF8
 Write-Output ($report | ConvertTo-Json -Depth 8)
}
if($failure){exit 1}
