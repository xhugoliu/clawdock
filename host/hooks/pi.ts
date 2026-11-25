import { basename } from 'node:path';
import type { ExtensionAPI, ExtensionContext } from '@earendil-works/pi-coding-agent';
import sessionCache from './session-cache.cjs';

export default function observeSessions(pi: ExtensionAPI) {
    const events = sessionCache.cache('pi');
    let startedAt: number | undefined;
    let reply: string | undefined;
    let stopReason: string | undefined;
    let heartbeat: ReturnType<typeof setInterval> | undefined;
    let waitingForAnswer = false;

    function publish(ctx: ExtensionContext, state: string) {
        if (startedAt === undefined) return;
        const session = ctx.sessionManager;
        const title = session.getSessionName() || basename(session.getCwd()) || 'Pi';
        events.write({ id: session.getSessionId(), title, state, startedAt, updatedAt: Date.now(), reply });
    }

    function publishActive(ctx: ExtensionContext) {
        publish(ctx, waitingForAnswer ? 'waitingForAnswer' : 'running');
    }

    function finish(ctx: ExtensionContext, state: string) {
        clearInterval(heartbeat);
        heartbeat = undefined;
        publish(ctx, state);
        startedAt = undefined;
        reply = undefined;
        stopReason = undefined;
        waitingForAnswer = false;
    }

    pi.on('session_start', () => { try { events.prune(); } catch {} });
    pi.on('agent_start', (_event, ctx) => {
        if (startedAt === undefined) {
            startedAt = Date.now();
            heartbeat = setInterval(() => publishActive(ctx), 30_000);
            heartbeat.unref();
        }
        stopReason = undefined;
        publishActive(ctx);
    });
    pi.on('ui_prompt_start', ({ kind }, ctx) => {
        if (startedAt === undefined || (kind !== 'select' && kind !== 'custom')) return;
        waitingForAnswer = true;
        publishActive(ctx);
    });
    pi.on('ui_prompt_end', (_event, ctx) => {
        if (!waitingForAnswer) return;
        waitingForAnswer = false;
        publishActive(ctx);
    });
    pi.on('message_end', ({ message }, ctx) => {
        if (message.role !== 'assistant' || startedAt === undefined) return;
        stopReason = message.stopReason;
        const text = message.content.filter(part => part.type === 'text').map(part => part.text).join('\n');
        reply = sessionCache.visibleReply(text) ?? reply;
        publishActive(ctx);
    });
    pi.on('agent_settled', (event, ctx) => {
        const state = event.aborted || stopReason === 'aborted' ? 'cancelled' :
            stopReason === 'error' ? 'failed' : 'completed';
        finish(ctx, state);
    });
    pi.on('session_shutdown', (_event, ctx) => finish(ctx, 'cancelled'));
}
