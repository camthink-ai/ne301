import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import {
  requestMock,
  installedState,
  installedStateFor,
  stubGetInstalled,
  stubPath,
  setupSuite,
  teardownSuite,
  envelopeOk,
  envelopeFail,
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

const renderPage = () => render(
    <I18nWrapper>
      <ApplicationManagement />
    </I18nWrapper>,
  )

const openManageDialog = async () => {
  await waitFor(() => {
    expect(useAppManagementStore.getState().view?.kind).toBe('installed')
  })
  fireEvent.click(screen.getByTestId('slot-manage'))
  await screen.findByTestId('manage-identity')
}

const startResponse = (overrides: Record<string, unknown> = {}) => envelopeOk({
    status: 'started',
    reason: null,
    detail: null,
    publisher_id: 'pub-beta',
    app_id: 'mystery-app',
    abi_version: 1,
    granted_caps: '0x0',
    event_max: 8,
    running: true,
    ...overrides,
  })

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('start session store action', () => {
  it('posts the start route with an empty body and records the started outcome plus the refreshed device state', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    await useAppManagementStore.getState().refresh()
    stubGetInstalled(installedStateFor('mystery-app', {}, true))
    stubPath('post', '/api/v1/app/session/start', () => Promise.resolve(startResponse()))
    await useAppManagementStore.getState().startApp()
    expect(requestMock.post).toHaveBeenCalledTimes(1)
    expect(String(requestMock.post.mock.calls[0][0])).toBe('/api/v1/app/session/start')
    expect(requestMock.post.mock.calls[0][1]).toEqual({})
    const s = useAppManagementStore.getState()
    expect(s.startOutcome).toEqual({ status: 'started', reason: null, detail: null })
    expect(s.lastError).toBeNull()
    expect(s.starting).toBe(false)
    expect(s.view?.kind).toBe('installed')
    expect(s.view?.kind === 'installed' && s.view.appRunning).toBe(true)
  })

  it('keeps the refusal reason and detail for the UI without flipping the running state', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    await useAppManagementStore.getState().refresh()
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    stubPath('post', '/api/v1/app/session/start', () => Promise.resolve(startResponse({
      status: 'refused',
      reason: 'LOAD_FAILED',
      detail: 'entry link missing',
      running: false,
    })))
    await useAppManagementStore.getState().startApp()
    const s = useAppManagementStore.getState()
    expect(s.startOutcome).toEqual({
      status: 'refused',
      reason: 'LOAD_FAILED',
      detail: 'entry link missing',
    })
    expect(s.lastError).toBeNull()
    expect(s.view?.kind === 'installed' && s.view.appRunning).toBe(false)
  })

  it('ignores start requests while another app operation is busy', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    await useAppManagementStore.getState().refresh()
    useAppManagementStore.setState({ stopping: true })
    await useAppManagementStore.getState().startApp()
    useAppManagementStore.setState({ stopping: false, running: true })
    await useAppManagementStore.getState().startApp()
    useAppManagementStore.setState({ running: false, installing: true })
    await useAppManagementStore.getState().startApp()
    expect(requestMock.post).not.toHaveBeenCalled()
  })

  it('ignores overlapping start requests while one is in flight', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    await useAppManagementStore.getState().refresh()
    stubGetInstalled(installedStateFor('mystery-app', {}, true))
    let releaseStart!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseStart = resolve
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/session/start')) {
        return gate.then(() => Promise.resolve(startResponse()))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    const first = useAppManagementStore.getState().startApp()
    const second = useAppManagementStore.getState().startApp()
    await Promise.resolve()
    expect(useAppManagementStore.getState().starting).toBe(true)
    releaseStart()
    await Promise.all([first, second])
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('maps start envelope failures to the start scope without faking success', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    await useAppManagementStore.getState().refresh()
    stubPath('post', '/api/v1/app/session/start', () => Promise.resolve(envelopeFail('SERVICE_UNAVAILABLE')))
    await useAppManagementStore.getState().startApp()
    const s = useAppManagementStore.getState()
    expect(s.lastError).toEqual({ scope: 'start', kind: 'envelope', reason: 'SERVICE_UNAVAILABLE' })
    expect(s.startOutcome).toBeNull()
  })

  it('maps an unauthorized start failure to the auth error class', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    await useAppManagementStore.getState().refresh()
    stubPath('post', '/api/v1/app/session/start', () => Promise.resolve(envelopeFail('UNAUTHORIZED')))
    await useAppManagementStore.getState().startApp()
    const s = useAppManagementStore.getState()
    expect(s.lastError).toEqual({ scope: 'start', kind: 'auth', reason: 'unauthorized' })
    expect(s.startOutcome).toBeNull()
  })
})

