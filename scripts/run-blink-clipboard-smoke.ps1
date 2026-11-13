[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$TestBinaryPath='',
 [ValidateSet('Presentation','DataObject','Session','Controller','All')][string]$Feature='Presentation',
 [string]$OutputDirectory=''
)
# Exact headless suites only. No build, GUI, live Clipboard or service actions.
# All launches each selected suite once, in separate sequential native processes.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
if(-not $TestBinaryPath){$TestBinaryPath=Join-Path $project ('x64\'+$Configuration+'\Blink.Phase1.Tests.exe')}
if(-not [IO.Path]::IsPathRooted($TestBinaryPath)){$TestBinaryPath=Join-Path $project $TestBinaryPath}
$TestBinaryPath=[IO.Path]::GetFullPath($TestBinaryPath)
if(-not $OutputDirectory){$OutputDirectory=Join-Path $project '.build\clipboard-transfer\integration'}
if(-not [IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory=Join-Path $project $OutputDirectory}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$encoding=[Text.UTF8Encoding]::new($true)
$scriptSha=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
$definitions=[ordered]@{
 Presentation=[pscustomobject]@{name='Presentation';suite='clipboard-transfer-presentation';completion='clipboard_transfer_presentation_tests';prefix='clipboard_presentation_';expected_pass=64}
 DataObject=[pscustomobject]@{name='DataObject';suite='clipboard-data-object';completion='clipboard_data_object_tests';prefix='clipboard_data_object_';expected_pass=60}
 Session=[pscustomobject]@{name='Session';suite='clipboard-paste-session';completion='clipboard_paste_session_tests';prefix='clipboard_paste_session_';expected_pass=33}
 Controller=[pscustomobject]@{name='Controller';suite='clipboard-paste-controller';completion='clipboard_paste_controller_tests';prefix='clipboard_paste_controller_';expected_pass=23}
}
function Save-FeatureReport($Record,[string]$Path){[IO.File]::WriteAllText($Path,($Record|ConvertTo-Json -Depth 8),$encoding)}
function Invoke-ClipboardSuite($Definition,[string]$SharedBinarySha){
 $arguments=@('--suite',$Definition.suite)
 $stem='clipboard-'+$Definition.name.ToLowerInvariant()+'-'+$Configuration.ToLowerInvariant()
 $stdoutPath=Join-Path $OutputDirectory ($stem+'-stdout.log')
 $stderrPath=Join-Path $OutputDirectory ($stem+'-stderr.log')
 $reportPath=Join-Path $OutputDirectory ($stem+'-report.json')
 $record=[ordered]@{
  run_id=[Guid]::NewGuid().ToString('N');feature=$Definition.name;suite=$Definition.suite;configuration=$Configuration
  started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;status='FAIL';passed=$false
  test_binary=$TestBinaryPath;test_binary_sha256=$null;script_sha256=$scriptSha
  arguments=$arguments;process_launch_count=0;process_id=$null;exit_code=$null;elapsed_ms=$null
  counts=[ordered]@{PASS=0;FAIL=0;SKIP=0};expected_pass=$Definition.expected_pass
  feature_pass_count=0;unique_feature_pass_count=0
  suite_marker_count=0;completion_marker_count=0;completion_result=$null
  stdout=$stdoutPath;stdout_sha256=$null;stderr=$stderrPath;stderr_sha256=$null;report_path=$reportPath;failure=$null
  scope='Headless exact suite only. DataObject/Session/Controller use injected objects and sequence sources; no live Clipboard, GUI, real file transfer or performance acceptance.'
 }
 # Preserve the original Presentation report fields for existing consumers.
 if($Definition.name -ceq 'Presentation'){$record.presentation_pass_count=0;$record.unique_presentation_pass_count=0}
 $process=$null;$started=$false;$stdout='';$stderr='';$timer=$null
 try{
  Save-FeatureReport $record $reportPath
  if($Definition.expected_pass -le 0){throw 'The exact native check count has not been frozen; no process launched.'}
  if([IO.Path]::GetFileName($TestBinaryPath) -ine 'Blink.Phase1.Tests.exe'){throw 'TestBinaryPath must identify Blink.Phase1.Tests.exe; no process launched.'}
  if(-not(Test-Path -LiteralPath $TestBinaryPath -PathType Leaf)){throw 'Test binary does not exist; build the candidate separately.'}
  $record.test_binary_sha256=(Get-FileHash -LiteralPath $TestBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
  if($SharedBinarySha -and $SharedBinarySha -cne $record.test_binary_sha256){throw 'Test binary changed between selected suites; no process launched.'}
  $info=[Diagnostics.ProcessStartInfo]::new()
  $info.FileName=$TestBinaryPath;$info.WorkingDirectory=$project;$info.Arguments=($arguments -join ' ')
  $info.UseShellExecute=$false;$info.CreateNoWindow=$true
  $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
  $info.StandardOutputEncoding=[Text.UTF8Encoding]::new($false)
  $info.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
  $process=[Diagnostics.Process]::new();$process.StartInfo=$info
  $timer=[Diagnostics.Stopwatch]::StartNew()
  if(-not $process.Start()){throw 'Native Clipboard suite could not start.'}
  $started=$true;$retainedHandle=$process.Handle
  $record.process_launch_count=1;$record.process_id=$process.Id;Save-FeatureReport $record $reportPath
  # Drain both redirected pipes while waiting on this exact native process.
  $stdoutTask=$process.StandardOutput.ReadToEndAsync()
  $stderrTask=$process.StandardError.ReadToEndAsync()
  $process.WaitForExit()
  $stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult()
  $timer.Stop();$record.elapsed_ms=$timer.Elapsed.TotalMilliseconds;$record.exit_code=$process.ExitCode
  $combined=$stdout+"`n"+$stderr
  foreach($tag in @('PASS','FAIL','SKIP')){$record.counts[$tag]=[regex]::Matches($combined,('(?m)^'+$tag+'[ \t]+')).Count}
  $passPattern='(?m)^PASS[ \t]+('+[regex]::Escape($Definition.prefix)+'[a-z0-9_]+)\r?$'
  $featurePasses=[regex]::Matches($combined,$passPattern)
  $record.feature_pass_count=$featurePasses.Count
  $record.unique_feature_pass_count=@($featurePasses|ForEach-Object{$_.Groups[1].Value}|Select-Object -Unique).Count
  if($Definition.name -ceq 'Presentation'){$record.presentation_pass_count=$record.feature_pass_count;$record.unique_presentation_pass_count=$record.unique_feature_pass_count}
  $record.suite_marker_count=[regex]::Matches($stdout,('(?m)^suite='+[regex]::Escape($Definition.suite)+'\r?$')).Count
  $completion=[regex]::Matches($stdout,('(?m)^'+[regex]::Escape($Definition.completion)+'=(PASS|FAIL)\r?$'))
  $record.completion_marker_count=$completion.Count
  if($completion.Count -eq 1){$record.completion_result=$completion[0].Groups[1].Value}
  if($record.exit_code -ne 0){$record.failure='Native Clipboard suite returned a nonzero exit code.'}
  elseif($record.process_launch_count -ne 1 -or $record.suite_marker_count -ne 1 -or $record.completion_marker_count -ne 1 -or $record.completion_result -cne 'PASS'){$record.failure='The exact CLI suite did not identify itself and complete once; wrong/full/old suites are not accepted.'}
  elseif($record.counts.FAIL -ne 0 -or $record.counts.SKIP -ne 0 -or $record.counts.PASS -ne $Definition.expected_pass -or $record.feature_pass_count -ne $Definition.expected_pass -or $record.unique_feature_pass_count -ne $Definition.expected_pass){$record.failure='Suite counts differ from the frozen complete contract; partial results and SKIP do not pass.'}
  elseif((Get-FileHash -LiteralPath $TestBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant() -cne $record.test_binary_sha256){$record.failure='Test binary changed during the suite.'}
  else{$record.status='PASS';$record.passed=$true}
 }catch{$record.status='FAIL';$record.passed=$false;$record.failure=$_.Exception.Message}
 finally{
  if($process){if($started -and -not $process.HasExited){$process.WaitForExit()};$process.Dispose()}
  if($timer -and $timer.IsRunning){$timer.Stop();$record.elapsed_ms=$timer.Elapsed.TotalMilliseconds}
  [IO.File]::WriteAllText($stdoutPath,$stdout,$encoding)
  [IO.File]::WriteAllText($stderrPath,$stderr,$encoding)
  $record.stdout_sha256=(Get-FileHash -LiteralPath $stdoutPath -Algorithm SHA256).Hash.ToLowerInvariant()
  $record.stderr_sha256=(Get-FileHash -LiteralPath $stderrPath -Algorithm SHA256).Hash.ToLowerInvariant()
  $record.ended_utc=[DateTime]::UtcNow.ToString('o');Save-FeatureReport $record $reportPath
 }
 Write-Host ($Definition.suite+': '+$record.status+'; '+$record.counts.PASS+' PASS / '+$record.counts.FAIL+' FAIL / '+$record.counts.SKIP+' SKIP; exit='+$record.exit_code)
 if($record.failure){Write-Host $record.failure}
 Write-Host ('Report: '+$reportPath)
 return $record
}
$selected=if($Feature -eq 'All'){@('Presentation','DataObject','Session','Controller')}else{@($Feature)}
$sharedSha=$null
if([IO.Path]::GetFileName($TestBinaryPath) -ieq 'Blink.Phase1.Tests.exe' -and (Test-Path -LiteralPath $TestBinaryPath -PathType Leaf)){$sharedSha=(Get-FileHash -LiteralPath $TestBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant()}
if($Feature -ne 'All'){
 $result=Invoke-ClipboardSuite $definitions[$Feature] $sharedSha
 if(-not $result.passed){exit 1};exit 0
}
$batchPath=Join-Path $OutputDirectory ('clipboard-all-'+$Configuration.ToLowerInvariant()+'-report.json')
$batch=[ordered]@{
 run_id=[Guid]::NewGuid().ToString('N');feature='All';configuration=$Configuration;status='FAIL';passed=$false
 started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;script_sha256=$scriptSha
 test_binary=$TestBinaryPath;test_binary_sha256=$sharedSha;features=$selected;results=@();process_launch_count=0
 counts=[ordered]@{PASS=0;FAIL=0;SKIP=0};failure=$null
 scope='Four isolated headless groups, sequential processes; group counts are subsets of full Phase1, never additive to its full-regression total.'
}
try{
 Save-FeatureReport $batch $batchPath
 if(@($selected|Where-Object{$definitions[$_].expected_pass -le 0}).Count){throw 'All selected native check counts must be frozen before any suite is launched.'}
 foreach($name in $selected){
  $result=Invoke-ClipboardSuite $definitions[$name] $sharedSha
  $batch.results+=@($result)
  $batch.process_launch_count+=$result.process_launch_count
  foreach($tag in @('PASS','FAIL','SKIP')){$batch.counts[$tag]+=$result.counts[$tag]}
  Save-FeatureReport $batch $batchPath
 }
 if($batch.results.Count -ne $selected.Count -or $batch.process_launch_count -ne $selected.Count -or @($batch.results|Where-Object{-not $_.passed -or $_.test_binary_sha256 -cne $sharedSha}).Count){throw 'At least one isolated suite failed or used a different binary; inspect its retained report.'}
 $batch.status='PASS';$batch.passed=$true
}catch{$batch.failure=$_.Exception.Message}
finally{$batch.ended_utc=[DateTime]::UtcNow.ToString('o');Save-FeatureReport $batch $batchPath}
Write-Host ('Clipboard All: '+$batch.status+'; '+$batch.counts.PASS+' PASS / '+$batch.counts.FAIL+' FAIL / '+$batch.counts.SKIP+' SKIP; native processes='+$batch.process_launch_count)
if($batch.failure){Write-Host $batch.failure}
Write-Host ('Report: '+$batchPath)
if(-not $batch.passed){exit 1};exit 0
