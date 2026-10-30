import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent } from '@testing-library/preact'
import { I18nWrapper, setLocale } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import { businessUiCatalog } from '@/pages/applicationManagement/appSlot'
import {
  requestMock,
  installedState,
  noneState,
  stubGetInstalled,
  setupSuite,
  teardownSuite,
  envelopeOk,
  envelopeFail,
  selectNeappFile,
  makeNeappFile,
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

beforeEach(() => {
  setupSuite()
  useAppManagementStore.setState({ sidecar: { kind: 'unknown' } })
})

afterEach(() => {
  teardownSuite()
  for (const key of Object.keys(businessUiCatalog)) {
    delete businessUiCatalog[key]
  }
  setLocale('en')
})

describe('AC1 neutral empty state', () => {
  it('shows the finalized copy in English with no warning styling and no MQTT fields', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitView('not_installed')
    const empty = screen.getByTestId('app-status-empty')
    expect(empty.textContent).toContain('No app installed')
    expect(empty.textContent).toContain('Install a single-file .neapp package as a high-privilege administrator to add optional features.')
    expect(empty.textContent).toContain('Install App')
    expect(empty.className).not.toMatch(/red|amber/)
    expect(screen.getByTestId('app-status-panel').textContent).not.toContain('MQTT')
    expect(screen.queryByText('Deploy App')).toBeNull()
  })

  it('shows the finalized copy in Chinese', async () => {
    setLocale('zh')
    stubGetInstalled(noneState())
    renderPage()
    await waitView('not_installed')
    const empty = screen.getByTestId('app-status-empty')
    expect(empty.textContent).toContain('尚未安装应用')
    expect(empty.textContent).toContain('由高权限管理员安装单文件 .neapp 包以扩展设备功能。')
    expect(empty.textContent).toContain('安装应用')
    expect(empty.className).not.toMatch(/red|amber/)
  })

  it('opens only the existing single-layer deploy dialog and uploads nothing before explicit confirmation', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitView('not_installed')
    fireEvent.click(screen.getByTestId('slot-deploy'))
    await screen.findByTestId('deploy-confirm')
    expect(screen.getByTestId('neapp-input')).toBeTruthy()
    expect((screen.getByTestId('deploy-confirm') as HTMLButtonElement).disabled).toBe(true)
    expect(requestMock.post).not.toHaveBeenCalled()
    selectNeappFile(screen.getByTestId('neapp-input') as HTMLInputElement, makeNeappFile([1, 2]))
    await waitFor(() => {
      expect((screen.getByTestId('deploy-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
    expect(requestMock.post).not.toHaveBeenCalled()
  })
})

describe('AC2 panel isolation and focus retention', () => {
  it('keeps the manage entry inside the app status panel only when installed without a business UI', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitView('installed')
    expect(screen.getByTestId('no-business-notice')).toBeTruthy()
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
    fireEvent.mouseDown(screen.getByTestId('tab-mqtt'))
    expect(await screen.findByTestId('mqtt-module-stub')).toBeTruthy()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
    expect(screen.queryByText('Manage App')).toBeNull()
    expect(screen.queryByTestId('no-business-notice')).toBeNull()
    fireEvent.mouseDown(screen.getByTestId('tab-webhook'))
    expect(await screen.findByTestId('webhook-module-stub')).toBeTruthy()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
    expect(screen.queryByText('Manage App')).toBeNull()
    fireEvent.mouseDown(screen.getByTestId('tab-app-status'))
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
  })

  it('renders the trusted business UI in place inside the app status panel with a single manage entry', async () => {
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.sidecar.tab_label',
      render: () => <div data-testid="business-stub">business</div>,
    }
    stubGetInstalled(installedState(false))
    renderPage()
    await waitView('installed')
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('business-stub'))).toBe(true)
    expect(screen.getByTestId('business-manage')).toBeTruthy()
    expect(screen.queryByTestId('app-status-installed')).toBeNull()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
    expect(screen.queryByTestId('no-business-notice')).toBeNull()
    fireEvent.mouseDown(screen.getByTestId('tab-mqtt'))
    expect(await screen.findByTestId('mqtt-module-stub')).toBeTruthy()
    expect(screen.queryByText('Manage App')).toBeNull()
    expect(screen.queryByTestId('business-manage')).toBeNull()
    fireEvent.mouseDown(screen.getByTestId('tab-app-status'))
    expect(screen.getByTestId('business-manage')).toBeTruthy()
  })

  it('keeps the user-selected MQTT tab when a background refresh confirms the business UI', async () => {
    stubGetInstalled(envelopeOk({ state: 'unknown', reason: 'STORAGE_STATE_UNKNOWN', executable: false, app_running: false }))
    renderPage()
    await waitView('undetermined')
    fireEvent.mouseDown(screen.getByTestId('tab-mqtt'))
    expect(await screen.findByTestId('mqtt-module-stub')).toBeTruthy()
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.sidecar.tab_label',
      render: () => <div data-testid="business-stub">business</div>,
    }
    stubGetInstalled(installedState(false))
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('mqtt-module-stub')).toBeTruthy()
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('business-stub')).toBeNull()
  })

  it('lands on the in-place sidecar business UI inside the app status panel only after the device confirms an explicit install', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitView('not_installed')
    fireEvent.click(screen.getByTestId('slot-deploy'))
    await screen.findByTestId('deploy-confirm')
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return Promise.resolve(installedState(false))
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(envelopeOk(activeProjection))
      }
      return Promise.reject(new Error(`unexpected GET ${url}`))
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/install')) {
        return Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false }))
      }
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(envelopeFail('FORBIDDEN'))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    selectNeappFile(screen.getByTestId('neapp-input') as HTMLInputElement, makeNeappFile([1, 2]))
    await waitFor(() => {
      expect((screen.getByTestId('deploy-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
    fireEvent.click(screen.getByTestId('deploy-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().sidecar.kind).toBe('active')
    })
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('deploy-confirm')).toBeNull()
    const panel = await screen.findByTestId('app-status-panel')
    const embedded = await screen.findByTestId('sidecar-frame').catch(() => screen.findByTestId('sidecar-revoked'))
    expect(panel.contains(embedded)).toBe(true)
    expect(screen.getByTestId('business-manage')).toBeTruthy()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
  })
})

