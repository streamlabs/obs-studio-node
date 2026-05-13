#!/usr/bin/env node
/**
 * long-calls-analyzer.ts
 *
 * Downloads user cache zips from S3, extracts long_calls.txt from each one,
 * loads the IPC call data into a local SQLite database, and prints a
 * performance report identifying the worst-offending calls.
 *
 * Setup:
 *   npm install @aws-sdk/client-s3 better-sqlite3 adm-zip
 *   npm install --save-dev typescript @types/better-sqlite3 @types/adm-zip @types/node
 *
 * Usage:
 *   npx ts-node long-calls-analyzer.ts             # run (resumes previous run)
 *   npx ts-node long-calls-analyzer.ts --reset     # wipe DB and start fresh
 *   npx ts-node long-calls-analyzer.ts --age 2w    # last 2 weeks (default)
 *   npx ts-node long-calls-analyzer.ts --age 1m    # last 1 month
 *   npx ts-node long-calls-analyzer.ts --age 6h    # last 6 hours
 *
 * AWS credentials are read from the environment (AWS_ACCESS_KEY_ID /
 * AWS_SECRET_ACCESS_KEY) or from ~/.aws/credentials.
 */

import { S3Client, ListObjectsV2Command, GetObjectCommand } from '@aws-sdk/client-s3';
import Database, { Database as DatabaseType, Statement } from 'better-sqlite3';
import AdmZip from 'adm-zip';
import path from 'path';
import fs from 'fs';

// ─── Config ──────────────────────────────────────────────────────────────────

const BUCKET = 'streamlabs-obs-user-cache';
const REGION = 'us-west-2';
const CONCURRENCY = 20;
const DB_PATH = path.resolve(process.cwd(), 'long_calls.db');

// ─── S3 client ───────────────────────────────────────────────────────────────

const s3 = new S3Client({ region: REGION });

// ─── Age / cutoff ────────────────────────────────────────────────────────────

type AgeUnit = 'h' | 'w' | 'm';

interface Cutoff {
  cutoff: Date;
  label: string;
}

/**
 * Parses --age values like "6h" (hours), "2w" (weeks), or "1m" (months)
 * and returns a cutoff Date. Defaults to 2 weeks if not specified.
 */
function parseCutoff(ageArg?: string): Cutoff {
  const match = /^(\d+)(h|w|m)$/.exec(ageArg ?? '2w');
  if (!match) throw new Error(`Invalid --age value "${ageArg}". Use e.g. "6h" (hours), "2w" (weeks), or "1m" (months).`);
  const n = parseInt(match[1], 10);
  const unit = match[2] as AgeUnit;
  const cutoff = new Date();
  if (unit === 'h') cutoff.setHours(cutoff.getHours() - n);
  else if (unit === 'w') cutoff.setDate(cutoff.getDate() - n * 7);
  else cutoff.setMonth(cutoff.getMonth() - n);
  const unitLabel: Record<AgeUnit, string> = { h: 'hour(s)', w: 'week(s)', m: 'month(s)' };
  return { cutoff, label: `${n} ${unitLabel[unit]}` };
}

// ─── Key parsing ─────────────────────────────────────────────────────────────

interface ParsedKey {
  uploadedAt: Date;
  username: string | null;
}

/**
 * Cache keys are expected to use the format "{isoDate}-{username}.zip".
 * The ISO timestamp portion is always 24 characters
 * ("2024-01-15T10:23:45.123Z"), so the username begins at index 25.
 * Anonymous uploads omit the username suffix.
 */
function parseKey(key: string): ParsedKey {
  const name = path.basename(key, '.zip');
  const uploadedAt = new Date(name.substring(0, 24));
  const username = name.length > 25 ? name.substring(25) : null;
  return { uploadedAt, username };
}

// ─── Log line parsing ─────────────────────────────────────────────────────────

/**
 * Parses a line from long_calls.txt written by ipc_freeze_callback() in utility.cpp:
 *
 *   [YYYY-MM-DD HH:MM:SS.mmm] [pid:N, tid:N] [(freeze) ]CALL_NAME, total:Nms, obs:Nms
 *
 * A freeze is recorded when obs_time < 0 (the "(freeze)" prefix is redundant
 * with the sign of obs_ms but both are captured for cross-checking).
 */
const LINE_RE =
  /^\[([^\]]+)\] \[pid:\d+, tid:[^\]]+\] (\(freeze\) )?(.*?), total:(-?\d+)ms, obs:(-?\d+)ms\s*$/;

interface ParsedLine {
  timestamp: string;
  call_name: string;
  total_ms: number;
  obs_ms: number;
  is_freeze: 0 | 1;
}

