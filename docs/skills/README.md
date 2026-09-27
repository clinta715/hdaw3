# Skills Index

- [`hdaw-guard`](hdaw-guard/SKILL.md) — required before any code change in this repo.
- [`codebase-memory`](codebase-memory/SKILL.md) — required for semantic, cross-file, and cross-repository analysis after graphify; includes live index freshness checks.
- [`pre-build-time-sync`](pre-build-time-sync/SKILL.md) — **WSL-only, opt-in**: a no-op on the native Windows dev box; set `HDAW_TIME_SYNC=1` only when the tree is reached through WSL's drvfs/9p view, to stop timestamp-drift build traps (stale-`.obj`/stale bundles).
- [`psy-song-session`](psy-song-session/SKILL.md) — end-to-end psytrance song sessions: six scoped subagent roles around an immutable Song Brief, with machine-verifiable gates between roles.

See `AGENTS.md` for the project-level rule that loads this skill before implementation.

## Harness discovery

`docs/skills/` is the canonical location, but not every agent harness scans it. DSH
scans only `<projectRoot>/.dsh/skills`, `<projectRoot>/.agents/skills`,
`~/.dsh/skills`, and `~/.agents/skills` — never `docs/skills/` or `.pi/skills/`. This
repo therefore exposes the whole directory through a **junction** (`.agents/` is
gitignored):

```powershell
New-Item -ItemType Junction -Path .agents\skills -Target docs\skills
```

One junction mirrors every skill *and* its sibling resources (`reference.md`,
`roles/*.md`, `brief.schema.json`) — which DSH resolves relative to the skill's base
directory — and a new skill under `docs/skills/` needs no `.agents` change.
Verify with `Get-Item .agents\skills` (`LinkType: Junction`).

**Do not mirror with per-file hard links.** DSH's `edit`/`write` *replace* a file (new
inode) instead of writing in place, so a hard link silently keeps serving the OLD content
after any edit: the skill still loads, with stale instructions. For `hdaw-guard` that is
worse than no skill at all (measured 2026-09-24).

**Never traverse the junction destructively.** `.agents\skills` points at `docs\skills`,
so `Remove-Item -Recurse .agents` or `icacls .agents /reset /T` walks into the canonical
files. Remove the link itself with `Remove-Item .agents\skills` (no `-Recurse`) or
`cmd /c rmdir .agents\skills`. The workspace sandbox also denies delete-child inside the
tree, so such repairs may need an unsandboxed shell.

Discovery is one directory level deep (`<root>/<name>/SKILL.md` or `<root>/<name>.md`),
and a file without `name` + `description` YAML frontmatter is skipped with a warning the
model never sees — this `README.md` is skipped for exactly that reason.
