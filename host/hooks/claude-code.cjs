const { readFileSync } = require('node:fs');
const { basename } = require('node:path');
const { cache, visibleReply } = require('./session-cache.cjs');

function observe() {
    const hook = JSON.parse(readFileSync(0, 'utf8'));
    const events = cache('claude');
    const id = hook.session_id;
    if (typeof id !== 'string' || !id || hook.agent_id) return;
    const now = Date.now();
    switch (hook.hook_event_name) {
    case 'SessionStart':
        events.prune();
        break;
    case 'UserPromptSubmit':
        events.write({ id, state: 'running', startedAt: now, updatedAt: now,
            title: basename(hook.cwd || '') || 'Claude Code', transcript: hook.transcript_path });
        break;
    case 'Stop':
    case 'StopFailure':
    case 'SessionEnd': {
        const run = events.read(id);
        if (run?.state !== 'running' || run.startedAt > now) break;
        const state = hook.hook_event_name === 'Stop' ? 'completed' :
            hook.hook_event_name === 'StopFailure' ? 'failed' : 'cancelled';
        events.write({ ...run, state, updatedAt: now,
            reply: state === 'completed' ? visibleReply(hook.last_assistant_message) : undefined });
        break;
    }
    }
}

// A monitoring hook always exits successfully and emits no decisions.
try { observe(); } catch {}
