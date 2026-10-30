import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent, cleanup } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import { businessUiCatalog } from '@/pages/applicationManagement/appSlot'
import { SidecarBroker } from '@/services/sidecarBridge'
import {
  requestMock,
  installedState,
  noneState,
  unknownState,
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

vi.mock('@/services/sidecarBridge', async (importOriginal) => {
  const actual = await importOriginal<typeof import('@/services/sidecarBridge')>()
  return {
    ...actual,
    SidecarBroker: vi.fn(),
  }
})

const activeProjection = {
  state: 'active' as const,
  publisher_id: 'cnt-publisher',
  app_id: 'hello-app',
  publisher_key_sha256: 'aa'.repeat(32),
  native_tbs_sha256: 'bb'.repeat(32),
  ui_tbs_sha256: 'cc'.repeat(32),
  version_major: 1,
  version_minor: 4,
  version_patch: 2,
  host_ui_abi: 1,
  sidecar_service_generation: '42',
  host_boot_generation: 7,
  native_session_generation: 3,
  ui_generation: 11,
  ui_base_path: '/appsite/g42/',
  entry_path: 'index.html',
  app_site_origin: 'https://app.ne301.example.com',
  file_count: 3,
}

const grantedEnvelope = () => envelopeOk({
  status: 'granted',
  grant: {
    grant_id: 'b'.repeat(64),
    expires_at_ms: 123456,
    issued_at_ms: 1000,
    capabilities: ['snapshot.read'],
    identity: {
      publisher_id: 'cnt-publisher',
      app_id: 'hello-app',
      publisher_key_sha256: 'dd'.repeat(32),
      native_tbs_sha256: 'ee'.repeat(32),
      ui_tbs_sha256: 'ff'.repeat(32),
      build_id: '1.0.0',
      host_boot_generation: 7,
      native_session_generation: 3,
      ui_generation: 11,
      sidecar_service_generation: 42,
    },
  },
})

const registerBusinessUi = () => {
  businessUiCatalog['hello-app'] = {
    labelKey: 'sys.app_management.sidecar.tab_label',
    render: () => <div data-testid="business-stub">business</div>,
  }
}

const stubInstalledWithSidecar = () => {
  requestMock.get.mockImplementation((url: string) => {
    if (String(url).includes('/api/v1/app/installed')) {
      return Promise.resolve(installedState(false))
    }
    if (String(url).includes('/api/v1/app/sidecar/ui')) {
      return Promise.resolve(envelopeOk(activeProjection))
    }
    return Promise.reject(new Error(`unexpected GET ${url}`))
  })
  stubPath('post', '/app/sidecar/bridge/grant', () => Promise.resolve(grantedEnvelope()))
}

const renderPage = () => render(
  <I18nWrapper>
    <ApplicationManagement />
  </I18nWrapper>,
)

const waitView = async (kind: string) => {
  await waitFor(() => {
    expect(useAppManagementStore.getState().view?.kind).toBe(kind)
  })
}

const tabLabels = () => {
  const tablist = screen.getByRole('tablist') as HTMLElement
  return Array.from(tablist.querySelectorAll('button')).map((b) => b.textContent || '')
}

const expectThreeFixedTabs = () => {
  expect(tabLabels()).toEqual(['App Status', 'MQTT/MQTTS', 'Webhook'])
  expect(screen.queryByTestId('slot-business')).toBeNull()
}

const mockedBroker = SidecarBroker as unknown as ReturnType<typeof vi.fn>

beforeEach(() => {
  setupSuite()
  useAppManagementStore.setState({ sidecar: { kind: 'unknown' } })
  mockedBroker.mockReset()
  mockedBroker.mockImplementation(
    (options: { onRevoke?: (r: string) => void }) => ({
      grant: { grant_id: 'b'.repeat(64) },
      video: {
        subscribe: () => () => undefined,
        markUnavailable: () => undefined,
        markReconnecting: () => undefined,
        markLive: () => undefined,
      },
      revoke: (reason: string) => options.onRevoke?.(reason),
      start: () => undefined,
      confirmDangerousOp: vi.fn(async () => true),
      isRevoked: () => false,
      pendingCount: () => 0,
    }),
  )
})

afterEach(() => {
  cleanup()
  teardownSuite()
  for (const key of Object.keys(businessUiCatalog)) {
    delete businessUiCatalog[key]
  }
})

describe('three fixed tabs across every slot kind', () => {
  it('keeps exactly the three tabs for a mocked not-installed device response', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitView('not_installed')
    expectThreeFixedTabs()
  })

  it('keeps exactly the three tabs for a mocked installed device response with a registered business UI', async () => {
    registerBusinessUi()
    stubGetInstalled(installedState(false))
    renderPage()
    await waitView('installed')
    expectThreeFixedTabs()
  })

  it('keeps exactly the three tabs for a mocked installed device response with an active trusted sidecar projection', async () => {
    stubInstalledWithSidecar()
    renderPage()
    await waitView('installed')
    await waitFor(() => {
      expect(useAppManagementStore.getState().sidecar.kind).toBe('active')
    })
    expectThreeFixedTabs()
  })
})

