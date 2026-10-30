import {
  SidecarBroker,
  isCrossSite,
  type SidecarGrant,
  type SidecarIdentity,
  type SidecarOp,
  type SidecarUiProjection,
} from '@/services/sidecarBridge'

const status = document.getElementById('status') as HTMLElement
const projectionBox = document.getElementById('projection') as HTMLElement
const snapshotBox = document.getElementById('snapshot') as HTMLElement
const revokeBox = document.getElementById('revoke') as HTMLElement
const frameSlot = document.getElementById('frame-slot') as HTMLElement
const confirmBox = document.getElementById('confirm-dangerous') as HTMLElement

const query = new URLSearchParams(window.location.search)
const timeoutMs = query.get('timeoutMs') ? Number(query.get('timeoutMs')) : undefined
const livePollMs = query.get('livePollMs') ? Number(query.get('livePollMs')) : undefined
const appOriginOverride = query.get('appOrigin')

const dangerousOps: ReadonlySet<SidecarOp> = new Set(['counters.reset', 'enable.apply'])

let pendingDangerousOp: SidecarOp | null = null

window.addEventListener('message', (event) => {
  const expected = query.get('appOrigin')
  if (!expected || event.origin !== expected) {
    return
  }
  const marker = document.getElementById('forged')
  if (marker) {
    marker.textContent = 'ignored-window-message'
  }
})

async function postJson(path: string, body: unknown): Promise<any> {
  const response = await fetch(path, {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify(body),
  })
  return response.json()
}

async function serverCall(
  op: SidecarOp,
  request: {
    grant_id: string
    request_id: number
    payload: Record<string, unknown>
    expected_config_generation?: number
    confirm_token?: string
  },
): Promise<{
  outcome: any
  reason?: string
  payload?: unknown
  revoked?: boolean
}> {
  const envelope = await postJson('/api/v1/app/sidecar/bridge', { op, ...request })
  if (!envelope || envelope.success !== true) {
    return {
      outcome: 'UNKNOWN',
      reason: envelope?.error_code || 'TRANSPORT_FAILED',
      revoked: true,
    }
  }
  const data = envelope.data || {}
  const result = data.result || {}
  return {
    outcome: result.outcome ?? 'UNKNOWN',
    reason: result.reason,
    payload: result.payload,
    revoked: data.revoked === true,
  }
}

async function main(): Promise<void> {
  const projectionResponse = await fetch('/api/v1/app/sidecar/ui')
  const projectionEnvelope = await projectionResponse.json()
  const projection = (projectionEnvelope.data ?? {}) as SidecarUiProjection
  projectionBox.textContent = JSON.stringify(projection)
  if (projection.state !== 'active' || !projection.app_site_origin) {
    status.textContent = 'unavailable'
    return
  }
  const appOrigin = appOriginOverride ?? String(projection.app_site_origin)
  const adminOrigin = window.location.origin
  if (adminOrigin === appOrigin || !isCrossSite(adminOrigin, appOrigin)) {
    status.textContent = 'blocked-same-site'
    return
  }
  status.textContent = 'creating-frame'
  const iframe = document.createElement('iframe')
  iframe.setAttribute('sandbox', 'allow-scripts allow-same-origin')
  iframe.src = `${appOrigin}${projection.ui_base_path ?? '/appsite/g1/'}index.html?hostOrigin=${encodeURIComponent(adminOrigin)}`
  iframe.setAttribute('data-testid', 'host-frame')
  iframe.title = 'Line Crossing (App)'
  frameSlot.appendChild(iframe)

  await new Promise<void>((resolve) => {
    iframe.addEventListener('load', () => resolve(), { once: true })
  })

  const grantEnvelope = await postJson('/api/v1/app/sidecar/bridge/grant', {
    capabilities: [
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
    ],
  })
  if (!grantEnvelope || grantEnvelope.success !== true || grantEnvelope.data?.status !== 'granted') {
    status.textContent = 'grant-refused'
    iframe.remove()
    return
  }
  const grantData = grantEnvelope.data.grant
  const grant: SidecarGrant = {
    grant_id: String(grantData.grant_id),
    expires_at_ms: Number(grantData.expires_at_ms),
    issued_at_ms: Number(grantData.issued_at_ms),
    capabilities: (grantEnvelope.data.capabilities ?? grantData.capabilities) as SidecarGrant['capabilities'],
    identity: grantData.identity as SidecarIdentity,
  }
  status.textContent = 'granted'

  const broker = new SidecarBroker({
    appWindow: iframe.contentWindow as Window,
    appOrigin: new URL(appOrigin).origin,
    grant,
    serverCall,
    timeoutMs,
    livePollIntervalMs: livePollMs,
    onResult: (result) => {
      if (result.op === 'snapshot.read') {
        snapshotBox.textContent = JSON.stringify({
          outcome: result.outcome,
          payload: result.payload,
        })
      }
      const reason = (result.payload as { reason?: unknown } | undefined)?.reason
      if (
        result.op !== undefined
        && dangerousOps.has(result.op)
        && result.outcome === 'REJECTED'
        && reason === 'ADMIN_CONFIRM_REQUIRED'
      ) {
        pendingDangerousOp = result.op
        confirmBox.hidden = false
      }
      ;(window as any).lastResult = result
      ;((window as any).results ||= []).push(result)
    },
    onRevoke: (reason) => {
      revokeBox.textContent = reason
      revokeBox.hidden = false
      iframe.remove()
      status.textContent = 'revoked'
    },
  })
  ;(window as any).hostBroker = broker
  broker.start()
}

const confirmButton = document.getElementById('confirm-dangerous-button') as HTMLButtonElement | null
confirmButton?.addEventListener('click', () => {
  const broker = (window as any).hostBroker as SidecarBroker | undefined
  if (!broker || pendingDangerousOp === null) return
  const confirmedOp = pendingDangerousOp
  void broker
    .confirmDangerousOp(confirmedOp, async (grantId, op) => {
      const res = (await postJson('/api/v1/app/sidecar/bridge/confirm', {
        grant_id: grantId,
        op,
      })) as { success?: boolean; data?: { confirm_token?: string; expires_at_ms?: number } }
      if (!res || res.success !== true || !res.data?.confirm_token || !res.data?.expires_at_ms) {
        throw new Error('CONFIRM_REFUSED')
      }
      return { token: res.data.confirm_token, expiresAtMs: Number(res.data.expires_at_ms) }
    })
    .then((delivered) => {
      if (delivered) {
        confirmBox.hidden = true
        pendingDangerousOp = null
      }
      ;(window as any).confirmDelivered = true
    })
})

void main()
