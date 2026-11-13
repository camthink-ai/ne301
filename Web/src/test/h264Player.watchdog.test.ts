import {
  afterEach,
  beforeAll,
  beforeEach,
  describe,
  expect,
  it,
  vi,
} from 'vitest';

const logStreamError = vi.fn();
vi.mock('@/services/request', () => ({
  logStreamError: (...args: unknown[]) => logStreamError(...args),
}));

class FakeWorker {
  onmessage: ((ev: { data: unknown }) => void) | null = null;

  posted: unknown[] = [];

  terminated = false;

  postMessage(msg: unknown): void {
    this.posted.push(msg);
  }

  terminate(): void {
    this.terminated = true;
  }
}

type PlayerConstructor = typeof import('@/lib/MSE/h264Player').default;
let H264Player: PlayerConstructor;

function makeVideoElement(): HTMLVideoElement {
  return {
    currentTime: 0,
    paused: true,
    src: '',
    seekable: { length: 0 },
    buffered: { length: 0 },
    addEventListener: vi.fn(),
    removeEventListener: vi.fn(),
  } as unknown as HTMLVideoElement;
}

function makeFrame(timestampSeconds = 1_700_000_000): ArrayBuffer {
  const headerSize = 64;
  const payload = new Uint8Array([0, 0, 0, 1, 0x65, 0x88, 0x84]);
  const frame = new ArrayBuffer(headerSize + payload.byteLength);
  const view = new DataView(frame);
  view.setUint32(12, timestampSeconds, false);
  new Uint8Array(frame, headerSize).set(payload);
  return frame;
}

function workerOf(player: InstanceType<PlayerConstructor>): FakeWorker {
  return (player as unknown as { webSocketWorker: FakeWorker }).webSocketWorker;
}

function emitWs(
  player: InstanceType<PlayerConstructor>,
  msg: Record<string, unknown>
): void {
  workerOf(player).onmessage?.({ data: msg });
}

function recoverReasons(): string[] {
  return logStreamError.mock.calls
    .filter(c => c[0] === 'VIDEO' && c[1] === 'stall-recover')
    .map(c => String(c[3]));
}

beforeAll(async () => {
  const appendChild = vi
    .spyOn(document.head, 'appendChild')
    .mockImplementation(node => node);
  if (typeof window.URL.revokeObjectURL !== 'function') {
    window.URL.revokeObjectURL =      vi.fn() as unknown as typeof window.URL.revokeObjectURL;
  }
  H264Player = (await import('@/lib/MSE/h264Player')).default;
  appendChild.mockRestore();
});

