import {
  SIDECAR_BRIDGE_PROTOCOL,
  SIDECAR_DANGEROUS_OPS,
  SIDECAR_LIVE_POLL_REQUEST_ID_BASE,
  SIDECAR_LIVE_POLL_REQUEST_ID_MASK,
  SIDECAR_LIVE_STREAM,
  SIDECAR_MAX_PENDING_REQUESTS,
  SIDECAR_MSG_MAX_BYTES,
  SIDECAR_OPS,
  SIDECAR_REQUEST_TIMEOUT_MS,
  SIDECAR_MAX_CONSECUTIVE_TIMEOUTS,
  type SidecarAppToHostMessage,
  type SidecarBridgeMessage,
  type SidecarEventMessage,
  type SidecarGrant,
  type SidecarGrantMessage,
  type SidecarHostToAppMessage,
  type SidecarIdentity,
  type SidecarLiveEventData,
  type SidecarLiveEventView,
  type SidecarLiveTrack,
  type SidecarLiveView,
  type SidecarOp,
  type SidecarOutcome,
  type SidecarRequestMessage,
  type SidecarResultMessage,
  type SidecarRevokeMessage,
} from '@/types/sidecar'

export const SIDECAR_LIVE_POLL_INTERVAL_MS = 1000

export const SIDECAR_GRANT_REFRESH_MARGIN_MS = 60_000

/**
 * Deadline the broker keeps a dangerous op (counters.reset / enable.apply)
 * stashed while waiting for the operator to answer the admin confirm banner.
 * Mirrors the firmware APP_BRIDGE_CONFIRM_TTL_MS so the app-side pending
 * request survives exactly as long as the issued confirm token does.
 */
export const SIDECAR_DANGEROUS_CONFIRM_TTL_MS = 120_000

const SHA256_HEX = /^[a-f0-9]{64}$/
const GRANT_ID = /^[A-Za-z0-9_-]{16,128}$/

const isPlainObject = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value)

const isUint32 = (value: unknown): value is number => typeof value === 'number' && Number.isInteger(value) && value >= 0 && value <= 0xffffffff

const isBoundedString = (value: unknown, max: number): value is string => typeof value === 'string' && value.length >= 1 && value.length <= max

const isEpochMs = (value: unknown): value is number => typeof value === 'number' && Number.isSafeInteger(value) && value > 0
const identityKeys = [
  'publisher_id',
  'app_id',
  'publisher_key_sha256',
  'native_tbs_sha256',
  'ui_tbs_sha256',
  'build_id',
  'host_boot_generation',
  'native_session_generation',
  'ui_generation',
  'sidecar_service_generation',
] as const

const identityValid = (identity: unknown): identity is SidecarIdentity => {
  if (!isPlainObject(identity)) return false
  if (Object.keys(identity).length !== identityKeys.length) return false
  for (const key of ['publisher_id', 'app_id', 'build_id'] as const) {
    if (!isBoundedString(identity[key], 128)) return false
  }
  for (const key of ['publisher_key_sha256', 'native_tbs_sha256', 'ui_tbs_sha256'] as const) {
    if (typeof identity[key] !== 'string' || !SHA256_HEX.test(identity[key] as string)) return false
  }
  for (const key of [
    'host_boot_generation',
    'native_session_generation',
    'ui_generation',
    'sidecar_service_generation',
  ] as const) {
    if (!isUint32(identity[key])) return false
  }
  return true
}

export interface EnvelopeValidation {
  ok: boolean
  reason?: string
  message?: SidecarBridgeMessage
}

export function validateBridgeEnvelope(raw: unknown): EnvelopeValidation {
  const encoded = (() => {
    try {
      return JSON.stringify(raw)
    } catch {
      return null
    }
  })()
  if (encoded === null || encoded.length > SIDECAR_MSG_MAX_BYTES) {
    return { ok: false, reason: 'MESSAGE_SIZE_EXCEEDED' }
  }
  if (!isPlainObject(raw)) return { ok: false, reason: 'ENVELOPE_NOT_OBJECT' }
  if (raw.protocol !== SIDECAR_BRIDGE_PROTOCOL) return { ok: false, reason: 'PROTOCOL_MISMATCH' }
  if (typeof raw.type !== 'string') return { ok: false, reason: 'TYPE_MISSING' }
  if (!isUint32(raw.seq)) return { ok: false, reason: 'SEQ_INVALID' }
  if (typeof raw.grant_id !== 'string' || !GRANT_ID.test(raw.grant_id)) {
    return { ok: false, reason: 'GRANT_ID_INVALID' }
  }
  if (!identityValid(raw.identity)) return { ok: false, reason: 'IDENTITY_INVALID' }
  const { type } = raw
  if (type === 'REQUEST') {
    if (!isUint32(raw.request_id) || raw.request_id === 0) {
      return { ok: false, reason: 'REQUEST_ID_INVALID' }
    }
    if (typeof raw.op !== 'string' || !(SIDECAR_OPS as readonly string[]).includes(raw.op)) {
      return { ok: false, reason: 'OP_UNKNOWN' }
    }
    if (!isPlainObject(raw.payload)) {
      return { ok: false, reason: 'PAYLOAD_NOT_OBJECT' }
    }
  } else if (type === 'REVOKE') {
    if (!isBoundedString(raw.reason, 256)) {
      return { ok: false, reason: 'REVOKE_REASON_MISSING' }
    }
  } else {
    return { ok: false, reason: 'TYPE_NOT_ALLOWED_FROM_APP' }
  }
  return { ok: true, message: raw as unknown as SidecarBridgeMessage }
}

