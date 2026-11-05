// Read-only metering for the dedicated on-device profile. No inference or account mutations.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const zlib = require('node:zlib');
const { DatabaseSync } = require('node:sqlite');

function estimate(usage, model, provider, prices) {
  const rate = provider === 'github-copilot' ? prices.models[model] : null;
  const fields = ['input', 'output', 'cacheRead', 'cacheWrite'];
  if (!rate || fields.some(k => !Number.isFinite(usage?.[k]) || usage[k] < 0)) return null;
  const context = usage.input + usage.cacheRead + usage.cacheWrite;
  const tier = context > rate.threshold ? rate.long : rate.default;
  const usd = fields.reduce((sum, k) => sum + usage[k] * tier[k] / 1e6, 0);
  return usd / prices.usdPerCredit;
}

function text(message) {
  if (typeof message?.content === 'string') return message.content;
  return Array.isArray(message?.content) ? message.content.filter(p => p.type === 'text').map(p => p.text).join('') : '';
}

function readRecords(dbPath, prices) {
  if (!fs.existsSync(dbPath)) return [];
  const db = new DatabaseSync(dbPath, { readOnly: true });
  try {
    const nodes = db.prepare(`SELECT session_key, current_session_id, session_started_at FROM session_nodes
      WHERE session_key LIKE 'agent:main:openai:%' OR session_key LIKE 'agent:main:lightbox:%'
      ORDER BY session_started_at DESC LIMIT 300`).all();
    const query = db.prepare('SELECT event_json, event_zstd FROM transcript_events WHERE session_id=? ORDER BY seq');
    const result = [];
    for (const node of nodes) {
      let prompt = '', reply = null;
      for (const row of query.all(node.current_session_id)) {
        let raw = row.event_json;
        if (!raw && row.event_zstd && zlib.zstdDecompressSync) raw = zlib.zstdDecompressSync(row.event_zstd).toString('utf8');
        if (!raw) continue;
        const event = JSON.parse(raw), message = event.message;
        if (message?.role === 'user') prompt = text(message);
        if (message?.role === 'assistant') reply = message;
      }
      if (!reply) continue;
      const after = text(reply);
      // Restrict old, untagged sessions to scene JSON replies. Do not export unrelated chat.
      const marker = 'agent:main:lightbox:';
      const tagged = node.session_key.startsWith(marker);
      let scene;
      try { scene = JSON.parse(after.trim().replace(/^```(?:json)?\s*/, '').replace(/\s*```$/, '')); } catch {}
      const isScene = scene && typeof scene.title === 'string' && scene.particles && scene.world;
      if (!isScene && !tagged) continue;
      const usage = reply.usage || {}, model = reply.model || '', provider = reply.provider || '';
      const repair = prompt.includes('The renderer rejected the previous scene:');
      if (repair && prompt.includes('User: ')) prompt = prompt.slice(prompt.indexOf('User: ') + 6).split('\n')[0];
      result.push({
        id: node.session_key.startsWith(marker) ? node.session_key.slice(marker.length) : node.current_session_id,
        utc: new Date(node.session_started_at).toISOString(), words: prompt.slice(0, 500),
        after: isScene ? JSON.stringify(scene) : '', raw: after.slice(0, 65536), model, repair,
        input: Number.isFinite(usage.input) ? usage.input : -1,
        output: Number.isFinite(usage.output) ? usage.output : -1,
        cacheRead: Number.isFinite(usage.cacheRead) ? usage.cacheRead : -1,
        cacheWrite: Number.isFinite(usage.cacheWrite) ? usage.cacheWrite : -1,
        total: Number.isFinite(usage.totalTokens) ? usage.totalTokens : -1,
        credits: estimate(usage, model, provider, prices),
        pricingDate: prices.asOf, pricingSource: prices.source,
        result: '過去の返答（適用未確認）', imported: true
      });
    }
    return result.reverse();
  } finally { db.close(); }
}

function createServer({ token, dbPath, prices }) {
  if (!token || token.length < 16) throw new Error('A gateway token is required');
  return http.createServer((req, res) => {
    res.setHeader('Content-Type', 'application/json; charset=utf-8');
    res.setHeader('Cache-Control', 'no-store');
    const supplied = Buffer.from(req.headers.authorization || '');
    const expected = Buffer.from('Bearer ' + token);
    if (supplied.length !== expected.length || !crypto.timingSafeEqual(supplied, expected)) {
      res.writeHead(401); res.end('{"error":"unauthorized"}'); return;
    }
    try {
      if (req.method !== 'GET') { res.writeHead(405); res.end('{}'); return; }
      if (req.url === '/v1/history') { res.end(JSON.stringify({ records: readRecords(dbPath, prices), billingConfirmed: false })); return; }
      const match = /^\/v1\/runs\/([0-9a-fA-F-]{36})$/.exec(req.url);
      if (match) {
        const record = readRecords(dbPath, prices).find(r => r.id.toLowerCase() === match[1].toLowerCase());
        if (record) res.end(JSON.stringify(record));
        else { res.writeHead(404); res.end('{"error":"not_ready"}'); }
        return;
      }
      res.writeHead(404); res.end('{}');
    } catch {
      res.writeHead(503); res.end('{"error":"usage_not_available"}');
    }
  });
}

if (require.main === module) {
  const state = process.env.LIGHTBOX_STATE_DIR || path.join(os.homedir(), '.openclaw-lightbox');
  const config = JSON.parse(fs.readFileSync(path.join(state, 'openclaw.json'), 'utf8'));
  const prices = JSON.parse(fs.readFileSync(path.join(__dirname, 'lightbox-pricing.json'), 'utf8'));
  const token = process.env.OPENCLAW_GATEWAY_TOKEN || config.gateway?.auth?.token;
  createServer({ token, dbPath: path.join(state, 'agents/main/agent/openclaw-agent.sqlite'), prices })
    .listen(18791, '127.0.0.1', () => process.stdout.write('利用量の記録を開始しました (18791)\n'));
}
module.exports = { estimate, readRecords, createServer };
