[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$TestBinaryPath='',
 [ValidateSet('Presentation')][string]$Feature='Presentation'
)
# Headless presentation checks only. No build, GUI, Clipboard or service actions.
# The selected native CLI is launched exactly once; no fallback to its full suite.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
if(-not $TestBinaryPath){$TestBinaryPath=Join-Path $project ('x64\'+$Configuration+'\Blink.Phase1.Tests.exe')}
if(-not [IO.Path]::IsPathRooted($TestBinaryPath)){$TestBinaryPath=Join-Path $project $TestBinaryPath}
$TestBinaryPath=[IO.Path]::GetFullPath($TestBinaryPath)
$outputDirectory=Join-Path $project '.build\clipboard-transfer\integration'
New-Item -ItemType Directory -Path $outputDirectory -Force|Out-Null
$encoding=[Text.UTF8Encoding]::new($true)
$suite='clipboard-transfer-presentation'
$arguments=@('--suite',$suite)
$expectedPass=64
$stem='clipboard-'+$Feature.ToLowerInvariant()+'-'+$Configuration.ToLowerInvariant()
$stdoutPath=Join-Path $outputDirectory ($stem+'-stdout.log')
$stderrPath=Join-Path $outputDirectory ($stem+'-stderr.log')
$reportPath=Join-Path $outputDirectory ($stem+'-report.json')
$record=[ordered]@{
 run_id=[Guid]::NewGuid().ToString('N');feature=$Feature;suite=$suite;configuration=$Configuration
 started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;status='FAIL';passed=$false
 test_binary=$TestBinaryPath;test_binary_sha256=$null
 script_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
 arguments=$arguments;process_launch_count=0;process_id=$null;exit_code=$null;elapsed_ms=$null
 counts=[ordered]@{PASS=0;FAIL=0;SKIP=0};expected_pass=$expectedPass
 presentation_pass_count=0;unique_presentation_pass_count=0
 suite_marker_count=0;completion_marker_count=0;completion_result=$null
 stdout=$stdoutPath;stdout_sha256=$null;stderr=$stderrPath;stderr_sha256=$null;failure=$null
 scope='Headless presentation/model checks only; no real Clipboard protocol, GUI, file transfer or performance acceptance.'
}
function Save-Report{[IO.File]::WriteAllText($reportPath,($record|ConvertTo-Json -Depth 7),$encoding)}
$process=$null;$started=$false;$stdout='';$stderr='';$timer=$null
try{
 Save-Report
 if([IO.Path]::GetFileName($TestBinaryPath) -ine 'Blink.Phase1.Tests.exe'){throw 'TestBinaryPath must identify Blink.Phase1.Tests.exe; no process launched.'}
 if(-not(Test-Path -LiteralPath $TestBinaryPath -PathType Leaf)){throw 'Test binary does not exist; build the candidate separately.'}
 $record.test_binary_sha256=(Get-FileHash -LiteralPath $TestBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
 $info=[Diagnostics.ProcessStartInfo]::new()
 $info.FileName=$TestBinaryPath;$info.WorkingDirectory=$project;$info.Arguments=($arguments -join ' ')
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $info.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
 $info.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
 $process=[Diagnostics.Process]::new();$process.StartInfo=$info
 $timer=[Diagnostics.Stopwatch]::StartNew()
 if(-not $process.Start()){throw 'Native presentation suite could not start.'}
 $started=$true;$retainedHandle=$process.Handle
 $record.process_launch_count=1;$record.process_id=$process.Id;Save-Report
 # Drain both pipes concurrently and wait on this exact native process.
 $stdoutTask=$process.StandardOutput.ReadToEndAsync()
 $stderrTask=$process.StandardError.ReadToEndAsync()
 $process.WaitForExit()
 $stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult()
 $timer.Stop();$record.elapsed_ms=$timer.Elapsed.TotalMilliseconds;$record.exit_code=$process.ExitCode
 $combined=$stdout+"`n"+$stderr
 foreach($tag in @('PASS','FAIL','SKIP')){
  $record.counts[$tag]=[regex]::Matches($combined,('(?m)^'+$tag+'[ \t]+')).Count
 }
 $presentationPasses=[regex]::Matches($combined,'(?m)^PASS[ \t]+(clipboard_presentation_[a-z0-9_]+)\r?$')
 $record.presentation_pass_count=$presentationPasses.Count
 $record.unique_presentation_pass_count=@($presentationPasses|ForEach-Object{$_.Groups[1].Value}|Select-Object -Unique).Count
 $record.suite_marker_count=[regex]::Matches($stdout,'(?m)^suite=clipboard-transfer-presentation\r?$').Count
 $completion=[regex]::Matches($stdout,'(?m)^clipboard_transfer_presentation_tests=(PASS|FAIL)\r?$')
 $record.completion_marker_count=$completion.Count
 if($completion.Count -eq 1){$record.completion_result=$completion[0].Groups[1].Value}
 if($record.exit_code -ne 0){$record.failure='Native presentation suite returned a nonzero exit code.'}
 elseif($record.process_launch_count -ne 1 -or $record.suite_marker_count -ne 1 -or $record.completion_marker_count -ne 1 -or $record.completion_result -cne 'PASS'){$record.failure='The exact presentation CLI suite did not identify itself and complete once; wrong/full/old suites are not accepted.'}
 elseif($record.counts.FAIL -ne 0 -or $record.counts.SKIP -ne 0 -or $record.counts.PASS -ne $expectedPass -or $record.presentation_pass_count -ne $expectedPass -or $record.unique_presentation_pass_count -ne $expectedPass){$record.failure='Presentation suite counts differ from the frozen complete contract; partial results and SKIP do not pass.'}
 elseif((Get-FileHash -LiteralPath $TestBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.test_binary_sha256){$record.failure='Test binary changed during the presentation run.'}
 else{$record.status='PASS';$record.passed=$true}
}catch{
 $record.status='FAIL';$record.passed=$false;$record.failure=$_.Exception.Message
}finally{
 if($process){
  if($started -and -not $process.HasExited){$process.WaitForExit()}
  $process.Dispose()
 }
 if($timer -and $timer.IsRunning){$timer.Stop();$record.elapsed_ms=$timer.Elapsed.TotalMilliseconds}
 [IO.File]::WriteAllText($stdoutPath,$stdout,$encoding)
 [IO.File]::WriteAllText($stderrPath,$stderr,$encoding)
 $record.stdout_sha256=(Get-FileHash -LiteralPath $stdoutPath -Algorithm SHA256).Hash.ToLowerInvariant()
 $record.stderr_sha256=(Get-FileHash -LiteralPath $stderrPath -Algorithm SHA256).Hash.ToLowerInvariant()
 $record.ended_utc=[DateTime]::UtcNow.ToString('o');Save-Report
}
Write-Host ($suite+': '+$record.status+'; '+$record.counts.PASS+' PASS / '+$record.counts.FAIL+' FAIL / '+$record.counts.SKIP+' SKIP; exit='+$record.exit_code)
if($record.failure){Write-Host $record.failure}
Write-Host ('Report: '+$reportPath)
if(-not $record.passed){exit 1};exit 0