function parseLine(line: string): ParsedLine | null {
  const m = LINE_RE.exec(line);
  if (!m) return null;
  const obs_ms = parseInt(m[5], 10);
  return {
    timestamp: m[1],
    call_name: m[3].trim(),
    total_ms: parseInt(m[4], 10),
    obs_ms,
    is_freeze: obs_ms < 0 ? 1 : 0,
  };
}

// ─── Concurrency pool ─────────────────────────────────────────────────────────

async function pool<T>(items: T[], concurrency: number, fn: (item: T) => Promise<void>): Promise<void> {
  let idx = 0;
  async function worker() {
    while (idx < items.length) {
      const item = items[idx++];
      await fn(item).catch((err: Error) =>
        console.error(`\n  Error processing ${item}: ${err.message}`),
      );
    }
  }
  await Promise.all(Array.from({ length: Math.min(concurrency, items.length) }, worker));
}

// ─── Database ─────────────────────────────────────────────────────────────────

interface Stmts {
  insertSource: Statement;
  insertCall: Statement;
}

function setupDb(reset: boolean): DatabaseType {
  if (reset && fs.existsSync(DB_PATH)) fs.unlinkSync(DB_PATH);

  const db = new Database(DB_PATH);
  db.pragma('journal_mode = WAL');
  db.pragma('synchronous = NORMAL');

  db.exec(`
    CREATE TABLE IF NOT EXISTS sources (
      id          INTEGER PRIMARY KEY,
      s3_key      TEXT    NOT NULL UNIQUE,
      username    TEXT,
      uploaded_at TEXT    NOT NULL
    );

    CREATE TABLE IF NOT EXISTS long_calls (
      id          INTEGER PRIMARY KEY,
      source_id   INTEGER NOT NULL REFERENCES sources(id),
      timestamp   TEXT    NOT NULL,
      call_name   TEXT    NOT NULL,
      total_ms    INTEGER NOT NULL,
      obs_ms      INTEGER NOT NULL,
      is_freeze   INTEGER NOT NULL
    );

    CREATE INDEX IF NOT EXISTS idx_lc_call_name ON long_calls(call_name);
    CREATE INDEX IF NOT EXISTS idx_lc_source    ON long_calls(source_id);
  `);

  return db;
}

// ─── S3 listing ───────────────────────────────────────────────────────────────

async function listRecentKeys(cutoff: Date): Promise<string[]> {
  const keys: string[] = [];
  let continuationToken: string | undefined;

  do {
    const resp = await s3.send(
      new ListObjectsV2Command({ Bucket: BUCKET, ContinuationToken: continuationToken }),
    );
    for (const obj of resp.Contents ?? []) {
      if (obj.Key && obj.Key.endsWith('.zip') && obj.LastModified && obj.LastModified >= cutoff) {
        keys.push(obj.Key);
      }
    }
    continuationToken = resp.NextContinuationToken;
  } while (continuationToken);

  return keys;
}

// ─── Deduplication ────────────────────────────────────────────────────────────

/**
 * For each username keep only their most recent cache.
 * Anonymous caches (no username in key) cannot be deduplicated so all are kept.
 */
function deduplicateByUser(keys: string[]): string[] {
  const byUser = new Map<string, { key: string; uploadedAt: Date }>();
  const anonymous: string[] = [];

  for (const key of keys) {
    const { uploadedAt, username } = parseKey(key);
    if (!username) {
      anonymous.push(key);
      continue;
    }
    const existing = byUser.get(username);
    if (!existing || uploadedAt > existing.uploadedAt) {
      byUser.set(username, { key, uploadedAt });
    }
  }

  return [...Array.from(byUser.values(), e => e.key), ...anonymous];
}

// ─── Download + parse ─────────────────────────────────────────────────────────

async function downloadToBuffer(key: string): Promise<Buffer> {
  const resp = await s3.send(new GetObjectCommand({ Bucket: BUCKET, Key: key }));
  const chunks: Uint8Array[] = [];
  for await (const chunk of resp.Body as AsyncIterable<Uint8Array>) chunks.push(chunk);
  return Buffer.concat(chunks);
}

type ProcessResult = 'ok' | 'skipped' | 'no_long_calls' | 'bad_zip' | 'bad_entry';

