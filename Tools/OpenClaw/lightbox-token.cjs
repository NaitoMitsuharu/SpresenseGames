'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');

function ensureToken(file, override) {
  const config = JSON.parse(fs.readFileSync(file, 'utf8'));
  const saved = config.gateway?.auth?.token;
  const token = override || (typeof saved === 'string' && saved) || crypto.randomBytes(32).toString('hex');
  if (typeof token !== 'string' || !token.trim() || token.length > 2048 || /[\r\n]/.test(token)) {
    throw new Error('Invalid gateway credential; value withheld');
  }
  config.gateway ??= {}; config.gateway.auth ??= {};
  if (saved !== token) {
    config.gateway.auth.token = token;
    const temporary = file + '.token-' + process.pid;
    try {
      fs.writeFileSync(temporary, JSON.stringify(config, null, 2) + '\n', { mode: 0o600, flag: 'wx' });
      fs.renameSync(temporary, file);
    } finally { if (fs.existsSync(temporary)) fs.unlinkSync(temporary); }
  }
  fs.chmodSync(file, 0o600);
  return token;
}
module.exports = { ensureToken };
if (require.main === module) {
  const state = process.env.OPENCLAW_STATE_DIR || path.join(os.homedir(), '.openclaw-lightbox');
  const file = process.env.OPENCLAW_CONFIG_PATH || path.join(state, 'openclaw.json');
  try { process.stdout.write(ensureToken(file, process.env.OPENCLAW_GATEWAY_TOKEN)); }
  catch { process.stderr.write('Cannot prepare gateway credential; configuration was not reset.\n'); process.exitCode = 1; }
}
