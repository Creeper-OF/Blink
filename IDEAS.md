# Blink Ideas

## Phase 0.5 follow-ups

- Replace the prototype authenticated-users pipe DACL with a per-user SID/session ACL and add a signed/installer-owned service configuration.
- Measure LocalService/NetworkService volume-handle access on supported Windows builds before reducing the explicit LocalSystem fallback.
- Add a durable B-tree/LSM-backed store only after real 100k/1M/whole-volume measurements justify it; BLK5 is intentionally a checkpoint prototype.
- Promote MetadataHost from the current background-thread workaround to an isolated host before untrusted Shell property handlers are treated as production-safe.

这里只记录未经用户确认、也未纳入 Phase 0 验收范围的想法。

- 后续可评估独立 MetadataHost，用于承载可能执行第三方 Property Handler 的读取任务。
- 后续可评估将索引持久层替换为专用 B+Tree/LSM 结构；Phase 0 仅验证路线。