export function buildGrantMessage(grant: SidecarGrant, seq: number): SidecarGrantMessage {
  return {
    protocol: SIDECAR_BRIDGE_PROTOCOL,
    type: 'GRANT',
    seq,
    grant_id: grant.grant_id,
    identity: grant.identity,
    expires_at_ms: grant.expires_at_ms,
    ops: [...grant.capabilities],
  }
}

export function buildRevokeMessage(
  grant: SidecarGrant,
  seq: number,
  reason: string,
): SidecarRevokeMessage {
  return {
    protocol: SIDECAR_BRIDGE_PROTOCOL,
    type: 'REVOKE',
    seq,
    grant_id: grant.grant_id,
    identity: grant.identity,
    reason,
  }
}

export interface BridgeServerCall {
  (op: SidecarOp, request: {
    grant_id: string
    request_id: number
    payload: Record<string, unknown>
    expected_config_generation?: number
    confirm_token?: string
  }): Promise<{
    outcome: SidecarOutcome
    reason?: string
    freshness?: string
    config_generation?: number
    state_revision?: number
    age_ms?: number
    payload?: unknown
    revoked?: boolean
    expires_at_ms?: number
  }>
}

export interface SidecarBrokerOptions {
  appWindow: Window
  appOrigin: string
  grant: SidecarGrant
  serverCall: BridgeServerCall
  onResult?: (result: SidecarResultMessage) => void
  onRevoke?: (reason: string) => void
  timeoutMs?: number
  livePollIntervalMs?: number
}

export interface SidecarConfirmGrant {
  token: string
  expiresAtMs: number
}

interface ArmedDangerousConfirm {
  op: SidecarOp
  token: string
  expiresAtMs: number
  grantId: string
}

export type VideoSessionState = 'Unavailable' | 'Reconnecting' | 'Live'

export class VideoSessionTracker {
  private state: VideoSessionState = 'Unavailable'

  private listeners = new Set<(state: VideoSessionState) => void>()

  current(): VideoSessionState {
    return this.state
  }

  subscribe(listener: (state: VideoSessionState) => void): () => void {
    this.listeners.add(listener)
    listener(this.state)
    return () => {
      this.listeners.delete(listener)
    }
  }

  markUnavailable(): void {
    this.set('Unavailable')
  }

  markReconnecting(): void {
    this.set('Reconnecting')
  }

  markLive(): void {
    this.set('Live')
  }

  private set(state: VideoSessionState): void {
    if (this.state === state) return
    this.state = state
    for (const listener of this.listeners) listener(state)
  }
}

interface PendingRequest {
  op: SidecarOp
  seq: number
  timer: ReturnType<typeof setTimeout>
  resolve: (result: SidecarResultMessage) => void
}

/**
 * A dangerous op REQUEST whose first device answer was REJECTED with
 * ADMIN_CONFIRM_REQUIRED. The broker holds it (unsettled for the app) until
 * the operator confirms the banner, then re-sends it once with the confirm
 * token and rewrites the eventual RESULT back to the app's request id.
 */
interface PendingDangerousRequest {
  op: SidecarOp
  payload: Record<string, unknown>
  expectedConfigGeneration?: number
  iframeRequestId: number
  seq: number
  timer: ReturnType<typeof setTimeout>
}

const isBoundedNumber = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value) && value >= 0

