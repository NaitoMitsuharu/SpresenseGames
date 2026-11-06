const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const { ensureToken, readToken } = require('./lightbox-token.cjs');
test('fresh gateway credential survives subsequent launches without changing the provider', () => {
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'lightbox-token-'));
  try {
    const file=path.join(dir,'openclaw.json');
    fs.writeFileSync(file,JSON.stringify({gateway:{auth:{mode:'token'}},agents:{defaults:{model:'fixture/model'}}}));
    const token=ensureToken(file);assert.match(token,/^[a-f0-9]{64}$/);
    assert.equal(ensureToken(file),token);
    const config=JSON.parse(fs.readFileSync(file));assert.equal(config.agents.defaults.model,'fixture/model');
    assert.equal(config.gateway.auth.token,token);
    assert.equal(ensureToken(file,'fixture-replacement'), 'fixture-replacement');
    assert.equal(ensureToken(file),'fixture-replacement');
    const before=fs.readFileSync(file,'utf8');
    assert.throws(()=>ensureToken(file,'bad\nkey'));assert.equal(fs.readFileSync(file,'utf8'),before);
  } finally { fs.rmSync(dir,{recursive:true,force:true}); }
});
test('invalid gateway configuration is preserved for repair', () => {
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'lightbox-token-'));
  try { const file=path.join(dir,'openclaw.json');fs.writeFileSync(file,'broken');assert.throws(()=>ensureToken(file));assert.equal(fs.readFileSync(file,'utf8'),'broken'); }
  finally { fs.rmSync(dir,{recursive:true,force:true}); }
});
test('read-only mode never creates a key', () => {
  const dir=fs.mkdtempSync(path.join(os.tmpdir(),'lightbox-token-'));
  try {
    const file=path.join(dir,'openclaw.json');
    assert.equal(readToken(file),'');
    fs.writeFileSync(file,JSON.stringify({gateway:{auth:{mode:'token'}}}));
    const before=fs.readFileSync(file,'utf8');
    assert.equal(readToken(file),'');assert.equal(fs.readFileSync(file,'utf8'),before);
    const token=ensureToken(file);assert.equal(readToken(file),token);
  } finally { fs.rmSync(dir,{recursive:true,force:true}); }
});
