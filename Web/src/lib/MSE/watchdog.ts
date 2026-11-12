export type WatchdogRecoverReason =
  | 'connect-timeout'
  | 'no-valid-frame-join'
  | 'no-valid-frame';

export interface WatchdogConfig {
  joinConnectTimeoutMs: number;
  joinFirstFrameWindowMs: number;
  steadyNoValidFrameMs: number;
}

export const defaultWatchdogConfig: WatchdogConfig = {
  joinConnectTimeoutMs: 30_000,
  joinFirstFrameWindowMs: 10_000,
  steadyNoValidFrameMs: 5_000,
};

export interface WatchdogSnapshot {
  nowMs: number;
  connectionStartedAtMs: number;
  wsOpenAtMs: number | null;
  wsReconnectPending: boolean;
  wsGiveUp: boolean;
  lastValidFrameAtMs: number;
}

export type WatchdogDecision =
  | { action: 'wait'; owner: 'ws-retry' | 'ws-connect' | 'join' | 'steady' }
  | { action: 'recover'; reason: WatchdogRecoverReason };

export function evaluateNoValidFrameWatchdog(
  snapshot: WatchdogSnapshot,
  config: WatchdogConfig = defaultWatchdogConfig
): WatchdogDecision {
  if (snapshot.wsGiveUp) {
    return { action: 'wait', owner: 'ws-retry' };
  }

  const steady = snapshot.lastValidFrameAtMs > 0;
  if (steady) {
    if (snapshot.wsReconnectPending || snapshot.wsOpenAtMs === null) {
      return { action: 'wait', owner: 'ws-retry' };
    }
    if (
      snapshot.nowMs - snapshot.lastValidFrameAtMs
      >= config.steadyNoValidFrameMs
    ) {
      return { action: 'recover', reason: 'no-valid-frame' };
    }
    return { action: 'wait', owner: 'steady' };
  }

  if (snapshot.wsReconnectPending) {
    return { action: 'wait', owner: 'ws-retry' };
  }
  if (snapshot.wsOpenAtMs === null) {
    if (
      snapshot.nowMs - snapshot.connectionStartedAtMs
      >= config.joinConnectTimeoutMs
    ) {
      return { action: 'recover', reason: 'connect-timeout' };
    }
    return { action: 'wait', owner: 'ws-connect' };
  }
  if (snapshot.nowMs - snapshot.wsOpenAtMs >= config.joinFirstFrameWindowMs) {
    return { action: 'recover', reason: 'no-valid-frame-join' };
  }
  return { action: 'wait', owner: 'join' };
}

export function isSocketLive(snapshot: WatchdogSnapshot): boolean {
  return (
    snapshot.wsOpenAtMs !== null
    && !snapshot.wsReconnectPending
    && !snapshot.wsGiveUp
  );
}
