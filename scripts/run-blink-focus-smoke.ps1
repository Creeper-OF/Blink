[CmdletBinding()]
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$Binary='',[ValidateRange(5,90)][int]$StepTimeoutSeconds=30,[ValidateSet('All','Remaining')][string]$Phase='All')
# Only a new owned PID/HWND and GUID fixture; no global input, Clipboard,
# service/configuration edits, forced termination, or claimed real IME coverage.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
$integration=Join-Path $project '.build\explorer-focus\integration'
New-Item -ItemType Directory -Path $integration -Force|Out-Null
$reportPath=Join-Path $integration 'ui-focus-report.json'
if($Phase -eq 'Remaining'){$reportPath=Join-Path $integration 'ui-focus-remaining-report.json'}
if(-not $Binary){$Binary=Join-Path $project ('src\Blink\x64\'+$Configuration+'\Blink.exe')}
$Binary=[IO.Path]::GetFullPath($Binary)
$guid=[Guid]::NewGuid().ToString('N');$token=$guid.Substring(0,8)
$fixture=Join-Path $integration ('ui-fixtures\owned-'+$guid)
$source=Join-Path $fixture '目录';$empty=Join-Path $fixture 'empty'
$marker='focus-'+$token+'.txt';$file=Join-Path $source $marker;$payload='Blink focus '+$guid
$allowedDirectories=@($fixture,$source,$empty)
$compilerTemp=Join-Path $integration ('compiler-temp\'+$guid);$oldTemp=$env:TEMP;$oldTmp=$env:TMP
$process=$null;$fixtureCreated=$false;$tempChanged=$false;$script:windowCache=$null;$script:currentStep='preflight'
$checks=[Collections.Generic.List[object]]::new()
$report=[ordered]@{status='RUNNING';passed=$false;configuration=$Configuration;binary=$Binary;binary_sha256=$null;started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;process_id=$null;process_exited=$false;exit_code=$null;forced_termination=$false;checks=@();current_step='preflight';failure=$null;focus_evidence=@();text_selection_evidence=@();environment=$null;fixture=$fixture;fixture_removed=$false;fixture_retained=$false;fixture_bytes_removed=0;compiler_temp_removed=$false;desktop_preflight=$null;last_ui_snapshot=$null;initial_file_identity=$null;real_search_evidence=$null;not_run=@('real IME composition and candidate UI','UNC/network','full theme/DPI/multi-monitor/Narrator matrix','all native ComboBox/menu key combinations');input_mode='Owned PID/HWND UIA and InputSite keyboard messages only'}
function Save-Report{$report.current_step=$script:currentStep;$report.checks=@($checks.ToArray());[IO.File]::WriteAllText($reportPath,($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($true))}
function Record([string]$Name,[string]$Status='PASS',[string]$Detail=''){$checks.Add([pscustomobject]@{name=$Name;status=$Status;detail=$Detail;utc=[DateTime]::UtcNow.ToString('o')});Save-Report}
function Check([string]$Name,[scriptblock]$Body){if($Phase -eq 'Remaining' -and [int]$Name.Substring(0,2) -lt 18){return};$script:currentStep=$Name;Save-Report;try{$null=&$Body;Record $Name}catch{Record $Name 'FAIL' $_.Exception.Message;throw}}
function Window{$process.Refresh();if($process.HasExited){throw ('Owned Blink exited '+$process.ExitCode)};if(-not $script:windowCache -and $process.MainWindowHandle -ne [IntPtr]::Zero){$script:windowCache=[Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)};return $script:windowCache}
function Visible($e){return $null -ne $e -and -not $e.Current.IsOffscreen}
function Find-In($root,[string]$Id){if($root){foreach($p in $root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,$Id))){if($p.Current.ProcessId -eq $process.Id -and (Visible $p)){return $p}}};return $null}
function By-Id([string]$Id){return Find-In (Window) $Id}
function Owned-Id([string]$Id){$p=By-Id $Id;if($p){return $p};foreach($h in [BlinkFocusUiNative]::OwnedTopWindows($process.Id)){$w=[Windows.Automation.AutomationElement]::FromHandle($h);if($w.Current.ProcessId -ne $process.Id){throw 'UIA/window owner mismatch'};$p=Find-In $w $Id;if($p){return $p}};return $null}
function Value($e){return $e.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value}
function Set-Value([string]$Id,[string]$Text){$p=Owned-Id $Id;if(-not(Visible $p) -or $p.Current.ProcessId -ne $process.Id){throw ('Owned editor unavailable '+$Id)};$p.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($Text)}
function Act($e){if(-not(Visible $e) -or -not $e.Current.IsEnabled -or $e.Current.ProcessId -ne $process.Id){throw 'Owned enabled action required'};$e.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()}
function Focus-Chain{$p=[Windows.Automation.AutomationElement]::FocusedElement;$chain=@();for($i=0;$p -and $i -lt 24;$i++){$c=$p.Current;$chain+=@([pscustomobject]@{id=$c.AutomationId;class=$c.ClassName;name=$c.Name;pid=$c.ProcessId;role=$c.ControlType.ProgrammaticName;has_keyboard_focus=$c.HasKeyboardFocus;runtime_id=($p.GetRuntimeId()-join ',')});if($c.ProcessId -ne $process.Id){break};$p=[Windows.Automation.TreeWalker]::ControlViewWalker.GetParent($p)};return $chain}
function Focus-In([string]$Id){$chain=@(Focus-Chain);return $chain.Count -gt 0 -and $chain[0].pid -eq $process.Id -and $chain[0].has_keyboard_focus -and @($chain|Where-Object{$_.pid -eq $process.Id -and $_.id -ceq $Id}).Count -gt 0}
function Region{foreach($item in @(Focus-Chain)){if($item.pid -ne $process.Id){return 'Outside'};switch -CaseSensitive ($item.id){'RenameName'{return 'Dialog'};'FolderEntries'{return 'Files'};'SearchResults'{return 'Files'};'AddressBar'{return 'Address'};'SearchQuery'{return 'Search'};'ExplorerCommandBar'{return 'Commands'};'NavigationPane'{return 'Navigation'};'WindowTabs'{return 'Tabs'}}};return 'Other'}
function Snapshot([string]$Why){$report.last_ui_snapshot=[ordered]@{why=$Why;region=(Region);focus_chain=@(Focus-Chain);address=$(if(By-Id 'AddressBar'){(By-Id 'AddressBar').Current.HelpText}else{$null});search=$(if(By-Id 'SearchQuery'){Value(By-Id 'SearchQuery')}else{$null});selected=@(Selected-Names);desktop=([BlinkFocusUiNative]::DesktopState())};Save-Report}
function Wait-Ui([string]$Why,[scriptblock]$Predicate,[int]$Seconds=$StepTimeoutSeconds){$clock=[Diagnostics.Stopwatch]::StartNew();$last='';do{try{if(&$Predicate){return}}catch{$last=$_.Exception.Message};if($process -and $process.WaitForExit(100)){throw ('Owned Blink exited while '+$Why+'; '+$process.ExitCode)}}while($clock.Elapsed.TotalSeconds -lt $Seconds);Snapshot $Why;throw ('Timed out: '+$Why+'; '+$last)}
function Require-Desktop{$report.desktop_preflight=[BlinkFocusUiNative]::DesktopState();if(-not $report.desktop_preflight.InputReady){throw $report.desktop_preflight.Reason}}
function Key([uint32]$Code,[int]$Modifiers=0){Require-Desktop;if($report.desktop_preflight.ForegroundPid -ne $process.Id){throw 'Owned test window lost foreground before input'};[BlinkFocusUiNative]::Key($process.Id,$Code,$Modifiers)}
function Focus-Owned([string]$Id){$e=Owned-Id $Id;if(-not(Visible $e) -or -not $e.Current.IsEnabled){throw ('Owned focus target missing '+$Id)};$e.SetFocus();Wait-Ui ('actual focus '+$Id) {(Focus-In $Id) -and [BlinkFocusUiNative]::DesktopState().ForegroundPid -eq $process.Id}}
function Assert-Region([string]$Expected){Wait-Ui ('actual focused region '+$Expected) {(Region) -ceq $Expected};$report.focus_evidence+=@([pscustomobject]@{step=$script:currentStep;region=$Expected;chain=@(Focus-Chain);foreground_pid=[BlinkFocusUiNative]::DesktopState().ForegroundPid});Save-Report}
function Rows([string]$List='FolderEntries'){$e=By-Id $List;if(-not $e){return @()};return @($e.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))|Where-Object{$_.Current.ProcessId -eq $process.Id -and (Visible $_)})}
function Selected-Names([string]$List='FolderEntries'){$e=By-Id $List;if(-not $e){return @()};return @($e.GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection()|ForEach-Object{$_.Current.Name})}
function Marker-Selected([string]$List='FolderEntries'){$names=@(Selected-Names $List);return $names.Count -eq 1 -and $names[0] -ceq $marker}
function Select-Marker([string]$List='FolderEntries'){$rows=@(Rows $List|Where-Object{$_.Current.Name -ceq $marker});if($rows.Count -ne 1){throw 'Exact owned marker is not unique'};$rows[0].GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select();$rows[0].SetFocus();Assert-Region 'Files'}
function Focus-Content{$list=if(By-Id 'SearchResults'){'SearchResults'}else{'FolderEntries'};$rows=@(Rows $list);if($rows.Count){$chosen=$rows[0];$selected=@((By-Id $list).GetCurrentPattern([Windows.Automation.SelectionPattern]::Pattern).Current.GetSelection());if($selected.Count){$chosen=$selected[0]};$chosen.SetFocus()}else{Focus-Owned $list};Assert-Region 'Files'}
function Cycle([bool]$Reverse=$false){$order=if($Reverse){@('Tabs','Navigation','Commands','Search','Address','Files')}else{@('Address','Search','Commands','Navigation','Tabs','Files')};foreach($region in $order){Key 0x75 $(if($Reverse){2}else{0});Assert-Region $region}}
function Assert-TextSelection([string]$Id,[string]$Expected){$e=Owned-Id $Id;$p=$null;$supported=$e.TryGetCurrentPattern([Windows.Automation.TextPattern]::Pattern,[ref]$p);$texts=@();if($supported){$texts=@($p.GetSelection()|ForEach-Object{$_.GetText(-1)})};$report.text_selection_evidence+=@([pscustomobject]@{id=$Id;expected=$Expected;supported=$supported;selected=$texts});Save-Report;if($supported -and ($texts.Count -ne 1 -or $texts[0] -cne $Expected)){throw ('Native selection mismatch '+$Id)}}
function Type-Text([string]$Id,[string]$Text){if(-not(Focus-In $Id)){throw ('Actual editor focus required '+$Id)};Require-Desktop;[BlinkFocusUiNative]::Text($process.Id,$Text);Wait-Ui 'actual typed editor value' {(Value(Owned-Id $Id)) -ceq $Text}}
function Navigate-Owned([string]$Path){if($Path -notin $allowedDirectories){throw 'Unowned navigation path'};Focus-Owned 'AddressHistoryButton';Key 0x4C 1;Wait-Ui 'address editor visible' {$null -ne (By-Id 'AddressEditor')};Focus-Owned 'AddressEditor';Set-Value 'AddressEditor' $Path;Key 13;Wait-Ui 'owned directory committed' {(By-Id 'AddressBar').Current.HelpText -ieq $Path -and (By-Id 'CommandNew').Current.IsEnabled -and (By-Id 'CommandSort').Current.IsEnabled}}
function Verify-File{if(-not(Test-Path -LiteralPath $file -PathType Leaf) -or [Convert]::ToBase64String([IO.File]::ReadAllBytes($file)) -cne [Convert]::ToBase64String([Text.UTF8Encoding]::new($false).GetBytes($payload)) -or [BlinkFocusUiNative]::Identity($file) -cne $report.initial_file_identity){throw 'Owned marker identity or bytes changed'}}
try{
 New-Item -ItemType Directory -Path $compilerTemp -Force|Out-Null;$env:TEMP=$compilerTemp;$env:TMP=$compilerTemp;$tempChanged=$true
 Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes
 Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public sealed class BlinkFocusUiDesktop { public bool InputReady; public string Reason,DesktopName,CursorWindowClass,CenterWindowClass,ForegroundClass; public int SessionId,CursorX,CursorY,CenterX,CenterY; public uint CursorWindowPid,CenterWindowPid,ForegroundPid; public long Foreground; }
public static class BlinkFocusUiNative {
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X,Y; public Point(int x,int y){X=x;Y=y;} }
    delegate bool EnumWindowProc(IntPtr window,IntPtr parameter);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("user32.dll",SetLastError=true)] static extern bool EnumWindows(EnumWindowProc callback,IntPtr parameter);
    [DllImport("user32.dll")] static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern IntPtr FindWindowEx(IntPtr parent,IntPtr after,string cls,string title);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr window,StringBuilder text,int length);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint pid);
    [DllImport("user32.dll",EntryPoint="SendMessageTimeoutW",CharSet=CharSet.Unicode,ExactSpelling=true,SetLastError=true)] static extern IntPtr SendMessageTimeout(IntPtr window,uint message,UIntPtr w,IntPtr l,uint flags,uint timeout,out UIntPtr result);
    [DllImport("user32.dll",SetLastError=true)] static extern bool GetCursorPos(out Point point);
    [DllImport("user32.dll")] static extern IntPtr WindowFromPoint(Point point);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern int GetSystemMetrics(int index);
    [DllImport("user32.dll",SetLastError=true)] static extern IntPtr OpenInputDesktop(uint flags,bool inherit,uint access);
    [DllImport("user32.dll")] static extern bool CloseDesktop(IntPtr desktop);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool GetUserObjectInformationW(IntPtr handle,int index,StringBuilder value,uint size,out uint needed);
    [DllImport("user32.dll")] static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("user32.dll",SetLastError=true)] static extern bool AttachThreadInput(uint a,uint b,bool attach);
    [DllImport("user32.dll")] static extern bool GetKeyboardState(byte[] state);
    [DllImport("user32.dll")] static extern bool SetKeyboardState(byte[] state);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    public static IntPtr OwnedWindow;
    public static bool WindowExists(IntPtr window) { return window!=IntPtr.Zero&&IsWindow(window); }
    public static bool IsOwnedVisible(IntPtr window,int pid) {
        uint owner;
        return WindowExists(window)&&IsWindowVisible(window)&&GetWindowThreadProcessId(window,out owner)!=0&&owner==(uint)pid;
    }
    public static IntPtr[] OwnedTopWindows(int pid) {
        var windows=new List<IntPtr>();
        if(!EnumWindows(delegate(IntPtr window,IntPtr parameter){if(IsOwnedVisible(window,pid))windows.Add(window);return true;},IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error(),"Owned top-level window enumeration failed");
        return windows.ToArray();
    }
    public static BlinkFocusUiDesktop DesktopState() {
        var result=new BlinkFocusUiDesktop{InputReady=true,SessionId=System.Diagnostics.Process.GetCurrentProcess().SessionId,Reason="Unlocked input desktop"};
        var foreground=GetForegroundWindow();result.Foreground=foreground.ToInt64();uint foregroundPid;GetWindowThreadProcessId(foreground,out foregroundPid);result.ForegroundPid=foregroundPid;var name=new StringBuilder(256);GetClassName(foreground,name,name.Capacity);result.ForegroundClass=name.ToString();
        var desktop=OpenInputDesktop(0,false,1);
        if(desktop==IntPtr.Zero){result.InputReady=false;result.Reason="Input desktop is inaccessible (locked, disconnected, or non-interactive)";}
        else{try{uint needed;var text=new StringBuilder(256);if(GetUserObjectInformationW(desktop,2,text,(uint)(text.Capacity*2),out needed)){result.DesktopName=text.ToString();if(!String.Equals(result.DesktopName,"Default",StringComparison.OrdinalIgnoreCase)){result.InputReady=false;result.Reason="Input desktop is "+result.DesktopName+", not the user's unlocked desktop";}}}finally{CloseDesktop(desktop);}}
        var previous=SetThreadDpiAwarenessContext(new IntPtr(-4));try{
            Point cursor;if(GetCursorPos(out cursor)){result.CursorX=cursor.X;result.CursorY=cursor.Y;var hit=WindowFromPoint(cursor);uint pid;GetWindowThreadProcessId(hit,out pid);result.CursorWindowPid=pid;name.Length=0;GetClassName(hit,name,name.Capacity);result.CursorWindowClass=name.ToString();}
            result.CenterX=GetSystemMetrics(76)+GetSystemMetrics(78)/2;result.CenterY=GetSystemMetrics(77)+GetSystemMetrics(79)/2;
            var center=WindowFromPoint(new Point(result.CenterX,result.CenterY));uint centerPid;GetWindowThreadProcessId(center,out centerPid);result.CenterWindowPid=centerPid;
            name.Length=0;GetClassName(center,name,name.Capacity);result.CenterWindowClass=name.ToString();
            if(result.ForegroundClass=="LockScreenBackstopFrame"||(foreground==IntPtr.Zero&&(result.CursorWindowClass=="LockScreenBackstopFrame"||result.CenterWindowClass=="LockScreenBackstopFrame"))){result.InputReady=false;result.Reason="No foreground window and Windows lock-screen backstop owns the cursor/desktop center";}
        }finally{if(previous!=IntPtr.Zero)SetThreadDpiAwarenessContext(previous);}
        return result;
    }
    static void Own(IntPtr window,int pid) { uint owner;GetWindowThreadProcessId(window,out owner);if(window==IntPtr.Zero||owner!=(uint)pid)throw new InvalidOperationException("Owned HWND required"); }
    static IntPtr InputSite(IntPtr parent) { IntPtr child=IntPtr.Zero;while((child=FindWindowEx(parent,child,null,null))!=IntPtr.Zero){var name=new StringBuilder(128);GetClassName(child,name,128);if(name.ToString()=="InputSiteWindowClass")return child;var nested=InputSite(child);if(nested!=IntPtr.Zero)return nested;}return IntPtr.Zero; }
    static void Message(IntPtr window,int pid,uint msg,uint key,IntPtr data) { Own(window,pid);UIntPtr result;if(SendMessageTimeout(window,msg,new UIntPtr(key),data,2,3000,out result)==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error(),"Owned message timed out"); }
    public static void Key(int pid,uint key,int modifiers) {
        Own(OwnedWindow,pid);var site=InputSite(OwnedWindow);Own(site,pid);uint ignored;var target=GetWindowThreadProcessId(site,out ignored);var caller=GetCurrentThreadId();
        byte[] saved=new byte[256];if(!GetKeyboardState(saved))throw new InvalidOperationException("Keyboard snapshot unavailable");
        bool attached=false;try {
            if(modifiers!=0&&target!=caller){if(!AttachThreadInput(caller,target,true))throw new InvalidOperationException("Owned keyboard thread attachment unavailable");attached=true;}
            var state=(byte[])saved.Clone();state[0x10]=state[0x11]=state[0x12]=0;
            if((modifiers&1)!=0)state[0x11]=0x80;if((modifiers&2)!=0)state[0x10]=0x80;if((modifiers&4)!=0)state[0x12]=0x80;
            if(modifiers!=0&&!SetKeyboardState(state))throw new InvalidOperationException("Owned modifier state unavailable");
            uint[] keys={0x11,0x10,0x12};int[] masks={1,2,4};
            for(int n=0;n<3;n++)if((modifiers&masks[n])!=0)Message(site,pid,keys[n]==0x12?0x104u:0x100u,keys[n],new IntPtr(1));
            bool alt=(modifiers&4)!=0;Message(site,pid,alt?0x104u:0x100u,key,new IntPtr(alt?0x20000001:1));Message(site,pid,alt?0x105u:0x101u,key,new IntPtr(unchecked((int)(alt?0xE0000001u:0xC0000001u))));
            for(int n=2;n>=0;n--)if((modifiers&masks[n])!=0){state[keys[n]]=0;SetKeyboardState(state);Message(site,pid,keys[n]==0x12?0x105u:0x101u,keys[n],new IntPtr(unchecked((int)0xC0000001)));}
        }finally{if(modifiers!=0)SetKeyboardState(saved);if(attached){AttachThreadInput(caller,target,false);SetKeyboardState(saved);}}
    }

    [StructLayout(LayoutKind.Sequential)] struct WindowRect { public int Left,Top,Right,Bottom; }
    [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr window,out WindowRect rect);
    [DllImport("user32.dll",SetLastError=true)] static extern bool SetWindowPos(IntPtr window,IntPtr after,int x,int y,int width,int height,uint flags);
    public static int[] WindowSize(int pid) { Own(OwnedWindow,pid);WindowRect rect;if(!GetWindowRect(OwnedWindow,out rect))throw new Win32Exception();return new int[]{rect.Right-rect.Left,rect.Bottom-rect.Top}; }
    public static void Resize(int pid,int width,int height) { Own(OwnedWindow,pid);if(width<420||height<300)throw new ArgumentOutOfRangeException();if(!SetWindowPos(OwnedWindow,IntPtr.Zero,0,0,width,height,0x16))throw new Win32Exception(); }

    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFileW(string p,uint a,uint s,IntPtr z,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)]static extern bool GetFileInformationByHandleEx(Microsoft.Win32.SafeHandles.SafeFileHandle f,int k,byte[] b,uint n);
    public static string Identity(string path){using(var f=CreateFileW(path,0x80,7,IntPtr.Zero,3,0x02000000,IntPtr.Zero)){if(f.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error());var b=new byte[24];if(!GetFileInformationByHandleEx(f,18,b,24))throw new Win32Exception(Marshal.GetLastWin32Error());return Convert.ToBase64String(b);}}
    public static void Text(int pid,string text){Own(OwnedWindow,pid);var w=InputSite(OwnedWindow);Own(w,pid);foreach(char c in text)Message(w,pid,0x102u,(uint)c,new IntPtr(1));}
}

