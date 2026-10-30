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

const stopResponse = (overrides: Record<string, unknown> = {}) => envelopeOk({
    status: 'stopped',
    entry_returned: true,
    busy: false,
    reason: null,
    log: '',
    log_truncated: false,
    running: false,
    ...overrides,
  })

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('stop session store action', () => {
  it('posts the stop route with an empty body and records the stopped outcome plus the refreshed device state', async () => {
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(stopResponse()))
    await useAppManagementStore.getState().stopApp()
    expect(requestMock.post).toHaveBeenCalledTimes(1)
    expect(String(requestMock.post.mock.calls[0][0])).toBe('/api/v1/app/session/stop')
    expect(requestMock.post.mock.calls[0][1]).toEqual({})
    const s = useAppManagementStore.getState()
    expect(s.stopOutcome).toEqual({ status: 'stopped', entryReturned: true, busy: false, reason: null })
    expect(s.lastError).toBeNull()
    expect(s.stopping).toBe(false)
    expect(s.view?.kind).toBe('installed')
    expect(s.view?.kind === 'installed' && s.view.appRunning).toBe(false)
  })

  it('records the stopping intermediate verdict without inventing a stopped device state', async () => {
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    stubGetInstalled(installedState(true))
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(stopResponse({
      status: 'stopping',
      entry_returned: false,
      busy: true,
      running: true,
    })))
    await useAppManagementStore.getState().stopApp()
    const s = useAppManagementStore.getState()
    expect(s.stopOutcome).toEqual({ status: 'stopping', entryReturned: false, busy: true, reason: null })
    expect(s.view?.kind === 'installed' && s.view.appRunning).toBe(true)
  })

  it('ignores stop requests while another app operation is busy', async () => {
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    useAppManagementStore.setState({ running: true })
    await useAppManagementStore.getState().stopApp()
    useAppManagementStore.setState({ running: false, stopping: true })
    await useAppManagementStore.getState().stopApp()
    expect(requestMock.post).not.toHaveBeenCalled()
  })

  it('ignores overlapping stop requests while one is in flight', async () => {
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    stubGetInstalled(installedState(false))
    let releaseStop!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseStop = resolve
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/session/stop')) {
        return gate.then(() => Promise.resolve(stopResponse()))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    const first = useAppManagementStore.getState().stopApp()
    const second = useAppManagementStore.getState().stopApp()
    await Promise.resolve()
    expect(useAppManagementStore.getState().stopping).toBe(true)
    releaseStop()
    await Promise.all([first, second])
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('maps stop envelope failures to the stop scope without faking success', async () => {
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(envelopeFail('INVALID_REQUEST')))
    await useAppManagementStore.getState().stopApp()
    const s = useAppManagementStore.getState()
    expect(s.lastError).toEqual({ scope: 'stop', kind: 'envelope', reason: 'INVALID_REQUEST' })
    expect(s.stopOutcome).toBeNull()
  })

  it('maps an unauthorized stop failure to the auth error class', async () => {
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(envelopeFail('UNAUTHORIZED')))
    await useAppManagementStore.getState().stopApp()
    const s = useAppManagementStore.getState()
    expect(s.lastError).toEqual({ scope: 'stop', kind: 'auth', reason: 'unauthorized' })
    expect(s.stopOutcome).toBeNull()
  })
})

describe('stop control inside the manage dialog run section', () => {
  it('offers an enabled stop button next to the disabled run button while running, and the stopped device state unlocks uninstall', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await openManageDialog()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('runtime-state').textContent).toBe('Running')
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(true)
    const stop = screen.getByTestId('stop-button') as HTMLButtonElement
    expect(stop.disabled).toBe(false)
    expect(stop.textContent).toContain('Stop')
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(stopResponse()))
    fireEvent.click(stop)
    await waitFor(() => {
      expect(requestMock.post).toHaveBeenCalledTimes(1)
    })
    expect(String(requestMock.post.mock.calls[0][0])).toBe('/api/v1/app/session/stop')
    await waitFor(() => {
      expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
    })
    expect(screen.getByTestId('runtime-state').textContent).toBe('Idle')
    fireEvent.click(screen.getByTestId('uninstall-row'))
    await screen.findByTestId('confirm-uninstall')
    expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(false)
  })

  it('shows the stop control for a running app that has no single-run section', async () => {
    stubGetInstalled(installedStateFor('mystery-app', {}, true))
    renderPage()
    await openManageDialog()
    expect(screen.queryByTestId('run-section')).toBeNull()
    expect(screen.queryByTestId('run-button')).toBeNull()
    const stop = screen.getByTestId('stop-button') as HTMLButtonElement
    expect(stop.disabled).toBe(false)
    expect(screen.getByTestId('runtime-state').textContent).toBe('Running')
  })

  it('keeps update gated while running and unlocks it once the device reports idle after the stop', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await openManageDialog()
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(true)
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(stopResponse()))
    fireEvent.click(screen.getByTestId('stop-button'))
    await waitFor(() => {
      expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    })
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
  })

  it('does not render the stop button while the app is idle', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openManageDialog()
    expect(screen.queryByTestId('stop-button')).toBeNull()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(false)
  })

  it('shows the submitting label and disables the control while a stop request is in flight', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await openManageDialog()
    let releaseStop!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseStop = resolve
    })
    stubGetInstalled(installedState(false))
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/session/stop')) {
        return gate.then(() => Promise.resolve(stopResponse()))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    fireEvent.click(screen.getByTestId('stop-button'))
    await waitFor(() => {
      expect((screen.getByTestId('stop-button') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(screen.getByTestId('stop-button').textContent).toContain('Stopping')
    releaseStop()
    await waitFor(() => {
      expect(useAppManagementStore.getState().stopOutcome?.status).toBe('stopped')
    })
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('explains that the stop request is still settling when the device answers stopping', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await openManageDialog()
    stubGetInstalled(installedState(true))
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(stopResponse({
      status: 'stopping',
      entry_returned: false,
      busy: true,
      running: true,
    })))
    fireEvent.click(screen.getByTestId('stop-button'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().stopOutcome?.status).toBe('stopping')
    })
    expect(screen.getByTestId('stop-busy-note').textContent).toContain('still in progress')
  })

  it('shows the host failure class when the device refuses the stop request', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await openManageDialog()
    stubGetInstalled(installedState(true))
    stubPath('post', '/api/v1/app/session/stop', () => Promise.resolve(stopResponse({
      status: 'refused',
      entry_returned: false,
      busy: false,
      reason: 'BUSY',
      running: true,
    })))
    fireEvent.click(screen.getByTestId('stop-button'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().stopOutcome?.status).toBe('refused')
    })
    const note = screen.getByTestId('stop-refused-note').textContent || ''
    expect(note).toContain('Stop refused')
    expect(note).toMatch(/busy/i)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(true)
  })
})
