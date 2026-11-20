const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createHash, randomUUID } = require('node:crypto');

function cache(source) {
    const override = process.env.CLAWDOCK_SESSION_EVENTS_DIR ?? process.env.JINTAI_SESSION_EVENTS_DIR;
    const root = override && path.isAbsolute(override) ? override :
        path.join(os.homedir(), 'Library/Caches/com.jintai.host/sessions');
    const directory = path.join(root, source);
    const file = id => path.join(directory, createHash('sha256').update(id).digest('hex') + '.json');

    function prepare() {
        fs.mkdirSync(directory, { recursive: true, mode: 0o700 });
    }

    return {
        prepare,
        read(id) {
            try { return JSON.parse(fs.readFileSync(file(id), 'utf8')); }
            catch { return undefined; }
        },
        write(event) {
            const temporary = path.join(directory, '.' + randomUUID());
            try {
                prepare();
                fs.writeFileSync(temporary, JSON.stringify(event), { mode: 0o600, flag: 'wx' });
                fs.renameSync(temporary, file(event.id));
            } catch {
                // Observation should leave the agent running when the cache cannot be written.
                try { fs.unlinkSync(temporary); } catch {}
            }
        },
        prune() {
            prepare();
            const cutoff = Date.now() - 24 * 60 * 60 * 1000;
            for (const entry of fs.readdirSync(directory)) {
                if (!/^[a-f0-9]{64}\.json$/.test(entry)) continue;
                const candidate = path.join(directory, entry);
                try {
                    if (fs.lstatSync(candidate).mtimeMs < cutoff) fs.unlinkSync(candidate);
                } catch {}
            }
        },
    };
}

function visibleReply(text) {
    if (typeof text !== 'string') return undefined;
    const characters = Array.from(text.trim());
    if (characters.length === 0) return undefined;
    if (characters.length <= 2400) return characters.join('');
    return characters.slice(0, 2399).join('') + '…';
}

module.exports = { cache, visibleReply };
