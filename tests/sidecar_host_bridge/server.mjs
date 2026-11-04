import { createHash } from 'node:crypto'
import { createServer } from 'node:http'
import { readFileSync, existsSync, mkdirSync, rmSync, writeFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const fixtureRoot = path.dirname(fileURLToPath(import.meta.url))
const sha256 = (value) => createHash('sha256').update(value).digest('hex')
const randomId = () => createHash('sha256').update(`${Date.now()}:${Math.random()}:${process.pid}`).digest('hex')
const OPS = [
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
  'live.unsubscribe'
]
const UNAVAILABLE_REASONS = {
  'report-policy.read': 'CAPABILITY_NOT_REVIEWED',
  'report-policy.apply': 'CAPABILITY_NOT_REVIEWED'
}

const BRIDGE_IDENTITY = {
  publisher_id: 'test-publisher',
  app_id: 'line-crossing',
  publisher_key_sha256: '5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3',
  native_tbs_sha256: 'd793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e',
  ui_tbs_sha256: 'fc8bd17703aec3a9b6f0e2875e99731fcbe59be4e68efee6083c6e5553ecb92e',
  build_id: '1.0.0',
  host_boot_generation: 7,
  native_session_generation: 3,
  ui_generation: 11,
  sidecar_service_generation: 42
}

const signedManifest = extractSignedManifest()

function extractSignedManifest() {
  const bundle = readFileSync(path.join(fixtureRoot, 'fixtures/line-crossing.neui'))
  if (bundle.subarray(0, 8).toString('hex') !== '4e45554901000000') {
    throw new Error('fixture bundle is not NEUI v1')
  }
  const manifestLen = bundle.readUInt32LE(8)
  const payloadLen = bundle.readUInt32LE(12)
  const manifest = bundle.subarray(16, 16 + manifestLen)
  const payload = bundle.subarray(16 + manifestLen, 16 + manifestLen + payloadLen)
  const fileCount = manifest.readUInt16LE(118)
  const files = new Map()
  let cursor = 128
  let offset = 0
  for (let index = 0; index < fileCount; index += 1) {
    const pathLen = manifest.readUInt16LE(cursor)
    const fileLen = manifest.readUInt32LE(cursor + 2)
    const digest = manifest.subarray(cursor + 6, cursor + 38).toString('hex')
    const name = manifest.subarray(cursor + 38, cursor + 38 + pathLen).toString('ascii')
    const bytes = payload.subarray(offset, offset + fileLen)
    if (sha256(bytes) !== digest) {
      throw new Error(`signed asset ${name} digest mismatch`)
    }
    files.set(name, { bytes: Buffer.from(bytes), type: mimeOf(name), len: fileLen, digest })
    cursor += 38 + pathLen
    offset += fileLen
  }
  if (cursor !== manifestLen || offset !== payloadLen) {
    throw new Error('signed manifest records do not cover the payload exactly')
  }
  return files
}

function mimeOf(file) {
  return ({
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.json': 'application/json; charset=utf-8'
  }[path.extname(file)] || 'application/octet-stream')
}

const candidate = {
  schema: 1,
  counter_name: 'Counter Test',
  target_class_name: 'person',
  line: { x1: 100, y1: 100, x2: 900, y2: 500, outside_x: 500, outside_y: 900 },
  conf_threshold_permille: 500,
  max_dist_permille: 300,
  track_history_k: 16,
  max_miss: 32,
  k_confirm: 3,
  tracks_report_enable: true,
  heat_grid_enable: false,
  window_minutes: 60
}

const status = {
  state: 'running',
  target_class: 'person',
  model: {
    name: 'yolov8n-pose',
    version: '1.0.0',
    postprocess_type: 'YOLO_POSE',
    generation: 4,
    classes: [{ id: 0, name: 'person' }, { id: 1, name: 'bicycle' }]
  }
}

const stats = { window: { in: 3, out: 1 }, total: { in: 42, out: 12 } }

const snapshotPayload = (sessionGeneration, configGeneration) => ({
  session_generation: sessionGeneration,
  config_generation: configGeneration,
  config: candidate,
  status,
  stats
})

const now = () => Date.now()

export function setBehavior(patch) {
  Object.assign(activeState.behavior, patch)
}

export function setLiveBehavior(patch) {
  Object.assign(activeState.live, patch)
}

export function pushLiveCrossing({ direction = 'in', trackId = 7 } = {}) {
  const live = activeState.live
  live.events.push({
    cursor: live.nextCursor,
    event_seq: live.nextSeq,
    track_id: trackId,
    event_ts_ms: Date.now(),
    direction: direction === 'out' ? 2 : 1,
    flags: 0,
    x_q8: 128,
    y_q8: 64
  })
  live.nextCursor += 1
  live.nextSeq += 1
  live.sourceTotal += 1
  live.packetCount += 1
  live.lastPacketSeq += 1
  live.lastPacketAt = Date.now()
}

export function setLiveTracks(tracks, truncated = false) {
  const live = activeState.live
  live.tracks = tracks
  live.tracksTruncated = truncated
  live.packetCount += 1
  live.lastPacketSeq += 1
  live.lastPacketAt = Date.now()
}

export function liveState() {
  return activeState.live
}

export function submitSnapshot() {
  activeState.snapshot = { json: snapshotPayload(3, 6), arrival: now() }
}

export function currentGeneration() {
  return activeState.identity.sidecar_service_generation
}

export function grantCount() {
  return [...activeState.grants.values()].filter((g) => g.active && !g.revoked).length
}

export function bridgeCalls() {
  return activeState.calls
}

export function issuedConfirmTokens() {
  return [...activeState.confirms.keys()]
}

let activeState = null

function createServerState() {
  return {
    identity: { ...BRIDGE_IDENTITY },
    grants: new Map(),
    confirms: new Map(),
    snapshot: null,
    configGeneration: 6,
    sessionGeneration: 3,
    enableRunning: true,
    calls: [],
    seenRequests: new Set(),
    appSiteOrigin: null,
    live: {
      available: true,
      freshnessOverride: null,
      stateOverride: null,
      events: [],
      nextCursor: 1,
      nextSeq: 1,
      tracks: [],
      tracksTruncated: false,
      sourceTotal: 0,
      sourceGap: 0,
      hostDropped: 0,
      packetCount: 0,
      lastPacketSeq: 0,
      lastPacketAt: 0,
      lossUnknown: false,
      resyncRequired: false,
      viewEpoch: 5,
      targetClass: 'person'
    },
    behavior: {
      staleReceipt: false,
      enableStateConflict: false,
      enableStorageUnknown: false,
      enableTimeout: false,
      videoLiveUrl: null,
      videoServiceRunning: true,
      videoSourceActive: true,
      requireConfirmToken: true
    }
  }
}

export function createHostServer({ distRoot, getAppSiteOrigin }) {
  const state = createServerState()
  activeState = state
  state.resolveAppSiteOrigin = getAppSiteOrigin
  const activeDescriptor = () => {
    const resolved = state.resolveAppSiteOrigin ? state.resolveAppSiteOrigin() : null
    return {
      state: 'active',
      publisher_id: state.identity.publisher_id,
      app_id: state.identity.app_id,
      publisher_key_sha256: state.identity.publisher_key_sha256,
      native_tbs_sha256: state.identity.native_tbs_sha256,
      ui_tbs_sha256: state.identity.ui_tbs_sha256,
      version_major: 1,
      version_minor: 0,
      version_patch: 0,
      host_ui_abi: 1,
      sidecar_service_generation: String(state.identity.sidecar_service_generation),
      host_boot_generation: state.identity.host_boot_generation,
      native_session_generation: state.identity.native_session_generation,
      ui_generation: state.identity.ui_generation,
      ui_base_path: `/appsite/g${state.identity.sidecar_service_generation}/`,
      entry_path: 'index.html',
      app_site_origin: resolved,
      file_count: signedManifest.size
    }
  }
  const mime = (file) => ({
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8'
  }[path.extname(file)] || 'application/octet-stream')

  const json = (response, code, payload, extraHeaders = {}) => {
    response.writeHead(code, {
      'content-type': 'application/json',
      'cache-control': 'no-store',
      ...extraHeaders
    })
    response.end(JSON.stringify(payload))
  }
  const ok = (response, data, message = 'ok') => json(response, 200, { success: true, message, data })
  const fail = (response, code) => json(response, 200, { success: false, error_code: code })

  const readBody = (request) =>
    new Promise((resolve, reject) => {
      let data = ''
      request.on('data', (chunk) => (data += chunk))
      request.on('end', () => resolve(data))
      request.on('error', reject)
    })

  const grantMatches = (grant) =>
    grant.publisher_id === state.identity.publisher_id &&
    grant.app_id === state.identity.app_id &&
    grant.publisher_key_sha256 === state.identity.publisher_key_sha256 &&
    grant.native_tbs_sha256 === state.identity.native_tbs_sha256 &&
    grant.ui_tbs_sha256 === state.identity.ui_tbs_sha256 &&
    grant.sidecar_service_generation === state.identity.sidecar_service_generation &&
    grant.native_session_generation === state.identity.native_session_generation

  const reply = (response, envelope, payload, revoked = false) => {
    const root = {
      protocol: 'ne301.sidecar.bridge.v1',
      request_id: envelope.request_id,
      op: envelope.op,
      revoked,
      result: payload
    }
    ok(response, root)
  }

  const dispatch = async (request, response) => {
    const body = await readBody(request)
    const envelope = body ? JSON.parse(body) : {}
    const grant = state.grants.get(envelope.grant_id)
    if (!grant || grant.revoked || now() > grant.expiresAt) {
      return fail(response, 'INVALID_REQUEST')
    }
    if (!grantMatches(grant)) {
      grant.revoked = true
      return reply(response, envelope, { outcome: 'REJECTED', reason: 'IDENTITY_OR_GENERATION_CHANGED' }, true)
    }
    state.calls.push({
      op: envelope.op,
      request_id: envelope.request_id,
      payload: envelope.payload,
      expected_config_generation: envelope.expected_config_generation,
      confirm_token: typeof envelope.confirm_token === 'string'
    })
    const opKey = `${envelope.grant_id}:${envelope.op}:${envelope.request_id}`
    if (state.seenRequests.has(opKey)) {
      return reply(response, envelope, { outcome: 'REJECTED', reason: 'REPLAY' })
    }
    state.seenRequests.add(opKey)
    if (!grant.capabilities.includes(envelope.op)) {
      return reply(response, envelope, { outcome: 'REJECTED', reason: 'OP_NOT_IN_GRANT' })
    }
    if (UNAVAILABLE_REASONS[envelope.op]) {
      return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: UNAVAILABLE_REASONS[envelope.op] })
    }
    if (envelope.op === 'video.open' || envelope.op === 'video.close') {
      if (!envelope.payload || typeof envelope.payload !== 'object') {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'PAYLOAD_REQUIRED' })
      }
      const payload = envelope.payload
      if (typeof payload.session_generation !== 'number') {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'SESSION_GENERATION_REQUIRED' })
      }
      if (payload.session_generation !== state.sessionGeneration) {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'STALE_SESSION_GENERATION' })
      }
      if (envelope.op === 'video.open') {
        if (state.behavior.videoServiceRunning === false) {
          return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: 'VIDEO_SERVICE_UNAVAILABLE' })
        }
        if (state.behavior.videoSourceActive === false) {
          return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: 'VIDEO_SOURCE_UNAVAILABLE' })
        }
        if (!state.behavior.videoLiveUrl) {
          return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: 'VIDEO_ENDPOINT_UNAVAILABLE' })
        }
        grant.videoOpen = true
        return reply(response, envelope, {
          outcome: 'APPLIED',
          payload: {
            url: state.behavior.videoLiveUrl,
            session_generation: state.sessionGeneration,
            expires_at_ms: grant.expiresAt,
            grant_id: grant.grantId
          }
        })
      }
      if (typeof payload.grant_id !== 'string' || payload.grant_id !== grant.grantId) {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'GRANT_MISMATCH' })
      }
      if (grant.videoOpen !== true) {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'VIDEO_SESSION_NOT_OPEN' })
      }
      grant.videoOpen = false
      return reply(response, envelope, {
        outcome: 'APPLIED',
        payload: { session_generation: state.sessionGeneration }
      })
    }
    if (envelope.op === 'snapshot.read') {
      if (!state.snapshot) {
        return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: 'SNAPSHOT_UNAVAILABLE' })
      }
      if (state.identity.sidecar_service_generation !== grant.sidecar_service_generation) {
        grant.revoked = true
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'IDENTITY_OR_GENERATION_CHANGED' }, true)
      }
      const payload = {
        ...state.snapshot.json,
        session_generation: state.sessionGeneration,
        config_generation: state.configGeneration
      }
      return reply(response, envelope, { outcome: 'APPLIED', payload })
    }
    if (envelope.op === 'config.read') {
      return reply(response, envelope, {
        outcome: 'APPLIED',
        payload: { config_generation: state.configGeneration }
      })
    }
    if (envelope.op === 'config.apply' || envelope.op === 'counters.reset') {
      const payload = envelope.payload || {}
      if (typeof payload.session_generation !== 'number' || payload.session_generation !== state.sessionGeneration) {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'STALE_SESSION_GENERATION' })
      }
      if (envelope.op === 'config.apply') {
        if (typeof envelope.expected_config_generation !== 'number' || envelope.expected_config_generation <= 0) {
          return reply(response, envelope, { outcome: 'REJECTED', reason: 'CONFIG_GENERATION_REQUIRED' })
        }
        if (!payload.candidate || typeof payload.candidate !== 'object') {
          return reply(response, envelope, { outcome: 'REJECTED', reason: 'CANDIDATE_REQUIRED' })
        }
        if (state.behavior.staleReceipt || envelope.expected_config_generation !== state.configGeneration) {
          return reply(response, envelope, { outcome: 'CONFLICT', reason: 'STALE_CONFIG_GENERATION' })
        }
        state.configGeneration += 1
        return reply(response, envelope, {
          outcome: 'APPLIED',
          payload: {
            session_generation: state.sessionGeneration,
            config_generation: state.configGeneration,
            snapshot: snapshotPayload(state.sessionGeneration, state.configGeneration)
          }
        })
      }
      if (state.behavior.requireConfirmToken) {
        const presented = typeof envelope.confirm_token === 'string' ? envelope.confirm_token : null
        const token = presented ? state.confirms.get(presented) : undefined
        if (!token || token.used || token.grantId !== grant.grantId || now() > token.expiresAt) {
          return reply(response, envelope, { outcome: 'REJECTED', reason: 'ADMIN_CONFIRM_REQUIRED' })
        }
        token.used = true
        return reply(response, envelope, {
          outcome: 'APPLIED',
          payload: { session_generation: state.sessionGeneration }
        })
      }
      return reply(response, envelope, {
        outcome: 'APPLIED',
        payload: { session_generation: state.sessionGeneration }
      })
    }
    if (envelope.op === 'enable.read') {
      if (!state.snapshot || !state.snapshot.json?.status) {
        return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: 'ENABLE_STATE_UNKNOWN' })
      }
      const enableState = state.snapshot.json.status.state
      if (enableState !== 'running' && enableState !== 'stopped') {
        return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: 'ENABLE_STATE_UNKNOWN' })
      }
      return reply(response, envelope, {
        outcome: 'APPLIED',
        payload: { session_generation: state.sessionGeneration, value: enableState === 'running' }
      })
    }
    if (envelope.op === 'enable.apply') {
      const payload = envelope.payload || {}
      if (typeof payload.session_generation !== 'number' || payload.session_generation !== state.sessionGeneration) {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'STALE_SESSION_GENERATION' })
      }
      if (typeof payload.value !== 'boolean') {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'ENABLE_VALUE_INVALID' })
      }
      const presented = typeof envelope.confirm_token === 'string' ? envelope.confirm_token : null
      const token = presented ? state.confirms.get(presented) : undefined
      if (!token || token.used || token.grantId !== grant.grantId || now() > token.expiresAt) {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'ADMIN_CONFIRM_REQUIRED' })
      }
      token.used = true
      if (state.behavior.enableStorageUnknown) {
        return reply(response, envelope, { outcome: 'UNKNOWN', reason: 'STORAGE_UNKNOWN' })
      }
      if (state.behavior.enableStateConflict) {
        return reply(response, envelope, { outcome: 'CONFLICT', reason: 'STATE_CONFLICT' })
      }
      if (state.behavior.enableTimeout) {
        return reply(response, envelope, { outcome: 'UNKNOWN', reason: 'ACK_TIMEOUT' })
      }
      state.enableRunning = payload.value
      if (state.snapshot) {
        state.snapshot.json = {
          ...state.snapshot.json,
          status: { ...state.snapshot.json.status, state: payload.value ? 'running' : 'stopped' }
        }
      }
      return reply(response, envelope, {
        outcome: 'APPLIED',
        payload: { session_generation: state.sessionGeneration, value: payload.value }
      })
    }
    if (envelope.op === 'live.subscribe' || envelope.op === 'live.unsubscribe') {
      if (!envelope.payload || envelope.payload.stream !== 'live') {
        return reply(response, envelope, { outcome: 'REJECTED', reason: 'STREAM_UNKNOWN' })
      }
    }
    if (envelope.op === 'live.subscribe') {
      if (state.live.available === false) {
        return reply(response, envelope, { outcome: 'UNAVAILABLE', reason: 'LIVE_UNAVAILABLE' })
      }
      if (grant.liveEpoch !== state.live.viewEpoch) {
        grant.liveEpoch = state.live.viewEpoch
        grant.liveCursor = state.live.nextCursor - 1
      }
      const pending = state.live.events.filter((event) => event.cursor > (grant.liveCursor ?? 0))
      const events = pending.slice(0, 32)
      if (events.length > 0) {
        grant.liveCursor = events[events.length - 1].cursor
      }
      const age = state.live.lastPacketAt === 0 ? 0 : now() - state.live.lastPacketAt
      const freshness = state.live.freshnessOverride
        ?? (state.live.lastPacketAt === 0 ? 'none' : age < 10000 ? 'fresh' : 'stale')
      const streamState = state.live.stateOverride ?? 'continuous'
      return reply(response, envelope, {
        outcome: 'APPLIED',
        payload: {
          session_generation: state.sessionGeneration,
          view_epoch: state.live.viewEpoch,
          freshness,
          age_ms: age,
          state: streamState,
          through_seq: state.live.nextSeq - 1,
          source_total: state.live.sourceTotal,
          source_gap: state.live.sourceGap,
          host_dropped_events: state.live.hostDropped,
          packet_count: state.live.packetCount,
          last_packet_seq: state.live.lastPacketSeq,
          loss_unknown: state.live.lossUnknown,
          resync_required: state.live.resyncRequired,
          tracks_truncated: state.live.tracksTruncated,
          next_cursor: state.live.nextCursor,
          target_class: state.live.targetClass,
          tracks: state.live.tracks.slice(0, 16),
          events
        }
      })
    }
    if (envelope.op === 'live.unsubscribe') {
      grant.liveEpoch = undefined
      grant.liveCursor = undefined
      return reply(response, envelope, { outcome: 'APPLIED', payload: { active: false } })
    }
    return reply(response, envelope, { outcome: 'REJECTED', reason: 'OP_UNKNOWN' })
  }

  return createServer(async (request, response) => {
    const url = new URL(request.url || '/', `http://${request.headers.host}`)
    const pathname = url.pathname
    if (pathname.startsWith('/appsite/')) {
      const match = /^\/appsite\/g(\d+)\/(.*)$/.exec(pathname)
      if (!match || Number(match[1]) !== state.identity.sidecar_service_generation) {
        response.writeHead(404, { 'cache-control': 'no-store' })
        response.end('rejected\n')
        return
      }
      const logical = decodeURIComponent(match[2])
      const asset = signedManifest.get(logical)
      if (!asset || logical.includes('..')) {
        response.writeHead(404, { 'cache-control': 'no-store' })
        response.end('rejected\n')
        return
      }
      const integrity = url.searchParams.get('tamper')
      const bytes = integrity
        ? Buffer.from(integrity === 'empty' ? '' : 'tampered-bytes')
        : asset.bytes
      if (sha256(bytes) !== asset.digest || bytes.length !== asset.len) {
        response.writeHead(404, { 'cache-control': 'no-store' })
        response.end('rejected\n')
        return
      }
      response.writeHead(200, {
        'content-type': asset.type,
        'cache-control': 'no-store',
        'x-content-type-options': 'nosniff',
        'cross-origin-resource-policy': 'cross-origin'
      })
      response.end(bytes)
      return
    }
    if (pathname === '/api/v1/app/sidecar/ui' && request.method === 'GET') {
      return ok(response, activeDescriptor(), 'Sidecar UI projection')
    }
    if (pathname === '/api/v1/app/sidecar/bridge/grant' && request.method === 'POST') {
      const descriptor = activeDescriptor()
      if (descriptor.state !== 'active') {
        return ok(response, { status: 'refused', reason: 'SIDECAR_NOT_ACTIVE' }, 'grant')
      }
      const body = JSON.parse(await readBody(request))
      const capabilities = Array.isArray(body.capabilities) ? body.capabilities.filter((op) => OPS.includes(op)) : []
      if (capabilities.length === 0) {
        return fail(response, 'INVALID_REQUEST')
      }
      if (grantCount() >= 4) {
        return ok(response, { status: 'refused', reason: 'GRANT_LIMIT_REACHED' }, 'grant')
      }
      const grantId = randomId()
      const grant = {
        grantId,
        active: true,
        revoked: false,
        capabilities,
        expiresAt: now() + 300000,
        ...state.identity
      }
      state.grants.set(grantId, grant)
      return ok(response, {
        status: 'granted',
        capabilities,
        grant: {
          grant_id: grantId,
          expires_at_ms: grant.expiresAt,
          issued_at_ms: now(),
          capabilities,
          identity: {
            publisher_id: state.identity.publisher_id,
            app_id: state.identity.app_id,
            publisher_key_sha256: state.identity.publisher_key_sha256,
            native_tbs_sha256: state.identity.native_tbs_sha256,
            ui_tbs_sha256: state.identity.ui_tbs_sha256,
            build_id: state.identity.build_id,
            host_boot_generation: state.identity.host_boot_generation,
            native_session_generation: state.identity.native_session_generation,
            ui_generation: state.identity.ui_generation,
            sidecar_service_generation: state.identity.sidecar_service_generation
          }
        }
      }, 'grant')
    }
    if (pathname === '/api/v1/app/sidecar/bridge/confirm' && request.method === 'POST') {
      const body = JSON.parse(await readBody(request))
      const grant = state.grants.get(body.grant_id)
      if (!grant || grant.revoked || (body.op !== 'counters.reset' && body.op !== 'enable.apply')) {
        return fail(response, 'INVALID_REQUEST')
      }
      const token = randomId()
      state.confirms.set(token, { used: false, grantId: body.grant_id, expiresAt: now() + 120000 })
      return ok(response, { status: 'confirmed', confirm_token: token, expires_at_ms: now() + 120000 }, 'confirm')
    }
    if (pathname === '/api/v1/app/sidecar/bridge/revoke' && request.method === 'POST') {
      const body = await readBody(request)
      const parsed = body ? JSON.parse(body) : {}
      let count = 0
      for (const grant of state.grants.values()) {
        if (!grant.active) continue
        if (parsed.grant_id && grant.grantId !== parsed.grant_id) continue
        grant.revoked = true
        grant.active = false
        count += 1
      }
      state.confirms.clear()
      return ok(response, { status: 'revoked', count }, 'revoke')
    }
    if (pathname === '/api/v1/app/sidecar/bridge' && request.method === 'POST') {
      return dispatch(request, response)
    }
    if (pathname === '/trigger-generation-switch' && request.method === 'GET') {
      state.identity.sidecar_service_generation += 1
      response.writeHead(200, { 'content-type': 'text/plain' })
      response.end('switched\n')
      return
    }
    const distFile = path.join(distRoot, pathname === '/' ? 'index.html' : pathname)
    if (existsSync(distFile) && !distFile.includes('app-site')) {
      response.writeHead(200, { 'content-type': mime(distFile), 'cache-control': 'no-store' })
      response.end(readFileSync(distFile))
      return
    }
    response.writeHead(404, { 'cache-control': 'no-store' })
    response.end('not found\n')
  })
}

