const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { recoverBeforeBoot } = require('./lightbox-boot.cjs');
const bootId = '5545ffa2-6ffe-4c8a-b5f8-9887d67955f0';

function fixture(run) {
    const state = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'lightbox-boot-')));
    const uid = process.getuid?.();
    const dir = path.join(state, 'tmp', `openclaw-${uid}`);
    fs.mkdirSync(dir, { recursive: true });
    const now = Date.now(), bootTime = now - 120000;
    const owner = { pid: 5138, ownerId: 'previous-owner', role: 'gateway', stateDir: state,
        configPath: path.join(state, 'openclaw.json'), createdAt: new Date(bootTime - 60000).toISOString() };
    const lock = path.join(dir, 'state.1234567890abcdef.lock');
    fs.writeFileSync(lock, JSON.stringify(owner));
    fs.utimesSync(lock, (bootTime - 45000) / 1000, (bootTime - 45000) / 1000);
    const options = { bootId, bootTime, now, uid };
    try { run({ state, dir, lock, owner, options }); }
    finally { fs.rmSync(state, { recursive: true, force: true }); }
}

test('pre-boot owner records are archived once; credentials and database remain unchanged', () => fixture(({state,lock,options}) => {
    for (const name of ['openclaw.json', 'state.sqlite']) fs.writeFileSync(path.join(state,name), 'preserve');
    const original = fs.readFileSync(lock);
    assert.equal(recoverBeforeBoot(state, options), 1);
    assert.deepEqual(fs.readFileSync(`${lock}.before-boot-${bootId}`), original);
    assert.equal(recoverBeforeBoot(state, options), 0);
    for (const name of ['openclaw.json', 'state.sqlite']) assert.equal(fs.readFileSync(path.join(state,name),'utf8'), 'preserve');
}));
test('same-boot owners and owners with a post-boot heartbeat remain protected', () => fixture(({state,lock,owner,options}) => {
    fs.utimesSync(lock, options.now / 1000, options.now / 1000);
    assert.equal(recoverBeforeBoot(state,options),0);
    owner.createdAt=new Date(options.bootTime+1000).toISOString();
    fs.writeFileSync(lock,JSON.stringify(owner));
    fs.utimesSync(lock,(options.bootTime-45000)/1000,(options.bootTime-45000)/1000);
    assert.equal(recoverBeforeBoot(state,options),0);
}));
test('foreign profiles, malformed records and invalid boot facts are preserved', () => fixture(({state,lock,owner,options}) => {
    owner.stateDir += '-other'; fs.writeFileSync(lock,JSON.stringify(owner));
    fs.utimesSync(lock,(options.bootTime-45000)/1000,(options.bootTime-45000)/1000);
    assert.equal(recoverBeforeBoot(state,options),0);
    fs.writeFileSync(lock,'broken'); assert.equal(recoverBeforeBoot(state,options),0);
    assert.equal(recoverBeforeBoot(state,{...options,bootId:'unknown'}),0);
    assert.equal(recoverBeforeBoot(state,{...options,bootTime:NaN}),0);
    assert.equal(recoverBeforeBoot(state,{...options,bootTime:options.now-1000}),0);
}));
test('the boot-boundary grace preserves uncertain owners; a final pre-boot heartbeat recovers', () => fixture(({state,lock,options}) => {
    fs.utimesSync(lock,(options.bootTime-500)/1000,(options.bootTime-500)/1000);
    assert.equal(recoverBeforeBoot(state,options),0);
    fs.utimesSync(lock,(options.bootTime-2000)/1000,(options.bootTime-2000)/1000);
    assert.equal(recoverBeforeBoot(state,options),1);
}));
test('symbolic owner records are not followed', { skip: process.platform === 'win32' }, () => fixture(({state,lock,options}) => {
    const original=lock+'.original'; fs.renameSync(lock,original);fs.symlinkSync(original,lock);
    assert.equal(recoverBeforeBoot(state,options),0);
    assert.equal(fs.lstatSync(lock).isSymbolicLink(),true);
}));
