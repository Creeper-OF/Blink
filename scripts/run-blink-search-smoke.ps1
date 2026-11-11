[CmdletBinding()]
param(
 [ValidateSet('Debug','Release')][string]$Configuration='Release',
 [string]$TestBinaryPath='',
 [string]$OutputDirectory='',
 [ValidateSet('All','Response','Scope')][string]$Feature='All'
)
# Headless functional suites only. No build, GUI, service or Clipboard actions.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
if(-not $TestBinaryPath){$TestBinaryPath=Join-Path $project ('x64\'+$Configuration+'\Blink.Phase1.Tests.exe')}
if(-not [IO.Path]::IsPathRooted($TestBinaryPath)){$TestBinaryPath=Join-Path $project $TestBinaryPath}
$TestBinaryPath=[IO.Path]::GetFullPath($TestBinaryPath)
if(-not $OutputDirectory){$OutputDirectory=Join-Path $project '.build\search-response\integration\functional'}
if(-not [IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory=Join-Path $project $OutputDirectory}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$encoding=[Text.UTF8Encoding]::new($true)
$scriptHash=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
$ordinaryUser=-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$runId=[Guid]::NewGuid().ToString('N');$runStart=[DateTime]::UtcNow.ToString('o')
$expectedPassCounts=@{'search-response'=15;'search-scope'=25}
$suites=@(switch($Feature){'All'{'search-response';'search-scope'}'Response'{'search-response'}'Scope'{'search-scope'}})
function Write-Json([string]$Path,$Value){[IO.File]::WriteAllText($Path,($Value|ConvertTo-Json -Depth 7),$encoding)}
function Invoke-Suite([string]$Suite){
 $stem=$Suite+'-'+$Configuration.ToLowerInvariant()
 $stdoutPath=Join-Path $OutputDirectory ($stem+'-stdout.log');$stderrPath=Join-Path $OutputDirectory ($stem+'-stderr.log');$reportPath=Join-Path $OutputDirectory ($stem+'-report.json')
 $record=[ordered]@{run_id=$runId;feature=$Feature;suite=$Suite;configuration=$Configuration;ordinary_user=$ordinaryUser;started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;status='FAIL';passed=$false;test_binary=$TestBinaryPath;test_binary_sha256=$null;script_sha256=$scriptHash;arguments=@('--suite',$Suite);process_id=$null;exit_code=$null;counts=[ordered]@{PASS=0;FAIL=0;SKIP=0};expected_pass=$expectedPassCounts[$Suite];stdout=$stdoutPath;stdout_sha256=$null;stderr=$stderrPath;stderr_sha256=$null;failure=$null}
 $process=$null;$started=$false;$stdout='';$stderr=''
 try{
  if(-not $ordinaryUser){$record.status='SKIP';$record.failure='Ordinary-user shell required; no test process launched.'}
  else{
   if(-not(Test-Path -LiteralPath $TestBinaryPath -PathType Leaf)){throw 'Test binary does not exist; build the candidate separately.'}
   $record.test_binary_sha256=(Get-FileHash -LiteralPath $TestBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
   $info=[Diagnostics.ProcessStartInfo]::new();$info.FileName=$TestBinaryPath;$info.WorkingDirectory=$project;$info.Arguments='--suite '+$Suite;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
   $info.StandardOutputEncoding=[Text.UTF8Encoding]::new($false);$info.StandardErrorEncoding=[Text.UTF8Encoding]::new($false)
   $process=[Diagnostics.Process]::new();$process.StartInfo=$info;if(-not $process.Start()){throw 'Native suite process could not start.'};$started=$true;$retainedHandle=$process.Handle;$record.process_id=$process.Id
   # Drain both pipes concurrently, while keeping suite processes sequential.
   $stdoutTask=$process.StandardOutput.ReadToEndAsync();$stderrTask=$process.StandardError.ReadToEndAsync();$process.WaitForExit()
   $stdout=$stdoutTask.GetAwaiter().GetResult();$stderr=$stderrTask.GetAwaiter().GetResult();$record.exit_code=$process.ExitCode
   $combined=$stdout+"`n"+$stderr
   foreach($tag in @('PASS','FAIL','SKIP')){$record.counts[$tag]=[regex]::Matches($combined,('(?m)^'+$tag+'[ \t]+')).Count}
   if($record.exit_code -ne 0){$record.failure='Native suite returned a nonzero exit code.'}
   elseif($record.counts.FAIL -ne 0){$record.failure='Native suite reported FAIL checks.'}
   elseif($record.counts.PASS -eq 0){$record.failure='Native suite reported zero PASS checks.'}
   elseif($record.counts.PASS -ne $record.expected_pass -or $record.counts.SKIP -ne 0){$record.failure='Native suite check counts differ from its current exact contract; verify the selected candidate binary.'}
   elseif((Get-FileHash -LiteralPath $TestBinaryPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.test_binary_sha256){$record.failure='Test binary changed during this suite run.'}
   else{$record.status='PASS';$record.passed=$true}
  }
 }catch{$record.status='FAIL';$record.failure=$_.Exception.Message}
 finally{
  if($process){if($started -and -not $process.HasExited){$process.WaitForExit()};$process.Dispose()}
  [IO.File]::WriteAllText($stdoutPath,$stdout,$encoding);[IO.File]::WriteAllText($stderrPath,$stderr,$encoding)
  $record.stdout_sha256=(Get-FileHash -LiteralPath $stdoutPath -Algorithm SHA256).Hash.ToLowerInvariant();$record.stderr_sha256=(Get-FileHash -LiteralPath $stderrPath -Algorithm SHA256).Hash.ToLowerInvariant();$record.ended_utc=[DateTime]::UtcNow.ToString('o');Write-Json $reportPath $record
 }
 Write-Host ($Suite+': '+$record.status+'; '+$record.counts.PASS+' PASS / '+$record.counts.FAIL+' FAIL / '+$record.counts.SKIP+' SKIP; exit='+$record.exit_code)
 return [pscustomobject]@{suite=$Suite;status=$record.status;passed=$record.passed;test_binary_sha256=$record.test_binary_sha256;report=$reportPath;report_sha256=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToLowerInvariant()}
}
$results=@(foreach($suite in $suites){Invoke-Suite $suite})
$passed=@($results|Where-Object{-not $_.passed}).Count -eq 0 -and $results.Count -eq $suites.Count -and @($results.test_binary_sha256|Select-Object -Unique).Count -eq 1
$summary=[ordered]@{run_id=$runId;feature=$Feature;configuration=$Configuration;started_utc=$runStart;ended_utc=[DateTime]::UtcNow.ToString('o');status=$(if($passed){'PASS'}else{'FAIL'});passed=$passed;sequential=$true;script_sha256=$scriptHash;suites=$results;scope='Headless functional native suites; no UI or real-service performance acceptance.'}
$summaryPath=Join-Path $OutputDirectory ('search-'+$Feature.ToLowerInvariant()+'-'+$Configuration.ToLowerInvariant()+'-summary.json');Write-Json $summaryPath $summary
Write-Host ('Summary: '+$summaryPath)
if(-not $passed){exit 1};exit 0