const liveViewValid = (payload: unknown): payload is SidecarLiveView => {
  if (!isPlainObject(payload)) return false
  if (!isUint32(payload.session_generation) || !isUint32(payload.view_epoch)) return false
  if (payload.freshness !== 'fresh' && payload.freshness !== 'stale' && payload.freshness !== 'none') {
    return false
  }
  if (!isUint32(payload.age_ms) || typeof payload.state !== 'string' || payload.state.length === 0) {
    return false
  }
  for (const key of ['through_seq', 'source_total', 'source_gap', 'packet_count', 'last_packet_seq', 'next_cursor'] as const) {
    if (!isBoundedNumber(payload[key])) return false
  }
  if (!isBoundedNumber(payload.host_dropped_events)) return false
  if (typeof payload.loss_unknown !== 'boolean' || typeof payload.resync_required !== 'boolean') {
    return false
  }
  if (typeof payload.tracks_truncated !== 'boolean') return false
  if (typeof payload.target_class !== 'string' || payload.target_class.length === 0 || payload.target_class.length > 64) {
    return false
  }
  return true
}

const scaleQ16 = (value: number): number => Math.round((value * 1000) / 65535)

const deviceNowMs = (view: SidecarLiveView): number => (
  isBoundedNumber(view.server_now_ms) ? view.server_now_ms : Date.now()
)

const liveDataOf = (view: SidecarLiveView, replaceEmptyTracks: boolean): SidecarLiveEventData | null => {
  const data: SidecarLiveEventData = { server_now_ms: deviceNowMs(view) }
  const events = Array.isArray(view.events)
    ? view.events
        .filter((event: SidecarLiveEventView) => isPlainObject(event) && isUint32(event.event_seq) && isUint32(event.track_id))
        .map((event: SidecarLiveEventView) => ({
          sequence: event.event_seq,
          timestamp_ms: event.event_ts_ms,
          track_id: event.track_id,
          direction: event.direction === 1 ? ('in' as const) : ('out' as const),
          target_class: view.target_class,
        }))
    : []
  const tracks = Array.isArray(view.tracks)
    ? (view.tracks as SidecarLiveTrack[])
        .filter((track) => isPlainObject(track) && isUint32(track.track_id) && Array.isArray(track.points) && track.points.length > 0)
        .map((track) => ({
          track_id: track.track_id,
          points: track.points
            .filter((point) => isPlainObject(point) && isBoundedNumber(point.x_q16) && isBoundedNumber(point.y_q16))
            .map((point) => ({ x: scaleQ16(point.x_q16), y: scaleQ16(point.y_q16) })),
        }))
        .filter((track) => track.points.length > 0)
    : []
  if (events.length > 0) data.events = events
  if (tracks.length > 0) {
    data.tracks = tracks
  } else if (replaceEmptyTracks && Array.isArray(view.tracks)) {
    data.tracks = []
  }
  if (data.events === undefined && data.tracks === undefined) return null
  return data
}

export class SidecarBroker {
  private grant: SidecarGrant

  readonly video = new VideoSessionTracker()

  private readonly appWindow: Window

  private readonly appOrigin: string

  private readonly serverCall: BridgeServerCall

  private readonly onResult?: (result: SidecarResultMessage) => void

  private readonly onRevoke?: (reason: string) => void

  private readonly timeoutMs: number

  private readonly livePollIntervalMs: number

  private channel: MessageChannel | null = null

  private inboundSeq = 0

  private outboundSeq = 0

  private readonly seenRequestIds = new Set<number>()

  private readonly pending = new Map<number, PendingRequest>()

  private consecutiveTimeouts = 0

  private revoked = false

  private started = false

  private armedConfirm: ArmedDangerousConfirm | null = null

  private pendingDangerous: PendingDangerousRequest | null = null

  private resendRequestId = 0x3fffffff

  private liveTimer: ReturnType<typeof setInterval> | null = null

  private forwardedExpiryMs = 0

  private livePolling = false

  private livePollId = 0

  private liveStreamSeq = 0

  private liveFramePosted = false

  private liveSourceGap: number | null = null

  private liveHostDropped: number | null = null

  private liveTracksProjected = false

  constructor(options: SidecarBrokerOptions) {
    this.appWindow = options.appWindow
    this.appOrigin = options.appOrigin
    this.grant = options.grant
    this.serverCall = options.serverCall
    this.onResult = options.onResult
    this.onRevoke = options.onRevoke
    this.timeoutMs = options.timeoutMs ?? SIDECAR_REQUEST_TIMEOUT_MS
    this.livePollIntervalMs = options.livePollIntervalMs ?? SIDECAR_LIVE_POLL_INTERVAL_MS
    this.forwardedExpiryMs = options.grant.expires_at_ms
  }

