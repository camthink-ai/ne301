let ws: WebSocket | null = null;
const ctx: any = globalThis as any;

let currentUrl: string | null = null;
let shouldReconnect = false;

let retryCount = 0;
const baseDelay = 1000; // ms
let retryTimer: number | null = null;
 
function clearRetryTimer(): void {
    if (retryTimer !== null) {
        clearTimeout(retryTimer);
        retryTimer = null;
    }
}

function safeClose(socket: WebSocket): void {
    try {
        if (socket.readyState === WebSocket.CONNECTING) {
            socket.addEventListener('open', () => {
                try {
                    socket.close(1000, 'teardown');
                } catch (error) {
                    ctx.postMessage({ type: 'error', error: String(error) });
                }
            }, { once: true });
        } else if (socket.readyState === WebSocket.OPEN || socket.readyState === WebSocket.CLOSING) {
            console.log('safeClose', socket.readyState);
            socket.close(1000, 'teardown');
        }
    } catch (error) {
        ctx.postMessage({ type: 'error', error: String(error) });
    }
}

function teardown(resetUrl = false): void {
    if (ws) {
        const socket = ws;
        ws = null;
        socket.onopen = null;
        socket.onclose = null;
        socket.onerror = null;
        socket.onmessage = null;
        safeClose(socket);
    }

    if (resetUrl) {
        currentUrl = null;
    }

    clearRetryTimer();
}

function scheduleReconnect(): void {
    if (!shouldReconnect || !currentUrl || retryTimer !== null) {
        return;
    }

    retryCount += 1;
    const expo = Math.min(baseDelay * (2 ** (retryCount - 1)), 10000);
    const jitter = Math.floor(Math.random() * 300);
    const delay = expo + jitter;
    clearRetryTimer();
    retryTimer = setTimeout(() => {
        attemptConnect(currentUrl!);
        retryTimer = null;
    }, delay) as unknown as number;
}

function attemptConnect(url: string): void {
    teardown(false);

    try {
        // Notify main thread: connection is starting (used to show loading UI)
        ctx.postMessage({ type: 'connecting' });
        ws = new WebSocket(url);
        const socket = ws;
        socket.binaryType = 'arraybuffer';

        socket.onopen = () => {
            if (ws !== socket) return;
            retryCount = 0;
            clearRetryTimer();
            ctx.postMessage({ type: 'open' });
        };

        socket.onclose = (event: CloseEvent) => {
            if (ws !== socket) return;
            const wasManual = !shouldReconnect;
            teardown(!shouldReconnect);

            if (wasManual) {
                ctx.postMessage({ type: 'close', code: event.code, reason: event.reason });
                return;
            }

            if (event.reason === 'Connection replaced') {
                ctx.postMessage({ type: 'close', code: event.code, reason: event.reason });
                return;
            }
            scheduleReconnect();
        };

        socket.onerror = () => {
            if (ws !== socket) return;
            ctx.postMessage({ type: 'error', error: 'WebSocket error' });
        };

        socket.onmessage = (event: MessageEvent) => {
            if (ws !== socket) return;
            const payload = event.data;
            const message = { type: 'video-data', payload } as const;
            if (payload instanceof ArrayBuffer) {
                ctx.postMessage(message, [payload]);
            } else {
                ctx.postMessage(message);
            }
        };
    } catch (error) {
        ctx.postMessage({ type: 'error', error: String(error) });
        scheduleReconnect();
    }
}

(globalThis as any).onmessage = (e: MessageEvent) => {
    const data = e.data || {};
    switch (data.type) {
        case 'connect': {
            const { url } = data;
            if (!url) {
                ctx.postMessage({ type: 'error', error: 'Missing url for connect' });
                return;
            }

            if (ws && currentUrl === url) {
                const state = ws.readyState;
                if (state === WebSocket.OPEN || state === WebSocket.CONNECTING) {
                    // Already connected or connected to the same URL
                    return;
                }
            }

            shouldReconnect = true;
            currentUrl = url;
            retryCount = 0;
            clearRetryTimer();
            attemptConnect(url);
            break;
        }
        case 'disconnect': {
            shouldReconnect = false;
            teardown(true);
            break;
        }
        case 'send': {
            const { payload } = data;
            if (ws && ws.readyState === WebSocket.OPEN) {
                try {
                    ws.send(payload as any);
                } catch (error) {
                    ctx.postMessage({ type: 'error', error: String(error) });
                }
            }
            break;
        }
        default:
            break;
    }
};