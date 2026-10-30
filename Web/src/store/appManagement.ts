import { create } from 'zustand'
import appManagementApi, {
  APP_PACKAGE_MAX_BYTES,
  type ApiEnvelope,
  type AppOperationResponse,
  type AppStateResponse,
} from '@/services/api/appManagement'
import type { SidecarUiProjection } from '@/types/sidecar'

export interface AppRecord {
  publisherId: string
  appId: string
  version: string
  packageLen: number
  packageSha256: string
  tbsSha256: string
}

export type AppView =
  | { kind: 'installed'; record: AppRecord; appRunning: boolean }
  | { kind: 'unavailable'; record: AppRecord; reason: string; appRunning: boolean }
  | { kind: 'not_installed'; appRunning: boolean }
  | { kind: 'cleanup_pending'; record: AppRecord | null; appRunning: boolean }
  | { kind: 'undetermined'; reason: string | null }

export type SidecarView =
  | { kind: 'active'; projection: SidecarUiProjection }
  | { kind: 'unavailable'; reason: string }
  | { kind: 'unknown' }

export interface RunOutcome {
  status: 'completed' | 'refused'
  entryReturned: boolean
  entryResult: number | null
  log: string
  logTruncated: boolean
  reason: string | null
  detail: string | null
}

export interface StopOutcome {
  status: 'stopped' | 'stopping' | 'refused'
  entryReturned: boolean
  busy: boolean
  reason: string | null
}

export interface StartOutcome {
  status: 'started' | 'refused'
  reason: string | null
  detail: string | null
}

export type AppAuthBlock = 'unauthorized' | 'forbidden' | null

export interface AppLastError {
  scope: 'install' | 'uninstall' | 'run' | 'stop' | 'start' | 'query'
  kind: 'auth' | 'refused' | 'envelope' | 'indeterminate' | 'file'
  reason: string | null
}

interface AppManagementStore {
  view: AppView | null
  querying: boolean
  pendingVerification: boolean
  authBlock: AppAuthBlock
  installing: boolean
  uninstalling: boolean
  running: boolean
  stopping: boolean
  starting: boolean
  installOutcome: { status: 'installed' | 'already_installed' } | null
  uninstallOutcome: { status: 'uninstalled' } | null
  runOutcome: RunOutcome | null
  stopOutcome: StopOutcome | null
  startOutcome: StartOutcome | null
  lastError: AppLastError | null
  sidecar: SidecarView
  sidecarQuerying: boolean
  sidecarNonActiveStreak: number
  refresh: () => Promise<void>
  refreshSidecar: () => Promise<void>
  installPackage: (file: File) => Promise<void>
  uninstallApp: () => Promise<void>
  runOnce: () => Promise<void>
  stopApp: () => Promise<void>
  startApp: () => Promise<void>
  clearError: () => void
}

const normalizeRecord = (d: AppStateResponse): AppRecord => ({
  publisherId: d.publisher_id || '',
  appId: d.app_id || '',
  version: `v${d.version_major ?? '?'}.${d.version_minor ?? '?'}.${d.version_patch ?? '?'}`,
  packageLen: d.package_len ?? 0,
  packageSha256: d.package_sha256 || '',
  tbsSha256: d.tbs_sha256 || '',
})

const extractEnvelopeError = (err: unknown): ApiEnvelope | null => {
  const candidate = err as { data?: unknown; response?: { data?: unknown } }
  if (candidate && typeof candidate === 'object') {
    if (candidate.data && typeof candidate.data === 'object') {
      return candidate.data as ApiEnvelope
    }
    if (candidate.response && typeof candidate.response === 'object') {
      const inner = (candidate.response as { data?: unknown }).data
      if (inner && typeof inner === 'object') {
        return inner as ApiEnvelope
      }
    }
  }
  return null
}

