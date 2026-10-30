import { useEffect, useMemo, useRef, useState } from 'preact/hooks'
import { useLingui } from '@lingui/react'
import { SidecarBroker } from '@/services/sidecarBridge'
import appManagementApi from '@/services/api/appManagement'
import { useAppManagementStore } from '@/store/appManagement'
import type { SidecarIdentity, SidecarOp, SidecarUiProjection } from '@/types/sidecar'

interface SidecarAppFrameProps {
  projection: SidecarUiProjection
  appId: string
}

type BridgePhase = 'handshaking' | 'active' | 'revoked'

const DANGEROUS_CONFIRM_OPS: ReadonlySet<SidecarOp> = new Set(['counters.reset', 'enable.apply'])

export default function SidecarAppFrame({ projection, appId }: SidecarAppFrameProps) {
  const { i18n } = useLingui()
  const frameRef = useRef<HTMLIFrameElement | null>(null)
  const frameLoadedRef = useRef<{ done: boolean; waiters: Array<() => void> }>({ done: false, waiters: [] })
  const brokerRef = useRef<SidecarBroker | null>(null)
  const observerRef = useRef<ResizeObserver | null>(null)
  const frameHeightRef = useRef(0)
  const [phase, setPhase] = useState<BridgePhase>('handshaking')
  const [revokeReason, setRevokeReason] = useState<string | null>(null)
  const [dangerousOp, setDangerousOp] = useState<SidecarOp | null>(null)
  const [confirmWorking, setConfirmWorking] = useState(false)
  const [retryNonce, setRetryNonce] = useState(0)
  const autoRetryRef = useRef({ attempts: 0, timer: null as number | null })
  const bootWatchdogRef = useRef({ reloads: 0, timer: null as number | null })
  const grant = useRef<{ grantId: string; capabilities: string[]; identity: SidecarIdentity } | null>(null)

  const appOrigin = projection.app_site_origin ?? ''
  const frameSrc = useMemo(() => {
    if (!projection.ui_base_path) return ''
    const entry = projection.entry_path || 'index.html'
    if (!appOrigin) return `${projection.ui_base_path}${entry}`
    return `${appOrigin}${projection.ui_base_path}${entry}`
  }, [appOrigin, projection.ui_base_path, projection.entry_path])

  useEffect(() => {
    frameLoadedRef.current = { done: false, waiters: [] }
    autoRetryRef.current = { attempts: 0, timer: null }
    bootWatchdogRef.current = { reloads: 0, timer: null }
    return () => {
      if (autoRetryRef.current.timer !== null) window.clearTimeout(autoRetryRef.current.timer)
      if (bootWatchdogRef.current.timer !== null) window.clearTimeout(bootWatchdogRef.current.timer)
    }
  }, [frameSrc, appOrigin])

  useEffect(() => {
    if (phase !== 'active') return
    if (bootWatchdogRef.current.reloads >= 2) return
    bootWatchdogRef.current.timer = window.setTimeout(() => {
      bootWatchdogRef.current.timer = null
      const doc = frameRef.current?.contentDocument
      if (!doc) return
      const shell = doc.getElementById('app')
      if (shell !== null && shell.childElementCount > 0) return
      bootWatchdogRef.current.reloads += 1
      setRetryNonce((value) => value + 1)
    }, 6000)
    return () => {
      if (bootWatchdogRef.current.timer !== null) {
        window.clearTimeout(bootWatchdogRef.current.timer)
        bootWatchdogRef.current.timer = null
      }
    }
  }, [phase, retryNonce])

  useEffect(() => {
    if (phase !== 'revoked') return
    autoRetryRef.current.attempts += 1
    const delay = autoRetryRef.current.attempts <= 3 ? 3000 * autoRetryRef.current.attempts : 30000
    autoRetryRef.current.timer = window.setTimeout(() => {
      autoRetryRef.current.timer = null
      useAppManagementStore.getState().refreshSidecar()
      setRetryNonce((value) => value + 1)
    }, delay)
    return () => {
      if (autoRetryRef.current.timer !== null) {
        window.clearTimeout(autoRetryRef.current.timer)
        autoRetryRef.current.timer = null
      }
    }
  }, [phase, retryNonce])

  useEffect(() => {
    let cancelled = false
    const teardown = () => {
      if (observerRef.current) {
        observerRef.current.disconnect()
        observerRef.current = null
      }
      if (brokerRef.current) {
        brokerRef.current.revoke('FRAME_UNMOUNT')
        brokerRef.current = null
      }
    }
    const setup = async () => {
      if (!frameSrc || cancelled) return
      frameLoadedRef.current = { done: false, waiters: [] }
      setPhase('handshaking')
      try {
        const res = (await appManagementApi.requestSidecarGrant([
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
        ])) as { success?: boolean; data?: { status?: string; capabilities?: string[]; grant?: { grant_id: string; expires_at_ms?: number; issued_at_ms?: number; capabilities?: string[]; identity?: unknown } }; error_code?: string }
        if (cancelled) return
        if (!res || res.success !== true || !res.data || res.data.status !== 'granted' || !res.data.grant) {
          setPhase('revoked')
          setRevokeReason(res?.error_code || 'GRANT_REFUSED')
          return
        }
        const requested = res.data.grant
        const grantIdentity = requested.identity as SidecarIdentity | undefined
        const grantCaps = (Array.isArray(requested.capabilities)
          ? requested.capabilities
          : res.data.capabilities) as string[] | undefined
        if (!grantIdentity || !Array.isArray(grantCaps)) {
          setPhase('revoked')
          setRevokeReason('GRANT_IDENTITY_MISSING')
          return
        }
        grant.current = {
          grantId: requested.grant_id,
          capabilities: grantCaps,
          identity: grantIdentity,
        }
        await Promise.race([
          new Promise<void>((resolve) => {
            if (frameLoadedRef.current.done) resolve()
            else frameLoadedRef.current.waiters.push(resolve)
          }),
          new Promise<void>((resolve) => { setTimeout(resolve, 15000) }),
        ])
        if (cancelled) return
        const iframeWindow = frameRef.current?.contentWindow
        if (!iframeWindow) {
          setPhase('revoked')
          setRevokeReason('IFRAME_WINDOW_UNAVAILABLE')
          return
        }
        const broker = new SidecarBroker({
          appWindow: iframeWindow,
          appOrigin: appOrigin ? new URL(appOrigin).origin : window.location.origin,
          grant: {
            grant_id: requested.grant_id,
            expires_at_ms: Number(requested.expires_at_ms) || 0,
            issued_at_ms: Number(requested.issued_at_ms) || 0,
            capabilities: grantCaps as never[],
            identity: grantIdentity,
          },
          serverCall: async (op, body) => {
            const response = (await appManagementApi.sidecarBridgeCall(op, body)) as {
              success?: boolean
              error_code?: string
              data?: { revoked?: boolean; result?: Record<string, unknown> }
            }
            if (!response || response.success !== true) {
              return {
                outcome: 'UNKNOWN' as const,
                reason: response?.error_code || 'TRANSPORT_FAILED',
                revoked: true,
              }
            }
            const data = response.data || {}
            const result = data.result || {}
            return {
              outcome: (result.outcome as never) ?? 'UNKNOWN',
              reason: result.reason as string | undefined,
              freshness: result.freshness as string | undefined,
              config_generation: result.config_generation as number | undefined,
              state_revision: result.state_revision as number | undefined,
              age_ms: result.age_ms as number | undefined,
              payload: result.payload,
              revoked: data.revoked === true,
            }
          },
          onResult: (result) => {
            const resultReason = (result.payload as { reason?: unknown } | undefined)?.reason
            if (
              result.op !== undefined
              && DANGEROUS_CONFIRM_OPS.has(result.op)
              && result.outcome === 'REJECTED'
              && resultReason === 'ADMIN_CONFIRM_REQUIRED'
            ) {
              setDangerousOp(result.op)
            }
            if (result.op === 'video.open') {
              if (result.outcome === 'APPLIED') broker.video.markLive()
              else if (result.outcome === 'UNKNOWN') broker.video.markReconnecting()
              else broker.video.markUnavailable()
            }
            if (result.op === 'video.close' && result.outcome !== 'UNAVAILABLE') {
              broker.video.markUnavailable()
            }
          },
          onRevoke: (reason) => {
            setPhase('revoked')
            setRevokeReason(reason)
          },
        })
        brokerRef.current = broker
        broker.start()
        autoRetryRef.current.attempts = 0
        setPhase('active')
      } catch {
        if (!cancelled) {
          setPhase('revoked')
          setRevokeReason('GRANT_REQUEST_FAILED')
        }
      }
    }
    setup()
    return () => {
      cancelled = true
      teardown()
    }
  }, [frameSrc, appOrigin, retryNonce])

  const handleConfirmDangerous = async () => {
    const broker = brokerRef.current
    if (!broker || dangerousOp === null || confirmWorking) return
    const confirmedOp = dangerousOp
    setConfirmWorking(true)
    const delivered = await broker.confirmDangerousOp(confirmedOp, async (grantId, op) => {
      const res = (await appManagementApi.confirmSidecarDangerous(grantId, op)) as {
        success?: boolean
        data?: { confirm_token?: string; expires_at_ms?: number }
      }
      if (!res || res.success !== true || !res.data?.confirm_token || !res.data?.expires_at_ms) {
        throw new Error('CONFIRM_REFUSED')
      }
      return { token: res.data.confirm_token, expiresAtMs: Number(res.data.expires_at_ms) }
    })
    setConfirmWorking(false)
    if (delivered) setDangerousOp(null)
  }

  const handleRevokeRetry = async () => {
    autoRetryRef.current.attempts = 0
    setRetryNonce((value) => value + 1)
    await useAppManagementStore.getState().refreshSidecar()
  }

  const attachFrameObserver = () => {
    if (typeof ResizeObserver === 'undefined') return
    try {
      const doc = frameRef.current?.contentDocument
      if (!doc) return
      const target = doc.documentElement ?? doc.body
      if (!target) return
      observerRef.current?.disconnect()
      const observer = new ResizeObserver(() => {
        const frame = frameRef.current
        const currentDoc = frame?.contentDocument
        if (!frame || !currentDoc) return
        const scrollHeight = currentDoc.documentElement?.scrollHeight
          ?? currentDoc.body?.scrollHeight
          ?? 0
        const height = Math.max(480, scrollHeight)
        if (Math.abs(height - frameHeightRef.current) <= 8) return
        frameHeightRef.current = height
        frame.style.height = `${height}px`
      })
      observer.observe(target)
      observerRef.current = observer
      frameHeightRef.current = 0
    } catch {
      observerRef.current = null
    }
  }

  if (!frameSrc) {
    return (
      <div className="rounded border border-amber-300 bg-amber-50 p-3 text-sm text-amber-800" data-testid="sidecar-frame-blocked">
        {i18n._('sys.app_management.sidecar.origin_unavailable')}
      </div>
    )
  }

  return (
    <div>
      {phase === 'revoked' && (
        <div className="rounded border border-red-300 bg-red-50 p-3 text-sm text-red-700" data-testid="sidecar-revoked">
          <p className="font-medium">{i18n._('sys.app_management.sidecar.revoked_title')}</p>
          <p className="mt-1 font-mono text-xs">{revokeReason}</p>
          <div className="mt-2 flex gap-2">
            <button
              type="button"
              className="rounded border border-[#dce3e9] bg-white px-2 py-1 text-xs hover:bg-[#f5f6f8]"
              onClick={() => {
                handleRevokeRetry()
              }}
              data-testid="sidecar-recheck"
            >
              {i18n._('sys.app_management.actions.refresh')}
            </button>
          </div>
        </div>
      )}
      {dangerousOp !== null && phase === 'active' && (
        <div className="mb-2 rounded border border-[#f0d8be] bg-[#fff8f1] p-3 text-sm text-[#745638]" data-testid="sidecar-dangerous-confirm">
          <p>{i18n._('sys.app_management.sidecar.dangerous_confirm_note')}</p>
          <button
            type="button"
            className="mt-2 rounded border border-[#efcfce] bg-white px-2 py-1 text-xs font-semibold text-[#c44a48] hover:bg-[#fdf3f2] disabled:opacity-50"
            onClick={() => {
              handleConfirmDangerous()
            }}
            disabled={confirmWorking}
            data-testid="sidecar-dangerous-confirm-button"
          >
            {i18n._('sys.app_management.sidecar.dangerous_confirm_button')}
          </button>
        </div>
      )}
      {phase !== 'revoked' && (
        <iframe
          key={`${appOrigin}/${projection.sidecar_service_generation ?? ''}/${retryNonce}`}
          onLoad={() => {
            frameLoadedRef.current.done = true
            frameLoadedRef.current.waiters.forEach((w) => w())
            frameLoadedRef.current.waiters = []
            attachFrameObserver()
          }}
          ref={frameRef}
          src={frameSrc}
          title={i18n._('sys.app_management.sidecar.tab_label')}
          sandbox="allow-scripts allow-same-origin"
          className="mt-2 min-h-[560px] w-full rounded-lg border border-[#e6eaee] bg-white"
          data-testid="sidecar-frame"
          data-app-id={appId}
        />
      )}
    </div>
  )
}