  private maybeForwardExpiry(expiresAtMs: number | undefined): void {
    if (typeof expiresAtMs !== 'number' || !Number.isFinite(expiresAtMs)) return
    if (expiresAtMs <= this.forwardedExpiryMs) return
    if (Date.now() < this.forwardedExpiryMs - SIDECAR_GRANT_REFRESH_MARGIN_MS) return
    if (this.revoked || this.channel === null) return
    this.grant = { ...this.grant, expires_at_ms: expiresAtMs }
    this.forwardedExpiryMs = expiresAtMs
    try {
      this.post(buildGrantMessage(this.grant, ++this.outboundSeq))
    } catch {
      this.revoke('GRANT_DELIVERY_FAILED')
    }
  }

  start(): void {
    if (this.started || this.revoked) return
    this.started = true
    const channel = new MessageChannel()
    this.channel = channel
    channel.port1.onmessage = (event: MessageEvent) => this.handlePortMessage(event)
    channel.port1.onmessageerror = () => this.revoke('MESSAGE_DECODE_ERROR')
    const hello = {
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'HELLO',
      seq: ++this.outboundSeq,
      grant_id: this.grant.grant_id,
      identity: this.grant.identity,
      channel: SIDECAR_BRIDGE_PROTOCOL,
    }
    this.appWindow.postMessage(hello, this.appOrigin, [channel.port2])
    setTimeout(() => {
      if (this.revoked || this.channel === null) return
      try {
        this.post(buildGrantMessage(this.grant, ++this.outboundSeq))
      } catch {
        this.revoke('GRANT_DELIVERY_FAILED')
      }
    }, 0)
  }

  revoke(reason: string): void {
    if (this.revoked) return
    this.revoked = true
    this.stopLivePump()
    if (this.channel !== null) {
      const { channel } = this
      try {
        this.post(buildRevokeMessage(this.grant, ++this.outboundSeq, reason))
      } catch {
        this.dropPort()
      }
      channel.port1.onmessage = null
      channel.port1.onmessageerror = null
      try {
        channel.port1.close()
      } catch {
        this.dropPort()
      }
      this.channel = null
    }
    for (const [, entry] of this.pending) {
      clearTimeout(entry.timer)
      entry.resolve(this.makeResult(entry.op, entry.seq, 'UNKNOWN', { reason: 'GRANT_REVOKED' }))
    }
    this.pending.clear()
    this.clearPendingDangerous('UNKNOWN', 'GRANT_REVOKED')
    this.armedConfirm = null
    this.video.markUnavailable()
    this.onRevoke?.(reason)
  }

  isRevoked(): boolean {
    return this.revoked
  }

  pendingCount(): number {
    return this.pending.size
  }

  isLivePumping(): boolean {
    return this.liveTimer !== null
  }

  private startLivePump(): void {
    if (this.revoked || this.liveTimer !== null) return
    this.liveTimer = setInterval(() => {
      this.pumpLiveOnce().catch(() => undefined)
    }, this.livePollIntervalMs)
  }

  private stopLivePump(): void {
    if (this.liveTimer !== null) {
      clearInterval(this.liveTimer)
      this.liveTimer = null
    }
    this.livePolling = false
  }

  private postLiveStatus(status: 'GAP' | 'STALE', fatal = false): void {
    if (fatal) {
      this.stopLivePump()
    }
    try {
      this.post({
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'EVENT',
        seq: ++this.outboundSeq,
        grant_id: this.grant.grant_id,
        identity: this.grant.identity,
        payload: {
          stream: SIDECAR_LIVE_STREAM,
          stream_seq: ++this.liveStreamSeq,
          status,
        },
      } satisfies SidecarEventMessage)
    } catch {
      this.dropPort()
    }
  }

  private postLiveSeedOnce(view: SidecarLiveView): void {
    if (this.liveFramePosted) return
    const data = liveDataOf(view, true)
    const seeded = {
      server_now_ms: deviceNowMs(view),
      events: data?.events ?? [],
      tracks: data?.tracks ?? [],
    }
    if (seeded.tracks !== undefined && seeded.tracks.length > 0) {
      this.liveTracksProjected = true
    }
    this.liveFramePosted = true
    try {
      this.post({
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'EVENT',
        seq: ++this.outboundSeq,
        grant_id: this.grant.grant_id,
        identity: this.grant.identity,
        payload: {
          stream: SIDECAR_LIVE_STREAM,
          stream_seq: ++this.liveStreamSeq,
          data: seeded as unknown as Record<string, unknown>,
        },
      } satisfies SidecarEventMessage)
    } catch {
      this.liveFramePosted = false
    }
  }