const envelopeCodeToLastError = (
  scope: AppLastError['scope'],
  envelope: ApiEnvelope | null,
): AppLastError => {
  const code = envelope?.error_code || 'UNKNOWN'
  if (code === 'UNAUTHORIZED') {
    return { scope, kind: 'auth', reason: 'unauthorized' }
  }
  if (code === 'FORBIDDEN') {
    return { scope, kind: 'auth', reason: 'forbidden' }
  }
  return { scope, kind: 'envelope', reason: code }
}

export const useAppManagementStore = create<AppManagementStore>((set, get) => ({
  view: null,
  querying: false,
  pendingVerification: false,
  authBlock: null,
  installing: false,
  uninstalling: false,
  running: false,
  stopping: false,
  starting: false,
  installOutcome: null,
  uninstallOutcome: null,
  runOutcome: null,
  stopOutcome: null,
  startOutcome: null,
  lastError: null,
  sidecar: { kind: 'unknown' },
  sidecarQuerying: false,
  sidecarNonActiveStreak: 0,

  refreshSidecar: async () => {
    const keepActive = () => get().sidecar.kind === 'active'
    set({ sidecarQuerying: true })
    try {
      const res = (await appManagementApi.getSidecarUi()) as ApiEnvelope<SidecarUiProjection>
      if (!res || res.success === false) {
        const lastError = envelopeCodeToLastError('query', res || null)
        if (lastError.kind === 'auth') {
          set({ sidecar: { kind: 'unknown' }, sidecarNonActiveStreak: 0, sidecarQuerying: false })
        } else if (!keepActive()) {
          set({ sidecar: { kind: 'unavailable', reason: lastError.reason ?? 'UNKNOWN' }, sidecarQuerying: false })
        } else {
          set({ sidecarQuerying: false })
        }
        return
      }
      const d = (res.data ?? {}) as SidecarUiProjection
      if (d.state === 'active' && d.ui_base_path &&
          (d.app_site_origin || d.app_site_origin === null)) {
        set({ sidecar: { kind: 'active', projection: d }, sidecarNonActiveStreak: 0, sidecarQuerying: false })
      } else if (d.state === 'active') {
        set({
          sidecar: { kind: 'unavailable', reason: 'APP_SITE_ORIGIN_UNCONFIGURED' },
          sidecarNonActiveStreak: 0,
          sidecarQuerying: false,
        })
      } else if (keepActive() && get().sidecarNonActiveStreak < 1) {
        set({ sidecarNonActiveStreak: get().sidecarNonActiveStreak + 1, sidecarQuerying: false })
      } else {
        set({
          sidecar: { kind: 'unavailable', reason: d.reason || 'SIDECAR_NOT_ACTIVE' },
          sidecarNonActiveStreak: 0,
          sidecarQuerying: false,
        })
      }
    } catch {
      if (!keepActive()) {
        set({ sidecar: { kind: 'unavailable', reason: 'QUERY_FAILED' }, sidecarQuerying: false })
      } else {
        set({ sidecarQuerying: false })
      }
    }
  },

  refresh: async () => {
    set({ querying: true })
    try {
      const res = (await appManagementApi.getInstalledState()) as ApiEnvelope<AppStateResponse>
      if (!res || res.success === false) {
        const lastError = envelopeCodeToLastError('query', res || null)
        set({
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
          view: lastError.kind === 'auth' ? null : { kind: 'undetermined', reason: lastError.reason },
          pendingVerification: false,
          installOutcome: null,
          uninstallOutcome: null,
        })
        return
      }
      const d = res.data || {}
      const state = String(d.state || d.status || 'unknown').toLowerCase()
      set({ authBlock: null })
      if (state === 'installed' && d.ui?.status === 'active') {
        set({
          view: { kind: 'installed', record: normalizeRecord(d), appRunning: d.app_running === true },
          pendingVerification: false,
        })
      } else if (state === 'installed') {
        set({
          view: {
            kind: 'unavailable',
            record: normalizeRecord(d),
            reason: 'PAIR_UI_IDENTITY_MISSING',
            appRunning: d.app_running === true,
          },
          pendingVerification: false,
          runOutcome: null,
          installOutcome: null,
        })
      } else if (state === 'unavailable') {
        set({
          view: {
            kind: 'unavailable',
            record: normalizeRecord(d),
            reason: d.reason || '',
            appRunning: d.app_running === true,
          },
          pendingVerification: false,
          runOutcome: null,
          installOutcome: null,
        })
      } else if (state === 'none') {
        set({
          view: { kind: 'not_installed', appRunning: d.app_running === true },
          pendingVerification: false,
          runOutcome: null,
          installOutcome: null,
        })
      } else if (state === 'cleanup_pending') {
        set({
          view: { kind: 'cleanup_pending', record: null, appRunning: d.app_running === true },
          pendingVerification: false,
          runOutcome: null,
          installOutcome: null,
        })
      } else {
        set({
          view: { kind: 'undetermined', reason: d.reason || null },
          pendingVerification: false,
          runOutcome: null,
          installOutcome: null,
        })
      }
    } catch {
      set({
        view: { kind: 'undetermined', reason: null },
        installOutcome: null,
        uninstallOutcome: null,
      })
    } finally {
      set({ querying: false })
    }
  },

  installPackage: async (file: File) => {
    if (get().installing || get().uninstalling || get().running || get().stopping || get().starting) return
    if (file.size <= 0 || file.size > APP_PACKAGE_MAX_BYTES) {
      set({
        lastError: { scope: 'install', kind: 'file', reason: file.size <= 0 ? 'EMPTY_FILE' : 'FILE_TOO_LARGE' },
      })
      return
    }
    set({ installing: true, lastError: null, installOutcome: null, uninstallOutcome: null })
    try {
      const res = (await appManagementApi.installPackage(file)) as ApiEnvelope<AppOperationResponse>
      if (!res || res.success === false) {
        const lastError = envelopeCodeToLastError('install', res || null)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
        return
      }
      const d = res.data || {}
      if (d.status === 'installed' || d.status === 'already_installed') {
        set({ installOutcome: { status: d.status } })
      } else {
        set({ lastError: { scope: 'install', kind: 'refused', reason: d.reason || 'UNKNOWN' } })
      }
    } catch (err) {
      const envelope = extractEnvelopeError(err)
      if (envelope) {
        const lastError = envelopeCodeToLastError('install', envelope)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
      } else {
        set({
          lastError: { scope: 'install', kind: 'indeterminate', reason: null },
          pendingVerification: true,
        })
      }
    } finally {
      set({ installing: false })
      await get().refresh()
      await get().refreshSidecar()
    }
  },

  uninstallApp: async () => {
    if (get().installing || get().uninstalling || get().running || get().stopping || get().starting) return
    set({ uninstalling: true, lastError: null, uninstallOutcome: null })
    try {
      await appManagementApi.revokeSidecarGrants()
    } catch {
      void 0
    }
    try {
      const res = (await appManagementApi.uninstallApp()) as ApiEnvelope<AppOperationResponse>
      if (!res || res.success === false) {
        const lastError = envelopeCodeToLastError('uninstall', res || null)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
        return
      }
      const d = res.data || {}
      if (d.status === 'uninstalled') {
        set({ uninstallOutcome: { status: 'uninstalled' } })
      } else {
        set({ lastError: { scope: 'uninstall', kind: 'refused', reason: d.reason || 'UNKNOWN' } })
      }
    } catch (err) {
      const envelope = extractEnvelopeError(err)
      if (envelope) {
        const lastError = envelopeCodeToLastError('uninstall', envelope)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
      } else {
        set({
          lastError: { scope: 'uninstall', kind: 'indeterminate', reason: null },
          pendingVerification: true,
        })
      }
    } finally {
      set({ uninstalling: false })
      await get().refresh()
      await get().refreshSidecar()
    }
  },

  runOnce: async () => {
    if (get().installing || get().uninstalling || get().running || get().stopping || get().starting) return
    set({ running: true, lastError: null })
    try {
      const res = (await appManagementApi.runApp()) as ApiEnvelope<AppOperationResponse>
      if (!res || res.success === false) {
        const lastError = envelopeCodeToLastError('run', res || null)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
        return
      }
      const d = res.data || {}
      if (d.status === 'completed') {
        set({
          runOutcome: {
            status: 'completed',
            entryReturned: d.entry_returned === true,
            entryResult: typeof d.entry_result === 'number' ? d.entry_result : null,
            log: typeof d.log === 'string' ? d.log : '',
            logTruncated: d.log_truncated === true,
            reason: null,
            detail: null,
          },
        })
      } else {
        set({
          runOutcome: {
            status: 'refused',
            entryReturned: false,
            entryResult: null,
            log: '',
            logTruncated: false,
            reason: d.reason || 'UNKNOWN',
            detail: d.detail || null,
          },
        })
      }
    } catch (err) {
      const envelope = extractEnvelopeError(err)
      if (envelope) {
        const lastError = envelopeCodeToLastError('run', envelope)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
      } else {
        set({
          lastError: { scope: 'run', kind: 'indeterminate', reason: null },
          pendingVerification: true,
        })
      }
    } finally {
      set({ running: false })
      await get().refresh()
    }
  },

  stopApp: async () => {
    if (get().installing || get().uninstalling || get().running || get().stopping || get().starting) return
    set({ stopping: true, lastError: null })
    try {
      const res = (await appManagementApi.stopApp()) as ApiEnvelope<AppOperationResponse>
      if (!res || res.success === false) {
        const lastError = envelopeCodeToLastError('stop', res || null)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
        return
      }
      const d = res.data || {}
      if (d.status === 'stopped' || d.status === 'stopping') {
        set({
          stopOutcome: {
            status: d.status,
            entryReturned: d.entry_returned === true,
            busy: d.busy === true,
            reason: d.reason || null,
          },
        })
      } else {
        set({
          stopOutcome: {
            status: 'refused',
            entryReturned: false,
            busy: d.busy === true,
            reason: d.reason || 'UNKNOWN',
          },
        })
      }
    } catch (err) {
      const envelope = extractEnvelopeError(err)
      if (envelope) {
        const lastError = envelopeCodeToLastError('stop', envelope)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
      } else {
        set({
          lastError: { scope: 'stop', kind: 'indeterminate', reason: null },
          pendingVerification: true,
        })
      }
    } finally {
      set({ stopping: false })
      await get().refresh()
      await get().refreshSidecar()
      if (get().view?.kind !== 'installed' && get().view?.kind !== 'unavailable') {
        window.setTimeout(() => { void get().refresh(); void get().refreshSidecar() }, 1500)
      }
    }
  },

  startApp: async () => {
    if (get().installing || get().uninstalling || get().running || get().stopping || get().starting) return
    set({ starting: true, lastError: null })
    try {
      const res = (await appManagementApi.startApp()) as ApiEnvelope<AppOperationResponse>
      if (!res || res.success === false) {
        const lastError = envelopeCodeToLastError('start', res || null)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
        return
      }
      const d = res.data || {}
      if (d.status === 'started') {
        set({
          startOutcome: {
            status: 'started',
            reason: d.reason || null,
            detail: d.detail || null,
          },
        })
      } else {
        set({
          startOutcome: {
            status: 'refused',
            reason: d.reason || 'UNKNOWN',
            detail: d.detail || null,
          },
        })
      }
    } catch (err) {
      const envelope = extractEnvelopeError(err)
      if (envelope) {
        const lastError = envelopeCodeToLastError('start', envelope)
        set({
          lastError,
          authBlock: lastError.kind === 'auth' ? (lastError.reason as AppAuthBlock) : get().authBlock,
        })
      } else {
        set({
          lastError: { scope: 'start', kind: 'indeterminate', reason: null },
          pendingVerification: true,
        })
      }
    } finally {
      set({ starting: false })
      await get().refresh()
      await get().refreshSidecar()
      if (get().view?.kind !== 'installed' && get().view?.kind !== 'unavailable') {
        window.setTimeout(() => { void get().refresh(); void get().refreshSidecar() }, 1500)
      }
    }
  },

  clearError: () => set({ lastError: null }),
}))