describe('H264Player watchdog wiring', () => {
  let player: InstanceType<PlayerConstructor>;

  beforeEach(async () => {
    vi.useFakeTimers();
    vi.stubGlobal('Worker', FakeWorker);
    logStreamError.mockClear();
    player = new H264Player(() => {});
    player.initPlayer(makeVideoElement());
    await player.start('ws://dev/stream');
  });

  afterEach(() => {
    player.destroy();
    vi.useRealTimers();
    vi.unstubAllGlobals();
  });

  it('does not tear the pipeline down while reconnect backoff is pending (join phase)', async () => {
    emitWs(player, { type: 'open' });
    emitWs(player, { type: 'reconnect', code: 1006 });

    await vi.advanceTimersByTimeAsync(120_000);

    expect(recoverReasons()).toEqual([]);
    expect(workerOf(player).terminated).toBe(false);
  });

  it('recovers with connect-timeout when the first connect hangs', async () => {
    await vi.advanceTimersByTimeAsync(31_000);
    expect(recoverReasons()).toEqual(['connect-timeout; restarting stream']);
  });

  it('anchors the join window at ws open and recovers after it expires', async () => {
    await vi.advanceTimersByTimeAsync(29_000);
    expect(recoverReasons()).toEqual([]);

    emitWs(player, { type: 'open' });
    await vi.advanceTimersByTimeAsync(9_000);
    expect(recoverReasons()).toEqual([]);

    await vi.advanceTimersByTimeAsync(1_500);
    expect(recoverReasons()).toEqual([
      'no-valid-frame-join; restarting stream',
    ]);
  });

  it('keeps the steady 5s no-valid-frame window when the socket is live', async () => {
    emitWs(player, { type: 'open' });
    emitWs(player, { type: 'video-data', payload: makeFrame() });

    await vi.advanceTimersByTimeAsync(4_500);
    expect(recoverReasons()).toEqual([]);

    await vi.advanceTimersByTimeAsync(1_000);
    expect(recoverReasons()).toEqual(['no-valid-frame; restarting stream']);
  });

  it('does not hard-restart during a steady-phase outage with backoff pending', async () => {
    emitWs(player, { type: 'open' });
    emitWs(player, { type: 'video-data', payload: makeFrame() });
    emitWs(player, { type: 'reconnect', code: 1006 });
    emitWs(player, { type: 'error', error: 'WebSocket error' });

    await vi.advanceTimersByTimeAsync(60_000);

    expect(recoverReasons()).toEqual([]);
    expect(workerOf(player).terminated).toBe(false);
  });

  it('stays silent after reconnect attempts are exhausted and surfaces the failure', async () => {
    emitWs(player, { type: 'open' });
    emitWs(player, { type: 'video-data', payload: makeFrame() });
    emitWs(player, { type: 'reconnect', code: 1006 });
    emitWs(player, { type: 'reconnect-exhausted', attempts: 5 });

    await vi.advanceTimersByTimeAsync(300_000);

    expect(recoverReasons()).toEqual([]);
    const exhausted = logStreamError.mock.calls.filter(
      c => c[1] === 'reconnect-exhausted'
    );
    expect(exhausted).toHaveLength(1);
  });

  it('keeps the ws worker alive after a socket error (worker owns reconnection)', async () => {
    emitWs(player, { type: 'open' });
    emitWs(player, { type: 'error', error: 'WebSocket error' });

    const postedTypes = workerOf(player).posted.map(
      m => (m as { type: string }).type
    );
    expect(postedTypes).not.toContain('disconnect');
    expect(workerOf(player).terminated).toBe(false);
  });

  it('starts a new join window when a reconnect re-opens; the stale last frame must not fire the 5s steady stall', async () => {
    emitWs(player, { type: 'open' });
    emitWs(player, { type: 'video-data', payload: makeFrame() });
    emitWs(player, { type: 'reconnect', code: 1006 });

    await vi.advanceTimersByTimeAsync(20_000);
    expect(recoverReasons()).toEqual([]);

    emitWs(player, { type: 'open' });

    await vi.advanceTimersByTimeAsync(6_000);
    expect(recoverReasons()).toEqual([]);

    await vi.advanceTimersByTimeAsync(4_500);
    expect(recoverReasons()).toEqual([
      'no-valid-frame-join; restarting stream',
    ]);
  });

  it('marks the socket dead on a terminal close (Connection replaced) and never auto-restarts', async () => {
    const originalWorker = workerOf(player);
    emitWs(player, { type: 'open' });
    emitWs(player, { type: 'video-data', payload: makeFrame() });
    emitWs(player, { type: 'close', code: 1000, reason: 'Connection replaced' });

    await vi.advanceTimersByTimeAsync(120_000);

    expect(recoverReasons()).toEqual([]);
    expect(workerOf(player)).toBe(originalWorker);
    expect(originalWorker.terminated).toBe(false);
    const postedTypes = originalWorker.posted.map(
      m => (m as { type: string }).type
    );
    expect(postedTypes.filter(t => t === 'connect')).toHaveLength(1);
    expect(postedTypes).not.toContain('disconnect');

    const state = player as unknown as {
      wsOpenAt: number | null;
      wsReconnectPending: boolean;
      wsGiveUp: boolean;
    };
    expect(state.wsOpenAt).toBeNull();
    expect(state.wsReconnectPending).toBe(false);
    expect(state.wsGiveUp).toBe(true);
  });
});
