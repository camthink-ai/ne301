import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  SIDECAR_BRIDGE_PROTOCOL,
  SIDECAR_UNAVAILABLE_OPS,
  type SidecarGrant,
  type SidecarIdentity,
  type SidecarOp,
  type SidecarRequestMessage,
} from '@/types/sidecar'
import {
  SidecarBroker,
  VideoSessionTracker,
  buildGrantMessage,
  buildRevokeMessage,
  isCrossSite,
  validateBridgeEnvelope,
  type BridgeServerCall,
} from '@/services/sidecarBridge'

const identity: SidecarIdentity = {
  publisher_id: 'test-publisher',
  app_id: 'line-crossing',
  publisher_key_sha256: '5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3',
  native_tbs_sha256: 'd793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e',
  ui_tbs_sha256: 'fc8bd17703aec3a9b6f0e2875e99731fcbe59be4e68efee6083c6e5553ecb92e',
  build_id: '1.0.0',
  host_boot_generation: 42,
  native_session_generation: 3,
  ui_generation: 11,
  sidecar_service_generation: 7,
}

const grant: SidecarGrant = {
  grant_id: 'a'.repeat(64),
  expires_at_ms: 999999,
  issued_at_ms: 1,
  capabilities: ['snapshot.read', 'config.read', 'config.apply', 'counters.reset'],
  identity,
}

const flushAsync = async (): Promise<void> => {
  await Array.from({ length: 20 }).reduce(
    (pending: Promise<void>) => pending.then(() => undefined),
    Promise.resolve(),
  )
}

const callsOf = (
  serverCall: BridgeServerCall,
): Array<[SidecarOp, Record<string, unknown> & { confirm_token?: string }]> =>
  (serverCall as unknown as {
    mock: { calls: Array<[SidecarOp, Record<string, unknown> & { confirm_token?: string }]> }
  }).mock.calls

class FakePort {
  onmessage: ((event: MessageEvent) => void) | null = null

  onmessageerror: ((event: MessageEvent) => void) | null = null

  posted: unknown[] = []

  closed = false

  postMessage(message: unknown): void {
    this.posted.push(message)
  }

  close(): void {
    this.closed = true
  }

  deliver(data: unknown, origin = 'https://app.example.com'): void {
    this.onmessage?.({ origin, data } as MessageEvent)
  }
}

class FakeWindow {
  readonly postedTargets: Array<{ message: unknown; targetOrigin: string }> = []

  postMessage(message: unknown, targetOrigin: string, _transfer?: Transferable[]): void {
    this.postedTargets.push({ message, targetOrigin })
  }
}

interface BrokerHarness {
  broker: SidecarBroker
  hostPort: FakePort
  window: FakeWindow
  serverCall: BridgeServerCall
  results: unknown[]
  revocations: string[]
}

function startBroker(overrides?: { serverCall?: BridgeServerCall; grant?: SidecarGrant }): BrokerHarness {
  const hostPort = new FakePort()
  const fakeWindow = new FakeWindow()
  const results: unknown[] = []
  const revocations: string[] = []
  const serverCall = overrides?.serverCall ?? (vi.fn(async () => ({
    outcome: 'APPLIED' as const,
  })) as unknown as BridgeServerCall)
  class FakeChannel {
    port1: FakePort = hostPort

    port2: FakePort = new FakePort()
  }
  const broker = new SidecarBroker({
    appWindow: fakeWindow as unknown as Window,
    appOrigin: 'https://app.example.com',
    grant: overrides?.grant ?? grant,
    serverCall,
    onResult: (r) => results.push(r),
    onRevoke: (reason) => revocations.push(reason),
  });
(globalThis as { MessageChannel?: unknown }).MessageChannel = FakeChannel
  broker.start()
  const posted = fakeWindow.postedTargets[0]
  expect(posted).toBeTruthy()
  expect(posted?.targetOrigin).toBe('https://app.example.com')
  return {
    broker,
    hostPort,
    window: fakeWindow,
    serverCall,
    results,
    revocations,
  }
}