  private async pumpLiveOnce(): Promise<void> {
    if (this.revoked || this.liveTimer === null || this.livePolling) return
    this.livePolling = true
    let response: Awaited<ReturnType<BridgeServerCall>>
    try {
      this.livePollId = ((this.livePollId % SIDECAR_LIVE_POLL_REQUEST_ID_MASK) + 1)
      response = await this.serverCall('live.subscribe', {
        grant_id: this.grant.grant_id,
        request_id: SIDECAR_LIVE_POLL_REQUEST_ID_BASE + this.livePollId,
        payload: { stream: SIDECAR_LIVE_STREAM },
      })
    } catch {
      response = { outcome: 'UNKNOWN', reason: 'TRANSPORT_FAILED' }
    }
    this.livePolling = false
    if (this.revoked || this.liveTimer === null) return
    if (response.revoked) {
      this.revoke(response.reason ?? 'SERVER_REVOKED')
      return
    }
    this.maybeForwardExpiry(response.expires_at_ms)
    if (response.outcome !== 'APPLIED') {
      this.postLiveStatus('STALE', true)
      return
    }
    if (!liveViewValid(response.payload)) {
      this.postLiveStatus('STALE', true)
      return
    }
    const view = response.payload
    const hostDroppedIncreased = this.liveHostDropped !== null && view.host_dropped_events > this.liveHostDropped
    const sourceGapIncreased = this.liveSourceGap !== null && view.source_gap > this.liveSourceGap
    if (
      view.loss_unknown
      || view.resync_required
      || view.tracks_truncated
      || hostDroppedIncreased
      || sourceGapIncreased
    ) {
      this.postLiveStatus('GAP')
      this.liveSourceGap = view.source_gap
      this.liveHostDropped = view.host_dropped_events
      if (view.freshness !== 'fresh' || view.state !== 'continuous') {
        return
      }
    }
    if (view.freshness === 'stale') {
      this.postLiveSeedOnce(view)
      this.postLiveStatus('STALE')
      return
    }
    if (view.freshness === 'none') {
      if (this.liveFramePosted) {
        this.postLiveStatus('STALE')
        return
      }
      this.postLiveSeedOnce(view)
      this.liveSourceGap = view.source_gap
      this.liveHostDropped = view.host_dropped_events
      return
    }
    if (view.state !== 'continuous') {
      this.postLiveStatus('GAP')
      return
    }
    this.liveSourceGap = view.source_gap
    this.liveHostDropped = view.host_dropped_events
    const data = liveDataOf(view, this.liveTracksProjected)
    if (data === null) {
      this.postLiveSeedOnce(view)
      return
    }
    if (data.tracks !== undefined && data.tracks.length > 0) {
      this.liveTracksProjected = true
    }
    this.liveFramePosted = true
    try {
      this.post({
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'EVENT',
        seq: ++this.outboundSeq,
        grant_id: this.grant.grant_id,
        identity: this.grant.identity,
        payload: {
          stream: SIDECAR_LIVE_STREAM,
          stream_seq: ++this.liveStreamSeq,
          data: data as unknown as Record<string, unknown>,
        },
      } satisfies SidecarEventMessage)
    } catch {
      this.postLiveStatus('GAP')
    }
  }

  async confirmDangerousOp(
    op: SidecarOp,
    requestConfirm: (grantId: string, confirmOp: SidecarOp) => Promise<SidecarConfirmGrant>,
  ): Promise<boolean> {
    if (this.revoked) return false
    let confirmation: SidecarConfirmGrant
    try {
      confirmation = await requestConfirm(this.grant.grant_id, op)
    } catch {
      if (this.pendingDangerous?.op === op) {
        this.clearPendingDangerous('UNKNOWN', 'CONFIRM_TIMEOUT')
      }
      return false
    }
    if (!isBoundedString(confirmation.token, 256) || !isEpochMs(confirmation.expiresAtMs)) {
      if (this.pendingDangerous?.op === op) {
        this.clearPendingDangerous('UNKNOWN', 'CONFIRM_TIMEOUT')
      }
      return false
    }
    this.armedConfirm = {
      op,
      token: confirmation.token,
      expiresAtMs: confirmation.expiresAtMs,
      grantId: this.grant.grant_id,
    }
    try {
      this.post({
        protocol: SIDECAR_BRIDGE_PROTOCOL,
        type: 'EVENT',
        seq: ++this.outboundSeq,
        grant_id: this.grant.grant_id,
        identity: this.grant.identity,
        payload: {
          stream: 'dangerous.confirm',
          stream_seq: this.outboundSeq,
          data: { op },
        },
      } satisfies SidecarEventMessage)
    } catch {
      this.armedConfirm = null
      return false
    }
    const stash = this.pendingDangerous
    if (stash !== null && stash.op === op && !this.revoked) {
      await this.resendPendingDangerous(stash)
    }
    return true
  }