export function materializeSignedAssets(destDir) {
  rmSync(destDir, { recursive: true, force: true })
  mkdirSync(destDir, { recursive: true })
  for (const [name, asset] of signedManifest) {
    const target = path.join(destDir, name)
    mkdirSync(path.dirname(target), { recursive: true })
    writeFileSync(target, asset.bytes)
  }
  return destDir
}

export function createAppServer({ appRoot, probeRoot, allowedHostOrigin, hostOriginForFrame }) {
  return createServer((request, response) => {
    const url = new URL(request.url || '/', `http://${request.headers.host}`)
    let pathname = url.pathname
    const appRoute = /^\/appsite\/g(\d+)\/(.*)$/.exec(pathname)
    if (appRoute) {
      pathname = `/${appRoute[2]}`
    }
    let file = path.join(appRoot, pathname === '/' ? 'index.html' : pathname)
    if (!file.startsWith(appRoot)) {
      response.writeHead(403)
      response.end()
      return
    }
    if (!existsSync(file) && probeRoot) {
      const probe = path.join(probeRoot, pathname)
      if (probe.startsWith(probeRoot) && existsSync(probe)) {
        file = probe
      }
    }
    if (!existsSync(file)) {
      response.writeHead(404)
      response.end()
      return
    }
    const hostOrigin = url.searchParams.get('hostOrigin') || hostOriginForFrame
    response.writeHead(200, {
      'content-type': mimeOf(file),
      'cache-control': 'no-store',
      'content-security-policy': `frame-ancestors ${allowedHostOrigin}; connect-src 'self' ws: wss:; worker-src 'self' blob:; media-src 'self' blob:; script-src 'self' 'unsafe-inline' blob:; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; default-src 'self' blob:`
    })
    let content = readFileSync(file)
    if (file.endsWith('.html')) {
      content = Buffer.from(content.toString('utf8').replace(/__HOST_ORIGIN__/g, hostOrigin))
    }
    response.end(content)
  })
}