const requestFromApp = (overrides?: Partial<SidecarRequestMessage>): SidecarRequestMessage => ({
  protocol: SIDECAR_BRIDGE_PROTOCOL,
  type: 'REQUEST',
  seq: 1,
  grant_id: grant.grant_id,
  identity,
  request_id: 5,
  op: 'snapshot.read',
  payload: {},
  ...overrides,
})

describe('sidecar bridge v1 envelope validation', () => {
  it('accepts a golden REQUEST envelope and rejects structural deviations', () => {
    expect(validateBridgeEnvelope(requestFromApp()).ok).toBe(true)
    const mutations: Array<[string, unknown]> = [
      ['protocol', 'ne301.sidecar.bridge.v2'],
      ['type', 'RESULT'],
      ['seq', '1'],
      ['seq', -1],
      ['grant_id', 'short'],
      ['identity', { app_id: 'x' }],
      ['request_id', 0],
      ['op', 'admin.grant'],
      ['payload', 'not-an-object'],
      ['payload', undefined],
    ]
    for (const [key, value] of mutations) {
      const mutated = { ...requestFromApp(), [key]: value } as unknown as Record<string, unknown>
      const verdict = validateBridgeEnvelope(mutated)
      expect(verdict.ok, `${key}=${String(value)} should fail closed`).toBe(false)
    }
  })

  it('rejects a 13-field legacy identity and accepts exactly the 10-field v1 identity', () => {
    const legacy = {
      ...identity,
      version_major: 1,
      version_minor: 4,
      version_patch: 2,
      host_ui_abi: 1,
      sidecar_service_generation: '7',
    } as unknown as Record<string, unknown>
    delete (legacy as Record<string, unknown>).build_id
    expect(validateBridgeEnvelope({ ...requestFromApp(), identity: legacy }).ok).toBe(false)
    const extraField = { ...identity, extra: 1 } as unknown as SidecarIdentity
    expect(validateBridgeEnvelope({ ...requestFromApp(), identity: extraField }).ok).toBe(false)
  })

  it('rejects oversized messages beyond 16 KiB', () => {
    const huge = requestFromApp({
      payload: { blob: 'x'.repeat(20000) },
    })
    expect(validateBridgeEnvelope(huge).ok).toBe(false)
  })
})