  /**
   * Settles (and forgets) a stashed dangerous request with a synthetic UNKNOWN
   * result carrying the app's original request id.
   */
  private clearPendingDangerous(outcome: SidecarOutcome, reason: string): void {
    const stash = this.pendingDangerous
    if (stash === null) return
    this.pendingDangerous = null
    clearTimeout(stash.timer)
    this.deliverAppResult(stash.op, stash.seq, stash.iframeRequestId, outcome, { reason })
  }

  private stashDangerousConfirm(request: SidecarRequestMessage, seq: number): void {
    // Only one admin confirm window can be open at a time; a newer dangerous
    // request supersedes the previous stash and gives its app request a
    // definitive (timed-out) settlement instead of leaving it dangling.
    this.clearPendingDangerous('UNKNOWN', 'CONFIRM_TIMEOUT')
    const expectedGeneration = request.payload.expected_config_generation
    const timer = setTimeout(() => {
      this.clearPendingDangerous('UNKNOWN', 'CONFIRM_TIMEOUT')
    }, SIDECAR_DANGEROUS_CONFIRM_TTL_MS)
    this.pendingDangerous = {
      op: request.op,
      payload: request.payload,
      expectedConfigGeneration: isUint32(expectedGeneration)
        ? (expectedGeneration as number)
        : undefined,
      iframeRequestId: request.request_id,
      seq,
      timer,
    }
  }

  private nextResendRequestId(): number {
    this.resendRequestId = (this.resendRequestId - 1 + 0x100000000) % 0x100000000
    return this.resendRequestId
  }

  /**
   * Re-sends a stashed dangerous request on a fresh broker-owned request id
   * with the armed confirm token (same takeConfirmToken path as a manual
   * retry), then settles the app's original request id with the device answer.
   */
  private async resendPendingDangerous(stash: PendingDangerousRequest): Promise<void> {
    this.pendingDangerous = null
    clearTimeout(stash.timer)
    const confirmToken = this.takeConfirmToken(stash.op)
    const requestId = this.nextResendRequestId()
    const seq = ++this.outboundSeq
    const timer = setTimeout(() => {
      const pending = this.pending.get(requestId)
      if (!pending) return
      this.pending.delete(requestId)
      this.consecutiveTimeouts += 1
      if (this.consecutiveTimeouts >= SIDECAR_MAX_CONSECUTIVE_TIMEOUTS) {
        this.revoke('ACK_TIMEOUT_LIMIT')
        return
      }
      this.deliverAppResult(stash.op, seq, stash.iframeRequestId, 'UNKNOWN', { reason: 'ACK_TIMEOUT' })
    }, this.timeoutMs)
    this.pending.set(requestId, {
      op: stash.op,
      seq,
      timer,
      resolve: (result) => {
        result.request_id = stash.iframeRequestId
        this.onResult?.(result)
        try {
          this.post(result)
        } catch {
          this.dropPort()
        }
      },
    })
    try {
      const response = await this.serverCall(stash.op, {
        grant_id: this.grant.grant_id,
        request_id: requestId,
        payload: stash.payload,
        expected_config_generation: stash.expectedConfigGeneration,
        ...(confirmToken !== undefined ? { confirm_token: confirmToken } : {}),
      })
      const pending = this.pending.get(requestId)
      if (!pending) return
      this.pending.delete(requestId)
      clearTimeout(pending.timer)
      this.consecutiveTimeouts = 0
      if (response.revoked) {
        this.revoke(response.reason ?? 'SERVER_REVOKED')
        return
      }
      this.maybeForwardExpiry(response.expires_at_ms)
      this.deliverAppResult(stash.op, seq, stash.iframeRequestId, response.outcome, {
        reason: response.reason,
        freshness: response.freshness,
        config_generation: response.config_generation,
        state_revision: response.state_revision,
        age_ms: response.age_ms,
        payload: response.payload,
      })
    } catch {
      const pending = this.pending.get(requestId)
      if (!pending) return
      this.pending.delete(requestId)
      clearTimeout(pending.timer)
      this.deliverAppResult(stash.op, seq, stash.iframeRequestId, 'UNKNOWN', { reason: 'TRANSPORT_FAILED' })
    }
  }

