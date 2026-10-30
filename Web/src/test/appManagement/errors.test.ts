import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import i18n from '@/i18n'
import { appReasonKey, appEnvelopeKey, appFileErrorKey } from '@/pages/applicationManagement/messages'
import { useAppManagementStore } from '@/store/appManagement'
import {
  requestMock,
  envelopeOk,
  installedState,
  stubGetInstalled,
  stubPath,
  setupSuite,
  teardownSuite,
} from './helpers'

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
}))

vi.mock('sonner', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() },
}))

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('device failure class to friendly text mapping', () => {
  const reasons = [
    'PARAM',
    'BUSY',
    'BAD_PACKAGE',
    'LEGACY_SIGNED_FORMAT_REJECTED',
    'LEGACY_SIGNED_STATE_PRESERVED',
    'LEGACY_SIGNED_PAIR_STATE_PRESERVED',
    'UNKNOWN_FORMAT_REJECTED',
    'TARGET_INCOMPATIBLE',
    'ABI_INCOMPATIBLE',
    'RESOURCE_LIMIT',
    'STORAGE_STATE_UNKNOWN',
    'STORAGE_FULL',
    'WRITE_FAILED',
    'COMMIT_FAILED',
    'VERSION_CONFLICT',
    'CONTENT_CONFLICT',
    'NATIVE_VERIFY_FAILED',
    'UI_VERIFY_FAILED',
    'DOWNGRADE_REFUSED',
    'PUBLISHER_CONFLICT',
    'IDENTITY_CONFLICT',
    'NOT_FOUND',
    'UNAVAILABLE',
    'NOT_INSTALLED',
    'LOAD_FAILED',
    'RUNTIME_UNAVAILABLE',
    'UNKNOWN',
  ]

  it('resolves a distinct localized message for every known install/run reason', () => {
    const seen = new Set<string>()
    for (const reason of reasons) {
      const key = appReasonKey(reason)
      expect(key).toBe(`sys.app_management.reasons.${reason}`)
      const text = i18n._(key)
      expect(text).not.toBe(key)
      seen.add(text)
    }
    expect(seen.size).toBe(reasons.length)
  })

  it('falls back to a generic message for unrecognized reasons without inventing verdicts', () => {
    const key = appReasonKey('SOME_FUTURE_REASON')
    expect(key).toBe('sys.app_management.reasons.UNKNOWN')
    expect(i18n._(key)).not.toBe(key)
  })

  it('maps envelope codes and file validation to localized text', () => {
    for (const code of ['INVALID_REQUEST', 'METHOD_NOT_ALLOWED', 'INTERNAL_ERROR', 'SERVICE_UNAVAILABLE', 'TIMEOUT', 'TOO_MANY_REQUESTS']) {
      expect(i18n._(appEnvelopeKey(code))).not.toBe(appEnvelopeKey(code))
    }
    expect(i18n._(appEnvelopeKey('WHATEVER_NEW'))).toBe(i18n._('sys.app_management.envelope.GENERIC'))
    expect(i18n._(appFileErrorKey('EMPTY_FILE'))).not.toBe('sys.app_management.file.empty')
    expect(i18n._(appFileErrorKey('FILE_TOO_LARGE'))).not.toBe('sys.app_management.file.too_large')
  })
})

describe('refused operations keep server truth and never fake success', () => {
  it('fails closed when an installed native record lacks the composite UI identity', async () => {
    stubGetInstalled(envelopeOk({
      state: 'installed',
      publisher_id: 'pub-alpha',
      app_id: 'hello-app',
      version_major: 1,
      version_minor: 0,
      version_patch: 0,
    }))
    await useAppManagementStore.getState().refresh()
    expect(useAppManagementStore.getState().view).toMatchObject({
      kind: 'unavailable',
      reason: 'PAIR_UI_IDENTITY_MISSING',
    })
  })

  it('install refused legacy signed package leaves the previous device state untouched', async () => {
    stubGetInstalled(installedState(false))
    await useAppManagementStore.getState().refresh()
    stubPath('post', '/api/v1/app/install', () => Promise.resolve({
        success: true,
        data: { status: 'refused', reason: 'LEGACY_SIGNED_FORMAT_REJECTED', executable: false },
      }),)
    const file = new File([new Uint8Array([9, 9, 9])], 'bad.neapp')
    await useAppManagementStore.getState().installPackage(file)
    const s = useAppManagementStore.getState()
    expect(s.installOutcome).toBeNull()
    expect(s.lastError).toEqual({ scope: 'install', kind: 'refused', reason: 'LEGACY_SIGNED_FORMAT_REJECTED' })
    expect(s.view?.kind).toBe('installed')
    expect(i18n._(appReasonKey('LEGACY_SIGNED_FORMAT_REJECTED'))).toBeTruthy()
  })

  it('run refused BUSY is shown as a host failure class with retry-later text, not app output', async () => {
    stubGetInstalled(installedState(false))
    await useAppManagementStore.getState().refresh()
    stubPath('post', '/api/v1/app/run', () => Promise.resolve({
        success: true,
        data: { status: 'refused', reason: 'BUSY', executable: false, app_running: true },
      }),)
    await useAppManagementStore.getState().runOnce()
    const s = useAppManagementStore.getState()
    expect(s.runOutcome?.status).toBe('refused')
    expect(s.runOutcome?.reason).toBe('BUSY')
    expect(s.lastError).toBeNull()
    expect(i18n._(appReasonKey('BUSY'))).toMatch(/busy/i)
  })

  it('install envelope error INVALID_REQUEST maps to envelope text without app details', async () => {
    stubGetInstalled(installedState(false))
    await useAppManagementStore.getState().refresh()
    requestMock.post.mockImplementation(() => Promise.resolve({ success: false, error_code: 'INVALID_REQUEST', message: 'Invalid NEPK v1.1 package' }),)
    const file = new File([new Uint8Array([1])], 'x.neapp')
    await useAppManagementStore.getState().installPackage(file)
    const s = useAppManagementStore.getState()
    expect(s.lastError?.kind).toBe('envelope')
    expect(s.lastError?.reason).toBe('INVALID_REQUEST')
    expect(s.installOutcome).toBeNull()
  })

  it('query envelope forbidden blocks as auth without creating a device state verdict', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve({ success: false, error_code: 'FORBIDDEN', message: 'refused' }),)
    await useAppManagementStore.getState().refresh()
    const s = useAppManagementStore.getState()
    expect(s.authBlock).toBe('forbidden')
    expect(s.view).toBeNull()
    expect(s.lastError).toBeNull()
  })
})