describe('AC4 distinct abnormal state copies', () => {
  it('shows the dedicated cleanup-pending copy with a recheck entry in English', async () => {
    stubGetInstalled(envelopeOk({ state: 'cleanup_pending', executable: false, app_running: false }))
    renderPage()
    await waitView('cleanup_pending')
    expect(screen.getByTestId('cleanup-pending-warning').textContent).toContain('Uninstall incomplete')
    expect((screen.getByTestId('status-refresh') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
  })

  it('shows the dedicated cleanup-pending copy in Chinese', async () => {
    setLocale('zh')
    stubGetInstalled(envelopeOk({ state: 'cleanup_pending', executable: false, app_running: false }))
    renderPage()
    await waitView('cleanup_pending')
    expect(screen.getByTestId('cleanup-pending-warning').textContent).toContain('卸载尚未完成')
    expect(screen.getByTestId('cleanup-pending-warning').textContent).toContain('重新查询')
  })

  it('shows the retry copy for undetermined state in Chinese with recheck', async () => {
    setLocale('zh')
    stubGetInstalled(envelopeOk({ state: 'unknown', reason: 'STORAGE_STATE_UNKNOWN', executable: false, app_running: false }))
    renderPage()
    await waitView('undetermined')
    expect(screen.getByTestId('undetermined-warning').textContent).toContain('无法确认应用状态，请重试。')
    expect(screen.getByTestId('status-refresh').textContent).toContain('重新查询')
  })

  it('hides app identity and shows the forbidden notice in Chinese without device verdict', async () => {
    setLocale('zh')
    requestMock.get.mockImplementation(() => Promise.resolve({ success: false, error_code: 'FORBIDDEN', message: 'refused' }),)
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().authBlock).toBe('forbidden')
    })
    expect(screen.getByTestId('auth-block-notice').textContent).toContain('当前凭据没有高权限应用管理权限')
    expect(screen.queryByText('pub-alpha')).toBeNull()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
  })
})
