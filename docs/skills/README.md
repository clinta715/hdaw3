# Skills Index

- [`hdaw-guard`](hdaw-guard/SKILL.md) — required before any code change in this repo.
- [`codebase-memory`](codebase-memory/SKILL.md) — required for semantic, cross-file, and cross-repository analysis after graphify; includes live index freshness checks.
- [`pre-build-time-sync`](pre-build-time-sync/SKILL.md) — **WSL-only, opt-in**: a no-op on the native Windows dev box; set `HDAW_TIME_SYNC=1` only when the tree is reached through WSL's drvfs/9p view, to stop timestamp-drift build traps (stale-`.obj`/stale bundles).

See `AGENTS.md` for the project-level rule that loads this skill before implementation.
