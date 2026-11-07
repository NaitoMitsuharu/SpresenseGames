// Android cannot reliably inspect other PIDs (even a dead PID can return EPERM).
// Preserve every live/uncertain owner. Only retire private Gateway lock records
// whose creation AND last heartbeat demonstrably precede this kernel boot.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function recoverBeforeBoot(stateDir, { bootId, bootTime, now = Date.now(), uid = process.getuid?.() } = {}) {
    if (!/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/i.test(bootId || '') || !Number.isFinite(bootTime) || bootTime > now || now - bootTime < 5000) return 0;
    if (!fs.existsSync(stateDir)) return 0;
    const state = fs.realpathSync(stateDir);
    const directory = path.join(state, 'tmp', `openclaw-${uid}`);
    if (!fs.existsSync(directory)) return 0;
    // No symlinks or foreign-owned directories, including the tmp parent.
    for (const dir of [state, path.join(state, 'tmp'), directory]) {
        const stat = fs.lstatSync(dir);
        if (!stat.isDirectory() || stat.isSymbolicLink() || (uid !== undefined && stat.uid !== uid)) return 0;
    }
    let recovered = 0;
    for (const name of fs.readdirSync(directory)) {
        if (!/^(?:state\.[a-f0-9]{16}|gateway\.state)\.lock$/.test(name)) continue;
        const file = path.join(directory, name);
        const stat = fs.lstatSync(file);
        if (!stat.isFile() || stat.isSymbolicLink() || (uid !== undefined && stat.uid !== uid)) continue;
        const raw = fs.readFileSync(file);
        let owner;
        try { owner = JSON.parse(raw); } catch { continue; }
        const created = Date.parse(owner.createdAt);
        if (owner.stateDir !== state || owner.configPath !== path.join(state, 'openclaw.json') || owner.role !== 'gateway'
            || !Number.isSafeInteger(owner.pid) || owner.pid <= 0 || typeof owner.ownerId !== 'string'
            || !Number.isFinite(created) || created >= bootTime - 1000 || stat.mtimeMs >= bootTime - 1000) continue;
        // Recheck identity, heartbeat and contents immediately before moving the
        // record aside. Never delete a database, credential or owner record.
        const current = fs.lstatSync(file);
        if (current.ino !== stat.ino || current.mtimeMs !== stat.mtimeMs || !fs.readFileSync(file).equals(raw)) continue;
        fs.renameSync(file, `${file}.before-boot-${bootId}`);
        ++recovered;
    }
    return recovered;
}

if (require.main === module && process.platform === 'android') {
    const bootId = fs.readFileSync('/proc/sys/kernel/random/boot_id', 'utf8').trim();
    // Android restricts /proc/uptime for apps. os.uptime() uses the native uptime
    // API but truncates fractions: add one second to use the earliest possible
    // boot time, so a record created after boot never appears to predate it.
    const uptime = os.uptime() + 1;
    const now = Date.now();
    const recovered = recoverBeforeBoot(path.join(os.homedir(), '.openclaw-lightbox'), {
        bootId, now, bootTime: now - uptime * 1000,
    });
    if (recovered) console.log(`端末再起動前のサービス使用中記録を退避しました（${recovered}件）。`);
}
module.exports = { recoverBeforeBoot };
