import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import {
  SIDECAR_LIVE_POLL_REQUEST_ID_BASE,
  type SidecarGrant,
  type SidecarIdentity,
  type SidecarLiveView,
  type SidecarRequestMessage,
} from '@/types/sidecar'
import {
  SIDECAR_LIVE_POLL_INTERVAL_MS,
  SidecarBroker,
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
  grant_id: 'b'.repeat(64),
  expires_at_ms: 9999999,
  issued_at_ms: 1,
  capabilities: ['enable.read', 'enable.apply', 'live.subscribe', 'live.unsubscribe', 'counters.reset'],
  identity,
}

const chainMicrotasks = (count: number): Promise<void> => {
  if (count <= 0) return Promise.resolve()
  return Promise.resolve().then(() => chainMicrotasks(count - 1))
}

const flushAsync = (): Promise<void> => chainMicrotasks(20)

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

const baseView = (): SidecarLiveView => ({
  session_generation: 3,
  view_epoch: 5,
  freshness: 'fresh',
  age_ms: 40,
  state: 'continuous',
  through_seq: 2,
  source_total: 2,
  source_gap: 0,
  host_dropped_events: 0,
  packet_count: 2,
  last_packet_seq: 2,
  loss_unknown: false,
  resync_required: false,
  tracks_truncated: false,
  next_cursor: 3,
  target_class: 'person',
  tracks: [
    {
      track_id: 9,
      point_count: 2,
      class_index: 0,
      state: 1,
      flags: 0,
      confidence_q16: 60000,
      points: [
        { ts_ms: 1000, x_q16: 32768, y_q16: 16384 },
        { ts_ms: 1100, x_q16: 49152, y_q16: 65535 },
      ],
    },
  ],
  events: [
    {
      cursor: 1,
      event_seq: 1,
      track_id: 9,
      event_ts_ms: 1700000000000,
      direction: 1,
      flags: 0,
      x_q8: 128,
      y_q8: 64,
    },
    {
      cursor: 2,
      event_seq: 2,
      track_id: 10,
      event_ts_ms: 1700000001000,
      direction: 2,
      flags: 1,
      x_q8: 0,
      y_q8: 255,
    },
  ],
})

interface LiveHarness {
  broker: SidecarBroker
  hostPort: FakePort
  serverCall: BridgeServerCall
  views: SidecarLiveView[]
  revocations: string[]
  results: unknown[]
}

function startLiveBroker(views: SidecarLiveView[], outcomes?: string[]): LiveHarness {
  const hostPort = new FakePort()
  const fakeWindow = new FakeWindow()
  const revocations: string[] = []
  const results: unknown[] = []
  let call = 0
  const serverCall = vi.fn(async () => {
    const index = Math.min(call, views.length - 1)
    call += 1
    const outcome = outcomes?.[index] ?? 'APPLIED'
    if (outcome === 'APPLIED') {
      return { outcome, payload: views[index] }
    }
    return { outcome, reason: 'LIVE_UNAVAILABLE' }
  }) as unknown as BridgeServerCall
  class FakeChannel {
    port1: FakePort = hostPort

    port2: FakePort = new FakePort()
  }
  const broker = new SidecarBroker({
    appWindow: fakeWindow as unknown as Window,
    appOrigin: 'https://app.example.com',
    grant,
    serverCall,
    onResult: (r) => results.push(r),
    onRevoke: (reason) => revocations.push(reason),
  })
  const channelGlobal = globalThis as { MessageChannel?: unknown }
  channelGlobal.MessageChannel = FakeChannel
  broker.start()
  return { broker, hostPort, serverCall, views, revocations, results }
}

const requestFromApp = (overrides?: Partial<SidecarRequestMessage>): SidecarRequestMessage => ({
  protocol: SIDECAR_LIVE_POLL_REQUEST_ID_BASE > 0 ? 'ne301.sidecar.bridge.v1' : '',
  type: 'REQUEST',
  seq: 1,
  grant_id: grant.grant_id,
  identity,
  request_id: 5,
  op: 'live.subscribe',
  payload: { stream: 'live' },
  ...overrides,
}) as SidecarRequestMessage