describe('sidecar broker handshake', () => {
  let originalMessageChannel: unknown

  beforeEach(() => {
    originalMessageChannel = globalThis.MessageChannel
    vi.useFakeTimers()
  })

  afterEach(() => {
    (globalThis as { MessageChannel?: unknown }).MessageChannel = originalMessageChannel
    vi.useRealTimers()
  })

  it('sends a strict 7-key HELLO with exact target origin and a transferred port, then a 7-key GRANT', async () => {
    const harness = startBroker()
    const hello = harness.window.postedTargets[0]?.message as Record<string, unknown>
    expect(hello.type).toBe('HELLO')
    expect(hello.protocol).toBe(SIDECAR_BRIDGE_PROTOCOL)
    expect(hello.channel).toBe(SIDECAR_BRIDGE_PROTOCOL)
    expect(hello.identity).toEqual(identity)
    expect(Object.keys(hello).sort()).toEqual(
      ['channel', 'grant_id', 'identity', 'protocol', 'seq', 'type'],
    )
    await vi.advanceTimersByTimeAsync(60)
    const grantMessage = harness.hostPort.posted[0] as Record<string, unknown>
    expect(grantMessage.type).toBe('GRANT')
    expect(grantMessage.grant_id).toBe(grant.grant_id)
    expect(grantMessage.expires_at_ms).toBe(grant.expires_at_ms)
    expect(grantMessage.ops).toEqual(grant.capabilities)
    expect(Object.keys(grantMessage).sort()).toEqual(
      ['expires_at_ms', 'grant_id', 'identity', 'ops', 'protocol', 'seq', 'type'],
    )
    harness.broker.revoke('TEST_END')
    expect(harness.revocations).toContain('TEST_END')
  })

  it('relays a REQUEST payload to the server and echoes a strict 9-key RESULT with request_id/op/seq/identity', async () => {
    const harness = startBroker({
      serverCall: vi.fn(async () => ({
        outcome: 'APPLIED' as const,
        config_generation: 6,
        state_revision: 5,
      })),
    })
    harness.hostPort.deliver(
      requestFromApp({
        seq: 1,
        request_id: 9,
        op: 'config.apply',
        payload: { session_generation: 3, expected_config_generation: 6, candidate: { line: { y: 5 } } },
      }),
    )
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(harness.results.length).toBe(1)
    const result = harness.results[0] as Record<string, unknown>
    expect(result.type).toBe('RESULT')
    expect(result.request_id).toBe(9)
    expect(result.op).toBe('config.apply')
    expect(result.outcome).toBe('APPLIED')
    expect(result.identity).toEqual(identity)
    expect(Object.keys(result).sort()).toEqual(
      ['grant_id', 'identity', 'op', 'outcome', 'payload', 'protocol', 'request_id', 'seq', 'type'],
    )
    expect(result.payload).toEqual({ config_generation: 6, state_revision: 5 })
    expect(harness.hostPort.posted.length).toBe(2)
    expect(harness.serverCall).toHaveBeenCalledWith(
      'config.apply',
      expect.objectContaining({ request_id: 9, expected_config_generation: 6 }),
    )
    harness.broker.revoke('TEST_END')
  })

  it('keeps an APPLIED data payload verbatim so the consumer schema stays exact', async () => {
    const snapshot = { session_generation: 3, config_generation: 6, config: { schema: 1 }, status: { state: 'running' } }
    const harness = startBroker({
      serverCall: vi.fn(async () => ({
        outcome: 'APPLIED' as const,
        freshness: 'fresh',
        config_generation: 6,
        payload: snapshot,
      })),
    })
    harness.hostPort.deliver(requestFromApp({ request_id: 4 }))
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    const result = harness.results[0] as Record<string, unknown>
    expect(result.payload).toEqual(snapshot)
    harness.broker.revoke('TEST_END')
  })

  it('wraps failure reasons into the RESULT payload for the accepted consumer', async () => {
    const harness = startBroker({
      serverCall: vi.fn(async () => ({
        outcome: 'UNAVAILABLE' as const,
        reason: 'CAPABILITY_NOT_REVIEWED',
      })),
    })
    harness.hostPort.deliver(
      requestFromApp({ request_id: 6 }),
    )
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    const result = harness.results[0] as Record<string, unknown>
    expect(result.outcome).toBe('UNAVAILABLE')
    expect(result.payload).toEqual({ reason: 'CAPABILITY_NOT_REVIEWED' })
    harness.broker.revoke('TEST_END')
  })

  it('revokes on cross-origin forgery with a non-matching event origin', () => {
    const harness = startBroker()
    harness.hostPort.deliver(requestFromApp(), 'https://attacker.example.com')
    expect(harness.revocations).toEqual(['ORIGIN_MISMATCH'])
    expect(harness.serverCall).not.toHaveBeenCalled()
  })

  it('revokes on protocol mismatch from the sandboxed frame', () => {
    const harness = startBroker()
    harness.hostPort.deliver({ protocol: 'ne301.sidecar.bridge.v2' }, 'https://app.example.com')
    expect(harness.revocations).toEqual(['PROTOCOL_MISMATCH'])
    harness.broker.revoke('TEST_END')
  })

  it('never accepts origin=null as an identity proof for sandboxed frames', () => {
    const harness = startBroker()
    harness.hostPort.deliver(requestFromApp({ request_id: 2 }), 'null')
    expect(harness.revocations).toEqual(['ORIGIN_MISMATCH'])
  })

  it('rejects ops outside the grant capability whitelist without contacting the server', () => {
    const harness = startBroker()
    harness.hostPort.deliver(
      requestFromApp({ request_id: 3, op: 'enable.apply', seq: 3 }),
    )
    expect(harness.results.length).toBe(1)
    const result = harness.results[0] as Record<string, unknown>
    expect(result.outcome).toBe('REJECTED')
    expect((result.payload as Record<string, unknown>).reason).toBe('OP_NOT_IN_GRANT')
    expect(harness.serverCall).not.toHaveBeenCalled()
    harness.broker.revoke('TEST_END')
  })

  it('rejects replayed request_id values', async () => {
    const harness = startBroker()
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 4 }))
    await flushAsync()
    harness.hostPort.deliver(requestFromApp({ seq: 2, request_id: 4 }))
    const outcomes = harness.results.map((r) => (r as Record<string, unknown>).outcome)
    expect(outcomes).toEqual(['APPLIED', 'REJECTED'])
    harness.broker.revoke('TEST_END')
  })

  it('bounds concurrent pending requests at 4 and rejects the fifth', () => {
    const harness = startBroker({
      serverCall: () => new Promise((resolve) => {
        setTimeout(() => resolve({ outcome: 'APPLIED' as const }), 5000)
      }),
    })
    for (let i = 1; i <= 5; i++) {
      harness.hostPort.deliver(requestFromApp({ seq: i, request_id: i }))
    }
    const outcomes = harness.results.map((r) => (r as Record<string, unknown>).outcome)
    expect(outcomes.filter((o) => o === 'REJECTED')).toHaveLength(1)
    expect(harness.broker.pendingCount()).toBe(4)
    harness.broker.revoke('TEST_END')
  })

  it('times out a pending request to UNKNOWN without optimistic commit and revokes after repeated timeouts', async () => {
    const harness = startBroker({
      serverCall: () => new Promise((resolve) => {
        setTimeout(() => resolve({ outcome: 'APPLIED' as const }), 60000)
      }),
    })
    harness.hostPort.deliver(
      requestFromApp({
        request_id: 10,
        op: 'config.apply',
        payload: { session_generation: 3, expected_config_generation: 6, candidate: { a: 1 } },
      }),
    )
    await vi.advanceTimersByTimeAsync(10001)
    const result = harness.results[0] as Record<string, unknown>
    expect(result.outcome).toBe('UNKNOWN')
    expect((result.payload as Record<string, unknown>).reason).toBe('ACK_TIMEOUT')
    harness.hostPort.deliver(
      requestFromApp({
        seq: 2,
        request_id: 11,
        op: 'config.apply',
        payload: { session_generation: 3, expected_config_generation: 6, candidate: { a: 2 } },
      }),
    )
    await vi.advanceTimersByTimeAsync(10001)
    expect(harness.revocations).toContain('ACK_TIMEOUT_LIMIT')
    expect(harness.broker.isRevoked()).toBe(true)
    expect(harness.hostPort.closed).toBe(true)
  })

  it('treats a revoked server response as terminal revocation', async () => {
    const harness = startBroker({
      serverCall: vi.fn(async () => ({
        outcome: 'UNKNOWN' as const,
        reason: 'IDENTITY_OR_GENERATION_CHANGED',
        revoked: true,
      })),
    })
    harness.hostPort.deliver(requestFromApp({ request_id: 20 }))
    await flushAsync()
    expect(harness.broker.isRevoked()).toBe(true)
    expect(harness.revocations).toEqual(['IDENTITY_OR_GENERATION_CHANGED'])
  })

  it('arms the dangerous confirm host-side and emits only a token-free typed EVENT to the app', async () => {
    const harness = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    const delivered = await harness.broker.confirmDangerousOp(
      'counters.reset',
      vi.fn(async () => ({ token: 'token-123', expiresAtMs: Date.now() + 60000 })),
    )
    expect(delivered).toBe(true)
    const event = harness.hostPort.posted[1] as Record<string, unknown>
    expect(event.type).toBe('EVENT')
    expect(Object.keys(event).sort()).toEqual(
      ['grant_id', 'identity', 'payload', 'protocol', 'seq', 'type'],
    )
    expect(event.payload).toEqual({
      stream: 'dangerous.confirm',
      stream_seq: (event.seq as number),
      data: { op: 'counters.reset' },
    })
    expect(JSON.stringify(harness.hostPort.posted)).not.toContain('token-123')
    harness.broker.revoke('TEST_END')
  })

  it('attaches the armed token host-side to the next matching counters.reset HTTP body and consumes it once', async () => {
    const harness = startBroker({
      serverCall: vi.fn(async (op, request) => {
        if (op === 'counters.reset' && request.confirm_token === 'token-123') {
          return { outcome: 'APPLIED' as const, payload: { session_generation: 3 } }
        }
        return { outcome: 'REJECTED' as const, reason: 'ADMIN_CONFIRM_REQUIRED' }
      }),
    })
    await vi.advanceTimersByTimeAsync(60)
    expect(
      await harness.broker.confirmDangerousOp(
        'counters.reset',
        vi.fn(async () => ({ token: 'token-123', expiresAtMs: Date.now() + 60000 })),
      ),
    ).toBe(true)
    harness.hostPort.deliver(
      requestFromApp({
        seq: 2,
        request_id: 30,
        op: 'counters.reset',
        payload: { session_generation: 3 },
      }),
    )
    await flushAsync()
    const calls = callsOf(harness.serverCall)
    expect(calls).toHaveLength(1)
    expect(calls[0][0]).toBe('counters.reset')
    expect(calls[0][1].confirm_token).toBe('token-123')
    expect(calls[0][1].payload).toEqual({ session_generation: 3 })
    expect(harness.results[0]).toMatchObject({ outcome: 'APPLIED', request_id: 30 })
    harness.hostPort.deliver(
      requestFromApp({
        seq: 3,
        request_id: 31,
        op: 'counters.reset',
        payload: { session_generation: 3 },
      }),
    )
    await flushAsync()
    expect(calls).toHaveLength(2)
    expect(calls[1][1].confirm_token).toBeUndefined()
    expect(harness.results[1]).toMatchObject({
      outcome: 'REJECTED',
      payload: { reason: 'ADMIN_CONFIRM_REQUIRED' },
    })
    expect(JSON.stringify(harness.hostPort.posted)).not.toContain('token-123')
    harness.broker.revoke('TEST_END')
  })

  it('keeps the armed token bound to the confirmed op and never leaks it to other ops', async () => {
    const harness = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    await harness.broker.confirmDangerousOp(
      'counters.reset',
      vi.fn(async () => ({ token: 'token-123', expiresAtMs: Date.now() + 60000 })),
    )
    harness.hostPort.deliver(requestFromApp({ seq: 2, request_id: 40, op: 'snapshot.read' }))
    await flushAsync()
    const calls = callsOf(harness.serverCall)
    expect(calls).toHaveLength(1)
    expect(calls[0][0]).toBe('snapshot.read')
    expect(calls[0][1].confirm_token).toBeUndefined()
    harness.hostPort.deliver(
      requestFromApp({
        seq: 3,
        request_id: 41,
        op: 'counters.reset',
        payload: { session_generation: 3 },
      }),
    )
    await flushAsync()
    expect(calls).toHaveLength(2)
    expect(calls[1][0]).toBe('counters.reset')
    expect(calls[1][1].confirm_token).toBe('token-123')
    harness.broker.revoke('TEST_END')
  })

  it('refuses an expired armed token and never replays it on later requests', async () => {
    const harness = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    const armedAt = Date.now()
    await harness.broker.confirmDangerousOp(
      'counters.reset',
      vi.fn(async () => ({ token: 'token-123', expiresAtMs: armedAt + 1000 })),
    )
    vi.advanceTimersByTime(2000)
    harness.hostPort.deliver(
      requestFromApp({
        seq: 2,
        request_id: 50,
        op: 'counters.reset',
        payload: { session_generation: 3 },
      }),
    )
    await flushAsync()
    const calls = callsOf(harness.serverCall)
    expect(calls).toHaveLength(1)
    expect(calls[0][1].confirm_token).toBeUndefined()
    harness.hostPort.deliver(
      requestFromApp({
        seq: 3,
        request_id: 51,
        op: 'counters.reset',
        payload: { session_generation: 3 },
      }),
    )
    await flushAsync()
    expect(calls).toHaveLength(2)
    expect(calls[1][1].confirm_token).toBeUndefined()
    harness.broker.revoke('TEST_END')
  })

  it('drops the armed confirm on revocation and refuses new confirmations afterwards', async () => {
    const harness = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    await harness.broker.confirmDangerousOp(
      'counters.reset',
      vi.fn(async () => ({ token: 'token-123', expiresAtMs: Date.now() + 60000 })),
    )
    harness.broker.revoke('TEST_END')
    expect(
      await harness.broker.confirmDangerousOp(
        'counters.reset',
        vi.fn(async () => ({ token: 'token-456', expiresAtMs: Date.now() + 60000 })),
      ),
    ).toBe(false)
    expect(JSON.stringify(harness.hostPort.posted)).not.toContain('token-456')
  })

  it('binds the armed token to its own grant instance so a sibling broker never receives it', async () => {
    const first = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    await first.broker.confirmDangerousOp(
      'counters.reset',
      vi.fn(async () => ({ token: 'token-123', expiresAtMs: Date.now() + 60000 })),
    )
    const second = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    second.hostPort.deliver(
      requestFromApp({
        seq: 2,
        request_id: 60,
        op: 'counters.reset',
        payload: { session_generation: 3 },
      }),
    )
    await flushAsync()
    const secondCalls = callsOf(second.serverCall)
    expect(secondCalls).toHaveLength(1)
    expect(secondCalls[0][1].confirm_token).toBeUndefined()
    first.hostPort.deliver(
      requestFromApp({
        seq: 2,
        request_id: 61,
        op: 'counters.reset',
        payload: { session_generation: 3 },
      }),
    )
    await flushAsync()
    const firstCalls = callsOf(first.serverCall)
    expect(firstCalls).toHaveLength(1)
    expect(firstCalls[0][1].confirm_token).toBe('token-123')
    first.broker.revoke('TEST_END')
    second.broker.revoke('TEST_END')
  })

  it('refuses malformed confirmations before arming or emitting any EVENT', async () => {
    const harness = startBroker()
    await vi.advanceTimersByTimeAsync(60)
    expect(
      await harness.broker.confirmDangerousOp(
        'counters.reset',
        vi.fn(async () => ({ token: '', expiresAtMs: 123 })),
      ),
    ).toBe(false)
    expect(
      await harness.broker.confirmDangerousOp(
        'counters.reset',
        vi.fn(async () => ({ token: 'abc', expiresAtMs: 'soon' as unknown as number })),
      ),
    ).toBe(false)
    const events = harness.hostPort.posted.filter(
      (message) => (message as { type?: string }).type === 'EVENT',
    )
    expect(events).toHaveLength(0)
    harness.broker.revoke('TEST_END')
  })
})

