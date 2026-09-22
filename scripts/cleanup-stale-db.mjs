#!/usr/bin/env bun
// cleanup-stale-db.mjs -- sqlite maintenance for agent chat stores (opencode.db).
//
// Why this exists: opencode.db had grown to 9.13 GB of which 6.34 GB was the
// FREELIST -- rows deleted by the app, pages never returned to the filesystem
// (auto_vacuum=0). VACUUM reclaims that with zero data loss. Optionally also
// prunes whole sessions past an age cutoff, cascading through
// message -> part and session_message (all FKs are ON DELETE CASCADE).
//
// Usage:
//   bun scripts/cleanup-stale-db.mjs                       report only
//   bun scripts/cleanup-stale-db.mjs --days 30             report what 30d would drop
//   bun scripts/cleanup-stale-db.mjs --archived             report archived sessions
//   bun scripts/cleanup-stale-db.mjs --apply               VACUUM only
//   bun scripts/cleanup-stale-db.mjs --days 30 --apply      prune + VACUUM
//   bun scripts/cleanup-stale-db.mjs --days 30 --apply --backup D:/backup/opencode.db
//
// Flags:
//   --db <path>    database (default: ~/.local/share/opencode/opencode.db)
//   --days <n>     delete sessions not updated in n days
//   --archived     instead delete only sessions flagged time_archived
//   --apply        perform writes (default is a dry run)
//   --backup <p>   VACUUM INTO <p> before mutating (compact copy, ~1/3 of source)
//   --no-vacuum    delete rows but leave the freelist alone
//
// Safe by construction: refuses to run while another process holds the write
// lock (close opencode first), and never touches project/credential/account
// rows.

import { Database } from "bun:sqlite";
import { homedir } from "node:os";
import { join } from "node:path";
import { statSync, existsSync } from "node:fs";

const argv = process.argv.slice(2);
const flag = (name) => argv.includes(`--${name}`);
const opt = (name, dflt = null) => {
  const i = argv.indexOf(`--${name}`);
  return i >= 0 && argv[i + 1] && !argv[i + 1].startsWith("--") ? argv[i + 1] : dflt;
};

const dbPath = opt("db") ?? join(homedir(), ".local", "share", "opencode", "opencode.db");
const days = Number(opt("days", "0"));
const apply = flag("apply");
const archivedOnly = flag("archived");
const vacuum = !flag("no-vacuum");
const backupPath = opt("backup");

if (!existsSync(dbPath)) {
  console.error(`db not found: ${dbPath}`);
  process.exit(1);
}
if (days > 0 && days < 1) {
  console.error("--days must be >= 1");
  process.exit(1);
}

const gb = (b) => (b / 1073741824).toFixed(2) + " GB";
const mb = (b) => (b / 1048576).toFixed(1) + " MB";
const sizeOnDisk = (p) => (existsSync(p) ? statSync(p).size : 0);

const root = dbPath;
const wal = root + "-wal";
const shm = root + "-shm";
const bytesBefore = sizeOnDisk(root);
const walBefore = sizeOnDisk(wal);
const shmBefore = sizeOnDisk(shm);

console.log(`db            : ${root}`);
console.log(`on disk       : ${gb(bytesBefore)}  (wal ${mb(walBefore)}, shm ${mb(shmBefore)})`);

const ro = new Database(root, { readonly: true });
const one = (sql) => ro.query(sql).get();
const pageSize = one("pragma page_size").page_size;
const pageCount = one("pragma page_count").page_count;
const freelist = one("pragma freelist_count").freelist_count;

console.log(`page_size     : ${pageSize}`);
console.log(`page_count    : ${pageCount}  (${gb(pageSize * pageCount)} of pages)`);
console.log(`freelist      : ${freelist} pages = ${gb(freelist * pageSize)} reclaimable by VACUUM`);
if (freelist === 0 && !vacuum) console.log("              (nothing to reclaim; --no-vacuum requested)");

console.log("\n-- btree sizes --");
for (const r of ro.query("select name, sum(pgsize) s from dbstat group by name order by s desc limit 10").all()) {
  console.log(`  ${String(r.name).padEnd(46)} ${mb(r.s)}`);
}

const now = Date.now();
const cutoff = days > 0 ? now - days * 86400000 : null;
const where = archivedOnly
  ? "time_archived is not null"
  : cutoff
    ? `time_updated < ${cutoff}`
    : null;

console.log("\n-- session inventory --");
const inv = ro.query(`
  select 'session'    t, count(*) n, sum(${where ?? "0"}) drop_me from session
  union all
  select 'session_v2' t, count(*) n, sum(${where ?? "0"}) drop_me from session_v2`).all();
for (const r of inv) console.log(`  ${r.t.padEnd(12)} rows=${String(r.n).padStart(6)}  selected=${r.drop_me ?? 0}`);
const range = one("select min(time_created) a, max(time_updated) b from session");
console.log(`  time range   ${new Date(range.a).toISOString()} .. ${new Date(range.b).toISOString()}`);
console.log(`  archived     ${one("select count(*) n from session_v2 where time_archived is not null").n}`);

