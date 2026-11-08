# Blink Phase 0.5 Acceptance Run Report

## Current final disposition — Security Closure Run `20260811-233345`

**Phase 0.5 = ACCEPTED / COMPLETE**  
**Phase 1 gate = READY TO PROCEED**

Run `20260811-233345` formally closed the Security Closure slice with `targeted security slice = PASS` and `failures = 0`. Its top-level `final_result=NOT READY` / `phase1_recommendation=DO NOT PROCEED` is the fixed aggregation behavior of a slice-scoped runner; it is not a security failure and does not override the combined Phase 0.5 status.

Accepted Production Service Security Baseline:

- Logon account: `LocalSystem`.
- Service SID type: `SERVICE_SID_TYPE_UNRESTRICTED`.
- Service SID: `NT SERVICE\Blink.IndexService`.
- RequiredPrivileges: `SeImpersonatePrivilege`.
- Runtime TokenUser/TokenGroups/TokenPrivileges, including Service SID membership and enabled privilege state, were captured from the running process and stored as machine-readable evidence.

Accepted Named Pipe ACL Hardening:

- SYSTEM: `0x0012019F`.
- Service SID: `0x0012019F`.
- Authorized Client: `0x00120183`.
- No Everyone; no Authenticated Users; `PIPE_REJECT_REMOTE_CLIENTS` remains enabled.
- The client ACE does not contain `FILE_CREATE_PIPE_INSTANCE`.

Security Closure also recorded and fixed two real issues: (A) impersonation lifecycle and unsafe process-token fallback, corrected with exception-safe revert/fail-closed handling; and (B) the `PIPE_ACCESS_DUPLEX` server ACE missing `FILE_READ_EA | FILE_WRITE_EA`, corrected from `0x00120187` to `0x0012019F` while retaining the client mask.

All historical FAIL/NOT READY runs below retain their original results. They are preserved as Harness, permission, and security-convergence evidence; they are not rewritten as successful runs.

Date: 2026-08-12  
Workspace: `D:\Arduino\src\Blink_文件管理器`  
Data/cache policy: project-local `.blink-data`, `.build`, and `.nuget` on D:

## 1. Final Result

**ACCEPTED / COMPLETE**

The combined Phase 0.5 evidence is accepted: full MFT/USN initial and incremental indexing, persistent BLK5 save/load and journal recovery, Search correctness/performance, continuous-write Search sentinel convergence (`20260811-201439`), service failure isolation, corruption and journal-invalid `RebuildRequired`, Windows Restart Acceptance (`20260811-204329`), and Security Closure (`20260811-233345`). LocalService's known `FSCTL_QUERY_USN_JOURNAL` error 5 remains explicitly recorded; it does not invalidate the accepted, explicitly selected LocalSystem prototype/production baseline.

No remaining Phase 0.5 blocker is identified. Virtual service account and `SERVICE_SID_TYPE_RESTRICTED` work remains a future hardening experiment, not a Phase 1 blocker. This status update does not enter Phase 1 or authorize unrelated product work.

Important artifact interpretation: the full run's `phase05-results.json` contains real LocalSystem service-side MFT/BLK5 evidence and five completed Search benchmark rows. Exact/prefix/substring/directory-scoped queries returned 1/1/13/1 results and extension returned 11 results. All five known-hit correctness rows returned valid identities and scope results, and the known miss returned 0. The mutation stage's File ID, Parent ID, lazy-path, hard-link, Unicode, create, and delete assertions passed, and this run observed non-initial CatchUp telemetry with error 0. Continuous-write stress completed its bounded writer and 18 fixed-target CatchUp batches; the subsequent targeted run accepted the Search-level sentinel create/delete convergence. The synthetic journal-invalid test reached terminal `RebuildRequired` with Win32 error 1181; only the historical machine-readable Harness expected-code flag was stale.

A unified manual-admin runner is available at `scripts/run-phase05-elevated-acceptance.ps1`. It refuses non-Administrator/Medium-integrity execution before any test action, builds both configurations, tests LocalService first, requires an explicit `LOCAL_SYSTEM` choice before a genuine volume-operation fallback, and writes `phase05-acceptance.log`, `phase05-results.json`, `phase05-benchmark.csv`, `phase05-failures.log`, and a manual Windows-restart instruction file under `.blink-data\acceptance`. The service records rejected-client API/PID/Win32 diagnostics under the acceptance telemetry root. The PowerShell client requests `TokenImpersonationLevel.Impersonation`, and the native `Blink.Index.Contracts` client requests matching `SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION` flags. The progress-response recovery works; the script uses bounded 120-second BLK5 load/Healthy warm-ups before search and after service restarts, while disconnect probes retain short timeouts. The Search benchmark retries only explicit Busy/997 responses within a bounded 120-second stage window. After run `20260811-162754`, the Harness was corrected to expect `ERROR_JOURNAL_ENTRY_DELETED (1181)` and to record the last Search candidates when sentinel convergence expires. PowerShell parsing and the UTF-8 child-process smoke test remain passing; no full elevated acceptance was rerun after these Harness-only corrections.

### Ordinary pre-acceptance confirmation (no service, no elevation)