describe('start control inside the manage dialog runtime section', () => {
  it('offers an enabled start button for an idle app without single-run eligibility, and the refreshed running state switches the badge', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    renderPage()
    await openManageDialog()
    expect(screen.queryByTestId('run-section')).toBeNull()
    expect(screen.getByTestId('runtime-state').textContent).toBe('Idle')
    const start = screen.getByTestId('start-button') as HTMLButtonElement
    expect(start.disabled).toBe(false)
    expect(start.textContent).toContain('Start')
    stubGetInstalled(installedStateFor('mystery-app', {}, true))
    stubPath('post', '/api/v1/app/session/start', () => Promise.resolve(startResponse()))
    fireEvent.click(start)
    await waitFor(() => {
      expect(requestMock.post).toHaveBeenCalledTimes(1)
    })
    expect(String(requestMock.post.mock.calls[0][0])).toBe('/api/v1/app/session/start')
    await waitFor(() => {
      expect(screen.getByTestId('runtime-state').textContent).toBe('Running')
    })
    expect(screen.queryByTestId('start-button')).toBeNull()
    expect((screen.getByTestId('stop-button') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(true)
  })

  it('shows the submitting label and disables the control while a start request is in flight', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    renderPage()
    await openManageDialog()
    let releaseStart!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseStart = resolve
    })
    stubGetInstalled(installedStateFor('mystery-app', {}, true))
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/session/start')) {
        return gate.then(() => Promise.resolve(startResponse()))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    fireEvent.click(screen.getByTestId('start-button'))
    await waitFor(() => {
      expect((screen.getByTestId('start-button') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(screen.getByTestId('start-button').textContent).toContain('Starting')
    releaseStart()
    await waitFor(() => {
      expect(useAppManagementStore.getState().startOutcome?.status).toBe('started')
    })
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('surfaces the host refusal reason and detail when the device refuses the start', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    renderPage()
    await openManageDialog()
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    stubPath('post', '/api/v1/app/session/start', () => Promise.resolve(startResponse({
      status: 'refused',
      reason: 'BUSY',
      detail: 'session already active',
      running: false,
    })))
    fireEvent.click(screen.getByTestId('start-button'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().startOutcome?.status).toBe('refused')
    })
    const note = screen.getByTestId('start-refused-note').textContent || ''
    expect(note).toContain('Start refused')
    expect(note).toMatch(/busy/i)
    expect(note).toContain('session already active')
    expect(screen.getByTestId('runtime-state').textContent).toBe('Idle')
  })

  it('does not render the start button for an app covered by the single-run section', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openManageDialog()
    expect(screen.queryByTestId('start-button')).toBeNull()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(false)
  })

  it('does not render the start button while the app is running', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, true))
    renderPage()
    await openManageDialog()
    expect(screen.queryByTestId('start-button')).toBeNull()
    expect((screen.getByTestId('stop-button') as HTMLButtonElement).disabled).toBe(false)
  })

  it('does not render the more app details block in the manage dialog', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, false))
    renderPage()
    await openManageDialog()
    expect(screen.queryByText('More app details')).toBeNull()
    expect(screen.queryByText('pub-beta')).toBeNull()
    expect(screen.queryByText('ff66')).toBeNull()
    expect(screen.queryByText('ee55')).toBeNull()
    expect(screen.getByTestId('manage-identity').textContent).toContain('mystery-app')
    expect(screen.getByTestId('manage-identity').textContent).toContain('v3.0.0')
  })
})
