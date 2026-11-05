const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { DatabaseSync } = require('node:sqlite');
const { estimate, readRecords, createServer } = require('./lightbox-observer.cjs');
const prices = require('./lightbox-pricing.json');

test('Credits include cache writes and output; an unknown model or missing metric stays unknown', () => {
  const u = { input: 3, output: 2369, cacheRead: 0, cacheWrite: 10885 };
  assert.equal(estimate(u, 'gpt-6-luna', 'github-copilot', prices), 0.2545425);
  assert.equal(estimate({}, 'gpt-6-luna', 'github-copilot', prices), null);
  assert.equal(estimate(u, 'unknown-model', 'github-copilot', prices), null);
  assert.equal(estimate(u, 'gpt-6-luna', 'other-provider', prices), null);
  assert.ok(Math.abs(estimate({ input: 300000, output: 100, cacheRead: 0, cacheWrite: 0 }, 'gpt-6-luna', 'github-copilot', prices) - 6.0075) < 1e-9);
});

test('Read-only history matches request IDs, ignores unrelated chats, and requires authentication', async () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'lightbox-meter-test-'));
  const file = path.join(dir, 'agent.sqlite'), db = new DatabaseSync(file);
  const id = '00000000-0000-4000-a000-000000000001';
  db.exec('CREATE TABLE session_nodes(session_key TEXT,current_session_id TEXT,session_started_at INTEGER); CREATE TABLE transcript_events(session_id TEXT,seq INTEGER,event_json TEXT,event_zstd BLOB);');
  const node = db.prepare('INSERT INTO session_nodes VALUES(?,?,?)');
  node.run('agent:main:lightbox:' + id, 'scene', Date.now()); node.run('agent:main:openai:other', 'other', Date.now());
  node.run('agent:main:lightbox:00000000-0000-4000-a000-000000000002', 'failed', Date.now());
  const add = db.prepare('INSERT INTO transcript_events VALUES(?,?,?,NULL)');
  add.run('scene', 1, JSON.stringify({ message: { role: 'user', content: '森を作って' } }));
  add.run('scene', 2, JSON.stringify({ message: { role: 'assistant', provider: 'github-copilot', model: 'gpt-6-luna', content: JSON.stringify({ title: '森', particles: {}, world: {} }), usage: { input: 3, output: 100, cacheRead: 0, cacheWrite: 1000, totalTokens: 1103, cost: { total: 0 } } } }));
  add.run('other', 1, JSON.stringify({ message: { role: 'assistant', content: 'unrelated private chat' } })); db.close();
  const again = new DatabaseSync(file);
  again.prepare('INSERT INTO transcript_events VALUES(?,?,?,NULL)').run('failed', 1, JSON.stringify({ message: { role: 'assistant', provider: 'github-copilot', model: 'gpt-6-luna', content: 'not JSON', usage: { input: 10, output: 20, cacheRead: 0, cacheWrite: 0 } } })); again.close();
  try {
    const rows = readRecords(file, prices); assert.equal(rows.length, 2);
    const scene = rows.find(r => r.id === id); assert.ok(scene.credits > 0, 'zero provider catalog cost does not mean zero credits');
    const failed = rows.find(r => r.after === ''); assert.ok(failed.credits > 0, 'tagged malformed replies still consume tokens');
    const server = createServer({ token: 'local-test-token-123456', dbPath: file, prices });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = 'http://127.0.0.1:' + server.address().port;
    try {
      assert.equal((await fetch(url + '/v1/history')).status, 401);
      const headers = { Authorization: 'Bearer local-test-token-123456' };
      const r = await fetch(url + '/v1/runs/' + id, { headers }); assert.equal(r.status, 200); assert.equal((await r.json()).id, id);
      assert.equal((await fetch(url + '/v1/history', { method: 'POST', headers })).status, 405);
    } finally { await new Promise(resolve => server.close(resolve)); }
  } finally {
    // Delete only the explicitly created files, never a computed recursive directory tree.
    for (const name of ['agent.sqlite', 'agent.sqlite-wal', 'agent.sqlite-shm']) { const target = path.join(dir, name); if (fs.existsSync(target)) fs.unlinkSync(target); }
    fs.rmdirSync(dir);
  }
});
