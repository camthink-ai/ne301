import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

class FakeWebSocket {
  static CONNECTING = 0;

  static OPEN = 1;

  static CLOSING = 2;

  static CLOSED = 3;

  static instances: FakeWebSocket[] = [];

  readyState = FakeWebSocket.CONNECTING;

  binaryType = '';

  url: string;

  onopen: ((ev: Event) => void) | null = null;

  onclose: ((ev: CloseEvent) => void) | null = null;

  onerror: ((ev: Event) => void) | null = null;

  onmessage: ((ev: MessageEvent) => void) | null = null;

  constructor(url: string) {
    this.url = url;
    FakeWebSocket.instances.push(this);
  }

  close() {
    this.readyState = FakeWebSocket.CLOSED;
  }

  simulateOpen() {
    this.readyState = FakeWebSocket.OPEN;
    this.onopen?.(new Event('open'));
  }

  simulateFailure() {
    this.readyState = FakeWebSocket.CLOSED;
    this.onerror?.(new Event('error'));
    this.onclose?.(new CloseEvent('close', { code: 1006 }));
  }
}

type WorkerMessage = { type: string; [key: string]: unknown };

async function importWorker() {
  vi.resetModules();
  FakeWebSocket.instances = [];
  const postMessage = vi.fn<(msg: WorkerMessage) => void>();
  vi.stubGlobal('postMessage', postMessage);
  vi.stubGlobal('WebSocket', FakeWebSocket);
  await import('@/lib/MSE/websocket-init');
  const send = (data: unknown) => {
    (globalThis as any).onmessage({ data });
  };
  return {
    send,
    postMessage,
    messages: () => postMessage.mock.calls.map(c => c[0]),
  };
}

describe('websocket-init bounded reconnect backoff', () => {
  beforeEach(() => {
    vi.useFakeTimers();
    vi.spyOn(Math, 'random').mockReturnValue(0);
  });

  afterEach(() => {
    vi.useRealTimers();
    vi.restoreAllMocks();
    vi.unstubAllGlobals();
  });

  it('retries with exponential backoff and gives up after 5 attempts', async () => {
    const { send, messages } = await importWorker();
    send({ type: 'connect', url: 'ws://dev/stream' });
    expect(FakeWebSocket.instances).toHaveLength(1);

    for (let retry = 1; retry <= 5; retry += 1) {
      FakeWebSocket.instances[
        FakeWebSocket.instances.length - 1
      ].simulateFailure();
      // eslint-disable-next-line no-await-in-loop
      await vi.advanceTimersByTimeAsync(1000 * 2 ** (retry - 1));
      expect(FakeWebSocket.instances).toHaveLength(retry + 1);
    }

    FakeWebSocket.instances[
      FakeWebSocket.instances.length - 1
    ].simulateFailure();
    await vi.advanceTimersByTimeAsync(60_000);

    expect(FakeWebSocket.instances).toHaveLength(6);
    const exhausted = messages().filter(m => m.type === 'reconnect-exhausted');
    expect(exhausted).toHaveLength(1);
    expect(exhausted[0].attempts).toBe(5);
  });

  it('posts exactly five reconnect notifications before giving up', async () => {
    const { send, messages } = await importWorker();
    send({ type: 'connect', url: 'ws://dev/stream' });
    FakeWebSocket.instances[0].simulateFailure();
    for (let retry = 1; retry <= 5; retry += 1) {
      // eslint-disable-next-line no-await-in-loop
      await vi.advanceTimersByTimeAsync(1000 * 2 ** (retry - 1));
      FakeWebSocket.instances[
        FakeWebSocket.instances.length - 1
      ].simulateFailure();
    }
    await vi.advanceTimersByTimeAsync(30_000);
    expect(messages().filter(m => m.type === 'reconnect')).toHaveLength(6);
  });

  it('re-arms the full budget on a new connect command after give-up', async () => {
    const { send } = await importWorker();
    send({ type: 'connect', url: 'ws://dev/stream' });
    FakeWebSocket.instances[0].simulateFailure();
    for (let retry = 1; retry <= 5; retry += 1) {
      // eslint-disable-next-line no-await-in-loop
      await vi.advanceTimersByTimeAsync(1000 * 2 ** (retry - 1));
      FakeWebSocket.instances[
        FakeWebSocket.instances.length - 1
      ].simulateFailure();
    }
    await vi.advanceTimersByTimeAsync(30_000);
    expect(FakeWebSocket.instances).toHaveLength(6);

    send({ type: 'connect', url: 'ws://dev/stream' });
    expect(FakeWebSocket.instances).toHaveLength(7);

    FakeWebSocket.instances[6].simulateFailure();
    await vi.advanceTimersByTimeAsync(1_000);
    expect(FakeWebSocket.instances).toHaveLength(8);
  });

  it('resets the backoff budget after a successful reconnect', async () => {
    const { send } = await importWorker();
    send({ type: 'connect', url: 'ws://dev/stream' });
    FakeWebSocket.instances[0].simulateFailure();
    await vi.advanceTimersByTimeAsync(1_000);
    FakeWebSocket.instances[1].simulateFailure();
    await vi.advanceTimersByTimeAsync(2_000);
    expect(FakeWebSocket.instances).toHaveLength(3);

    FakeWebSocket.instances[2].simulateOpen();
    FakeWebSocket.instances[2].simulateFailure();
    await vi.advanceTimersByTimeAsync(1_000);
    expect(FakeWebSocket.instances).toHaveLength(4);

    FakeWebSocket.instances[3].simulateFailure();
    await vi.advanceTimersByTimeAsync(2_000);
    expect(FakeWebSocket.instances).toHaveLength(5);
  });

  it('cancels pending retries on disconnect', async () => {
    const { send } = await importWorker();
    send({ type: 'connect', url: 'ws://dev/stream' });
    FakeWebSocket.instances[0].simulateFailure();
    send({ type: 'disconnect' });
    await vi.advanceTimersByTimeAsync(60_000);
    expect(FakeWebSocket.instances).toHaveLength(1);
  });

  it('does not reconnect on manual teardown (Connection replaced path)', async () => {
    const { send, messages } = await importWorker();
    send({ type: 'connect', url: 'ws://dev/stream' });
    FakeWebSocket.instances[0].simulateOpen();
    send({ type: 'disconnect' });
    FakeWebSocket.instances[0].simulateFailure();
    await vi.advanceTimersByTimeAsync(60_000);
    expect(FakeWebSocket.instances).toHaveLength(1);
    expect(messages().some(m => m.type === 'reconnect-exhausted')).toBe(false);
  });
});
