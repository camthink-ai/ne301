import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import {
  requestMock,
  installedState,
  stubGetInstalled,
  setupSuite,
  teardownSuite,
  envelopeOk,
  businessReject,
} from './helpers'

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
}))

vi.mock('sonner', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() },
}))

vi.mock('@/services/api/applicationManagement', () => ({
  default: {
    getMqttConfigReq: vi.fn(() => Promise.resolve({ data: {} })),
    setMqttConfigReq: vi.fn(),
    connectMqttReq: vi.fn(),
    disconnectMqttReq: vi.fn(),
    getWebhookConfigReq: vi.fn(() => Promise.resolve({ data: {} })),
    setWebhookConfigReq: vi.fn(),
    testWebhookReq: vi.fn(),
    getWebhookCaCertReq: vi.fn(),
    setWebhookCaCertReq: vi.fn(),
    deleteWebhookCaCertReq: vi.fn(),
  },
}))

vi.mock('@/pages/applicationManagement/mqtt-module', () => ({
  default: () => <div data-testid="mqtt-module-stub">MQTT module</div>,
}))

vi.mock('@/pages/applicationManagement/webhook-module', () => ({
  default: () => <div data-testid="webhook-module-stub">Webhook module</div>,
}))

const SECRET_TOKEN = 'Basic dXNlcjpwYXNzd29yZA=='

const renderPage = () => render(
    <I18nWrapper>
      <ApplicationManagement />
    </I18nWrapper>,
  )

const serializedStore = () => JSON.stringify(useAppManagementStore.getState())

const openManageDialog = async () => {
  await waitFor(() => {
    expect(useAppManagementStore.getState().view?.kind).toBe('installed')
  })
  fireEvent.click(screen.getByTestId('slot-manage'))
  await screen.findByTestId('manage-identity')
}

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('credential boundary', () => {
  it('carries credentials only in the transport layer, never in URLs or bodies', async () => {
    stubGetInstalled(installedState(false))
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/install')) {
        return Promise.resolve(envelopeOk({ status: 'installed', executable: false }))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    await useAppManagementStore.getState().refresh()
    await useAppManagementStore.getState().installPackage(new File([new Uint8Array([1])], 'a.neapp'))
    expect(requestMock.post).toHaveBeenCalledTimes(1)
    const [url, body, config] = requestMock.post.mock.calls[0]
    expect(String(url)).toBe('/api/v1/app/install')
    expect(String(url)).not.toMatch(/authorization|token|basic/i)
    expect(JSON.stringify(body)).not.toMatch(/authorization|token|basic/i)
    expect(body).toBeInstanceOf(File)
    expect((body as File).size).toBe(1)
    expect((config as { headers: Record<string, string> }).headers['Content-Type'])
      .toBe('application/octet-stream')
  })

  it('never echoes stored token material into store state, errors or the DOM', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve({ success: false, error_code: 'FORBIDDEN', message: 'refused' }),)
    requestMock.post.mockImplementation(() => businessReject('FORBIDDEN', 'refused'))
    localStorage.setItem('token', JSON.stringify(SECRET_TOKEN))
    await useAppManagementStore.getState().refresh()
    await useAppManagementStore.getState().installPackage(new File([new Uint8Array([5])], 'b.neapp'))
    const s = useAppManagementStore.getState()
    expect(s.authBlock).toBe('forbidden')
    expect(s.view).toBeNull()
    expect(serializedStore()).not.toContain('dXNlcjpwYXNzd29yZA')
    renderPage()
    await waitFor(() => {
      expect(screen.getByText(/do not have high-privilege app management permission/i)).toBeTruthy()
    })
    const domText = document.body.textContent || ''
    expect(domText).not.toContain('dXNlcjpwYXNzd29yZA')
    expect(domText).not.toContain(SECRET_TOKEN)
  })

  it('keeps the operation auth refusal visible when a later state query succeeds', async () => {
    stubGetInstalled(installedState(false))
    requestMock.post.mockImplementation(() => businessReject('FORBIDDEN', 'refused'))
    await useAppManagementStore.getState().refresh()
    await useAppManagementStore.getState().installPackage(new File([new Uint8Array([5])], 'e.neapp'))
    const s = useAppManagementStore.getState()
    expect(s.authBlock).toBeNull()
    expect(s.lastError).toEqual({ scope: 'install', kind: 'auth', reason: 'forbidden' })
    renderPage()
    await openManageDialog()
    await waitFor(() => {
      expect(screen.getByText(/do not have high-privilege app management permission/i)).toBeTruthy()
    })
  })

  it('never echoes the uploaded package payload into error state or DOM on refusal', async () => {
    stubGetInstalled(installedState(false))
    requestMock.post.mockImplementation(() => Promise.resolve({ success: true, data: { status: 'refused', reason: 'BAD_PACKAGE', executable: false } }),)
    const payloadBytes = new Uint8Array([42, 43, 44, 45])
    await useAppManagementStore.getState().installPackage(new File([payloadBytes], 'c.neapp'))
    const encoded = btoa(String.fromCharCode(...payloadBytes))
    const s = useAppManagementStore.getState()
    expect(s.lastError).toEqual({ scope: 'install', kind: 'refused', reason: 'BAD_PACKAGE' })
    expect(JSON.stringify(s)).not.toContain(encoded)
    renderPage()
    await openManageDialog()
    await waitFor(() => {
      expect(screen.getByTestId('last-error')).toBeTruthy()
    })
    expect((document.body.textContent || '')).not.toContain(encoded)
  })

  it('does not read the auth token in the app management service layer', async () => {
    const spy = vi.spyOn(window.localStorage, 'getItem')
    stubGetInstalled(installedState(false))
    await useAppManagementStore.getState().refresh()
    const tokenReads = spy.mock.calls.filter(([k]) => k === 'token')
    expect(tokenReads).toHaveLength(0)
  })

  it('shows relogin path on unauthorized and blocks operations instead of failing silently', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve({ success: false, error_code: 'UNAUTHORIZED', message: 'Authentication required' }),)
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('unauthorized')
    })
    const relogin = await screen.findByTestId('relogin-link')
    expect(relogin.getAttribute('href')).toBe('/login')
    expect(screen.getByTestId('auth-block-notice')).toBeTruthy()
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect(screen.queryByTestId('uninstall-row')).toBeNull()
    expect(screen.queryByTestId('neapp-input')).toBeNull()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(requestMock.post).not.toHaveBeenCalled()
  })
})
