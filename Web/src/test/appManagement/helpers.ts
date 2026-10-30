import { type Mock } from 'vitest'
import { cleanup } from '@testing-library/preact'
import request from '@/services/request'
import { useAppManagementStore } from '@/store/appManagement'
import { setLocale } from '@/i18n'
import { businessUiCatalog } from '@/pages/applicationManagement/appSlot'

export const requestMock = request as unknown as {
  get: Mock
  post: Mock
  delete: Mock
}

export const envelopeOk = <T, >(data: T) => ({ success: true, message: 'success', data })

export const envelopeFail = (code: string, message = 'refused') => ({
  success: false,
  error_code: code,
  message,
})

export const businessReject = (code: string, message = 'refused') => {
  const err: Record<string, unknown> = {
    data: envelopeFail(code, message),
    status: 200,
  }
  return Promise.reject(err)
}

export const networkReject = () => Promise.reject(new Error('Network Error'))

export const installedState = (appRunning = false) => envelopeOk({
    status: 'installed',
    publisher_id: 'pub-alpha',
    app_id: 'hello-app',
    version_major: 1,
    version_minor: 2,
    version_patch: 3,
    package_len: 1024,
    package_sha256: 'f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8',
    tbs_sha256: 'bb22',
    ui: { status: 'active' },
    executable: false,
    app_running: appRunning,
  })

export const noneState = () => envelopeOk({ state: 'none', executable: false, app_running: false })

export const installedStateFor = (
  appId: string,
  overrides: Record<string, unknown> = {},
  appRunning = false,
) => envelopeOk({
    status: 'installed',
    publisher_id: 'pub-beta',
    app_id: appId,
    version_major: 3,
    version_minor: 0,
    version_patch: 0,
    package_len: 4096,
    package_sha256: 'ee55',
    tbs_sha256: 'ff66',
    ui: { status: 'active' },
    executable: false,
    app_running: appRunning,
    ...overrides,
  })

export const installedStateRevision = (appRunning = false) => envelopeOk({
    status: 'installed',
    publisher_id: 'pub-alpha',
    app_id: 'hello-app',
    version_major: 1,
    version_minor: 3,
    version_patch: 0,
    package_len: 2048,
    package_sha256: 'aa12',
    tbs_sha256: 'bb23',
    ui: { status: 'active' },
    executable: false,
    app_running: appRunning,
  })

export const unknownState = (reason = 'STORAGE_STATE_UNKNOWN') => envelopeOk({ state: 'unknown', reason, executable: false, app_running: false })

export const unavailableState = (reason = 'LEGACY_SIGNED_STATE_PRESERVED', appRunning = false) => envelopeOk({
    status: 'unavailable',
    publisher_id: 'pub-alpha',
    app_id: 'hello-app',
    version_major: 1,
    version_minor: 0,
    version_patch: 0,
    package_len: 512,
    package_sha256: 'cc33',
    tbs_sha256: 'dd44',
    reason,
    executable: false,
    app_running: appRunning,
  })

export const stubGetInstalled = (envelope: unknown) => {
  requestMock.get.mockImplementation((url: string) => {
    if (String(url).includes('/api/v1/app/installed')) {
      return Promise.resolve(envelope)
    }
    return Promise.reject(new Error(`unexpected GET ${url}`))
  })
}

export const stubPath = (
  method: 'get' | 'post' | 'delete',
  fragment: string,
  result: () => Promise<unknown>,
) => {
  requestMock[method].mockImplementation((url: string) => {
    if (String(url).includes(fragment)) {
      return result()
    }
    return Promise.reject(new Error(`unexpected ${method.toUpperCase()} ${url}`))
  })
}

export function resetAppStore() {
  useAppManagementStore.setState({
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
  })
}

export function resetBusinessCatalog() {
  for (const key of Object.keys(businessUiCatalog)) {
    delete businessUiCatalog[key]
  }
}

export function setupSuite() {
  setLocale('en')
  resetAppStore()
  resetBusinessCatalog()
  requestMock.get.mockReset()
  requestMock.post.mockReset()
  requestMock.delete.mockReset()
}

export function teardownSuite() {
  cleanup()
  viRestore()
}

function viRestore() {
  requestMock.get.mockReset()
  requestMock.post.mockReset()
  requestMock.delete.mockReset()
}

export function selectNeappFile(input: HTMLInputElement, file: File) {
  Object.defineProperty(input, 'files', { value: [file], configurable: true })
  input.dispatchEvent(new Event('change', { bubbles: true }))
}

export function makeNeappFile(bytes: number[], name = 'pkg.neapp') {
  return new File([new Uint8Array(bytes)], name)
}