The volume-serial probe read the same target through `D:\`, the project DataRoot, and a DataRoot child. All three targets reported:

| API/value | Result |
| --- | --- |
| `GetVolumeInformationW` DWORD | `0xC8B2C41B` |
| `FILE_ID_INFO.VolumeSerialNumber` | `0x6AC8B2FAC8B2C41B` |
| `FILE_ID_INFO` low32 | `0xC8B2C41B` |
| `FILE_ID_INFO` high32 | `0x6AC8B2FA` |
| `BY_HANDLE_FILE_INFORMATION.dwVolumeSerialNumber` | `0xC8B2C41B` |
| Mapping | **PASS** for the current D: target |

The low32 value is used only as the canonical 32-bit key at the USN/InternalExcludedRoot identity boundary. Full `FileIdentity` continues to retain the complete FILE_ID_INFO serial and 128-bit FileId. The FastIndex root/path graph now compares the full root identity to USN identities through this canonical boundary without overwriting the full value.

The retained `20260811-112040` BLK5 was loaded by the ordinary offline regression executable. Separate correctness assertions passed: known exact `1`, known prefix `1`, known substring `12`, known extension `10`, directory-scoped `1`, and known miss `0`. The original all-zero elevated rows were therefore not valid search misses; they were caused by root identity representation mismatch and path reconstruction failure.

The same query set was sampled five times per known-hit case. Results: exact `min=2537.78`, `median=2660.04`, `p95=3055.58`, `max=3149.53 ms`; prefix `2678.85/2700.60/2931.87/2984.62 ms`; substring `2801.85/2865.39/2889.35/2893.69 ms`; directory-scoped `2653.86/2751.57/2790.04/2796.19 ms`; extension `6011.97/6183.51/6282.32/6289.93 ms`. The extension query is a confirmed stable over-5-second performance blocker; the IPC timeout was not increased. The CSV is `.blink-data\acceptance\offline-search-20260811-112040-fixed.csv`.

The elevated harness now has a separate `search_correctness` section with known-hit/known-miss assertions and an out-of-scope duplicate for the directory-scoped case. This ordinary regression did not start a service, elevate, or run full acceptance. Final result remains **NOT READY**.

### 2026-08-11 offline extension-index performance precondition (no service, no elevation)

The retained `20260811-112040` BLK5 was used for a Debug offline before/after run. The reference run forced the full-scan path; the after run loaded the same BLK5 and rebuilt the derived in-memory extension index. The BLK5 format and authoritative `links_` data were unchanged. The clean after CSV is `.blink-data\acceptance\offline-search-after-20260811-112040-final.csv`; the clean reference CSV is `.blink-data\acceptance\offline-search-before-20260811-112040-final.csv`.

All correctness sentinels passed in the clean after run: exact `1`, prefix `1`, substring `12`, extension `.txt` `10`, directory-scoped `1`, directory + extension `1`, and known miss `0`. Each known hit asserted its expected path and identity; extension cases also matched the full-scan oracle. The separate ordinary `--search-index-only` regression passed hard-link multi-name handling, BLK5-load rebuild, Create/Rename/Delete bucket maintenance, and InternalExcludedRoots filtering. The retained acceptance fixture's hard link was created before Initial MFT, so its old BLK5 contains only one name; the elevated fixture now creates that hard link after Initial Index and verifies it through USN Catch-up instead of treating the stale pre-initial fixture as a pass.

| Query family | Result count | Reference min/median/p95/max ms | Derived-index min/median/p95/max ms | Derived stage medians: candidate / path / result / sort ms |
| --- | ---: | --- | --- | --- |
| exact | 1 | 2330.05 / 2333.72 / 2342.66 / 2343.65 | 2272.54 / 2389.02 / 2413.10 / 2415.78 | 2387.99 / 0.98 / 0.008 / 0.002 |
| prefix | 1 | 2349.18 / 2363.93 / 2400.79 / 2404.88 | 2366.93 / 2451.27 / 2462.73 / 2464.00 | 2450.17 / 0.96 / 0.005 / 0.001 |
| substring | 12 | 2485.98 / 2517.00 / 2652.20 / 2667.22 | 2410.49 / 2448.67 / 2611.51 / 2629.60 | 2441.82 / 6.72 / 0.034 / 0.417 |
| extension | 10 | 5325.97 / 5336.71 / 5423.90 / 5433.58 | 192.03 / 240.31 / 244.70 / 245.18 | 61.03 / 175.58 / 0.031 / 0.256 |
| directory-scoped | 1 | 2324.20 / 2386.08 / 2396.18 / 2397.30 | 2276.32 / 2350.70 / 2396.30 / 2401.37 | 2349.26 / 1.29 / 0.006 / 0.001 |
| directory + extension | 1 | 5306.03 / 5341.03 / 5444.20 / 5455.67 | 186.62 / 216.38 / 216.85 / 216.90 | 46.55 / 153.67 / 0.011 / 0.003 |

Each row used three samples; p95 is the interpolated percentile of those samples. Extension median improved from `5336.71 ms` to `240.31 ms`, and max from `5433.58 ms` to `245.18 ms`; directory + extension shows the same reduction from `5341.03/5455.67 ms` to `216.38/216.90 ms`. Exact/prefix/substring remain full `O(N)` name scans by design and remain below the 5-second IPC budget in this Debug run. No IPC timeout was increased. The derived map is maintained on link mutations and rebuilt after BLK5 Load; `links_` remains authoritative and no checkpoint ABI change was made.

This removed the offline Search-performance precondition blocker and left the project ready for manual privileged acceptance at that point. It did not reclassify the full Phase 0.5 result then; the later full-run, targeted sentinel, and Windows Restart evidence are recorded below.

### Latest full manual run: 20260811-162754

| Area | Result |
| --- | --- |
| Administrator / Integrity | **PASS** — Administrator, High Integrity |
| Target volume | **PASS** — `D:\`, NTFS; direct volume probe passed |
| Direct volume handle / USN probes | **PASS** — MFT `1,012,833`, USN read `9`; NTFS V0 inputs |
| LocalService | **FAIL at `FSCTL_QUERY_USN_JOURNAL`**, Win32 `5` (`ERROR_ACCESS_DENIED`); enum/read not executed under this account |
| LocalSystem | **PASS** — explicitly selected, installed, started, and completed all volume operations |
| Initial MFT index | **PASS** — files `911,110`, directories `101,750`, records seen `1,012,889` |
| Initial telemetry | MFT records `1,012,860`; USN records `29`; MFT enumeration `138.039 s`; parent-tree `133.662 s`; total `144.940 s`; CPU `144.469 s`; peak working set `755,896,320` bytes; Save `6.67079 s`; BLK5 `113,131,795` bytes |
| BLK5 load / Journal validation | **PASS to Healthy** — `journal_id=134011753522823542` |
| Search benchmark | **PASS** — averages exact/prefix/substring/extension/directory `2268.52/2189.24/2413.42/245.67/2278.33 ms`; P95 `2411.72/2340.84/2508.02/284.89/2428.64 ms`; result counts `1/1/13/11/1` |
| Search correctness | **PASS** — known exact/prefix/substring/extension/directory hit and known miss `0`; identity and scope assertions passed |
| USN mutations | **PASS** — create/delete/rename/move, directory operations, lazy path, File ID/Parent ID, hard link, Unicode, and non-initial CatchUp telemetry |
| Continuous write stress | **PENDING AT THIS FULL RUN** — writer bounded and 18 fixed-target CatchUp batches completed; the later targeted run accepted sentinel create/delete convergence |
| Restart recovery | **PASS** — BLK5 load, Journal validation, USN CatchUp without full MFT rebuild, and change during service stop recovered; `76.876 s` client observation |
| Service failure | **PASS** — Stop disconnect `996.855 ms`, Kill disconnect `995.906 ms`, local browse remained available, reconnect reached Healthy |
| Synthetic invalid checkpoint | **Runtime behavior PASS / Harness flag stale** — terminal state `RebuildRequired`, Win32 `1181` (`ERROR_JOURNAL_ENTRY_DELETED`); JSON still expected `1178` in this run |
| Store corruption | **PASS** — corrupted store reported `Corrupted`, error `13` |
| Windows Restart Acceptance | **NOT EXECUTED** |
| Final result | **NOT READY** |

### Targeted Continuous-write Search sentinel acceptance: 20260811-201439

**Continuous-write Search sentinel convergence: ACCEPTED.**

This was a targeted acceptance slice, not a rerun of the complete Phase 0.5 suite. The runner copied the valid seed BLK5 from the previous run into the new RunId-specific `service-data-targeted` directory, skipped Initial MFT enumeration, loaded the copy with explicit LocalSystem, reached `Healthy`, ran the bounded continuous writer, and used the real service IPC/Search/CatchUp path. The source BLK5 was not modified.

- Create path: sentinel name, exact path, File ID, and canonical low32 volume identity converged through Search after USN CatchUp while the writer was running.
- Delete path: delete USN CatchUp completed and bounded Search polling reached an empty result for the sentinel.
- Search artifacts retained every attempt with query, candidates, result count, operation ID, old/target/resulting USN; the run's writer and CatchUp evidence are under `.blink-data\acceptance\20260811-201439`.
- `phase05-results.json` records the targeted sentinel result as `PASS`. Its top-level `final_result=NOT READY` is intentional: targeted mode does not recompute or claim the complete Phase 0.5 result. It must not be interpreted as a full-suite PASS or as Phase 1 approval.

This targeted result formally closes the Continuous-write Search sentinel convergence condition. Windows Restart Acceptance remains the next distinct manual acceptance slice and was not executed by this run.

The runner provides `-PrepareWindowsRestartAcceptance` and `-CompleteWindowsRestartAcceptance -RunId <id>`. Prepare writes the persistent `reboot-state.json`, retains the stopped explicit-LocalSystem SCM registration, and never restarts Windows. Complete reads that same state, proves a later OS boot session, loads the copied BLK5, requires non-initial USN CatchUp telemetry, and verifies the sentinel's path and identity before cleanup.

### Windows Restart Acceptance: 20260811-204329

**PASS.** This two-phase acceptance used the same persistent `reboot-state.json` and the copied RunId-specific BLK5. Prepare reached `Healthy`, recorded Journal ID `134011753522823542`, LastAppliedUSN `3663785776`, and stopped the explicit-LocalSystem SCM service. A real Windows reboot was proven by the boot session changing from `2026-08-11T20:39:54.5000000Z` to `2026-08-11T20:51:29.5000000Z`.

After reboot, Complete started the retained service, loaded the copied BLK5, validated Journal continuity, and observed non-initial USN CatchUp operation `10` with `initial=false`, `mft_records=0`, and `catchup_completed_usn=3664878520`. Search returned the RebootSentinel with `result_count=1`; exact path and FileIdentity checks both passed. The service registration and owned fixture were removed only after PASS. State is `CompletePass` in `.blink-data\acceptance\20260811-204329\reboot-state.json`.

The Complete runner's top-level `final_result=NOT READY` is intentional because this mode reports only the Windows Restart slice and does not recompute the full Phase 0.5 suite. It must not override the combined full-suite result above.

The `20260811-162754` stress result was not silently reclassified: its retained telemetry proves that the writer completed, 18 fixed-target CatchUp batches completed, USN advanced from `3659160096` to `3659321536`, and IPC latency remained bounded. That historical run did not retain enough Search payload to prove the generated sentinel, which is why the separate targeted run was required. The targeted run subsequently accepted the create/delete convergence, so this is no longer an unresolved runtime condition.

The journal-invalid runtime evidence is valid even though `phase05-results.json` says `rebuild_required=false`: the service returned terminal `RebuildRequired` with error `1181`, and the installed SDK defines `1181` as `ERROR_JOURNAL_ENTRY_DELETED`; `1178` is `ERROR_JOURNAL_DELETE_IN_PROGRESS`. The Harness assertion has been corrected locally and this historical run was not rerun.

### Previous manual run: 20260811-153431

| Area | Result |
| --- | --- |
| Administrator / Integrity | **PASS** — Administrator, High Integrity |
| Target volume | **PASS** — `D:\`, NTFS; direct volume probe passed |
| Direct volume handle / USN probes | **PASS** — MFT `1,012,438`, USN read `3` |
| LocalService | **FAIL at `FSCTL_QUERY_USN_JOURNAL`**, Win32 `5` (`ERROR_ACCESS_DENIED`) |
| LocalSystem | **PASS** — explicitly selected, installed, started, and completed initial operation |
| Initial MFT index | **PASS** — files `910,734`, directories `101,734`, records seen `1,012,650` |
| Initial telemetry | MFT `1,012,468`; USN `182`; MFT enumeration `140.645 s`; parent-tree `136.197 s`; total `147.708 s`; CPU `146.984375 s`; peak working set `756,981,760` bytes; Save `6.81049 s`; BLK5 `113,091,531` bytes |
| BLK5 load / Journal validation | **PASS to Healthy** — `journal_id=134011753522823542` |
| Search benchmark | **PASS as performance run** — averages exact/prefix/substring/extension/directory `2225.81/2289.87/2339.04/213.76/2227.41 ms`; P95 `2259.48/2556.31/2386.81/240.41/2288.81 ms`; result counts `1/1/13/11/1` |
| Search correctness | **HARNESS FAIL** — five known-hit rows returned valid hits but Harness accessed `.Count` on a scalar conditional result; known miss passed |
| USN mutations | **PASS** — individual File ID, Parent ID, lazy-path, delete, hard-link, Unicode, and directory assertions passed; non-initial CatchUp telemetry observed with error `0` |
| Continuous write stress | **NOT ACCEPTED** — writer completed, 15 fixed-target CatchUp batches completed, but sentinel create was checked before the new USN became searchable |
| Restart recovery | **PASS** — BLK5 load, Journal validation, CatchUp without full MFT rebuild, and changes during service stop recovered |
| Service failure | **PASS** — bounded Stop disconnect, local browse while stopped, bounded Kill disconnect, restart and Healthy reconnect |
| Synthetic invalid checkpoint | **HARNESS FAIL** — first response was asynchronous `Busy/997/Rebuilding`; Harness did not wait for terminal `RebuildRequired`; store restoration reached Healthy |
| Store corruption | **PASS** — corrupted store reported `Corrupted`, error `13` |
| Windows Restart Acceptance | **NOT EXECUTED** |
| Final result | **NOT READY** |

The run's real-service Search benchmark remained below the 5-second per-request deadline, including extension `.txt` at `213.76 ms` average and `240.41 ms` P95. The known-hit Search rows were not accepted only because the Harness attempted `.Count` on a scalar conditional collection; their result counts were `1/1/13/11/1`, identities matched the fixture, and scope checks were valid.

The mutation and recovery evidence is materially stronger than the prior run. The selected post-mutation CatchUp produced telemetry, the continuous writer completed without IPC deadlock and advanced USN monotonically, the service survived stop/kill/reconnect, and restart CatchUp recovered changes made while stopped without a full MFT rebuild. The stress sentinel needs a new search-level wait because `Healthy` may be observed before the scheduler has applied the just-created USN. The invalid-checkpoint test likewise needs to wait through the expected asynchronous load state to terminal `RebuildRequired`.

Harness-only corrections are now present for all three failures: conditional Search collections are forced to arrays, sentinel create/delete assertions poll Search itself, and synthetic invalid-store status polls for `RebuildRequired`. No product index/service code was changed for these findings, no timeout was increased, and no elevated acceptance was rerun after the corrections.

### Previous manual run: 20260811-145851

| Area | Result |
| --- | --- |
| Administrator / Integrity | **PASS** — Administrator, High Integrity |
| Target volume | **PASS** — `D:\`, NTFS; direct volume probe passed |
| Direct volume handle / USN probes | **PASS** — MFT `1,012,110`, USN read `6` |
| LocalService | **FAIL at `FSCTL_QUERY_USN_JOURNAL`**, Win32 `5` (`ERROR_ACCESS_DENIED`) |
| LocalSystem | **PASS** — explicitly selected, installed, started, and completed initial operation |
| Initial MFT index | **PASS** — files `910,421`, directories `101,718`, records seen `1,012,222` |
| Initial telemetry | MFT `1,012,139`; USN `83`; MFT enumeration `153.055 s`; parent-tree `148.085 s`; total `160.289 s`; CPU `157.09375 s`; peak working set `755,957,760` bytes; Save `6.95427 s`; BLK5 `113,058,621` bytes |
| BLK5 load / Journal validation | **PASS to Healthy** — `journal_id=134011753522823542` |
| Search benchmark | **PASS as performance run** — averages exact/prefix/substring/extension/directory `2684.77/2781.04/2729.05/244.16/2465.48 ms`; P95 `3463.71/3307.52/2898.66/276.76/2700.55 ms`; result counts `1/1/13/11/1` |
| Search correctness | **HARNESS FAIL** — five known-hit assertions hit PowerShell `UInt64(-1)` conversion; known miss passed |
| USN mutations | **Functional assertions PASS**; selected CatchUp telemetry evidence was absent and recorded as a harness failure |
| Continuous write stress | **NOT ACCEPTED** — harness stopped on strict-mode `.name` access in `Find-Hit` |
| Full client restart, service stop/kill, invalid journal, corruption | **NOT EXECUTED** |
| Windows Restart Acceptance | **NOT EXECUTED** |
| Final result | **NOT READY** |

The retained `search-timing.jsonl` records small lock-wait samples (roughly `0–4 ms`) and completed real-service Search work in the low-second range; the extension query completed in about `244 ms` average and `277 ms` P95. The Search implementation still scans all in-memory links for exact/prefix/substring/directory name queries (`O(N)` in indexed links), while extension queries use the derived in-memory extension candidates. No fresh directory traversal is requested by Search, and no IPC timeout was increased.

This run is evidence that the previous Search timeout blocker is not present in the current real-service benchmark. It is not a full acceptance: known-hit correctness must be rerun after the harness mask fix, and the post-mutation/read-journal, stress, restart, failure, invalid-journal, and corruption stages still need to complete.

### Previous manual run: 20260811-112040

The previous run remains historical evidence for the pre-derived-index Search timeout and is not the latest result. Its exact/prefix/substring queries returned zero rows before the volume-identity and derived-extension corrections were runtime-tested.

### 2026-08-11 harness/API correction (not a new acceptance result)

The source and harness audit found that the previous probe description treated the V1 input structures as a single NTFS/ReFS route. The corrected implementation now selects `MFT_ENUM_DATA_V0` and `READ_USN_JOURNAL_DATA_V0` for NTFS, and selects the V1 forms with `MinMajorVersion=2` and `MaxMajorVersion=3` for ReFS; parsed USN record major versions are checked against the selected range. The 2026-08-10 LocalSystem measurements above remain historical results from the previous implementation and are not silently reclassified as evidence for the corrected NTFS path. A new manual Administrator/High Integrity acceptance run is required.

The runner was also corrected so target/live-harness volume isolation is checked before any harness or fixture write, service DataRoot is checked against the target volume, all nested waits consume an absolute stage deadline, persisted-store loading is exempt from the zero-CatchUp inactivity false positive, stress writer completion and fixed-target catch-up batches are recorded, artifact export failure is represented separately, and long initial/load stages have a bounded 120-second IPC transport grace. Run `20260811-054222` passed those harness checks but showed that the prior per-CatchUp reset/telemetry suppression was insufficient because internal-root classification itself was incomplete. A follow-up source fix now resolves the excluded DataRoot identity once and checks the in-memory parent identity chain before path reconstruction.

### Previous manual run: 20260811-054222

| Area | Result |
| --- | --- |
| Administrator / Integrity | **PASS** — Administrator, High Integrity |
| Target volume | **PASS** — `D:\`, NTFS; direct volume probe passed |
| Direct volume handle / USN probes | **PASS** — MFT `1,011,206`, USN read `58` |
| LocalService | **FAIL at `FSCTL_QUERY_USN_JOURNAL`**, Win32 `5` |
| LocalSystem | **PASS** — service installed, started, and completed initial operation |
| Initial MFT index | **PASS** — files `909,605`, directories `101,627`, records seen `1,011,286` |
| Initial telemetry | MFT `1,011,232`; MFT enumeration `49.8042 s`; parent-tree `45.7152 s`; total `56.6801 s`; CPU `56.453125 s`; peak working set `642,007,040` bytes; Save `6.65208 s`; BLK5 `112,958,985` bytes |
| BLK5 load / CatchUp | **NOT COMPLETE** — repeated internal CatchUp/Save loop until stage deadline |
| Search, mutations, restart, failure, invalid journal | **NOT EXECUTED** |
| Final result | **NOT READY** |

The initial operation is real evidence, but it is not a complete acceptance because the load-time recovery did not settle. The retained telemetry proves the prior fix reset the counter per operation, while the remaining identity/path classification bug kept every internal CatchUp dirty. The new identity-chain fix was compiled and covered by ordinary tests only; it requires one new manual elevated run for runtime acceptance.

Encoding side finding: the saved `phase05-acceptance.log` starts with a UTF-8 BOM and preserves `Blink_文件管理器`. Redirected `icacls.exe` output is correctly decoded with OEM code page 936 by the installer helper; a read-only comparison reproduced mojibake only when that native stream was incorrectly decoded as UTF-8. The attached pasted log has no BOM and must be read explicitly as UTF-8 under Windows PowerShell 5.1.

### Previous manual run: 20260811-031602

| Area | Result |
| --- | --- |
| Administrator / Integrity | **PASS** — Administrator, High Integrity |
| Target volume | **PASS** — `D:\`, NTFS, label `软件` |
| Direct volume handle / USN probes | **PASS** — MFT `1,011,070`, USN read `26` |
| LocalService | **FAIL at `FSCTL_QUERY_USN_JOURNAL`**, Win32 `5` |
| LocalSystem | **PASS** — service installed, started, and completed initial operation |
| Initial MFT index | **PASS** — files `909,482`, directories `101,612`, records `1,011,451` |
| BLK5 load + load-time CatchUp | **PASS** — reached `Healthy`, no initial rebuild |
| Search benchmark | **NOT COMPLETE** — first Search returned; second request received `Busy/CatchingUp/997` |
| Artifact export | **PASS** |
| Final result | **NOT READY** |

Initial operation telemetry: MFT enumeration `67.998 s`, parent-tree build `62.2897 s`, total operation `76.6558 s`, CPU `73.53125 s`, peak working set `642,736,128` bytes, BLK5 save `8.25495 s`, BLK5 size `112,941,545` bytes. The service-side operation reported `1,011,094` MFT records and `357` USN records.

The first Search timing record shows lock wait `0.002 ms`, actual FastIndex Search `3,176.364 ms`, serialization `14.264 ms`, and pipe write `0.009 ms`. The later request was answered in about `7.6 ms` without entering FastIndex Search, because the scheduler had already started a CatchUp. Source inspection found the trigger: the scheduler reacts to any pending USN, while the service DataRoot/telemetry is intentionally on D:. Before the fix, `user_mutations_applied` accumulated across CatchUp operations and pure internal operations still wrote telemetry to D:, creating a self-sustaining CatchUp/Save loop. The fix is present in source but was not included in this already completed manual run; a new manual run is required to accept it.

### Previous manual run: 20260811-012750

| Area | Result |
| --- | --- |
| Administrator / Integrity | **PASS** — Administrator, High Integrity |
| Target volume | **PASS** — `D:\`, NTFS, label `软件` |
| Direct volume handle | **PASS** |
| Direct `FSCTL_QUERY_USN_JOURNAL` | **PASS** |
| Direct `FSCTL_ENUM_USN_DATA` | **PASS** — `MFT_ENUM_DATA_V0`, 24 bytes, `1,009,877` records, `4.3897694 s` |
| Direct `FSCTL_READ_USN_JOURNAL` | **PASS** — `READ_USN_JOURNAL_DATA_V0`, 40 bytes, 3 records, `0.0005453 s` |
| LocalService | **FAIL at `FSCTL_QUERY_USN_JOURNAL`**, Win32 `5` |
| LocalSystem | **NOT DETERMINED** — initial IPC progress read timed out; no terminal service telemetry |
| Artifact export | **PASS** |
| Final result | **NOT READY** |

Source inspection explains the transport symptom: `FastIndex::BuildInitial` holds the index mutex while building, while progress statistics take the same mutex. A 30-second transport grace was too short for this real volume. The runner now uses 120 seconds for the initial/load/reconnect stages, still bounded by the absolute stage deadline; no service or index architecture redesign was made.

## 2. Service Account

| Account | Install/start | Volume access | USN query | MFT enum | USN read | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| `NT AUTHORITY\LocalService` | **PASS** | **PASS** | **FAIL** | **NOT EXECUTED** | **NOT EXECUTED** | Latest run installed/started the SCM service and accepted IPC. The service reached `FSCTL_QUERY_USN_JOURNAL`, which returned Win32 error `5` (`ERROR_ACCESS_DENIED`). MFT enum and USN read were not reached. |
| `LocalSystem` | **PASS** | **PASS** | **PASS** | **PASS** | **PASS** | Explicitly selected and started. Volume handle, query, MFT enumeration, and USN read completed; service-side initial operation reported error `0`. Post-mutation CatchUp completed with error `0` and non-initial telemetry was observed. |

Current development token evidence: current interactive user, `IsAdmin=False`, `Medium Mandatory Level` (`S-1-16-8192`), Administrators group `deny only`. The manual run's elevated token was not assumed to be the Codex token.

**Accepted Service Account Baseline:** `LocalSystem` with `SERVICE_SID_TYPE_UNRESTRICTED`, Service SID `NT SERVICE\Blink.IndexService`, and RequiredPrivileges `SeImpersonatePrivilege`, based on Security Closure Run `20260811-233345`.  
**Production Service Security Baseline Status:** ACCEPTED.  

The pre-closure warning against declaring LocalSystem without runtime evidence is historical; the cited runtime token and hardened Pipe evidence now exist in Run `20260811-233345`.

## 3. Real Volume Test

The target volume was checked by the latest manual elevated process. The corrected structured direct-probe values are:

| Field | Value |
| --- | --- |
| Volume | `D:\` |
| File system | NTFS |
| Volume label | `软件` |
| Volume GUID | **Not emitted in latest structured result** |
| Size at check | **Not emitted in latest structured result** |
| Free space at check | **164,502,151,168 bytes** |
| Journal ID | **134011753522823542** |
| Direct `FSCTL_ENUM_USN_DATA` MFT record count | **1,012,833** (native probe; not a service-index file count) |
| Direct `FSCTL_ENUM_USN_DATA` elapsed time | **1.7399043 s** (native probe; not a service-index benchmark) |
| Direct `FSCTL_READ_USN_JOURNAL` records | **9** (native probe; not a service catch-up result) |
| Direct `FSCTL_READ_USN_JOURNAL` elapsed time | **0.0004648 s** (native probe; not a service catch-up benchmark) |
| LocalSystem service file count | **911,110** |
| LocalSystem service directory count | **101,750** |

The small Phase 0 DFS probe is not a real-volume MFT result and is reported separately below.

## 4. Initial Index Benchmark

**Service-side real `FSCTL_ENUM_USN_DATA` benchmark: PASS in the latest run.**

The latest manual process proved that the corrected elevated direct IOCTL probe can open D: and enumerate `1,012,833` MFT records. LocalService proved volume-handle open but was denied at `FSCTL_QUERY_USN_JOURNAL`. The explicitly selected LocalSystem service completed the real initial operation with `911,110` files, `101,750` directories, and `1,012,889` records seen. Service telemetry counted `1,012,860` MFT records and 29 USN records. MFT enumeration took `138.039 s`, parent-tree build `133.662 s`, total operation `144.940 s`, CPU `144.46875 s`, peak working set `755,896,320` bytes, BLK5 save `6.67079 s`, and BLK5 size was `113,131,795` bytes.

The older runs remain historical evidence only; the `20260811-162754` figures above are the current real-volume benchmark.

The only reproducible non-privileged baseline is the existing Phase 0 DFS probe:

| Metric | Result |
| --- | --- |
| Files / directories | 1 / 0 |
| DFS build time | 0.0023622 s |
| In-memory search | 1 result, 0.0922 ms |
| Working set | 13,930,496 bytes |
| Direct USN probe | unavailable, Win32 error 5 (`ERROR_ACCESS_DENIED`) |

This baseline must not be used as MFT/USN performance evidence.

## 5. Search Benchmark

**Privileged Fast Index search benchmark and correctness: PASS for this run.**

The latest run reached the benchmark after BLK5 load and CatchUp. Exact filename averaged `2268.52 ms` (P95 `2411.72 ms`, result `1`); prefix `2189.24 ms` (P95 `2340.84 ms`, result `1`); substring `2413.42 ms` (P95 `2508.02 ms`, result `13`); extension `.txt` `245.67 ms` (P95 `284.89 ms`, result `11`); directory-scoped `2278.33 ms` (P95 `2428.64 ms`, result `1`). All requests completed within the 5-second IPC budget. The five known-hit assertions passed with valid identities/scope, and the known miss returned zero results.

The service-side timing confirms that the real Search responses were not blocked by a long named-pipe write; lock-wait samples were only a few milliseconds and the derived extension path completed in the hundreds of milliseconds. The source path remains `HandleClient` Search request -> running-operation/loaded-state checks -> `FastIndex::Search` -> response construction -> serialization -> named-pipe write. Exact/prefix/substring/directory name queries remain `O(N)` in-memory scans with lazy path resolution; extension queries use the derived extension index. No IPC timeout was increased.

## 6. USN Incremental Test

**Real user-mutation service-account `FSCTL_READ_USN_JOURNAL` catch-up: functional mutation checks and telemetry evidence passed.**

The run performed create/delete, file rename/move and size change, directory create/delete/rename/move, hard link, symlink/reparse creation, and Unicode-name mutations. Every individual USN assertion passed: File ID stability, Parent ID update, lazy directory-move resolution, delete suppression, moved-child Parent ID, and hard-link multi-name retention. The selected post-mutation CatchUp reached `Healthy` with `records_seen=29,859`, `error=0`, `usn_records=2`, and `user_mutations_applied=2`; the stress stage separately completed 18 fixed-target CatchUp batches. This is direct evidence that the service consumed the post-initial mutation journal path successfully.

The normal-user console harness previously exercised the expected denial path: the initial operation was accepted by IPC, then finished with Win32 error 5 and `Unavailable`; this is separate from the successful LocalSystem mutation assertions above.

## 7. Restart Recovery

**BLK5 load plus USN catch-up after service restart: PASS without a full MFT rebuild.**

The latest run created a valid LocalSystem BLK5 (`113,131,795` bytes), stopped the service, created a file during the stop interval, reconnected, validated Journal ID `134011753522823542`, and reached `Healthy`. The restart catch-up reported `records_seen=74` with `telemetry_initial=False`, `no_full_mft_rebuild=true`, and the file created while stopped was returned by Search. Client observation from the recovery stage was `76.876 s`. This satisfies the client/service restart recovery path for this run.

`Windows Restart Acceptance = PASS` for the separate two-phase run `20260811-204329`. The earlier client/service restart recovery above remains a distinct non-reboot scenario; the separate Windows Restart run proved an actual OS boot transition and did not use a full MFT rebuild.

## 8. Service Failure Test

The current run `20260811-162754` is the accepted service-failure evidence for this section: SCM stop disconnect was bounded at `996.855 ms`, local browse remained available, kill disconnect was bounded at `995.906 ms`, and restart/reconnect reached `Healthy`. The automation did not launch WinUI `Blink.exe`; this is therefore a client/IPC failure-isolation result.

The latest service-failure evidence is:

| Scenario | Result |
| --- | --- |
| SCM `stop Blink.IndexService` while Blink is running | **PASS** — bounded disconnect `996.855 ms` |
| SCM start and reconnect | **PASS** — reconnect reached `Healthy` |
| Kill the real service process and reconnect | **PASS** — bounded disconnect `995.906 ms`; restart reached `Healthy` |
| Existing console-harness process termination | PASS for transport path: reconnect failed with error 2 (`ERROR_FILE_NOT_FOUND`/pipe absent in this setup) |
| Existing local browse during provider delay/hang | PASS: local browse remained responsive in the Phase 0 test |

The real service IPC failure-isolation checks did not hang indefinitely: both stop and kill disconnects returned within about one second and reconnects reached `Healthy`. The automation deliberately did not launch the WinUI `Blink.exe` process, so this remains a client/IPC acceptance rather than a visual UI observation.

## 9. Journal Invalid Test

**Real Journal Reset Test: NOT EXECUTED.**  
**Synthetic Invalid Checkpoint Test: Runtime behavior PASS; the historical Harness flag was stale.**

The checksum-preserving Journal ID mutation was performed. The terminal IPC response was `index_state=RebuildRequired`, `error=1181` (`ERROR_JOURNAL_ENTRY_DELETED`), and message `RebuildRequired`; the store was restored and subsequently reached `Healthy`. The historical run's JSON flag is `rebuild_required=false` only because that Harness version expected `1178` (`ERROR_JOURNAL_DELETE_IN_PROGRESS`) rather than the service's actual mismatch code `1181`. The corrected runner now validates `1181`; this historical artifact was not rewritten and the full acceptance was not rerun.

## 10. Persistence Corruption

The elevated run also executed the valid-store corruption check:

```text
corrupt_store status=Failed error=13 index=Corrupted
```

This confirms that an invalid BLK5 payload is not reported as `Healthy`. The checksum-valid synthetic Journal ID mismatch reached the correct terminal state in the service. A real Journal reset case and the other corruption variants are not claimed from this run.

BLK5 remains a **Versioned Prototype Checkpoint Format**. This run did not add a B+Tree, LSM, database engine, or final disk ABI.

## 11. Security Boundary

Static source inspection and the accepted security slice confirm:

- the pipe uses message mode and `PIPE_REJECT_REMOTE_CLIENTS`;
- the server checks a client token through named-pipe impersonation; identity/revert failure is fail-closed and there is no process-token fallback that can impersonate a client;
- protocol version and request/response IDs are validated;
- volume operations accept only a normalized local drive root;
- the command set is limited to service status, volume/index status, initial index, progress, search, and cancellation;
- the service protocol contains no Copy, Delete, Rename, arbitrary Open, Network, or Shell command.

The historical elevated runtime run used the prototype `SY + BA` / `Authenticated Users` boundary, but that is no longer the current implementation. Security Closure Run `20260811-233345` accepted the hardened baseline: LocalSystem + `SERVICE_SID_TYPE_UNRESTRICTED` + only `SeImpersonatePrivilege`, persisted authorized client SID, and a protected Pipe DACL containing SYSTEM, `NT SERVICE\Blink.IndexService`, and that user SID. The client uses exact FILE rights without `FILE_CREATE_PIPE_INSTANCE`; `PIPE_REJECT_REMOTE_CLIENTS` and token/protocol validation remain enabled. The run captured the running service process TokenUser, complete TokenGroups and complete TokenPrivileges through native Token APIs, including Service SID membership and SeImpersonatePrivilege attributes/enabled state, and the actual descriptor contained no Authenticated Users/Everyone ACE.

## 12. Remaining Conditions

No remaining Phase 0.5 blocker is identified.

Accepted closure evidence includes Continuous-write Search sentinel convergence (`20260811-201439`), Windows Restart Acceptance (`20260811-204329`, `CompletePass`), and Security Closure (`20260811-233345`, targeted slice PASS, failures 0).

Future hardening experiments only: evaluate a virtual service account and/or `SERVICE_SID_TYPE_RESTRICTED` with a separate security/reboot study. These are not Phase 0.5 or Phase 1 blockers.

Development data remains under `D:\Arduino\src\Blink_文件管理器\.blink-data`; no path migration was performed. Approved product placement follows D-0021: **Install-volume-following DataRoot + Per-Volume Persistent Index**.

## 13. Phase 1 Recommendation

**READY TO PROCEED.**

The full Phase 0.5 acceptance evidence is complete, including the real NTFS MFT/USN chain, persistent recovery, continuous-write convergence, service failure isolation, corruption/journal-invalid behavior, Windows reboot recovery, and the accepted hardened service/Pipe security baseline. This gate update does not itself start Phase 1 work.

## Reproducible Evidence

- `install-index-service.ps1 -Configuration Debug -Account LocalService -Start`: PowerShell `ScriptRequiresElevation`.
- Direct SCM create: `[SC] OpenSCManager FAILED 5: Access is denied.`; subsequent `sc query` reported service absent (`1060`).
- Debug Phase 0 test: PASS; direct USN probe `error=5`; DFS probe `1` file, `0` directories, `0.0023622 s`; working set `13,930,496` bytes.
- Debug Phase 0.5 test: PASS for IPC/corruption/denial/process-termination paths; initial operation finished with `error=5`, `Unavailable`; stopped transport error `2`.
- Debug Transfer test: PASS, exit code 0.
- Manual elevated run `20260811-162754`: Administrator/High Integrity PASS; D: NTFS; direct V0 probe PASS with `1,012,833` MFT records and 9 USN records (`1.7399043/0.0004648 s`); Debug/Release builds PASS; LocalService install/start and volume handle PASS, `FSCTL_QUERY_USN_JOURNAL` FAIL with Win32 error `5`; explicitly selected LocalSystem install/start PASS; initial index PASS with `911,110` files, `101,750` directories, `1,012,889` records seen, `138.039 s` MFT enumeration, `133.662 s` parent-tree build, `144.940 s` total, `755896320`-byte peak working set, `6.67079 s` Save, and `113131795`-byte BLK5. BLK5 load/Journal validation reached `Healthy`; Search performance and all known-hit/known-miss correctness assertions passed with result counts `1/1/13/11/1/0`; USN mutation, restart recovery without full MFT rebuild, service stop/kill/reconnect, and corruption checks passed. Continuous-write stress completed its bounded writer and 18 fixed-target CatchUp batches but did not accept the Search sentinel create/delete convergence. Synthetic Journal ID mismatch reached terminal `RebuildRequired` with Win32 `1181`; the historical JSON flag expected `1178`. Windows Restart Acceptance was not executed. Final result `NOT READY`.
- Manual elevated run `20260811-153431`: Administrator/High Integrity PASS; D: NTFS; direct V0 probe PASS with `1,012,438` MFT records and 3 USN records; Debug/Release builds PASS; LocalService install/start and volume handle PASS, `FSCTL_QUERY_USN_JOURNAL` FAIL with Win32 error `5`; explicitly selected LocalSystem install/start PASS; initial index PASS with `910,734` files, `101,734` directories, `1,012,650` records seen, `140.645 s` MFT enumeration, `136.197 s` parent-tree build, `147.708 s` total, `756981760`-byte peak working set, `6.81049 s` Save, and `113091531`-byte BLK5. BLK5 load reached `Healthy`; Search benchmark completed with result counts `1/1/13/11/1` and extension average/P95 `213.76/240.41 ms`. USN mutation, restart recovery without full MFT rebuild, service stop/kill/reconnect, and corruption checks passed. Search correctness hit a Harness `.Count` shape error; stress sentinel was checked before its create USN became searchable; synthetic journal-invalid observed `Busy/997/Rebuilding` before terminal `RebuildRequired`. Final result `NOT READY`.
- Manual elevated run `20260811-145851`: Administrator/High Integrity PASS; D: NTFS; direct V0 probe PASS with `1,012,110` MFT records and 6 USN records; Debug/Release builds PASS; LocalService install/start and volume handle PASS, `FSCTL_QUERY_USN_JOURNAL` FAIL with Win32 error `5`; explicitly selected LocalSystem install/start PASS; initial index PASS with `910,421` files, `101,718` directories, `1,012,222` records seen, `153.055 s` MFT enumeration, `148.085 s` parent-tree build, `160.289 s` total, `755957760`-byte peak working set, `6.95427 s` Save, and `113058621`-byte BLK5. BLK5 load reached `Healthy`; real Search benchmark completed with non-zero results and extension average/P95 `244.16/276.76 ms`. Search correctness hit a harness `UInt64(-1)` mask bug; mutation assertions passed but selected CatchUp telemetry was absent; continuous-write stress hit a harness `.name` error. Later restart, service-failure, invalid-journal, and corruption stages were not executed. Final result `NOT READY`.
- Manual elevated run `20260811-112040`: Administrator/High Integrity PASS; D: NTFS; direct V0 probe PASS with `1,011,596` MFT records and 6 USN records; Debug/Release builds PASS; LocalService install/start and volume handle PASS, `FSCTL_QUERY_USN_JOURNAL` FAIL with Win32 error `5`; explicit LocalSystem install/start PASS; initial index PASS with `909,952` files, `101,670` directories, `1,011,679` records seen, `76.7394 s` MFT enumeration, `72.6032 s` parent-tree build, `83.6026 s` total, `641998848`-byte peak working set, `6.64188 s` Save, and `113003643`-byte BLK5. BLK5 load reached `Healthy`, but operations 2–73 still classified same-volume internal records as dirty. Search exact/prefix/substring rows completed with zero results; the extension case timed out after the 5-second IPC read. Final result `NOT READY`; mutation, service-failure, invalid-journal, and corruption stages were not executed.
- Static Search timing from `20260811-112040`: 69 completed Search requests; lock wait average/max `0.00152/0.009 ms`, actual Search average/max `2615.797/5948.365 ms`, serialization duration average/max `0.01887/0.030 ms`, and pipe write duration average/max `0.00839/0.015 ms`. This rules out mutex wait, serialization, and pipe write as the primary timeout cause. The O(N) scan/path-resolution implementation remains unchanged pending a stable no-self-loop run and correctness evidence.
- Root-cause follow-up after `20260811-112040`: `ReadFileIdentity` returns the 64-bit `FILE_ID_INFO.VolumeSerialNumber`, while USN identities use the DWORD serial from `GetVolumeInformationW`. The excluded-root identity is now normalized to the USN representation in `FastIndex::SetInternalExcludedRoots`; ordinary Debug/Release builds and tests pass, but no new elevated acceptance was run by Codex.
- Manual elevated run `20260811-031602`: Administrator/High Integrity PASS; D: NTFS; direct V0 probe PASS with `1,011,070` MFT records and `26` USN records; Debug/Release builds PASS; LocalService install/start and volume handle PASS, `FSCTL_QUERY_USN_JOURNAL` FAIL with Win32 error `5`; explicit LocalSystem install/start PASS; initial index PASS with `909,482` files, `101,612` directories, `1,011,451` records, `67.998 s` MFT enumeration, `62.2897 s` parent-tree build, `76.6558 s` total, `642736128`-byte peak working set, `8.25495 s` Save, and `112941545`-byte BLK5; BLK5 load/CatchUp reached `Healthy`; Search stage stopped on `Busy/CatchingUp/997` after the first Search returned. Final result `NOT READY`; artifact export PASS.
- Static Search timing from `20260811-031602`: request 493 had `0.002 ms` lock wait, `3176.364 ms` actual Search, `14.264 ms` serialization, and `0.009 ms` pipe write; request 494 returned Busy without Search-stage events. The source fix resets `user_mutations_applied` per CatchUp and suppresses telemetry writes for pure internal CatchUp; the harness retries only bounded Busy/997 responses. No new elevated run was performed after this fix.
- Ordinary regression after the fix: Debug/Release builds, Debug/Release `Blink.Phase05.Tests`, Debug/Release Transfer tests, and Debug Phase 0 tests all passed. No elevated acceptance was run by Codex.
- Manual elevated run `20260811-012750`: Administrator/High Integrity PASS; D: NTFS; corrected direct `MFT_ENUM_DATA_V0` probe PASS with `1,009,877` MFT records in `4.3897694 s`; corrected direct `READ_USN_JOURNAL_DATA_V0` probe PASS with `3` records in `0.0005453 s`; Debug/Release builds PASS; LocalService install/start PASS, volume handle PASS, `FSCTL_QUERY_USN_JOURNAL` FAIL with Win32 error `5` (`ERROR_ACCESS_DENIED`); explicit LocalSystem install/start PASS, but initial progress IPC timed out after the old 30-second grace before terminal service telemetry. Final result `NOT READY`; artifact export PASS.
- Historical manual elevated run `20260810-114040`: direct volume handle/query/enum/read probes PASS; native probe counted `961187` MFT records and `9` USN records; after explicit `LOCAL_SYSTEM`, the client captured `mft_records=961220`, `files=867459`, `directories=93761`, `usn_records=113`, `operation_seconds=37.8792`, `save_seconds=28.2773`, `peak_working_set_bytes=611774464`, and `index_bytes=108283017`. The first Search request timed out during the newly started service's BLK5 load; search and all later acceptance phases were not executed. This remains historical pre-correction evidence.
- Windows Restart Acceptance `20260811-204329`: Prepare/Complete used a copied BLK5 under the new RunId-specific Service DataRoot. The boot session changed from `2026-08-11T20:39:54.5000000Z` to `2026-08-11T20:51:29.5000000Z`; Journal ID remained `134011753522823542`; recovery operation `10` reported `initial=false`, `mft_records=0`, and `catchup_completed_usn=3664878520`; RebootSentinel Search returned one result with exact path and identity matches. State ended `CompletePass`; service registration and owned fixture were cleaned after PASS. The mode-specific JSON top-level `final_result=NOT READY` is intentional and does not override the combined full-suite `READY WITH CONDITIONS` assessment.

Official API/security references: [FSCTL_ENUM_USN_DATA](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_enum_usn_data), [FSCTL_READ_USN_JOURNAL](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ni-winioctl-fsctl_read_usn_journal), [Using the Change Journal Identifier](https://learn.microsoft.com/en-us/windows/win32/fileio/using-the-change-journal-identifier), [USN_JOURNAL_DATA_V1](https://learn.microsoft.com/en-us/windows/win32/api/winioctl/ns-winioctl-usn_journal_data_v1), [service user accounts](https://learn.microsoft.com/en-us/windows/win32/services/service-user-accounts), and [named pipe security and access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).