  private deliverAppResult(
    op: SidecarOp,
    seq: number,
    requestId: number,
    outcome: SidecarOutcome,
    detail?: {
      reason?: string
      freshness?: string
      config_generation?: number
      state_revision?: number
      age_ms?: number
      payload?: unknown
    },
  ): void {
    const result = this.makeResult(op, seq, outcome, detail)
    result.request_id = requestId
    this.onResult?.(result)
    try {
      this.post(result)
    } catch {
      this.dropPort()
    }
  }

  private takeConfirmToken(op: SidecarOp): string | undefined {
    const armed = this.armedConfirm
    if (armed === null) return undefined
    if (armed.grantId !== this.grant.grant_id || Date.now() > armed.expiresAtMs) {
      this.armedConfirm = null
      return undefined
    }
    if (armed.op !== op) return undefined
    this.armedConfirm = null
    return armed.token
  }

  private post(message: SidecarHostToAppMessage): void {
    if (this.channel === null) throw new Error('BRIDGE_PORT_CLOSED')
    const encoded = JSON.stringify(message)
    if (encoded.length > SIDECAR_MSG_MAX_BYTES) throw new Error('MESSAGE_SIZE_EXCEEDED')
    this.channel.port1.postMessage(message)
  }

  private makeResult(
    op: SidecarOp,
    seq: number,
    outcome: SidecarOutcome,
    detail?: {
      reason?: string
      freshness?: string
      config_generation?: number
      state_revision?: number
      age_ms?: number
      payload?: unknown
    },
  ): SidecarResultMessage {
    let payload: Record<string, unknown> | undefined
    if (isPlainObject(detail?.payload)) {
      payload = detail.payload
    } else {
      payload = {}
      if (detail?.reason !== undefined) payload.reason = detail.reason
      if (detail?.freshness !== undefined) payload.freshness = detail.freshness
      if (detail?.config_generation !== undefined) payload.config_generation = detail.config_generation
      if (detail?.state_revision !== undefined) payload.state_revision = detail.state_revision
      if (detail?.age_ms !== undefined) payload.age_ms = detail.age_ms
      if (Object.keys(payload).length === 0) payload = undefined
    }
    return {
      protocol: SIDECAR_BRIDGE_PROTOCOL,
      type: 'RESULT',
      seq,
      grant_id: this.grant.grant_id,
      identity: this.grant.identity,
      request_id: 0,
      op,
      outcome,
      ...(payload !== undefined ? { payload } : {}),
    }
  }

