import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  SIDECAR_DANGEROUS_CONFIRM_TTL_MS,
  SidecarBroker,
  type BridgeServerCall,
} from '@/services/sidecarBridge'
import {
  SIDECAR_BRIDGE_PROTOCOL,
  type SidecarGrant,
  type SidecarIdentity,
  type SidecarRequestMessage,
} from '@/types/sidecar'

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
  capabilities: ['snapshot.read', 'config.read', 'config.apply', 'counters.reset', 'enable.apply'],
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
): Array<[string, Record<string, unknown> & { confirm_token?: string }]> =>
  (serverCall as unknown as {
    mock: { calls: Array<[string, Record<string, unknown> & { confirm_token?: string }]> }
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
  serverCall: BridgeServerCall
  results: Array<Record<string, unknown>>
  revocations: string[]
}

function startBroker(serverCall: BridgeServerCall): BrokerHarness {
  const hostPort = new FakePort()
  const fakeWindow = new FakeWindow()
  const results: Array<Record<string, unknown>> = []
  const revocations: string[] = []
  class FakeChannel {
    port1: FakePort = hostPort

    port2: FakePort = new FakePort()
  }
  const broker = new SidecarBroker({
    appWindow: fakeWindow as unknown as Window,
    appOrigin: 'https://app.example.com',
    grant,
    serverCall,
    onResult: (r) => results.push(r as unknown as Record<string, unknown>),
    onRevoke: (reason) => revocations.push(reason),
  });
  (globalThis as { MessageChannel?: unknown }).MessageChannel = FakeChannel
  broker.start()
  return { broker, hostPort, serverCall, results, revocations }
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

const resultsPostedToApp = (harness: BrokerHarness): Array<Record<string, unknown>> =>
  harness.hostPort.posted.filter(
    (message) => (message as Record<string, unknown>).type === 'RESULT',
  ) as Array<Record<string, unknown>>

const armConfirm = (harness: BrokerHarness, token = 'token-123'): Promise<boolean> =>
  harness.broker.confirmDangerousOp(
    'counters.reset',
    vi.fn(async () => ({ token, expiresAtMs: Date.now() + 60000 })),
  )

describe('sidecar broker dangerous confirm auto-completion flow', () => {
  let originalMessageChannel: unknown

  beforeEach(() => {
    originalMessageChannel = globalThis.MessageChannel
    vi.useFakeTimers()
  })

  afterEach(() => {
    (globalThis as { MessageChannel?: unknown }).MessageChannel = originalMessageChannel
    vi.useRealTimers()
  })

  it('holds the app pending when the first counters.reset is rejected with ADMIN_CONFIRM_REQUIRED', async () => {
    const serverCall = vi.fn(async () => ({
      outcome: 'REJECTED' as const,
      reason: 'ADMIN_CONFIRM_REQUIRED',
    }))
    const harness = startBroker(serverCall as unknown as BridgeServerCall)
    await vi.advanceTimersByTimeAsync(60)
    harness.hostPort.deliver(
      requestFromApp({ seq: 2, request_id: 30, op: 'counters.reset', payload: { session_generation: 3 } }),
    )
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    // The app's port stays free of RESULT traffic: no settlement happened.
    expect(resultsPostedToApp(harness)).toEqual([])
    expect(harness.broker.pendingCount()).toBe(0)
    // The host shell still saw the rejection so its confirm banner can appear.
    expect(harness.results).toHaveLength(1)
    expect(harness.results[0]).toMatchObject({
      type: 'RESULT',
      request_id: 30,
      op: 'counters.reset',
      outcome: 'REJECTED',
      payload: { reason: 'ADMIN_CONFIRM_REQUIRED' },
    })
    harness.broker.revoke('TEST_END')
  })

  it('auto-resends the stashed request after confirmDangerousOp and rewrites the APPLIED RESULT back to the app request id', async () => {
    const serverCall = vi.fn(async (_op, request) => {
      if (request.confirm_token === 'token-123') {
        return { outcome: 'APPLIED' as const, payload: { session_generation: 3 } }
      }
      return { outcome: 'REJECTED' as const, reason: 'ADMIN_CONFIRM_REQUIRED' }
    })
    const harness = startBroker(serverCall as unknown as BridgeServerCall)
    await vi.advanceTimersByTimeAsync(60)
    harness.hostPort.deliver(
      requestFromApp({ seq: 2, request_id: 30, op: 'counters.reset', payload: { session_generation: 3 } }),
    )
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(resultsPostedToApp(harness)).toEqual([])

    expect(await armConfirm(harness)).toBe(true)
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()

    const calls = callsOf(harness.serverCall)
    expect(calls).toHaveLength(2)
    // The re-send uses a fresh broker-owned request id with the armed token
    // attached via the same takeConfirmToken path as a manual retry.
    const resendId = calls[1][1].request_id as number
    expect(resendId).not.toBe(30)
    expect(resendId).toBeLessThan(0x40000000)
    expect(calls[1][1].confirm_token).toBe('token-123')
    expect(calls[1][1].payload).toEqual({ session_generation: 3 })
    expect(JSON.stringify(harness.hostPort.posted)).not.toContain('token-123')

    const appResults = resultsPostedToApp(harness)
    expect(appResults).toHaveLength(1)
    const result = appResults[0]
    expect(result.request_id).toBe(30)
    expect(result.op).toBe('counters.reset')
    expect(result.outcome).toBe('APPLIED')
    expect(result.payload).toEqual({ session_generation: 3 })
    expect(Object.keys(result).sort()).toEqual(
      ['grant_id', 'identity', 'op', 'outcome', 'payload', 'protocol', 'request_id', 'seq', 'type'],
    )
    harness.broker.revoke('TEST_END')
  })

  it('keeps the stashed request alive past the 10s deadline and settles UNKNOWN/CONFIRM_TIMEOUT at the 120s confirm TTL', async () => {
    const serverCall = vi.fn(async () => ({
      outcome: 'REJECTED' as const,
      reason: 'ADMIN_CONFIRM_REQUIRED',
    }))
    const harness = startBroker(serverCall as unknown as BridgeServerCall)
    await vi.advanceTimersByTimeAsync(60)
    harness.hostPort.deliver(
      requestFromApp({ seq: 2, request_id: 30, op: 'counters.reset', payload: { session_generation: 3 } }),
    )
    await vi.advanceTimersByTimeAsync(10_001)
    await flushAsync()
    // The stash must not inherit the default ACK timeout.
    expect(resultsPostedToApp(harness)).toEqual([])
    expect(harness.results.filter((r) => r.outcome === 'UNKNOWN')).toEqual([])

    await vi.advanceTimersByTimeAsync(SIDECAR_DANGEROUS_CONFIRM_TTL_MS - 10_001 + 10)
    await flushAsync()
    const appResults = resultsPostedToApp(harness)
    expect(appResults).toHaveLength(1)
    expect(appResults[0]).toMatchObject({
      request_id: 30,
      op: 'counters.reset',
      outcome: 'UNKNOWN',
      payload: { reason: 'CONFIRM_TIMEOUT' },
    })
    // The stash is gone: a later confirm must not re-send anything.
    expect(await armConfirm(harness)).toBe(true)
    await flushAsync()
    expect(callsOf(harness.serverCall)).toHaveLength(1)
    expect(resultsPostedToApp(harness)).toHaveLength(1)
    harness.broker.revoke('TEST_END')
  })

  it('settles UNKNOWN/CONFIRM_TIMEOUT immediately when the confirm request fails for the stashed op', async () => {
    const serverCall = vi.fn(async () => ({
      outcome: 'REJECTED' as const,
      reason: 'ADMIN_CONFIRM_REQUIRED',
    }))
    const harness = startBroker(serverCall as unknown as BridgeServerCall)
    await vi.advanceTimersByTimeAsync(60)
    harness.hostPort.deliver(
      requestFromApp({ seq: 2, request_id: 30, op: 'counters.reset', payload: { session_generation: 3 } }),
    )
    await flushAsync()
    expect(
      await harness.broker.confirmDangerousOp(
        'counters.reset',
        vi.fn(async () => {
          throw new Error('CONFIRM_REFUSED')
        }),
      ),
    ).toBe(false)
    const appResults = resultsPostedToApp(harness)
    expect(appResults).toHaveLength(1)
    expect(appResults[0]).toMatchObject({
      request_id: 30,
      outcome: 'UNKNOWN',
      payload: { reason: 'CONFIRM_TIMEOUT' },
    })
    harness.broker.revoke('TEST_END')
  })

  it('supersedes an older unconfirmed stash: the older app request gets UNKNOWN/CONFIRM_TIMEOUT, the newest body is re-sent', async () => {
    const serverCall = vi.fn(async (_op, request) => {
      if (request.confirm_token === 'token-123') {
        return { outcome: 'APPLIED' as const, payload: { session_generation: 3 } }
      }
      return { outcome: 'REJECTED' as const, reason: 'ADMIN_CONFIRM_REQUIRED' }
    })
    const harness = startBroker(serverCall as unknown as BridgeServerCall)
    await vi.advanceTimersByTimeAsync(60)
    harness.hostPort.deliver(
      requestFromApp({ seq: 2, request_id: 30, op: 'counters.reset', payload: { session_generation: 3 } }),
    )
    await flushAsync()
    harness.hostPort.deliver(
      requestFromApp({ seq: 3, request_id: 31, op: 'counters.reset', payload: { session_generation: 4 } }),
    )
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    const superseded = resultsPostedToApp(harness)
    expect(superseded).toHaveLength(1)
    expect(superseded[0]).toMatchObject({
      request_id: 30,
      outcome: 'UNKNOWN',
      payload: { reason: 'CONFIRM_TIMEOUT' },
    })

    expect(await armConfirm(harness)).toBe(true)
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    const calls = callsOf(harness.serverCall)
    expect(calls).toHaveLength(3)
    expect(calls[2][1].payload).toEqual({ session_generation: 4 })
    expect(calls[2][1].confirm_token).toBe('token-123')
    const appResults = resultsPostedToApp(harness)
    expect(appResults).toHaveLength(2)
    expect(appResults[1]).toMatchObject({ request_id: 31, outcome: 'APPLIED' })
    harness.broker.revoke('TEST_END')
  })

  it('never settles a non-dangerous REJECTED through the stash path', async () => {
    const serverCall = vi.fn(async () => ({
      outcome: 'REJECTED' as const,
      reason: 'ADMIN_CONFIRM_REQUIRED',
    }))
    const harness = startBroker(serverCall as unknown as BridgeServerCall)
    await vi.advanceTimersByTimeAsync(60)
    harness.hostPort.deliver(
      requestFromApp({ seq: 2, request_id: 33, op: 'snapshot.read', payload: {} }),
    )
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    const appResults = resultsPostedToApp(harness)
    expect(appResults).toHaveLength(1)
    expect(appResults[0]).toMatchObject({
      request_id: 33,
      outcome: 'REJECTED',
      payload: { reason: 'ADMIN_CONFIRM_REQUIRED' },
    })
    // Confirming the dangerous op afterwards has nothing to re-send.
    expect(await armConfirm(harness)).toBe(true)
    await flushAsync()
    expect(callsOf(harness.serverCall)).toHaveLength(1)
    expect(resultsPostedToApp(harness)).toHaveLength(1)
    harness.broker.revoke('TEST_END')
  })
})
