import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/preact'
import { I18nWrapper, setLocale } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import {
  requestMock,
  installedState,
  installedStateFor,
  noneState,
  unknownState,
  unavailableState,
  stubGetInstalled,
  setupSuite,
  teardownSuite,
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

const openSlotDialog = async (slotTestIds: string, contentTestId: string) => {
  fireEvent.click(screen.getByTestId(slotTestIds))
  await screen.findByTestId(contentTestId)
}

beforeEach(() => {
  setupSuite()
})

afterEach(() => {
  teardownSuite()
})

describe('device state drives the first slot and safe dialogs', () => {
  it('shows the manage slot for an installed app without inventing a business tab', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.queryByText('Line Counting')).toBeNull()
    expect(screen.getByTestId('no-business-notice')).toBeTruthy()
    await openSlotDialog('slot-manage', 'manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('hello-app')
    expect(screen.getByText('v1.2.3')).toBeTruthy()
    expect(screen.queryByText('pub-alpha')).toBeNull()
    expect(screen.queryByText('bb22')).toBeNull()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.getByTestId('runtime-state').textContent).toBe('Idle')
  })

  it('shows the app status panel for an unavailable app with retained identity and a recovery entry', async () => {
    stubGetInstalled(unavailableState('ABI_INCOMPATIBLE'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    expect(screen.getByTestId('slot-manage').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.getByTestId('unavailable-warning').textContent).toContain('ABI_INCOMPATIBLE')
    await openSlotDialog('slot-manage', 'status-reason')
    expect(screen.getByTestId('status-reason').textContent).toBe('ABI_INCOMPATIBLE')
    expect(screen.getByText('pub-alpha')).toBeTruthy()
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
    expect(requestMock.delete).not.toHaveBeenCalled()
  })

  it('blocks update and uninstall entries when an unavailable app is still reported running', async () => {
    stubGetInstalled(unavailableState('LOAD_FAILED', true))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    await openSlotDialog('slot-manage', 'status-reason')
    expect(screen.getByTestId('status-reason').textContent).toBe('LOAD_FAILED')
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('running-block-note')).toBeTruthy()
    expect(requestMock.delete).not.toHaveBeenCalled()
    expect(requestMock.post).not.toHaveBeenCalled()
  })

  it('shows the deploy slot only when the device proves absence', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.getByTestId('slot-deploy')).toBeTruthy()
    expect(screen.getByTestId('app-status-empty')).toBeTruthy()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
    await openSlotDialog('slot-deploy', 'deploy-confirm')
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect(screen.queryByTestId('uninstall-row')).toBeNull()
    expect((screen.getByTestId('deploy-confirm') as HTMLButtonElement).disabled).toBe(true)
  })

  it('maps state unknown to the app status panel, never to deploy, and blocks all dangerous actions', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.getByTestId('undetermined-warning')).toBeTruthy()
    expect(screen.getByText(/cannot confirm the app state/i)).toBeTruthy()
    expect(screen.queryByTestId('uninstall-row')).toBeNull()
    expect(screen.queryByTestId('update-row')).toBeNull()
    expect(screen.queryByTestId('neapp-input')).toBeNull()
    expect((screen.getByTestId('status-refresh') as HTMLButtonElement).disabled).toBe(false)
  })

  it('normalizes backend responses that carry status instead of state', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
  })

  it('stays in the app status panel when the state query itself fails at network level', async () => {
    requestMock.get.mockImplementation(() => Promise.reject(new Error('Network Error')))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(screen.getByTestId('undetermined-warning')).toBeTruthy()
    expect(screen.getByTestId('status-refresh')).toBeTruthy()
  })

  it('hides sensitive identity and shows the forbidden notice without a device verdict', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve(envelopeFail('FORBIDDEN', 'High-privilege administrator authentication required')),)
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('forbidden')
    })
    expect(useAppManagementStore.getState().view).toBeNull()
    expect(screen.queryByText('pub-alpha')).toBeNull()
    expect(screen.getByTestId('auth-block-notice')).toBeTruthy()
    expect(screen.getByText(/do not have high-privilege app management permission/i)).toBeTruthy()
  })

  it('hides sensitive identity and prompts relogin when unauthorized', async () => {
    requestMock.get.mockImplementation(() => Promise.resolve(envelopeFail('UNAUTHORIZED')))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('unauthorized')
    })
    expect(useAppManagementStore.getState().view).toBeNull()
    expect(screen.queryByText('pub-alpha')).toBeNull()
    const relogin = screen.getByTestId('relogin-link')
    expect(relogin.getAttribute('href')).toBe('/login')
  })
})

describe('observed app change invalidates a parked confirmation in both languages', () => {
  const parkUninstallThenSwapApp = async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    await openSlotDialog('slot-manage', 'manage-identity')
    fireEvent.click(screen.getByTestId('uninstall-row'))
    await screen.findByTestId('confirm-uninstall')
    stubGetInstalled(installedStateFor('other-app'))
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(requestMock.delete).not.toHaveBeenCalled()
    return screen.getByTestId('app-changed-note').textContent
  }

  it('explains the invalidated confirmation in English', async () => {
    expect(await parkUninstallThenSwapApp()).toBe(
      'The current app has changed since this confirmation was opened. Go back and confirm again.',
    )
  })

  it('explains the invalidated confirmation in Chinese', async () => {
    setLocale('zh')
    expect(await parkUninstallThenSwapApp()).toBe('当前应用已变化，请返回重新确认。')
  })
})