async function processKey(key: string, db: DatabaseType, stmts: Stmts): Promise<ProcessResult> {
  if (db.prepare('SELECT 1 FROM sources WHERE s3_key = ?').get(key)) return 'skipped';

  const buf = await downloadToBuffer(key);

  let zip: AdmZip;
  try {
    zip = new AdmZip(buf);
  } catch {
    return 'bad_zip';
  }

  const entry = zip.getEntry('long_calls.txt');
  if (!entry) return 'no_long_calls';
  console.log(`\n  Processing ${key} long_calls.txt...`);

  let text: string;
  try {
    text = entry.getData().toString('utf8');
  } catch {
    return 'bad_entry';
  }

  const { uploadedAt, username } = parseKey(key);
  const lines = text.split('\n').filter(Boolean);

  db.transaction(() => {
    stmts.insertSource.run(key, username ?? null, uploadedAt.toISOString());
    const row = db.prepare('SELECT id FROM sources WHERE s3_key = ?').get(key) as { id: number };
    for (const line of lines) {
      const parsed = parseLine(line);
      if (!parsed) continue;
      stmts.insertCall.run(
        row.id,
        parsed.timestamp,
        parsed.call_name,
        parsed.total_ms,
        parsed.obs_ms,
        parsed.is_freeze,
      );
    }
  })();

  return 'ok';
}

// ─── Statistics ───────────────────────────────────────────────────────────────

interface TotalTimeRow {
  call_name: string;
  occurrences: number;
  affected_users: number;
  avg_ms: number;
  p95_ms: number | null;
  max_ms: number;
  freezes: number;
  total_sec: number;
}

interface FreezesRow {
  call_name: string;
  freezes: number;
  affected_users: number;
  max_ms: number;
  avg_ms: number;
}

// ─── Cleanup ──────────────────────────────────────────────────────────────────
/**
 * Removes sources (and their long_calls rows) for users who have more than one
 * cache in the DB, keeping only the most recently uploaded one per username.
 */
function cleanOldSources(db: DatabaseType): void {
    const deleted = db.transaction(() => {
        db.prepare(`
            DELETE FROM long_calls WHERE source_id IN (
                SELECT id FROM sources
                WHERE username IS NOT NULL
                  AND uploaded_at < (
                    SELECT MAX(s2.uploaded_at) FROM sources s2
                    WHERE s2.username = sources.username
                  )
            )
        `).run();
        return db.prepare(`
            DELETE FROM sources
            WHERE username IS NOT NULL
              AND uploaded_at < (
                SELECT MAX(s2.uploaded_at) FROM sources s2
                WHERE s2.username = sources.username
              )
        `).run().changes;
    })();
    if (deleted > 0)
        console.log(`  Pruned ${deleted} older source(s) (kept latest per user).\n`);
}