if (where) {
  const pred = `select id from session_v2 where ${where}`;
  console.log("\n-- rows the selection would delete --");
  let payload = 0;
  for (const [label, sql] of [
    ["session_message", `select count(*) n, coalesce(sum(length(data)),0) b from session_message where session_id in (${pred})`],
    ["message", `select count(*) n, coalesce(sum(length(data)),0) b from message where session_id in (${pred})`],
    ["part (via message)", `select count(*) n, coalesce(sum(length(data)),0) b from part where message_id in (select id from message where session_id in (${pred}))`],
    ["todo", `select count(*) n, 0 b from todo where session_id in (${pred})`],
  ]) {
    const r = one(sql);
    payload += r.b ?? 0;
    console.log(`  ${label.padEnd(20)} rows=${String(r.n).padStart(7)}  payload=${mb(r.b ?? 0)}`);
  }
  for (const t of ["session_inbox", "session_pending", "instruction_entry", "instruction_state", "session_share"]) {
    const has = ro.query("select name from sqlite_master where type='table' and name=?").get(t);
    if (!has) continue;
    const r = one(`select count(*) n from "${t}" where session_id in (${pred})`);
    console.log(`  ${t.padEnd(20)} rows=${String(r.n).padStart(7)}`);
  }
  console.log(`  sum of deleted payload ~ ${mb(payload)} (plus index pages)`);
  if (cutoff) console.log(`  cutoff: sessions not updated since ${new Date(cutoff).toISOString()}`);
}
ro.close();

if (!apply) {
  console.log("\nDRY RUN. Add --apply to execute (VACUUM" + (where ? " + prune" : "") + ").");
  process.exit(0);
}

// -- write phase ------------------------------------------------------------
const rw = new Database(root);
rw.exec("pragma busy_timeout = 4000");
try {
  rw.exec("begin immediate");
  rw.exec("commit");
} catch (e) {
  rw.close();
  console.error(`\ncannot take the write lock (${e.message}).`);
  console.error("Another process still has the database open -- close opencode, then retry.");
  process.exit(2);
}
rw.exec("pragma foreign_keys = ON");

if (backupPath) {
  console.log(`\nbackup (VACUUM INTO) -> ${backupPath}`);
  rw.exec(`vacuum into '${backupPath.replace(/'/g, "''")}'`);
  console.log(`  backup written: ${gb(sizeOnDisk(backupPath))}`);
}

if (where) {
  console.log("\npruning sessions...");
  const counts = () => ({
    session: rw.query("select count(*) n from session").get().n,
    session_v2: rw.query("select count(*) n from session_v2").get().n,
    message: rw.query("select count(*) n from message").get().n,
    part: rw.query("select count(*) n from part").get().n,
    session_message: rw.query("select count(*) n from session_message").get().n,
  });
  const before = counts();
  const selected = rw.query(`select count(*) n from session where ${where}`).get().n;
  rw.exec("begin immediate");
  rw.prepare(`delete from session where ${where}`).run();
  rw.prepare(`delete from session_v2 where ${where}`).run();
  rw.exec("commit");
  const after = counts();
  console.log(`  sessions selected by '${where}': ${selected}`);
  for (const k of Object.keys(before)) {
    console.log(`  ${k.padEnd(16)} ${String(before[k]).padStart(7)} -> ${String(after[k]).padStart(7)}  (-${before[k] - after[k]})`);
  }
  const orphans = rw.query("select count(*) n from message where session_id not in (select id from session)").get().n;
  if (orphans > 0) console.warn(`  WARNING: ${orphans} message rows now reference a missing session`);
}

if (vacuum) {
  console.log("\nVACUUM (rewrites the file; needs free space ~= its final size)...");
  const t0 = Date.now();
  rw.exec("vacuum");
  console.log(`  done in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
} else {
  console.log("\n--no-vacuum: freelist left in place (the file will not shrink).");
}

// WAL mode: VACUUM's pages land in the -wal file and only move into the main
// file at a checkpoint. Without this the reclaim looks like a no-op and the
// next reader replays a multi-GB journal.
if (rw.query("pragma journal_mode").get().journal_mode === "wal") {
  const cp = rw.query("pragma wal_checkpoint(TRUNCATE)").get();
  console.log(`\nwal_checkpoint(TRUNCATE): busy=${cp.busy} log=${cp.log} checkpointed=${cp.checkpointed}`);
}
rw.close();

const bytesAfter = sizeOnDisk(root);
const walAfter = sizeOnDisk(wal);
const shmAfter = sizeOnDisk(shm);
console.log(`\non disk after  : ${gb(bytesAfter)}  (wal ${mb(walAfter)}, shm ${mb(shmAfter)})`);
console.log(`reclaimed      : ${gb(bytesBefore + walBefore + shmBefore - bytesAfter - walAfter - shmAfter)}`);
const check = new Database(root, { readonly: true });
console.log(`integrity      : ${check.query("pragma integrity_check").get().integrity_check}`);
check.close();