'@
 Require-Desktop;$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());if($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Ordinary-user GUI required'}
 $report.environment=[ordered]@{ui_culture=[Globalization.CultureInfo]::CurrentUICulture.Name;dpi=$null;ime='NOT RUN';actual_theme='NOT CAPTURED'};$report.binary_sha256=(Get-FileHash -LiteralPath $Binary -Algorithm SHA256).Hash
 if(Test-Path -LiteralPath $fixture){throw 'Fresh GUID fixture exists'};New-Item -ItemType Directory -Path $fixture -Force|Out-Null;$fixtureCreated=$true;foreach($d in @($source,$empty)){New-Item -ItemType Directory -Path $d|Out-Null};[IO.File]::WriteAllText($file,$payload,[Text.UTF8Encoding]::new($false));$report.initial_file_identity=[BlinkFocusUiNative]::Identity($file)
 $psi=[Diagnostics.ProcessStartInfo]::new();$psi.FileName=$Binary;$psi.WorkingDirectory=$project;$psi.UseShellExecute=$false;foreach($name in @('BLINK_INITIAL_PATH','BLINK_PHASE1A_UI_PROBE','BLINK_STARTUP_TELEMETRY')){$psi.EnvironmentVariables.Remove($name)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$psi;if(-not $process.Start()){throw 'Owned launch failed'};$ownedHandle=$process.Handle;$report.process_id=$process.Id;Save-Report
 Wait-Ui 'owned main window' {$null -ne (Window)};[BlinkFocusUiNative]::OwnedWindow=$process.MainWindowHandle;$report.environment.dpi=[BlinkFocusUiNative]::GetDpiForWindow($process.MainWindowHandle);$thisPcText=(By-Id 'AddressBar').Current.HelpText
 $report['phase']=$Phase
 if($Phase -eq 'Remaining'){Navigate-Owned $source;Key 0x72;Assert-Region 'Search';Set-Value 'SearchQuery' ('no-result-'+$guid);Key 13;Wait-Ui 'owned search surface ready for remaining checks' {$null -ne (By-Id 'SearchResults')};Focus-Content}
 Check '01 ThisPC F3 selects the search draft without submitting' {Focus-Content;Set-Value 'SearchQuery' ('draft-'+$token);Key 0x72;Assert-Region 'Search';Assert-TextSelection 'SearchQuery' ('draft-'+$token);Type-Text 'SearchQuery' ('probe-'+$token);if(By-Id 'SearchResults'){throw 'F3 submitted a query'}}
 Check '02 Ctrl F and Ctrl E retain the shared search behavior' {foreach($key in @(0x46,0x45)){Focus-Content;Set-Value 'SearchQuery' ('alias-'+$token);Key $key 1;Assert-Region 'Search';Assert-TextSelection 'SearchQuery' ('alias-'+$token);Type-Text 'SearchQuery' ('replace-'+$token)}}
 Check '03 ThisPC forward F6 reaches every region and wraps' {Focus-Content;Cycle}
 Check '04 ThisPC Shift F6 reverses the same six regions' {Cycle $true}
 Check '05 navigate and select the exact owned Unicode folder marker' {Navigate-Owned $source;Wait-Ui 'owned marker rendered' {@(Rows|Where-Object{$_.Current.Name -ceq $marker}).Count -eq 1};Select-Marker;Verify-File}
 Check '06 folder forward cycle preserves the selected file and path' {Cycle;if(-not(Marker-Selected) -or (By-Id 'AddressBar').Current.HelpText -ine $source){throw 'Focus cycle changed folder selection or path'};Verify-File}
 Check '07 folder reverse cycle preserves the selected file' {Cycle $true;if(-not(Marker-Selected)){throw 'Reverse focus cycle changed selection'}}
 Check '08 F6 address round trip retains the actual unsubmitted draft' {Key 0x4C 1;Wait-Ui 'address editor' {$null -ne (By-Id 'AddressEditor')};Set-Value 'AddressEditor' ('unsent-'+$token);Key 0x75;Assert-Region 'Search';Key 0x75 2;Assert-Region 'Address';if((Value(By-Id 'AddressEditor')) -cne ('unsent-'+$token)){throw 'F6 discarded the address draft'};Key 27;Assert-Region 'Files';if((By-Id 'AddressBar').Current.HelpText -ine $source){throw 'Draft changed committed path'}}
 Check '09 F3 from an address draft focuses search without navigation' {Key 0x4C 1;Wait-Ui 'editor visible' {$null -ne (By-Id 'AddressEditor')};Set-Value 'AddressEditor' $empty;Key 0x72;Assert-Region 'Search';Key 0x4C 1;Wait-Ui 'same address draft' {(Value(By-Id 'AddressEditor')) -ceq $empty};Key 27;Assert-Region 'Files';if((By-Id 'AddressBar').Current.HelpText -ine $source){throw 'F3 submitted address input'}}
 Check '10 F3 is available from tabs commands and navigation' {foreach($target in @('Commands','Navigation','Tabs')){Focus-Content;for($step=0;$step -lt 6 -and (Region) -cne $target;$step++){Key 0x75};Assert-Region $target;Key 0x72;Assert-Region 'Search'}}
 Check '11 rename dialog keeps F3 F6 Shift F6 and search shortcuts' {Focus-Content;Select-Marker;Key 0x71;Wait-Ui 'owned Rename editor focus' {Focus-In 'RenameName'};foreach($key in @(@(0x72,0),@(0x75,0),@(0x75,2),@(0x46,1),@(0x45,1))){Key $key[0] $key[1];if(-not(Focus-In 'RenameName') -or (Value(Owned-Id 'RenameName')) -cne $marker){throw 'Window focus shortcut escaped the modal editor'}};Key 27;Wait-Ui 'rename cancelled' {-not(Owned-Id 'RenameName')};Verify-File}
 Check '12 Delete and Ctrl A affect only search text' {Key 0x72;Assert-Region 'Search';Set-Value 'SearchQuery' ('text-'+$token);Key 0x41 1;Key 0x2E;Wait-Ui 'search text empty' {(Value(By-Id 'SearchQuery')) -ceq ''};Verify-File;if(-not(Marker-Selected)){throw 'Text Delete altered file selection'}}
 Check '13 empty folder forward cycle can return to its empty file list' {Navigate-Owned $empty;Focus-Content;Cycle;if(@(Rows).Count){throw 'Empty folder inherited rows'}}
 Check '14 empty folder reverse cycle can return to its empty file list' {Cycle $true}
 Check '15 real indexed search returns the exact owned marker' {Navigate-Owned $source;Key 0x72;Assert-Region 'Search';Set-Value 'SearchQuery' $marker;Key 13;Wait-Ui 'real exact search result' {@(Rows 'SearchResults'|Where-Object{$_.Current.Name -ceq $marker}).Count -eq 1} 90;Select-Marker 'SearchResults';$report.real_search_evidence=[pscustomobject]@{query=$marker;real_result=$true;pipeline='existing production index';index_performance='NOT TESTED'};Save-Report}
 Check '16 visible search result remains selected across forward F6' {Cycle;if(-not(Marker-Selected 'SearchResults')){throw 'Forward cycle changed the search result'}}
 Check '17 visible search result remains selected across reverse F6' {Cycle $true;if(-not(Marker-Selected 'SearchResults')){throw 'Reverse cycle changed the search result'}}
 Check '18 zero-result search supports both focus cycles' {Key 0x72;Set-Value 'SearchQuery' ('no-result-'+$guid);Key 13;Wait-Ui 'visible zero-result search' {(By-Id 'SearchResults') -and @(Rows 'SearchResults').Count -eq 0};Focus-Content;Cycle;Cycle $true}
 Check '19 advanced search editor keeps its native F6 behavior' {(By-Id 'SearchAdvanced').GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();Wait-Ui 'advanced scope visible' {$null -ne (By-Id 'SearchScope')};Focus-Owned 'SearchScope';(By-Id 'SearchScope').GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern).Expand();Key 0x23;Key 13;Wait-Ui 'explicit selected-folder scope editor visible' {$null -ne (By-Id 'SearchSelectedFolder')};Focus-Owned 'SearchSelectedFolder';Key 0x75;if(-not(Focus-In 'SearchSelectedFolder')){throw 'F6 stole an advanced editor function key'}}
 Check '20 F3 respects per-tab search drafts' {Key 0x4C 1;Wait-Ui 'address editor' {$null -ne (By-Id 'AddressEditor')};Key 27;Focus-Content;$old=Value(By-Id 'SearchQuery');Key 0x54 1;Wait-Ui 'new ThisPC tab' {(By-Id 'AddressBar').Current.HelpText -ceq $thisPcText};Key 0x72;Assert-Region 'Search';Set-Value 'SearchQuery' ('new-tab-'+$token);Key 0x57 1;Wait-Ui 'old search draft restored' {(Value(By-Id 'SearchQuery')) -ceq $old}}
 Check '21 keep an address draft alive after F6 for normal close' {Key 0x4C 1;Wait-Ui 'live address editor' {$null -ne (By-Id 'AddressEditor')};Set-Value 'AddressEditor' ('close-draft-'+$token);Key 0x75;Assert-Region 'Search';Key 0x75 2;Assert-Region 'Address';if((Value(By-Id 'AddressEditor')) -cne ('close-draft-'+$token)){throw 'Close probe draft lost'};Verify-File}
 $report.status='PASS'
}catch{$report.status='FAIL';$report.failure=$_.Exception.ToString()+"`n"+$_.ScriptStackTrace;try{Snapshot 'unexpected failure'}catch{}}
finally{
 if($process){try{$process.Refresh();if(-not $process.HasExited){if(-not $process.CloseMainWindow() -or -not $process.WaitForExit(10000)){throw ('Owned window still running PID '+$process.Id+'; no force termination')}};$process.Refresh();$report.process_exited=$process.HasExited;$report.exit_code=$process.ExitCode;if($process.ExitCode -ne 0){throw ('Owned Blink abnormal exit '+$process.ExitCode)};if($report.status -eq 'PASS'){Record '22 live address editor closes normally and process exits zero'}}catch{$report.status='FAIL';$report.failure+= "`n"+$_.Exception.Message;Record '22 normal owned close failed' 'FAIL' $_.Exception.Message}}
 if($fixtureCreated){try{if($process -and -not $process.HasExited){throw 'Owned window active; retain fixture'};Verify-File;$all=@(Get-ChildItem -LiteralPath $fixture -Force -Recurse);foreach($item in $all){if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Fixture contains a reparse entry'};if($item.PSIsContainer){if($item.FullName -notin $allowedDirectories){throw 'Unowned fixture directory'}}elseif($item.FullName -ine $file){throw 'Unowned fixture file'}};$report.fixture_bytes_removed=(Get-Item -LiteralPath $file).Length;Remove-Item -LiteralPath $fixture -Recurse -Force;$report.fixture_removed=-not(Test-Path -LiteralPath $fixture);if(-not $report.fixture_removed){throw 'Fixture remains'};if($report.status -eq 'PASS'){Record '23 exact marker identity and bytes verified before cleanup'}}catch{$report.status='FAIL';$report.fixture_retained=Test-Path -LiteralPath $fixture;$report.failure+="`n"+$_.Exception.Message;Record '23 changed or in-use fixture retained' 'FAIL' $_.Exception.Message}}
 if($tempChanged){$env:TEMP=$oldTemp;$env:TMP=$oldTmp};if(Test-Path -LiteralPath $compilerTemp){try{Remove-Item -LiteralPath $compilerTemp -Recurse -Force;$report.compiler_temp_removed=-not(Test-Path -LiteralPath $compilerTemp)}catch{$report.status='FAIL';$report.failure+="`n"+$_.Exception.Message}}
 $report.passed=$report.status -eq 'PASS';$report.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report;Write-Output ('UI FOCUS '+$report.status+': '+@($checks|Where-Object{$_.status -eq 'PASS'}).Count+' PASS / '+@($checks|Where-Object{$_.status -eq 'FAIL'}).Count+' FAIL');Write-Output ('Report: '+$reportPath)
}
if($report.status -ne 'PASS'){exit 1};exit 0