function statusEventsPostedToApp(harness: LiveHarness): string[] {
  return eventsPostedToApp(harness)
    .map((message) => (message.payload as { status?: string }).status)
    .filter((status): status is string => status !== undefined)
}

function eventsPostedToApp(harness: LiveHarness): Array<Record<string, unknown>> {
  return harness.hostPort.posted.filter(
    (message) => (message as Record<string, unknown>).type === 'EVENT',
  ) as Array<Record<string, unknown>>
}

describe('sidecar live projection pump', () => {
  let originalMessageChannel: unknown

  beforeEach(() => {
    originalMessageChannel = globalThis.MessageChannel
    vi.useFakeTimers()
  })

  afterEach(() => {
    const channelGlobal = globalThis as { MessageChannel?: unknown }
    channelGlobal.MessageChannel = originalMessageChannel
    vi.useRealTimers()
  })

  it('activates on an APPLIED live.subscribe and transforms the trusted view into exact App frames', async () => {
    const harness = startLiveBroker([baseView()])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(harness.results.at(-1)).toMatchObject({ op: 'live.subscribe', outcome: 'APPLIED' })
    expect(harness.broker.isLivePumping()).toBe(true)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    const pollCalls = (harness.serverCall as unknown as {
      mock: { calls: Array<[string, { request_id: number; payload: { stream: string } }]> }
    }).mock.calls.filter(([op]) => op === 'live.subscribe')
    expect(pollCalls.length).toBeGreaterThanOrEqual(2)
    const tickCalls = pollCalls.filter(([, body]) => body.request_id >= SIDECAR_LIVE_POLL_REQUEST_ID_BASE)
    expect(tickCalls.length).toBeGreaterThanOrEqual(1)
    for (const [, body] of tickCalls) {
      expect(body.payload).toEqual({ stream: 'live' })
    }
    expect(pollCalls.some(([, body]) => body.request_id === 5)).toBe(true)
    const frames = eventsPostedToApp(harness)
    expect(frames.length).toBe(1)
    const frame = frames[0]
    expect(frame.grant_id).toBe(grant.grant_id)
    expect(frame.identity).toEqual(identity)
    const payload = frame.payload as {
      stream: string
      stream_seq: number
      data: Record<string, unknown>
    }
    expect(payload.stream).toBe('live')
    expect(payload.stream_seq).toBe(1)
    expect(Object.keys(payload.data).sort()).toEqual(['events', 'server_now_ms', 'tracks'])
    const events = payload.data.events as Array<Record<string, unknown>>
    expect(events).toEqual([
      {
        sequence: 1,
        timestamp_ms: 1700000000000,
        track_id: 9,
        direction: 'in',
        target_class: 'person',
      },
      {
        sequence: 2,
        timestamp_ms: 1700000001000,
        track_id: 10,
        direction: 'out',
        target_class: 'person',
      },
    ])
    const tracks = payload.data.tracks as Array<{ track_id: number; points: Array<{ x: number; y: number }> }>
    expect(tracks).toEqual([
      {
        track_id: 9,
        points: [
          { x: 500, y: 250 },
          { x: 750, y: 1000 },
        ],
      },
    ])
    harness.broker.revoke('TEST_END')
  })

  it('seeds one empty frame while the view is fresh but carries no new facts and reports nothing on freshness none', async () => {
    const quiet = baseView()
    quiet.events = []
    quiet.tracks = []
    const harness = startLiveBroker([quiet])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS * 3 + 10)
    await flushAsync()
    const quietFrames = eventsPostedToApp(harness)
    expect(quietFrames.length).toBe(1)
    expect((quietFrames[0].payload as { data: { events: unknown[]; tracks: unknown[] } }).data).toMatchObject({ events: [], tracks: [] })
    expect(harness.broker.isLivePumping()).toBe(true)
    const empty = baseView()
    empty.freshness = 'none'
    empty.events = []
    empty.tracks = []
    harness.views.splice(0, harness.views.length, empty)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(eventsPostedToApp(harness).length).toBe(2)
    expect(statusEventsPostedToApp(harness)).toEqual(['STALE'])
    expect(harness.broker.isLivePumping()).toBe(true)
    harness.broker.revoke('TEST_END')
  })

  it('projects a fresh empty snapshot as a full tracks replacement after tracks were shown', async () => {
    const cleared = baseView()
    cleared.events = []
    cleared.tracks = []
    const harness = startLiveBroker([baseView(), baseView(), cleared, cleared])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    const frames = eventsPostedToApp(harness)
    expect(frames.length).toBe(2)
    const clearedPayload = frames[1].payload as { stream_seq: number; data: Record<string, unknown> }
    expect(clearedPayload.stream_seq).toBe(2)
    expect(Object.keys(clearedPayload.data).sort()).toEqual(['server_now_ms', 'tracks'])
    expect(clearedPayload.data.tracks).toEqual([])
    expect(clearedPayload.data.events).toBeUndefined()
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(eventsPostedToApp(harness).length).toBe(3)
    expect(harness.broker.isLivePumping()).toBe(true)
    harness.broker.revoke('TEST_END')
  })

  it('posts GAP but keeps polling when the first fresh view already reports a gapped host state', async () => {
    const late = baseView()
    late.state = 'gapped'
    late.source_gap = 2
    late.source_total = 4
    late.through_seq = 4
    const harness = startLiveBroker([late, late])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS * 2 + 10)
    await flushAsync()
    const statuses = eventsPostedToApp(harness).map(
      (message) => (message.payload as { status?: string }).status,
    )
    expect(statuses).toEqual(['GAP', 'GAP'])
    expect(harness.broker.isLivePumping()).toBe(true)
    harness.broker.revoke('TEST_END')
  })

  it('posts GAP but keeps polling when a later fresh view turns gapped without new increments', async () => {
    const quiet = baseView()
    quiet.events = []
    quiet.tracks = []
    const gapped = baseView()
    gapped.state = 'gapped'
    gapped.events = []
    gapped.tracks = []
    const harness = startLiveBroker([quiet, quiet, quiet, gapped])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS * 2 + 10)
    await flushAsync()
    expect(eventsPostedToApp(harness).length).toBe(1)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(statusEventsPostedToApp(harness)).toEqual(['GAP'])
    expect(harness.broker.isLivePumping()).toBe(true)
    harness.broker.revoke('TEST_END')
  })

  it('posts GAP but keeps polling when a fresh view reports an untrusted unknown state', async () => {
    const untrusted = baseView()
    untrusted.state = 'unknown'
    const harness = startLiveBroker([untrusted, untrusted])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS * 2 + 10)
    await flushAsync()
    const statuses = eventsPostedToApp(harness).map(
      (message) => (message.payload as { status?: string }).status,
    )
    expect(statuses).toEqual(['GAP', 'GAP'])
    expect(harness.broker.isLivePumping()).toBe(true)
    harness.broker.revoke('TEST_END')
  })

  const gapStatusCases: Array<{ name: string; patch: Partial<SidecarLiveView>; status: string }> = [
    { name: 'unknown loss', patch: { loss_unknown: true }, status: 'GAP' },
    { name: 'resync required', patch: { resync_required: true }, status: 'GAP' },
    { name: 'truncated track view', patch: { tracks_truncated: true }, status: 'GAP' },
    { name: 'new source gap', patch: { source_gap: 3 }, status: 'GAP' },
    { name: 'new host drops', patch: { host_dropped_events: 4 }, status: 'GAP' },
  ]
  for (const item of gapStatusCases) {
    it(`posts GAP but keeps polling on ${item.name}`, async () => {
      const first = baseView()
      first.events = []
      first.tracks = []
      const second = baseView()
      Object.assign(second, item.patch)
      const harness = startLiveBroker([first, first, second])
      harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
      await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
      await flushAsync()
      expect(harness.broker.isLivePumping()).toBe(true)
      expect(eventsPostedToApp(harness).length).toBe(1)
      await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
      await flushAsync()
      expect(statusEventsPostedToApp(harness)).toEqual([item.status])
      const dataFrames = eventsPostedToApp(harness).filter(
        (message) => (message.payload as { data?: unknown }).data !== undefined,
      )
      expect(dataFrames.length).toBe(2)
      const gapFrame = dataFrames[1].payload as {
        stream_seq: number
        data: { events: Array<{ sequence: number }>; tracks: Array<{ track_id: number }> }
      }
      expect(gapFrame.stream_seq).toBe(3)
      expect(gapFrame.data.events.map((event) => event.sequence)).toEqual([1, 2])
      expect(gapFrame.data.tracks.map((track) => track.track_id)).toEqual([9])
      expect(harness.broker.isLivePumping()).toBe(true)
      harness.broker.revoke('TEST_END')
    })
  }

  it('delivers the current frame events alongside the GAP status when host drops increase on a fresh continuous view', async () => {
    const quiet = baseView()
    quiet.events = []
    quiet.tracks = []
    const dropping = baseView()
    dropping.host_dropped_events = 7
    const healthy = baseView()
    healthy.host_dropped_events = 7
    const harness = startLiveBroker([quiet, quiet, dropping, healthy])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(eventsPostedToApp(harness).length).toBe(1)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(statusEventsPostedToApp(harness)).toEqual(['GAP'])
    const gapDataFrames = eventsPostedToApp(harness).filter(
      (message) => (message.payload as { data?: unknown }).data !== undefined,
    )
    expect(gapDataFrames.length).toBe(2)
    const gapFrame = gapDataFrames[1].payload as {
      stream_seq: number
      data: { events: Array<{ sequence: number }>; tracks: Array<{ track_id: number }> }
    }
    expect(gapFrame.stream_seq).toBe(3)
    expect(gapFrame.data.events.map((event) => event.sequence)).toEqual([1, 2])
    expect(gapFrame.data.tracks.map((track) => track.track_id)).toEqual([9])
    expect(harness.broker.isLivePumping()).toBe(true)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    const dataFrames = eventsPostedToApp(harness).filter(
      (message) => (message.payload as { data?: unknown }).data !== undefined,
    )
    expect(dataFrames.length).toBe(3)
    const resumed = dataFrames[2].payload as {
      stream_seq: number
      data: { events: Array<{ sequence: number }> }
    }
    expect(resumed.stream_seq).toBe(4)
    expect(resumed.data.events.map((event) => event.sequence)).toEqual([1, 2])
    expect(harness.broker.isLivePumping()).toBe(true)
    expect(harness.broker.isRevoked()).toBe(false)
    harness.broker.revoke('TEST_END')
  })

  it('resumes live data frames after a GAP status without revoke or pump stop', async () => {
    const quiet = baseView()
    quiet.events = []
    quiet.tracks = []
    const gapped = baseView()
    gapped.source_gap = 1
    gapped.source_total = 3
    const healthy = baseView()
    healthy.through_seq = 5
    healthy.source_total = 5
    healthy.packet_count = 5
    healthy.last_packet_seq = 5
    healthy.next_cursor = 6
    const harness = startLiveBroker([quiet, quiet, gapped, healthy])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(eventsPostedToApp(harness).length).toBe(1)
    expect(harness.broker.isLivePumping()).toBe(true)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(statusEventsPostedToApp(harness)).toEqual(['GAP'])
    expect(harness.broker.isLivePumping()).toBe(true)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    const dataFrames = eventsPostedToApp(harness).filter(
      (message) => (message.payload as { data?: unknown }).data !== undefined,
    )
    expect(dataFrames.length).toBe(3)
    const resumed = dataFrames[2].payload as {
      stream_seq: number
      data: { events: Array<{ sequence: number }>; tracks: Array<{ track_id: number }> }
    }
    expect(resumed.stream_seq).toBe(4)
    expect(resumed.data.events.map((event) => event.sequence)).toEqual([1, 2])
    expect(resumed.data.tracks.map((track) => track.track_id)).toEqual([9])
    expect(harness.broker.isLivePumping()).toBe(true)
    expect(harness.broker.isRevoked()).toBe(false)
    expect(harness.revocations).toEqual([])
    harness.broker.revoke('TEST_END')
  })

  it('survives staleness and resumes data frames when the view turns fresh again', async () => {
    const first = baseView()
    first.events = []
    first.tracks = []
    const stale = baseView()
    stale.freshness = 'stale'
    const resumed = baseView()
    const harness = startLiveBroker([first, stale, resumed, resumed])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS * 2 + 10)
    await flushAsync()
    expect(statusEventsPostedToApp(harness)).toEqual(['STALE'])
    expect(harness.broker.isLivePumping()).toBe(true)
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS * 2 + 10)
    await flushAsync()
    const dataFrames = eventsPostedToApp(harness).filter(
      (message) => (message.payload as { data?: unknown }).data !== undefined,
    )
    expect(dataFrames.length).toBeGreaterThanOrEqual(2)
    expect(harness.broker.isLivePumping()).toBe(true)
    harness.broker.revoke('TEST_END')
  })

  it('stops the pump after live.unsubscribe, a non-APPLIED poll and revocation, without any data frame', async () => {
    const empty = baseView()
    empty.events = []
    empty.tracks = []
    const harness = startLiveBroker([empty, empty], ['APPLIED', 'APPLIED', 'UNAVAILABLE'])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(harness.broker.isLivePumping()).toBe(true)
    harness.hostPort.deliver(requestFromApp({ seq: 2, request_id: 6, op: 'live.unsubscribe' }))
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(harness.broker.isLivePumping()).toBe(false)
    expect(eventsPostedToApp(harness)).toEqual([])
    harness.broker.revoke('TEST_END')

    const failing = startLiveBroker([empty, empty], ['APPLIED', 'UNAVAILABLE'])
    failing.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(failing.broker.isLivePumping()).toBe(false)
    expect(statusEventsPostedToApp(failing)).toEqual(['STALE'])
    failing.broker.revoke('TEST_END')

    const revoked = startLiveBroker([empty])
    revoked.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(revoked.broker.isLivePumping()).toBe(true)
    revoked.broker.revoke('FRAME_UNMOUNT')
    expect(revoked.broker.isLivePumping()).toBe(false)
  })

  it('treats a malformed host view as stale instead of trusting it', async () => {
    const empty = baseView()
    empty.events = []
    empty.tracks = []
    const harness = startLiveBroker([empty, { ...empty, freshness: 'bogus' } as unknown as SidecarLiveView])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(SIDECAR_LIVE_POLL_INTERVAL_MS + 10)
    await flushAsync()
    expect(statusEventsPostedToApp(harness)).toEqual(['STALE'])
    expect(harness.broker.isLivePumping()).toBe(false)
    harness.broker.revoke('TEST_END')
  })

  it('arms the admin confirm for enable.apply without injecting a non-live frame into an active live stream', async () => {
    const empty = baseView()
    empty.events = []
    empty.tracks = []
    const harness = startLiveBroker([empty])
    harness.hostPort.deliver(requestFromApp({ seq: 1, request_id: 5 }))
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    expect(harness.broker.isLivePumping()).toBe(true)
    const delivered = await harness.broker.confirmDangerousOp('enable.apply', async () => ({
      token: 't'.repeat(64),
      expiresAtMs: Date.now() + 60000,
    }))
    expect(delivered).toBe(true)
    expect(eventsPostedToApp(harness)).toEqual([])
    harness.hostPort.deliver(requestFromApp({ seq: 2, request_id: 7, op: 'enable.apply', payload: { session_generation: 3, value: false } }))
    await vi.advanceTimersByTimeAsync(60)
    await flushAsync()
    const enableCalls = (harness.serverCall as unknown as {
      mock: { calls: Array<[string, { op: string; confirm_token?: string }]> }
    }).mock.calls.filter(([op]) => op === 'enable.apply')
    expect(enableCalls.length).toBe(1)
    expect(enableCalls[0][1].confirm_token).toBe('t'.repeat(64))
    harness.broker.revoke('TEST_END')
  })
})