  private async handlePortMessage(event: MessageEvent): Promise<void> {
    if (this.revoked) return
    if (event.origin !== this.appOrigin && event.origin !== '') {
      this.revoke('ORIGIN_MISMATCH')
      return
    }
    const validation = validateBridgeEnvelope(event.data)
    if (!validation.ok || !validation.message) {
      this.revoke(validation.reason ?? 'ENVELOPE_INVALID')
      return
    }
    const message = validation.message as SidecarAppToHostMessage
    if (message.grant_id !== this.grant.grant_id) {
      this.revoke('GRANT_MISMATCH')
      return
    }
    if (JSON.stringify(message.identity) !== JSON.stringify(this.grant.identity)) {
      this.revoke('IDENTITY_MISMATCH')
      return
    }
    if (message.seq <= this.inboundSeq) {
      this.revoke('SEQ_REGRESSED')
      return
    }
    this.inboundSeq = message.seq
    if (message.type === 'REVOKE') {
      this.revoke('APP_REQUESTED_REVOKE')
      return
    }
    const request = message as SidecarRequestMessage
    if (this.pending.size >= SIDECAR_MAX_PENDING_REQUESTS) {
      this.postResult(request, 'REJECTED', { reason: 'TOO_MANY_PENDING' })
      return
    }
    if (this.seenRequestIds.has(request.request_id)) {
      this.postResult(request, 'REJECTED', { reason: 'REPLAY' })
      return
    }
    if (!(this.grant.capabilities as readonly string[]).includes(request.op)) {
      this.postResult(request, 'REJECTED', { reason: 'OP_NOT_IN_GRANT' })
      return
    }
    this.seenRequestIds.add(request.request_id)
    if (this.seenRequestIds.size > 256) {
      const oldest = this.seenRequestIds.values().next().value
      if (oldest !== undefined) this.seenRequestIds.delete(oldest)
    }
    const expectedGeneration = request.payload.expected_config_generation
    const seq = ++this.outboundSeq
    const timer = setTimeout(() => {
      const pending = this.pending.get(request.request_id)
      if (!pending) return
      this.pending.delete(request.request_id)
      this.consecutiveTimeouts += 1
      if (this.consecutiveTimeouts >= SIDECAR_MAX_CONSECUTIVE_TIMEOUTS) {
        this.revoke('ACK_TIMEOUT_LIMIT')
        return
      }
      this.postResult(request, 'UNKNOWN', { reason: 'ACK_TIMEOUT' })
    }, this.timeoutMs)
    this.pending.set(request.request_id, {
      op: request.op,
      seq,
      timer,
      resolve: (result) => {
        this.onResult?.(result)
        try {
          this.post(result)
        } catch {
          this.dropPort()
        }
      },
    })
    try {
      const confirmToken = this.takeConfirmToken(request.op)
      const response = await this.serverCall(request.op, {
        grant_id: this.grant.grant_id,
        request_id: request.request_id,
        payload: request.payload,
        expected_config_generation: isUint32(expectedGeneration)
          ? (expectedGeneration as number)
          : undefined,
        ...(confirmToken !== undefined ? { confirm_token: confirmToken } : {}),
      })
      const pending = this.pending.get(request.request_id)
      if (!pending) return
      this.pending.delete(request.request_id)
      clearTimeout(pending.timer)
      this.consecutiveTimeouts = 0
      if (response.revoked) {
        this.revoke(response.reason ?? 'SERVER_REVOKED')
        return
      }
      this.maybeForwardExpiry(response.expires_at_ms)
      if (request.op === 'live.subscribe' && response.outcome === 'APPLIED') {
        this.startLivePump()
      }
      if (request.op === 'live.unsubscribe' && response.outcome === 'APPLIED') {
        this.stopLivePump()
      }
      if (
        response.outcome === 'REJECTED'
        && response.reason === 'ADMIN_CONFIRM_REQUIRED'
        && SIDECAR_DANGEROUS_OPS.has(request.op)
      ) {
        // Dangerous op without a confirm token: hold the request open for the
        // admin confirm flow instead of settling the app's pending RESULT.
        // The host shell still learns about the rejection (its confirm banner
        // trigger) through onResult, but the app keeps waiting for the final
        // RESULT that arrives after the automatic re-send.
        const rejected = this.makeResult(request.op, pending.seq, 'REJECTED', { reason: response.reason })
        rejected.request_id = request.request_id
        this.onResult?.(rejected)
        this.stashDangerousConfirm(request, pending.seq)
        return
      }
      this.postResult(request, response.outcome, {
        reason: response.reason,
        freshness: response.freshness,
        config_generation: response.config_generation,
        state_revision: response.state_revision,
        age_ms: response.age_ms,
        payload: response.payload,
        echoRequestId: request.request_id,
      })
    } catch {
      const pending = this.pending.get(request.request_id)
      if (!pending) return
      this.pending.delete(request.request_id)
      clearTimeout(pending.timer)
      this.postResult(request, 'UNKNOWN', { reason: 'TRANSPORT_FAILED' })
    }
  }

  private postResult(
    request: SidecarRequestMessage,
    outcome: SidecarOutcome,
    extra: {
      reason?: string
      freshness?: string
      config_generation?: number
      state_revision?: number
      age_ms?: number
      payload?: unknown
      echoRequestId?: number
    },
  ): void {
    const pending = this.pending.get(request.request_id)
    const seq = pending ? pending.seq : ++this.outboundSeq
    const result = this.makeResult(request.op, seq, outcome, extra)
    if (extra.echoRequestId !== undefined) {
      result.request_id = extra.echoRequestId
    } else {
      result.request_id = request.request_id
    }
    this.onResult?.(result)
    try {
      this.post(result)
    } catch {
      this.dropPort()
    }
  }

  private dropPort(): void {
    this.channel = null
  }
}

export function isCrossSite(originA: string, originB: string): boolean {
  if (!originA || !originB) return false
  let a: URL
  let b: URL
  try {
    a = new URL(originA)
    b = new URL(originB)
  } catch {
    return false
  }
  if (a.protocol !== 'http:' && a.protocol !== 'https:') return false
  if (b.protocol !== 'http:' && b.protocol !== 'https:') return false
  if (a.origin === b.origin) return false
  const siteOf = (url: URL): string => {
    const host = url.hostname.toLowerCase()
    if (
      /^(\d{1,3}\.){3}\d{1,3}$/.test(host)
      || host.startsWith('[')
      || host === 'localhost'
    ) {
      return host
    }
    const labels = host.split('.')
    if (labels.length <= 2) return labels.join('.')
    return labels.slice(-2).join('.')
  }
  return siteOf(a) !== siteOf(b)
}