describe('installed business UI renders in place inside the App Status panel', () => {
  it('renders the registered business UI inside the app status panel with its manage entry and no duplicate status panel for a mocked installed response', async () => {
    registerBusinessUi()
    stubGetInstalled(installedState(false))
    renderPage()
    await waitView('installed')
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('business-stub'))).toBe(true)
    expect(screen.getByTestId('business-manage')).toBeTruthy()
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('business-manage'))).toBe(true)
    expect(screen.queryByTestId('app-status-installed')).toBeNull()
    expect(screen.queryByTestId('no-business-notice')).toBeNull()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
  })

  it('renders the trusted sidecar business UI inside the app status panel for a mocked installed and serviceable sidecar response', async () => {
    stubInstalledWithSidecar()
    renderPage()
    await waitView('installed')
    const frame = await screen.findByTestId('sidecar-frame')
    expect(screen.getByTestId('app-status-panel').contains(frame)).toBe(true)
    expect(screen.getByTestId('business-manage')).toBeTruthy()
    expect(screen.queryByTestId('app-status-installed')).toBeNull()
    expect(screen.queryByTestId('no-business-notice')).toBeNull()
    expect(screen.getByTestId('sidecar-frame')).toBeTruthy()
  })

  it('opens the existing management dialog from the in-place manage entry for a mocked installed response', async () => {
    registerBusinessUi()
    stubGetInstalled(installedState(false))
    renderPage()
    await waitView('installed')
    await screen.findByTestId('business-stub')
    fireEvent.click(screen.getByTestId('business-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('hello-app')
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
  })
})

describe('non-serviceable and exceptional states stay inside the App Status panel', () => {
  it('shows the querying placeholder while the mocked device query is still pending', async () => {
    requestMock.get.mockImplementation(() => new Promise(() => {}))
    renderPage()
    expect(screen.getByTestId('app-status-querying')).toBeTruthy()
    expect(screen.queryByTestId('business-stub')).toBeNull()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
  })

  it('shows the empty state for a mocked not-installed device response', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitView('not_installed')
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('app-status-empty'))).toBe(true)
    expect(screen.getByTestId('slot-deploy')).toBeTruthy()
  })

  it('shows the installed-without-business notice for a mocked installed response without any serviceable business entry', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitView('installed')
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('no-business-notice'))).toBe(true)
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
    expect(screen.queryByTestId('business-stub')).toBeNull()
    expect(screen.queryByTestId('business-manage')).toBeNull()
  })

  it('shows the undetermined warning for a mocked unknown-state response', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitView('undetermined')
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('undetermined-warning'))).toBe(true)
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.queryByTestId('business-stub')).toBeNull()
  })

  it('shows the auth-block notice and never masquerades a registered business UI as trusted for a mocked forbidden response', async () => {
    registerBusinessUi()
    stubGetInstalled(envelopeFail('FORBIDDEN'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('forbidden')
    })
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('auth-block-notice'))).toBe(true)
    expect(screen.queryByTestId('business-stub')).toBeNull()
    expect(screen.queryByTestId('app-status-empty')).toBeNull()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
  })

  it('drops the in-place business UI when a mocked background refresh reports the app removed', async () => {
    registerBusinessUi()
    stubGetInstalled(installedState(false))
    renderPage()
    await waitView('installed')
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    stubGetInstalled(noneState())
    await useAppManagementStore.getState().refresh()
    await waitView('not_installed')
    expect(screen.queryByTestId('business-stub')).toBeNull()
    expect(screen.queryByTestId('business-manage')).toBeNull()
    expect(screen.getByTestId('app-status-empty')).toBeTruthy()
    expect(screen.getByTestId('slot-deploy')).toBeTruthy()
  })
})

describe('grant failures keep the trusted error presentation', () => {
  it('keeps the revoked presentation without any iframe and without claiming not-installed when the mocked grant response is refused', async () => {
    stubInstalledWithSidecar()
    stubPath('post', '/app/sidecar/bridge/grant', () => Promise.resolve(envelopeFail('FORBIDDEN')))
    renderPage()
    await waitView('installed')
    expect(await screen.findByTestId('sidecar-revoked')).toBeTruthy()
    expect(screen.queryByTestId('sidecar-frame')).toBeNull()
    expect(screen.getByTestId('business-manage')).toBeTruthy()
    expect(screen.queryByTestId('app-status-empty')).toBeNull()
    expect(screen.queryByTestId('no-business-notice')).toBeNull()
  })
})

describe('background refresh keeps the user-selected tab', () => {
  it('keeps the MQTT tab selected when a mocked background refresh confirms the business UI', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitView('undetermined')
    fireEvent.mouseDown(screen.getByTestId('tab-mqtt'))
    expect(await screen.findByTestId('mqtt-module-stub')).toBeTruthy()
    registerBusinessUi()
    stubGetInstalled(installedState(false))
    await useAppManagementStore.getState().refresh()
    await waitView('installed')
    expect(screen.getByTestId('mqtt-module-stub')).toBeTruthy()
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('business-stub')).toBeNull()
  })

  it('keeps the Webhook tab selected when a mocked background refresh confirms the sidecar business UI', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitView('undetermined')
    fireEvent.mouseDown(screen.getByTestId('tab-webhook'))
    expect(await screen.findByTestId('webhook-module-stub')).toBeTruthy()
    stubInstalledWithSidecar()
    await useAppManagementStore.getState().refresh()
    await useAppManagementStore.getState().refreshSidecar()
    await waitView('installed')
    await waitFor(() => {
      expect(useAppManagementStore.getState().sidecar.kind).toBe('active')
    })
    expect(screen.getByTestId('webhook-module-stub')).toBeTruthy()
    expect(screen.queryByTestId('sidecar-status-strip')).toBeNull()
    expect(screen.queryByTestId('slot-business')).toBeNull()
  })
})