describe('video session tracker', () => {
  it('moves through Unavailable to Reconnecting to Live and back and notifies subscribers', () => {
    const tracker = new VideoSessionTracker()
    const seen: string[] = []
    tracker.subscribe((state) => seen.push(state))
    expect(seen).toEqual(['Unavailable'])
    tracker.markReconnecting()
    tracker.markLive()
    tracker.markUnavailable()
    tracker.markUnavailable()
    expect(seen).toEqual(['Unavailable', 'Reconnecting', 'Live', 'Unavailable'])
  })
})

describe('video.open media grant flow through the broker', () => {
  const videoGrant: SidecarGrant = {
    ...grant,
    grant_id: 'c'.repeat(64),
    capabilities: ['video.open', 'video.close'],
  }
  const videoPayload = {
    url: 'ws://10.0.0.42:8081/stream',
    session_generation: 3,
    expires_at_ms: 999999,
    grant_id: 'c'.repeat(64),
  }

  beforeEach(() => {
    vi.useFakeTimers()
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('forwards the exact bounded media payload of an APPLIED video.open to the app', async () => {
    const harness = startBroker({
      grant: videoGrant,
      serverCall: vi.fn(async () => ({
        outcome: 'APPLIED' as const,
        payload: videoPayload,
      })) as unknown as BridgeServerCall,
    })
    await vi.advanceTimersByTimeAsync(1)
    harness.hostPort.deliver(requestFromApp({
      grant_id: videoGrant.grant_id,
      op: 'video.open',
      request_id: 21,
      payload: { session_generation: 3 },
    }))
    await vi.advanceTimersByTimeAsync(5)
    const calls = callsOf(harness.serverCall)
    expect(calls).toHaveLength(1)
    expect(calls[0][0]).toBe('video.open')
    expect(calls[0][1].payload).toEqual({ session_generation: 3 })
    const result = harness.results.at(-1) as { op: string; outcome: string; payload?: Record<string, unknown> }
    expect(result.op).toBe('video.open')
    expect(result.outcome).toBe('APPLIED')
    expect(Object.keys(result.payload ?? {})).toEqual([
      'url',
      'session_generation',
      'expires_at_ms',
      'grant_id',
    ])
    expect(result.payload).toEqual(videoPayload)
    expect(harness.revocations).toEqual([])
    expect(harness.broker.isRevoked()).toBe(false)
    harness.broker.revoke('TEST_END')
  })

  it('keeps the session trusted when video.open is merely unavailable', async () => {
    const harness = startBroker({
      grant: videoGrant,
      serverCall: vi.fn(async () => ({
        outcome: 'UNAVAILABLE' as const,
        reason: 'VIDEO_SOURCE_UNAVAILABLE',
      })) as unknown as BridgeServerCall,
    })
    await vi.advanceTimersByTimeAsync(1)
    harness.hostPort.deliver(requestFromApp({
      grant_id: videoGrant.grant_id,
      op: 'video.open',
      request_id: 22,
      payload: { session_generation: 3 },
    }))
    await vi.advanceTimersByTimeAsync(5)
    const result = harness.results.at(-1) as { outcome: string; payload?: { reason?: string } }
    expect(result.outcome).toBe('UNAVAILABLE')
    expect(result.payload?.reason).toBe('VIDEO_SOURCE_UNAVAILABLE')
    expect(harness.revocations).toEqual([])
    expect(harness.broker.isRevoked()).toBe(false)
    expect(harness.broker.video.current()).toBe('Unavailable')
    harness.broker.revoke('TEST_END')
  })

  it('revokes the trusted session when the server flags a video.open as revoked', async () => {
    const harness = startBroker({
      grant: videoGrant,
      serverCall: vi.fn(async () => ({
        outcome: 'REJECTED' as const,
        reason: 'SIDECAR_NOT_ACTIVE',
        revoked: true,
      })) as unknown as BridgeServerCall,
    })
    await vi.advanceTimersByTimeAsync(1)
    harness.hostPort.deliver(requestFromApp({
      grant_id: videoGrant.grant_id,
      op: 'video.open',
      request_id: 23,
      payload: { session_generation: 3 },
    }))
    await vi.advanceTimersByTimeAsync(5)
    expect(harness.revocations).toEqual(['SIDECAR_NOT_ACTIVE'])
    expect(harness.broker.isRevoked()).toBe(true)
    expect(harness.broker.video.current()).toBe('Unavailable')
    expect(harness.hostPort.closed).toBe(true)
  })

  it('delivers an APPLIED video.close and drops the video session to Unavailable', async () => {
    const harness = startBroker({
      grant: videoGrant,
      serverCall: vi.fn(async (op: SidecarOp) => ({
        outcome: 'APPLIED' as const,
        payload: op === 'video.open' ? videoPayload : { session_generation: 3 },
      })) as unknown as BridgeServerCall,
    })
    await vi.advanceTimersByTimeAsync(1)
    harness.hostPort.deliver(requestFromApp({
      grant_id: videoGrant.grant_id,
      op: 'video.open',
      request_id: 24,
      payload: { session_generation: 3 },
    }))
    await vi.advanceTimersByTimeAsync(5)
    harness.hostPort.deliver(requestFromApp({
      grant_id: videoGrant.grant_id,
      seq: 2,
      op: 'video.close',
      request_id: 25,
      payload: { session_generation: 3, grant_id: videoGrant.grant_id },
    }))
    await vi.advanceTimersByTimeAsync(5)
    const closeResult = harness.results.at(-1) as { op: string; outcome: string; payload?: Record<string, unknown> }
    expect(closeResult.op).toBe('video.close')
    expect(closeResult.outcome).toBe('APPLIED')
    expect(closeResult.payload).toEqual({ session_generation: 3 })
    expect(harness.revocations).toEqual([])
    harness.broker.revoke('TEST_END')
  })

  it('classifies only the unreviewed report policy ops as permanently unavailable', () => {
    expect(SIDECAR_UNAVAILABLE_OPS.has('video.open')).toBe(false)
    expect(SIDECAR_UNAVAILABLE_OPS.has('video.close')).toBe(false)
    expect(SIDECAR_UNAVAILABLE_OPS.has('enable.read')).toBe(false)
    expect(SIDECAR_UNAVAILABLE_OPS.has('enable.apply')).toBe(false)
    expect(SIDECAR_UNAVAILABLE_OPS.has('live.subscribe')).toBe(false)
    expect(SIDECAR_UNAVAILABLE_OPS.has('live.unsubscribe')).toBe(false)
    expect(SIDECAR_UNAVAILABLE_OPS.has('report-policy.read')).toBe(true)
    expect(SIDECAR_UNAVAILABLE_OPS.has('report-policy.apply')).toBe(true)
  })
})

describe('bridge message builders and site gate', () => {
  it('builds GRANT and REVOKE envelopes carrying protocol, seq, grant_id and identity', () => {
    const grantMessage = buildGrantMessage(grant, 7)
    expect(grantMessage.protocol).toBe(SIDECAR_BRIDGE_PROTOCOL)
    expect(grantMessage.seq).toBe(7)
    expect(grantMessage.grant_id).toBe(grant.grant_id)
    expect(grantMessage.expires_at_ms).toBe(grant.expires_at_ms)
    expect(grantMessage.ops).toEqual(grant.capabilities)
    const revoke = buildRevokeMessage(grant, 8, 'GENERATION_CHANGED')
    expect(revoke.type).toBe('REVOKE')
    expect(revoke.reason).toBe('GENERATION_CHANGED')
  })

  it('classifies cross-origin and cross-site pairs and refuses same-site substitutes', () => {
    expect(isCrossSite('http://127.0.0.1:8080', 'http://localhost:9090')).toBe(true)
    expect(isCrossSite('https://admin.ne301.local', 'https://app.ne301.local')).toBe(false)
    expect(isCrossSite('http://admin.example.com:8080', 'http://admin.example.com:9090')).toBe(false)
    expect(isCrossSite('http://127.0.0.1:8080', 'http://127.0.0.1:9090')).toBe(false)
    expect(isCrossSite('http://127.0.0.1:8080', 'http://127.0.0.1:8080')).toBe(false)
    expect(isCrossSite('not-a-url', 'http://localhost')).toBe(false)
  })
})
