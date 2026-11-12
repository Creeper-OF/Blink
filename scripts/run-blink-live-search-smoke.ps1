[CmdletBinding()]
param(
 [string]$OutputDirectory='',
 [string]$FixtureRoot='',
 [string]$ResumeFixture='',
 [ValidateRange(1000,15000)][int]$TimeoutMs=5000
)
# Real-service, read-only v1 queries. Fixture writes are confined to one owned GUID directory.
$ErrorActionPreference='Stop'
[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false)
$project=Split-Path -Parent $PSScriptRoot
if(-not $OutputDirectory){$OutputDirectory=Join-Path $project '.build\search-rollout\integration\live-search'}
if(-not [IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory=Join-Path $project $OutputDirectory}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory).TrimEnd('\')
New-Item -ItemType Directory -Path $OutputDirectory -Force|Out-Null
$utf8=[Text.UTF8Encoding]::new($false);$bom=[Text.UTF8Encoding]::new($true)
$runId=[Guid]::NewGuid().ToString('N');$logPath=Join-Path $OutputDirectory ('live-search-'+$runId+'.log')
$reportPath=Join-Path $OutputDirectory ('live-search-'+$runId+'-report.json')
$checks=[Collections.Generic.List[object]]::new();$requests=[Collections.Generic.List[object]]::new();$lines=[Collections.Generic.List[string]]::new()
$expected=@();$owned=@{};$manifest=$null;$manifestHash=$null;$fixtureValidated=$false;$notReady=$null;$requestId=[uint64]0
$record=[ordered]@{run_id=$runId;started_utc=[DateTime]::UtcNow.ToString('o');ended_utc=$null;status='FAIL';expected_checks=23;expected_requests=19;script_sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();pipe='Blink.IndexService.v1';protocol_version=1;ordinary_user=$false;fixture_root=$null;fixture_manifest=$null;resume_fixture=$null;not_ready_reason=$null;failure=$null;cleanup=$null;counts=$null;checks=$null;requests=$null;files=$null;unicode_oracle=@();log=$logPath;scope='Real service read-only fixture queries; no GUI/Clipboard/service control or direct index-store writes'}
function Check([bool]$Value,[string]$Name){
 $status=if($Value){'PASS'}else{'FAIL'};$checks.Add([pscustomobject]@{name=$Name;status=$status})
 $line=$status+' '+$Name;$lines.Add($line);Write-Host $line
}
function Not-Ready([string]$Reason){$script:notReady=$Reason;throw 'Fixture/service is not ready; resume the same manifest.'}
function Hash-Bytes([byte[]]$Bytes){$sha=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}}
function Read-String([IO.BinaryReader]$Reader){
 $n=$Reader.ReadUInt32();if($n -gt 262144){throw 'Oversized IPC string'}
 $b=$Reader.ReadBytes([int]($n*2));if($b.Length -ne $n*2){throw 'Truncated IPC string'}
 return [Text.Encoding]::Unicode.GetString($b)
}
function Read-Identity([IO.BinaryReader]$Reader){
 $serial=$Reader.ReadUInt64();$valid=$Reader.ReadByte() -ne 0;$id=$Reader.ReadBytes(16)
 if($id.Length -ne 16){throw 'Truncated IPC identity'}
 return [pscustomobject]@{valid=$valid;volume_serial=$serial;file_id=([BitConverter]::ToString($id)).Replace('-','')}
}
function Read-Response([byte[]]$Bytes,[uint64]$Id){
 $stream=[IO.MemoryStream]::new($Bytes);$r=[IO.BinaryReader]::new($stream)
 try{
  if($r.ReadUInt32() -ne 0x31504C42 -or $r.ReadUInt32() -ne 2 -or $r.ReadUInt32() -ne 1 -or $r.ReadUInt64() -ne $Id){throw 'IPC magic/kind/version/request ID mismatch'}
  $status=$r.ReadUInt32();$error=$r.ReadUInt32();$service=$r.ReadUInt32();$index=$r.ReadUInt32()
  $null=$r.ReadUInt64();$null=$r.ReadUInt64();$null=$r.ReadUInt64();$null=$r.ReadUInt64();$null=$r.ReadUInt64();$null=$r.ReadInt64();$null=$r.ReadUInt64()
  $null=Read-String $r;$count=$r.ReadUInt32();if($count -gt 10000){throw 'Oversized IPC result array'}
  $hits=[Collections.Generic.List[object]]::new();$foreign=0
  for($i=0;$i -lt $count;$i++){
   $idValue=Read-Identity $r;$parent=Read-Identity $r;$attributes=$r.ReadUInt32();$null=$r.ReadInt64();$directory=$r.ReadByte() -ne 0
   $name=Read-String $r;$path=Read-String $r
   if($owned.ContainsKey($path)){
    $file=$owned[$path]
    $hits.Add([pscustomobject]@{label=$file.label;path=$file.path;identity=$idValue;parent_identity=$parent;attributes=$attributes;is_directory=$directory;metadata_ok=($idValue.valid -and $parent.valid -and $idValue.volume_serial -eq $parent.volume_serial -and -not $directory -and $attributes -eq $file.attributes -and $name -ceq [IO.Path]::GetFileName($file.path))})
   }else{$foreign++}
  }
  if($stream.Position -ne $stream.Length){throw 'Trailing bytes in IPC response'}
  return [pscustomobject]@{status=$status;error=$error;service_state=$service;index_state=$index;results=@($hits.ToArray());result_count=$count;foreign_results=$foreign}
 }finally{$r.Dispose();$stream.Dispose()}
}
function ReadOnlyQuery {
 param([ValidateSet(1,2,5,7,8,9,10,11)][int]$Type,[string]$Text='',[string]$Path='',[string]$Extension='',[uint32]$Limit=20)
 $script:requestId++;$id=$script:requestId;$clock=[Diagnostics.Stopwatch]::StartNew()
 $trace=[ordered]@{id=$id;type=$Type;limit=$Limit;status=$null;error=$null;index_state=$null;returned=$null;owned=$null;owned_labels=@();owned_attributes=@();foreign=$null;elapsed_ms=$null;correlated=$false;failure=$null}
 $rights=[IO.Pipes.PipeAccessRights]::ReadData -bor [IO.Pipes.PipeAccessRights]::WriteData -bor [IO.Pipes.PipeAccessRights]::ReadAttributes -bor [IO.Pipes.PipeAccessRights]::WriteAttributes -bor [IO.Pipes.PipeAccessRights]::ReadPermissions -bor [IO.Pipes.PipeAccessRights]::Synchronize
 $pipe=[IO.Pipes.NamedPipeClientStream]::new('.','Blink.IndexService.v1',$rights,[IO.Pipes.PipeOptions]::Asynchronous,[Security.Principal.TokenImpersonationLevel]::Impersonation,[IO.HandleInheritability]::None)
 $m=[IO.MemoryStream]::new();$w=[IO.BinaryWriter]::new($m);$response=[IO.MemoryStream]::new()
 try{
  $pipe.Connect([Math]::Min(1000,$TimeoutMs));$pipe.ReadMode=[IO.Pipes.PipeTransmissionMode]::Message
  $w.Write([uint32]0x31504C42);$w.Write([uint32]1);$w.Write([uint32]1);$w.Write([uint64]$id);$w.Write([uint32]$Type);$w.Write([uint64]0);$w.Write([byte]1);$w.Write([uint32]$Limit)
  foreach($value in @($volume,$Text,$Extension,$Path)){$bytes=[Text.Encoding]::Unicode.GetBytes($value);$w.Write([uint32]$value.Length);$w.Write([byte[]]$bytes)}
  $w.Flush();$payload=$m.ToArray();$remaining=$TimeoutMs-[int]$clock.ElapsedMilliseconds
  if($remaining -le 0 -or -not $pipe.WriteAsync($payload,0,$payload.Length).Wait($remaining)){throw 'Bounded IPC write timed out'}
  $buffer=[byte[]]::new(65536)
  do{
   $remaining=$TimeoutMs-[int]$clock.ElapsedMilliseconds;if($remaining -le 0){throw 'Bounded IPC read timed out'}
   $read=$pipe.ReadAsync($buffer,0,$buffer.Length);if(-not $read.Wait($remaining)){throw 'Bounded IPC read timed out'}
   $n=$read.Result;if($n -le 0){throw 'IPC message ended early'};$response.Write($buffer,0,$n)
   if($response.Length -gt 1048576){throw 'Oversized IPC frame'}
  }while(-not $pipe.IsMessageComplete)
  $reply=Read-Response $response.ToArray() $id
  $trace.status=$reply.status;$trace.error=$reply.error;$trace.index_state=$reply.index_state;$trace.returned=$reply.result_count;$trace.owned=$reply.results.Count;$trace.foreign=$reply.foreign_results;$trace.correlated=$true
  $trace.owned_labels=@($reply.results|ForEach-Object{$_.label});$trace.owned_attributes=@($reply.results|ForEach-Object{$_.attributes})
  if($reply.foreign_results){throw 'Fixture query returned non-owned hits; their names are omitted'}
  return $reply
 }catch{$trace.failure=$_.Exception.Message;throw}
 finally{$trace.elapsed_ms=[Math]::Round($clock.Elapsed.TotalMilliseconds,3);$requests.Add([pscustomobject]$trace);$pipe.Dispose();$w.Dispose();$m.Dispose();$response.Dispose()}
}
function Require-Search($Reply){
 if($Reply.status -eq 3 -or $Reply.error -eq 997){Not-Ready 'Service is still catching up; resume the same indexed fixture'}
 if($Reply.status -ne 0 -or $Reply.error -ne 0 -or $Reply.index_state -notin @(1,2)){throw 'Search command did not return a successful healthy/catching-up response'}
}
function Is-Set($Reply,[string[]]$Labels){
 $actual=@($Reply.results|ForEach-Object{$_.label}|Sort-Object);$wanted=@($Labels|Sort-Object)
 if($Reply.result_count -ne $wanted.Count -or $actual.Count -ne $wanted.Count -or @($Reply.results|Where-Object{-not $_.metadata_ok}).Count){return $false}
 for($i=0;$i -lt $wanted.Count;$i++){if($actual[$i] -cne $wanted[$i]){return $false}}
 return $true
}
function Is-Capped($Reply,[string[]]$Labels){return $Reply.result_count -eq 1 -and $Reply.results.Count -eq 1 -and $Reply.results[0].metadata_ok -and $Reply.results[0].label -in $Labels}
function Verify-Fixture {
 if(-not $FixtureRoot -or -not [IO.Directory]::Exists($FixtureRoot)){throw 'Owned fixture directory is absent'}
 if(([IO.File]::GetAttributes($FixtureRoot) -band 1024) -or ([IO.File]::GetAttributes((Join-Path $FixtureRoot 'nested')) -band 1024)){throw 'Owned fixture directory became a reparse point'}
 $top=@([IO.Directory]::GetFileSystemEntries($FixtureRoot)|ForEach-Object{[IO.Path]::GetFileName($_)}|Sort-Object)
 $wanted=@('manifest.json','nested')+@($expected|Where-Object{$_.label -ne 'unicode'}|ForEach-Object{[IO.Path]::GetFileName($_.path)})
 if(@(Compare-Object $top @($wanted|Sort-Object)).Count){throw 'Unexpected fixture directory entries; retain for inspection'}
 $nested=@([IO.Directory]::GetFileSystemEntries((Join-Path $FixtureRoot 'nested')))
 if($nested.Count -ne 1 -or $nested[0] -cne ($expected|Where-Object{$_.label -eq 'unicode'}).path){throw 'Unexpected nested fixture entries; retain for inspection'}
 foreach($file in $expected){
  if([int][IO.File]::GetAttributes($file.path) -ne $file.attributes -or (Get-FileHash -LiteralPath $file.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256){throw 'Fixture bytes/attributes changed; retain for inspection'}
 }
 if((Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifestHash){throw 'Fixture manifest changed; retain for inspection'}
}
function Initialize-OrdinalOracle {
 if('BlinkLiveSearchV1.Native' -as [type]){return}
 $scratch=Join-Path $OutputDirectory ('compiler-'+$runId);New-Item -ItemType Directory -Path $scratch|Out-Null
 $savedTemp=$env:TEMP;$savedTmp=$env:TMP
 try{
  $env:TEMP=$scratch;$env:TMP=$scratch
  Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
namespace BlinkLiveSearchV1 { public static class Native {
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
 public static extern int CompareStringOrdinal(string left, int leftCount, string right, int rightCount, [MarshalAs(UnmanagedType.Bool)] bool ignoreCase);
} }
'@
 }finally{$env:TEMP=$savedTemp;$env:TMP=$savedTmp;Remove-Item -LiteralPath $scratch -Recurse -Force}
}
try{
 $principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent());$record.ordinary_user=-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
 Check $record.ordinary_user 'live_search_runs_as_ordinary_user';if(-not $record.ordinary_user){throw 'Ordinary-user shell required'}
 if($ResumeFixture){
  if($FixtureRoot){throw 'Use either FixtureRoot or ResumeFixture'}
  $manifest=[IO.Path]::GetFullPath($ResumeFixture);$FixtureRoot=Split-Path -Parent $manifest
 }else{
  if(-not $FixtureRoot){$FixtureRoot=Join-Path $OutputDirectory ('owned-'+[Guid]::NewGuid().ToString('N'))}
  if(-not [IO.Path]::IsPathRooted($FixtureRoot)){$FixtureRoot=Join-Path $OutputDirectory $FixtureRoot}
  $FixtureRoot=[IO.Path]::GetFullPath($FixtureRoot).TrimEnd('\');$manifest=Join-Path $FixtureRoot 'manifest.json'
 }
 if((Split-Path -Parent $FixtureRoot) -ine $OutputDirectory -or (Split-Path -Leaf $FixtureRoot) -notmatch '^owned-([0-9a-f]{32})$' -or [IO.Path]::GetFileName($manifest) -cne 'manifest.json'){throw 'Fixture must be an owned GUID child of OutputDirectory'}
 $fixtureId=(Split-Path -Leaf $FixtureRoot).Substring(6);$prefix='bls-'+$fixtureId+'-';$volume=[IO.Path]::GetPathRoot($FixtureRoot)
 if($volume -notmatch '^[A-Za-z]:\\$'){throw 'A local drive fixture is required'}
 $definitions=@(@('normal','normal.txt',32),@('hidden','hidden.txt',34),@('system','system.txt',36),@('both','both.txt',38),@('unicode','nested\Ångström-ΟΣ-Привет.txt',32))
 $expected=@(foreach($definition in $definitions){
  $leaf=if($definition[0] -eq 'unicode'){'nested\'+$prefix+'Ångström-ΟΣ-Привет.txt'}else{$prefix+$definition[1]}
  $content=$utf8.GetBytes("Blink owned live search fixture`n"+$fixtureId+"`n"+$definition[0]+"`n")
  [pscustomobject]@{label=$definition[0];relative=$leaf;path=(Join-Path $FixtureRoot $leaf);attributes=[int]$definition[2];sha256=(Hash-Bytes $content);bytes=$content.Length;content=$utf8.GetString($content)}
 })
 if($ResumeFixture){
  $saved=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8|ConvertFrom-Json
  if($saved.schema -ne 1 -or $saved.fixture_id -cne $fixtureId -or $saved.fixture_root -ine $FixtureRoot -or $saved.files.Count -ne 5){throw 'Resume manifest does not identify this fixture'}
  foreach($file in $expected){$entry=@($saved.files|Where-Object{$_.label -ceq $file.label});if($entry.Count -ne 1 -or $entry[0].relative -cne $file.relative -or $entry[0].sha256 -cne $file.sha256 -or $entry[0].attributes -ne $file.attributes){throw 'Resume manifest ownership differs from expected fixture'}}
 }else{
  if(Test-Path -LiteralPath $FixtureRoot){throw 'Refusing to replace existing fixture directory; resume its manifest explicitly'}
  New-Item -ItemType Directory -Path (Join-Path $FixtureRoot 'nested')|Out-Null
  foreach($file in $expected){$bytes=$utf8.GetBytes($file.content);$stream=[IO.File]::Open($file.path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read);try{$stream.Write($bytes,0,$bytes.Length)}finally{$stream.Dispose()};[IO.File]::SetAttributes($file.path,[IO.FileAttributes]$file.attributes)}
  [ordered]@{schema=1;fixture_id=$fixtureId;fixture_root=$FixtureRoot;prefix=$prefix;files=@($expected|Select-Object label,relative,attributes,sha256,bytes)}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath $manifest -Encoding UTF8
 }
 $manifestHash=(Get-FileHash -LiteralPath $manifest -Algorithm SHA256).Hash.ToLowerInvariant();Verify-Fixture;$fixtureValidated=$true
 foreach($file in $expected){$owned[$file.path]=$file}
 $record.fixture_root=$FixtureRoot;$record.fixture_manifest=$manifest;$record.resume_fixture=$manifest
 $record.files=@($expected|Select-Object label,relative,attributes,sha256,bytes)
 Check $true 'live_search_owned_five_file_bytes_and_attributes_verified'
 Initialize-OrdinalOracle
 $service=ReadOnlyQuery 1;if($service.status -ne 0 -or $service.error -ne 0 -or $service.service_state -ne 1){Not-Ready 'Service is not running/available'}
 Check $true 'live_search_service_status_available'
 $state=ReadOnlyQuery 2;if($state.status -ne 0 -or $state.error -ne 0 -or $state.index_state -notin @(1,2)){Not-Ready 'Volume index is not healthy/catching up'}
 Check $true 'live_search_volume_status_available'
 $scope=$FixtureRoot+'\';$all=@('normal','hidden','system','both','unicode');$direct=@('normal','hidden','system','both');$shown=@('normal','unicode');$hidden=@('normal','hidden','unicode')
 $reply=ReadOnlyQuery 5 $prefix $scope
 if($reply.status -ne 0 -or $reply.error -ne 0 -or $reply.index_state -notin @(1,2) -or -not (Is-Set $reply $all)){Not-Ready 'Owned fixture is not fully converged through USN; resume the same manifest'}
 Check $true 'live_search_legacy_recursive_confirms_all_five_indexed_files'
 $reply=ReadOnlyQuery 7 $prefix $scope;Require-Search $reply;Check (Is-Set $reply $direct) 'live_search_legacy_direct_children_keeps_all_four_attribute_classes'
 foreach($case in @(@(8,$shown,'visible_recursive'),@(9,@('normal'),'visible_direct'),@(10,$hidden,'hidden_recursive'),@(11,@('normal','hidden'),'hidden_direct'))){$reply=ReadOnlyQuery $case[0] $prefix $scope;Require-Search $reply;Check (Is-Set $reply $case[1]) ('live_search_'+$case[2]+'_exact_set')}
 foreach($case in @(@(5,$all,'legacy_recursive'),@(7,$direct,'legacy_direct'),@(8,$shown,'visible_recursive'),@(9,@('normal'),'visible_direct'),@(10,$hidden,'hidden_recursive'),@(11,@('normal','hidden'),'hidden_direct'))){$reply=ReadOnlyQuery -Type $case[0] -Text $prefix -Path $scope -Limit 1;Require-Search $reply;Check (Is-Capped $reply $case[1]) ('live_search_'+$case[2]+'_limit_one_returns_an_allowed_file')}
 $nestedScope=Join-Path $scope 'nested\'
 foreach($type in @(8,9)){$reply=ReadOnlyQuery $type $prefix $nestedScope;Require-Search $reply;Check (Is-Set $reply @('unicode')) ('live_search_nested_scope_type_'+$type+'_returns_only_unicode_file')}
 $unicodeFile=($expected|Where-Object{$_.label -eq 'unicode'});$unicodeName=[IO.Path]::GetFileName($unicodeFile.path)
 foreach($case in @(@('ångSTRÖM-οσ-привет','unicode_case'),@('ångström-ος-привет','unicode_final_sigma_ordinal'))){
  $text=$prefix+$case[0];$equal=[BlinkLiveSearchV1.Native]::CompareStringOrdinal($unicodeName,$text.Length,$text,$text.Length,$true)
  if(-not $equal){throw 'Windows ordinal oracle failed'}
  if($case[1] -eq 'unicode_case' -and $equal -ne 2){throw 'The positive Unicode case must have ordinal equality'}
  $wanted=@(if($equal -eq 2){'unicode'})
  $reply=ReadOnlyQuery 8 $text $scope;Require-Search $reply;Check (Is-Set $reply $wanted) ('live_search_'+$case[1]+'_matches_windows_oracle')
  $record.unicode_oracle+=@([pscustomobject]@{case=$case[1];compare_string_ordinal=$equal;expected=$wanted.Count;actual=$reply.result_count})
 }
 $reply=ReadOnlyQuery -Type 8 -Text $prefix -Path $scope -Extension '.TXT';Require-Search $reply;Check (Is-Set $reply $shown) 'live_search_visible_extension_query_preserves_unicode_match'
 Check ($requests.Count -eq 19 -and -not @($requests|Where-Object{-not $_.correlated -or $_.type -notin @(1,2,5,7,8,9,10,11)}).Count) 'live_search_only_readonly_correlated_v1_requests_sent'
 $record.status='PASS'
}catch{
 if($notReady){$record.status='NOT_READY';$record.not_ready_reason=$notReady}else{$record.status='FAIL';$record.failure=$_.Exception.Message}
}finally{
 if($fixtureValidated){
  if($record.status -eq 'NOT_READY'){$record.cleanup=[ordered]@{status='RETAINED_NOT_READY';reason='ResumeFixture reuses these exact files; no new GUID required';resume_fixture=$manifest}}
  else{
   try{
    Verify-Fixture;$removedBytes=([long](Get-Item -LiteralPath $manifest).Length)
    foreach($file in $expected){$removedBytes+=$file.bytes;[IO.File]::SetAttributes($file.path,[IO.FileAttributes]::Archive);[IO.File]::Delete($file.path)}
    [IO.Directory]::Delete((Join-Path $FixtureRoot 'nested'),$false);[IO.File]::Delete($manifest);[IO.Directory]::Delete($FixtureRoot,$false)
    $record.cleanup=[ordered]@{status='REMOVED';files=6;directories=2;bytes=$removedBytes};$record.resume_fixture=$null;Check $true 'live_search_owned_fixture_precisely_removed'
   }catch{$record.cleanup=[ordered]@{status='RETAINED_FOR_INSPECTION';reason=$_.Exception.Message;resume_fixture=$manifest};$record.status='FAIL';Check $false 'live_search_owned_fixture_precisely_removed'}
  }
 }else{$record.cleanup=[ordered]@{status='NOT_VALIDATED';reason='No deletion of an unvalidated or incomplete fixture';fixture_root=$FixtureRoot}}
 $record.ended_utc=[DateTime]::UtcNow.ToString('o');$record.checks=@($checks.ToArray());$record.requests=@($requests.ToArray())
 $record.counts=[ordered]@{PASS=@($checks|Where-Object{$_.status -eq 'PASS'}).Count;FAIL=@($checks|Where-Object{$_.status -eq 'FAIL'}).Count;NOT_READY=$(if($record.status -eq 'NOT_READY'){1}else{0})}
 if($record.status -eq 'PASS' -and ($checks.Count -ne 23 -or $record.counts.FAIL -ne 0 -or $record.cleanup.status -ne 'REMOVED')){$record.status='FAIL';$record.failure='Exact successful 23-check contract was not met'}
 $lines.Add('STATUS '+$record.status);[IO.File]::WriteAllLines($logPath,$lines.ToArray(),$bom)
 $record.log_sha256=(Get-FileHash -LiteralPath $logPath -Algorithm SHA256).Hash.ToLowerInvariant()
 [IO.File]::WriteAllText($reportPath,($record|ConvertTo-Json -Depth 9),$bom)
 [ordered]@{status=$record.status;report=$reportPath;report_sha256=(Get-FileHash -LiteralPath $reportPath -Algorithm SHA256).Hash.ToLowerInvariant();resume_fixture=$record.resume_fixture}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $OutputDirectory 'live-search-latest.json') -Encoding UTF8
 Write-Host ('Live search '+$record.status+'; '+$record.counts.PASS+' PASS / '+$record.counts.FAIL+' FAIL; report='+$reportPath)
 if($record.status -eq 'NOT_READY'){Write-Host ('ResumeFixture: '+$record.resume_fixture)}
}
if($record.status -eq 'PASS'){exit 0};if($record.status -eq 'NOT_READY'){exit 3};exit 1