function printStats(db: DatabaseType): void {
  const sep = '─'.repeat(100);
  const line = '━'.repeat(100);

  console.log(`\n${line}`);
  console.log('  IPC Call Performance Report');
  console.log(`${line}\n`);

  const totalSources = (db.prepare('SELECT COUNT(*) AS n FROM sources').get() as { n: number }).n;
  const totalCalls = (db.prepare('SELECT COUNT(*) AS n FROM long_calls').get() as { n: number }).n;
  const totalUsers = (db.prepare('SELECT COUNT(DISTINCT username) AS n FROM sources WHERE username IS NOT NULL').get() as { n: number }).n;

  console.log(`  Sources (cache zips)  : ${totalSources}`);
  console.log(`  Unique users          : ${totalUsers}`);
  console.log(`  Total long-call lines : ${totalCalls}\n`);

  // ── Top calls by total time consumed ────────────────────────────────────────
  let byTotalTime : TotalTimeRow[] = [];
  try {
    byTotalTime = db
    .prepare(
      `WITH ranked AS (
        SELECT
          call_name, source_id, total_ms, is_freeze,
          ROW_NUMBER() OVER (PARTITION BY call_name ORDER BY total_ms) AS rn,
          COUNT(*)     OVER (PARTITION BY call_name)                   AS cnt
        FROM long_calls
      ),
      p95 AS (
        SELECT call_name, total_ms AS p95_ms
        FROM ranked
        WHERE rn = IIF(CAST(cnt * 0.95 AS INT) < 1, 1, CAST(cnt * 0.95 AS INT))
      )
      SELECT
        lc.call_name,
        COUNT(*)                          AS occurrences,
        COUNT(DISTINCT s.username)        AS affected_users,
        ROUND(AVG(lc.total_ms))           AS avg_ms,
        p95.p95_ms,
        MAX(lc.total_ms)                  AS max_ms,
        SUM(lc.is_freeze)                 AS freezes,
        ROUND(SUM(lc.total_ms) / 1000.0, 1) AS total_sec
      FROM long_calls lc
      LEFT JOIN sources s ON s.id = lc.source_id
      LEFT JOIN p95 ON p95.call_name = lc.call_name
      GROUP BY lc.call_name
      ORDER BY total_sec DESC
      LIMIT 30`,
    )
    .all() as TotalTimeRow[];
  }
  catch (err) {
    console.error('Error fetching total time stats:', err);
  }

  const cw = 56;
  const pad = (s: string | number | null | undefined, w: number) => String(s ?? '–').padStart(w);
  const padL = (s: string | number | null | undefined, w: number) => String(s ?? '').padEnd(w);

  const header = [
    padL('Call name', cw),
    pad('Count', 7),
    pad('Users', 6),
    pad('Avg ms', 7),
    pad('P95 ms', 7),
    pad('Max ms', 7),
    pad('Freezes', 8),
    pad('Total s', 8),
  ].join('  ');

  console.log('  Top 30 by total wall-clock time consumed\n');
  console.log('  ' + header);
  console.log('  ' + sep);
  for (const r of byTotalTime) {
    console.log(
      '  ' +
        [
          padL(r.call_name, cw),
          pad(r.occurrences, 7),
          pad(r.affected_users, 6),
          pad(r.avg_ms, 7),
          pad(r.p95_ms, 7),
          pad(r.max_ms, 7),
          pad(r.freezes, 8),
          pad(r.total_sec, 8),
        ].join('  '),
    );
  }

  // ── Top freeze-inducing calls ────────────────────────────────────────────────
  let byFreezes: FreezesRow[] = [];
  try {
    byFreezes = db
      .prepare(
        `SELECT
          call_name,
          SUM(is_freeze)            AS freezes,
          COUNT(DISTINCT source_id) AS affected_users,
          MAX(total_ms)             AS max_ms,
          ROUND(AVG(total_ms))      AS avg_ms
        FROM long_calls
        WHERE is_freeze = 1
        GROUP BY call_name
        ORDER BY freezes DESC
        LIMIT 20`,
      )
      .all() as FreezesRow[];
  } catch (err) {
    console.error('Error fetching freeze stats:', err);
  }

  if (byFreezes.length) {
    console.log('\n\n  Top 20 freeze-inducing calls  (obs_time < 0)\n');
    console.log(
      '  ' +
        [padL('Call name', cw), pad('Freezes', 8), pad('Users', 6), pad('Avg ms', 7), pad('Max ms', 7)].join('  '),
    );
    console.log('  ' + sep);
    for (const r of byFreezes) {
      console.log(
        '  ' +
          [
            padL(r.call_name, cw),
            pad(r.freezes, 8),
            pad(r.affected_users, 6),
            pad(r.avg_ms, 7),
            pad(r.max_ms, 7),
          ].join('  '),
      );
    }
  }

  console.log(`\n${line}\n`);
}

// ─── Entry point ─────────────────────────────────────────────────────────────

(async () => {
  const reset = process.argv.includes('--reset');
  const db = setupDb(reset);

  const stmts: Stmts = {
    insertSource: db.prepare(
      'INSERT OR IGNORE INTO sources (s3_key, username, uploaded_at) VALUES (?, ?, ?)',
    ),
    insertCall: db.prepare(
      'INSERT INTO long_calls (source_id, timestamp, call_name, total_ms, obs_ms, is_freeze) VALUES (?, ?, ?, ?, ?, ?)',
    ),
  };

  const ageIdx = process.argv.indexOf('--age');
  const { cutoff, label } = parseCutoff(ageIdx !== -1 ? process.argv[ageIdx + 1] : undefined);

  console.log(`Listing objects in s3://${BUCKET} newer than ${label}...`);
  const allKeys = await listRecentKeys(cutoff);
  console.log(`Found ${allKeys.length} zip files.`);

  const keys = deduplicateByUser(allKeys);
  console.log(`After per-user deduplication: ${keys.length} caches to process.\n`);

  let done = 0;
  let errorCount = 0;
  const counts: Record<ProcessResult, number> = { ok: 0, skipped: 0, no_long_calls: 0, bad_zip: 0, bad_entry: 0 };

  await pool(keys, CONCURRENCY, async key => {
    try {
      const result = await processKey(key, db, stmts);
      counts[result]++;
    } catch (error) {
      errorCount++;
      console.error(`\nError processing ${key}:`, error);
    } finally {
      done++;
      if (done % 25 === 0 || done === keys.length) {
        process.stdout.write(
          `\r  ${done}/${keys.length}  ok=${counts.ok}  skipped=${counts.skipped}  no_data=${counts.no_long_calls}  errors=${errorCount}  `,
        );
      }
    }
  });

  console.log('\n');
  cleanOldSources(db);
  printStats(db);
  db.close();
})();
