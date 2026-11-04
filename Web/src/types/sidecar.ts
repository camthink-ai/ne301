export const SIDECAR_BRIDGE_PROTOCOL = 'ne301.sidecar.bridge.v1'

export const SIDECAR_BRIDGE_CHANNEL = 'ne301.sidecar.bridge.v1'

export const SIDECAR_MSG_MAX_BYTES = 16384

export const SIDECAR_MAX_PENDING_REQUESTS = 4

export const SIDECAR_REQUEST_TIMEOUT_MS = 10000

export const SIDECAR_MAX_CONSECUTIVE_TIMEOUTS = 2

export type SidecarOutcome = 'APPLIED' | 'CONFLICT' | 'UNKNOWN' | 'REJECTED' | 'UNAVAILABLE'

export type SidecarMessageType = 'HELLO' | 'GRANT' | 'REQUEST' | 'RESULT' | 'EVENT' | 'REVOKE'

export const SIDECAR_OPS = [
  'snapshot.read',
  'config.read',
  'config.apply',
  'counters.reset',
  'report-policy.read',
  'report-policy.apply',
  'video.open',
  'video.close',
  'enable.read',
  'enable.apply',
  'live.subscribe',
  'live.unsubscribe',
] as const

export type SidecarOp = (typeof SIDECAR_OPS)[number]

export const SIDECAR_UNAVAILABLE_OPS: ReadonlySet<SidecarOp> = new Set([
  'report-policy.read',
  'report-policy.apply',
])

export const SIDECAR_DANGEROUS_OPS: ReadonlySet<SidecarOp> = new Set([
  'counters.reset',
  'enable.apply',
])

export const SIDECAR_LIVE_STREAM = 'live'

export const SIDECAR_LIVE_POLL_REQUEST_ID_BASE = 0x80000000

export const SIDECAR_LIVE_POLL_REQUEST_ID_MASK = 0x07ffffff

export interface SidecarLivePoint {
  ts_ms: number
  x_q16: number
  y_q16: number
}

export interface SidecarLiveTrack {
  track_id: number
  point_count: number
  class_index: number
  state: number
  flags: number
  confidence_q16: number
  points: SidecarLivePoint[]
}

export interface SidecarLiveEventView {
  cursor: number
  event_seq: number
  track_id: number
  event_ts_ms: number
  direction: number
  flags: number
  x_q8: number
  y_q8: number
}

export interface SidecarLiveView {
  session_generation: number
  view_epoch: number
  freshness: string
  age_ms: number
  state: string
  through_seq: number
  source_total: number
  source_gap: number
  host_dropped_events: number
  packet_count: number
  last_packet_seq: number
  loss_unknown: boolean
  resync_required: boolean
  tracks_truncated: boolean
  next_cursor: number
  target_class: string
  server_now_ms?: number
  tracks?: SidecarLiveTrack[]
  events?: SidecarLiveEventView[]
}

export interface SidecarLiveEventData {
  server_now_ms: number
  events?: Array<{
    sequence: number
    timestamp_ms: number
    track_id: number
    direction: 'in' | 'out'
    target_class: string
  }>
  tracks?: Array<{ track_id: number; points: Array<{ x: number; y: number }> }>
}

export interface SidecarIdentity {
  publisher_id: string
  app_id: string
  publisher_key_sha256: string
  native_tbs_sha256: string
  ui_tbs_sha256: string
  build_id: string
  host_boot_generation: number
  native_session_generation: number
  ui_generation: number
  sidecar_service_generation: number
}

export interface SidecarUiProjection {
  state: 'active' | 'unavailable'
  reason?: string
  publisher_id?: string
  app_id?: string
  publisher_key_sha256?: string
  native_tbs_sha256?: string
  ui_tbs_sha256?: string
  version_major?: number
  version_minor?: number
  version_patch?: number
  host_ui_abi?: number
  sidecar_service_generation?: string
  host_boot_generation?: number
  native_session_generation?: number
  ui_generation?: number
  ui_base_path?: string
  entry_path?: string
  app_site_origin?: string | null
  file_count?: number
}

export interface SidecarGrant {
  grant_id: string
  expires_at_ms: number
  issued_at_ms: number
  capabilities: SidecarOp[]
  identity: SidecarIdentity
}

export interface SidecarHelloMessage {
  protocol: typeof SIDECAR_BRIDGE_PROTOCOL
  type: 'HELLO'
  seq: number
  grant_id: string
  identity: SidecarIdentity
  channel: typeof SIDECAR_BRIDGE_CHANNEL
}

export interface SidecarGrantMessage {
  protocol: typeof SIDECAR_BRIDGE_PROTOCOL
  type: 'GRANT'
  seq: number
  grant_id: string
  identity: SidecarIdentity
  expires_at_ms: number
  ops: SidecarOp[]
}

export interface SidecarRequestMessage {
  protocol: typeof SIDECAR_BRIDGE_PROTOCOL
  type: 'REQUEST'
  seq: number
  grant_id: string
  identity: SidecarIdentity
  request_id: number
  op: SidecarOp
  payload: Record<string, unknown>
}

export interface SidecarResultMessage {
  protocol: typeof SIDECAR_BRIDGE_PROTOCOL
  type: 'RESULT'
  seq: number
  grant_id: string
  identity: SidecarIdentity
  request_id: number
  op: SidecarOp
  outcome: SidecarOutcome
  payload?: Record<string, unknown>
}

export type SidecarStreamStatus = 'GAP' | 'STALE' | 'REVOKE'

export interface SidecarEventMessage {
  protocol: typeof SIDECAR_BRIDGE_PROTOCOL
  type: 'EVENT'
  seq: number
  grant_id: string
  identity: SidecarIdentity
  payload: {
    stream: string
    stream_seq: number
    status?: SidecarStreamStatus
    data?: Record<string, unknown>
  }
}

export interface SidecarRevokeMessage {
  protocol: typeof SIDECAR_BRIDGE_PROTOCOL
  type: 'REVOKE'
  seq: number
  grant_id: string
  identity: SidecarIdentity
  reason: string
}

export type SidecarHostToAppMessage =
  | SidecarGrantMessage
  | SidecarResultMessage
  | SidecarEventMessage
  | SidecarRevokeMessage

export type SidecarAppToHostMessage =
  | SidecarRequestMessage
  | SidecarRevokeMessage

export type SidecarBridgeMessage = SidecarHostToAppMessage | SidecarAppToHostMessage

export interface SidecarBridgeCallResponse {
  protocol?: string
  request_id?: number
  op?: string
  revoked?: boolean
  result?: {
    outcome?: string
    reason?: string
    freshness?: string
    config_generation?: number
    state_revision?: number
    age_ms?: number
    payload?: unknown
  }
}

export type SidecarViewState =
  | { kind: 'idle' }
  | { kind: 'active'; projection: SidecarUiProjection }
  | { kind: 'unavailable'; reason: string }
