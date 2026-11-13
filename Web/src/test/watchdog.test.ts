import { describe, expect, it } from 'vitest';
import {
  defaultWatchdogConfig,
  evaluateNoValidFrameWatchdog,
  isSocketLive,
  type WatchdogSnapshot,
} from '@/lib/MSE/watchdog';

const T0 = 1_700_000_000_000;

function snap(overrides: Partial<WatchdogSnapshot> = {}): WatchdogSnapshot {
  return {
    nowMs: T0,
    connectionStartedAtMs: T0 - 1_000,
    wsOpenAtMs: null,
    wsReconnectPending: false,
    wsGiveUp: false,
    lastValidFrameAtMs: 0,
    ...overrides,
  };
}

describe('no-valid-frame watchdog join phase', () => {
  it('waits while the first connect is still within the connect timeout', () => {
    const decision = evaluateNoValidFrameWatchdog(
      snap({ nowMs: T0 + 29_999, connectionStartedAtMs: T0, wsOpenAtMs: null })
    );
    expect(decision).toEqual({ action: 'wait', owner: 'ws-connect' });
  });

  it('recovers with connect-timeout when the first connect hangs', () => {
    const decision = evaluateNoValidFrameWatchdog(
      snap({ nowMs: T0 + 30_000, connectionStartedAtMs: T0, wsOpenAtMs: null })
    );
    expect(decision).toEqual({ action: 'recover', reason: 'connect-timeout' });
  });

  it('never triggers while the worker reconnect backoff owns the phase', () => {
    for (const elapsed of [5_000, 30_000, 120_000]) {
      const decision = evaluateNoValidFrameWatchdog(
        snap({
          nowMs: T0 + elapsed,
          connectionStartedAtMs: T0 - elapsed,
          wsReconnectPending: true,
        })
      );
      expect(decision).toEqual({ action: 'wait', owner: 'ws-retry' });
    }
  });

  it('anchors the join window at ws open, not at connection start', () => {
    const openedAt = T0;
    const stillWaiting = evaluateNoValidFrameWatchdog(
      snap({
        nowMs: openedAt + 9_999,
        connectionStartedAtMs: openedAt - 25_000,
        wsOpenAtMs: openedAt,
      })
    );
    expect(stillWaiting).toEqual({ action: 'wait', owner: 'join' });

    const expired = evaluateNoValidFrameWatchdog(
      snap({
        nowMs: openedAt + 10_000,
        connectionStartedAtMs: openedAt - 25_000,
        wsOpenAtMs: openedAt,
      })
    );
    expect(expired).toEqual({
      action: 'recover',
      reason: 'no-valid-frame-join',
    });
  });

  it('join window covers multiple GOP periods', () => {
    const gopMs = 1_000;
    expect(defaultWatchdogConfig.joinFirstFrameWindowMs).toBeGreaterThanOrEqual(
      gopMs * 5
    );
  });
});

describe('no-valid-frame watchdog steady phase', () => {
  it('keeps the 5s steady window once frames have flowed', () => {
    const lastFrame = T0;
    expect(
      evaluateNoValidFrameWatchdog(
        snap({
          nowMs: lastFrame + 4_999,
          wsOpenAtMs: T0 - 60_000,
          lastValidFrameAtMs: lastFrame,
        })
      )
    ).toEqual({ action: 'wait', owner: 'steady' });

    expect(
      evaluateNoValidFrameWatchdog(
        snap({
          nowMs: lastFrame + 5_000,
          wsOpenAtMs: T0 - 60_000,
          lastValidFrameAtMs: lastFrame,
        })
      )
    ).toEqual({ action: 'recover', reason: 'no-valid-frame' });
  });

  it('does not tear the pipeline down while reconnect backoff is pending', () => {
    const decision = evaluateNoValidFrameWatchdog(
      snap({
        nowMs: T0 + 60_000,
        connectionStartedAtMs: T0 - 60_000,
        wsOpenAtMs: null,
        wsReconnectPending: true,
        lastValidFrameAtMs: T0 - 55_000,
      })
    );
    expect(decision).toEqual({ action: 'wait', owner: 'ws-retry' });
  });

  it('stays silent after bounded reconnect attempts are exhausted', () => {
    const decision = evaluateNoValidFrameWatchdog(
      snap({
        nowMs: T0 + 300_000,
        connectionStartedAtMs: T0 - 300_000,
        wsGiveUp: true,
        lastValidFrameAtMs: T0 - 295_000,
      })
    );
    expect(decision).toEqual({ action: 'wait', owner: 'ws-retry' });
  });

  it('treats a closed socket without pending backoff as ws-retry owned', () => {
    const decision = evaluateNoValidFrameWatchdog(
      snap({
        nowMs: T0 + 12_000,
        connectionStartedAtMs: T0 - 12_000,
        wsOpenAtMs: null,
        lastValidFrameAtMs: T0 - 10_000,
      })
    );
    expect(decision).toEqual({ action: 'wait', owner: 'ws-retry' });
  });
});

describe('isSocketLive', () => {
  it('is live only when open with no pending reconnect or give-up', () => {
    expect(isSocketLive(snap({ wsOpenAtMs: T0 }))).toBe(true);
    expect(
      isSocketLive(snap({ wsOpenAtMs: T0, wsReconnectPending: true }))
    ).toBe(false);
    expect(isSocketLive(snap({ wsOpenAtMs: T0, wsGiveUp: true }))).toBe(false);
    expect(isSocketLive(snap({ wsOpenAtMs: null }))).toBe(false);
  });
});
