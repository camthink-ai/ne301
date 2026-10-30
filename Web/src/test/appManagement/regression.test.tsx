import { describe, it, expect, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent, cleanup } from '@testing-library/preact'
import type { ComponentChildren } from 'preact'
import { I18nWrapper, setLocale } from '@/i18n'
import Menu from '@/layout/pc/menu'
import ApplicationManagement from '@/pages/applicationManagement'
import {
  businessUiCatalog,
  confirmedV1SingleRunPackageDigests,
  runOnceEligible,
} from '@/pages/applicationManagement/appSlot'
import { useAppManagementStore } from '@/store/appManagement'
import applicationManagementService from '@/services/api/applicationManagement'
import {
  installedState,
  installedStateFor,
  noneState,
  unavailableState,
  unknownState,
  stubGetInstalled,
  resetBusinessCatalog,
} from './helpers'

const helloV1PackageSha256 = 'f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8'
const helloV2PackageSha256 = '2a86a2982c042385850daac8962b3cef1d49f838e03a503032b0c6a5874a644f'
const helloV2AltPackageSha256 = 'c431baf86273a18554db312d804f27b46a0d83052c14241cf918507a0eef27c0'
const helloV2HiSigPackageSha256 = 'd803bfe64a59f39b3472ef6af1c29e85fa98a6634246460db3466e38fa641a02'
const helloBadAbiPackageSha256 = 'ea23ec9425cf19aafe6dedde29be72b7e49cd97a3c50a90593e5500e31aac1b5'

vi.mock('react-router-dom', async (importOriginal) => {
  const actual = await importOriginal<typeof import('react-router-dom')>()
  return {
    ...actual,
    MemoryRouter: ({ children }: { children?: ComponentChildren }) => children,
    Link: ({ to, children }: { to?: string; children?: ComponentChildren }) => (
      <a href={to}>{children}</a>
    ),
    useLocation: () => ({ pathname: '/' }),
  }
})

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
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

vi.mock('sonner', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() },
}))

const renderPage = () => render(
    <I18nWrapper>
      <ApplicationManagement />
    </I18nWrapper>,
  )

afterEach(() => {
  cleanup()
  resetBusinessCatalog()
  setLocale('en')
})

describe('existing non-App management features do not regress', () => {
  it('keeps the MQTT and Webhook service client methods intact', () => {
    expect(typeof applicationManagementService.getMqttConfigReq).toBe('function')
    expect(typeof applicationManagementService.setMqttConfigReq).toBe('function')
    expect(typeof applicationManagementService.connectMqttReq).toBe('function')
    expect(typeof applicationManagementService.disconnectMqttReq).toBe('function')
    expect(typeof applicationManagementService.getWebhookConfigReq).toBe('function')
    expect(typeof applicationManagementService.setWebhookConfigReq).toBe('function')
    expect(typeof applicationManagementService.testWebhookReq).toBe('function')
  })

  it('keeps the app status panel first, then MQTT/MQTTS and Webhook tabs, on the single page', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    const labels = Array.from(
      (screen.getByRole('tablist') as HTMLElement).querySelectorAll('button'),
    ).map((b) => b.textContent || '')
    expect(labels[0]).toBe('App Status')
    expect(labels.some((t) => t.includes('MQTT/MQTTS'))).toBe(true)
    expect(labels.some((t) => t.includes('Webhook'))).toBe(true)
    expect(labels.findIndex((t) => t.includes('MQTT/MQTTS'))).toBeLessThan(
      labels.findIndex((t) => t.includes('Webhook')),
    )
  })
})

describe('single entry and navigation contract', () => {
  it('keeps only the legacy application management entry in the top navigation', () => {
    render(
      <I18nWrapper>
        <Menu />
      </I18nWrapper>,
    )
    const links = Array.from(document.querySelectorAll('a'))
    const paths = links.map((a) => a.getAttribute('href'))
    expect(paths).toContain('/application-management')
    expect(paths).not.toContain('/app-management')
    expect(paths.join(' ')).not.toMatch(/trusted app/i)
  })

  it('registers no standalone trusted app route and keeps the old page module removed', async () => {
    const legacySpecifier = ['@/', 'pages/', 'appManagement'].join('')
    await expect(import(legacySpecifier)).rejects.toThrow()
    vi.resetModules()
    const pageMocks: [string, { default: () => null }][] = [
      ['@/pages/login', { default: () => null }],
      ['@/pages/deviceTool', { default: () => null }],
      ['@/pages/home', { default: () => null }],
      ['@/pages/notFound', { default: () => null }],
      ['@/pages/modelVerification', { default: () => null }],
      ['@/pages/applicationManagement', { default: () => null }],
      ['@/pages/hardwareManagement', { default: () => null }],
      ['@/pages/captureSettings', { default: () => null }],
      ['@/pages/systemSettings', { default: () => null }],
      ['@/pages/storageManagement', { default: () => null }],
      ['@/pages/deviceInformation', { default: () => null }],
      ['@/pages/ImportFSBL', { default: () => null }],
      ['@/pages/ImportWifi', { default: () => null }],
      ['@/pages/ImportBundle', { default: () => null }],
      ['@/pages/UpgradeWaiting', { default: () => null }],
      ['@/layout', { default: () => null }],
    ]
    for (const [path, mock] of pageMocks) {
      vi.doMock(path, () => mock)
    }
    const mod = await import('@/router')
    const router = mod.default as unknown as { routes: Array<Record<string, unknown>> }
    const paths: string[] = []
    const walk = (rs: Array<Record<string, unknown>>) => {
      for (const r of rs) {
        if (typeof r.path === 'string') paths.push(r.path)
        if (Array.isArray(r.children)) walk(r.children as Array<Record<string, unknown>>)
      }
    }
    walk(router.routes || [])
    expect(paths).toContain('/application-management')
    expect(paths).not.toContain('/app-management')
  })
})

describe('four-state first slot inside the application management page', () => {
  it('shows the deploy action for a device-confirmed empty device and keeps MQTT/Webhook usable', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.getByTestId('slot-deploy').tagName).toBe('BUTTON')
    expect(screen.getByTestId('app-status-empty')).toBeTruthy()
    expect(screen.queryByTestId('slot-business')).toBeNull()
    fireEvent.click(screen.getByTestId('slot-deploy'))
    expect(await screen.findByTestId('deploy-confirm')).toBeTruthy()
    fireEvent.click(screen.getByText('Cancel'))
    await waitFor(() => {
      expect(screen.queryByTestId('deploy-confirm')).toBeNull()
    })
    fireEvent.mouseDown(screen.getByTestId('tab-mqtt'))
    expect(await screen.findByTestId('mqtt-module-stub')).toBeTruthy()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    fireEvent.mouseDown(screen.getByTestId('tab-webhook'))
    expect(await screen.findByTestId('webhook-module-stub')).toBeTruthy()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
  })

  it('shows the manage action for an installed app without a business UI and never fabricates one', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('slot-manage').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByText('Line Counting')).toBeNull()
    expect(screen.queryByText('过线统计')).toBeNull()
    expect(screen.getByTestId('no-business-notice')).toBeTruthy()
  })

  it('keeps update and uninstall operable for an installed idle app without any gating notice', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.queryByTestId('running-block-note')).toBeNull()
    fireEvent.click(screen.getByTestId('update-row'))
    await screen.findByTestId('update-confirm')
    expect(screen.queryByTestId('running-block-note')).toBeNull()
    expect(screen.queryByTestId('state-changed-note')).toBeNull()
  })

  it('shows the app status panel with a recovery entry for an unavailable app', async () => {
    stubGetInstalled(unavailableState('LEGACY_SIGNED_STATE_PRESERVED'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    expect(screen.getByTestId('unavailable-warning')).toBeTruthy()
    expect(screen.getByTestId('slot-manage').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
  })

  it('shows the app status panel for an unknown state and never claims not installed', async () => {
    stubGetInstalled(unknownState('STORAGE_STATE_UNKNOWN'))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('undetermined')
    })
    expect(screen.getByTestId('undetermined-warning')).toBeTruthy()
    expect(screen.getByTestId('status-refresh').tagName).toBe('BUTTON')
    expect(screen.queryByTestId('slot-deploy')).toBeNull()
    expect(screen.queryByTestId('slot-business')).toBeNull()
  })

  it('renders the registered business UI in place inside the app status panel only', async () => {
    expect(businessUiCatalog['hello-app']).toBeUndefined()
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('business-stub')).toBeNull()
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
    cleanup()
    resetBusinessCatalog()
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => <div data-testid="business-stub">business</div>,
    }
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('business-stub'))).toBe(true)
    expect(screen.getByTestId('business-manage')).toBeTruthy()
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('slot-manage')).toBeNull()
  })

  it('drops the in-place business UI when a background refresh reports the app removed', async () => {
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => <div data-testid="business-stub">business</div>,
    }
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    expect(screen.queryByTestId('mqtt-module-stub')).toBeNull()
    stubGetInstalled(noneState())
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByTestId('business-stub')).toBeNull()
    expect(await screen.findByTestId('app-status-empty')).toBeTruthy()
    expect(screen.getByTestId('slot-deploy')).toBeTruthy()
    expect(screen.queryByTestId('mqtt-module-stub')).toBeNull()
  })
})

describe('run-once eligibility gate inside the manage dialog', () => {
  it('suppresses run once when a real business UI is registered for the committed app', async () => {
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => <div data-testid="business-stub">business</div>,
    }
    expect(runOnceEligible('hello-app', helloV1PackageSha256)).toBe(false)
    stubGetInstalled(installedState(false))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    fireEvent.click(screen.getByTestId('business-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('hello-app')
    expect(screen.queryByTestId('run-section')).toBeNull()
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
  })

  it.each([
    ['hello-app 1.0.0', helloV1PackageSha256, 1],
    ['hello-app 2.0.0 after a v1 ABI update', helloV2PackageSha256, 2],
    ['accepted high-S v1 ABI package', helloV2HiSigPackageSha256, 2],
  ])('offers run once for %s when no business UI is registered', async (_kind, packageSha256, versionMajor) => {
    expect(businessUiCatalog['hello-app']).toBeUndefined()
    expect(confirmedV1SingleRunPackageDigests.has(packageSha256)).toBe(true)
    expect(runOnceEligible('hello-app', packageSha256)).toBe(true)
    stubGetInstalled(installedStateFor('hello-app', {
      package_sha256: packageSha256,
      version_major: versionMajor,
      version_minor: 0,
      version_patch: 0,
    }))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('run-section')).toBeTruthy()
    expect(screen.getByTestId('run-section').textContent).toContain('Run once')
    expect(screen.getByTestId('run-button').textContent).toContain('Run once')
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(false)
  })

  it('shows the v1 ABI Run Once copy in Chinese for the 2.0.0 package', async () => {
    setLocale('zh')
    stubGetInstalled(installedStateFor('hello-app', {
      package_sha256: helloV2PackageSha256,
      version_major: 2,
      version_minor: 0,
      version_patch: 0,
    }))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('run-section').textContent).toContain('运行一次')
    expect(screen.getByTestId('run-button').textContent).toContain('运行一次')
  })

  it.each([
    ['installer-refused same-version alternate fixture', helloV2AltPackageSha256],
    ['ABI-mismatch fixture', helloBadAbiPackageSha256],
    ['unknown', '0000000000000000000000000000000000000000000000000000000000000000'],
  ])('does not offer Run Once for hello-app with a %s package digest', async (_kind, packageSha256) => {
    expect(runOnceEligible('hello-app', packageSha256)).toBe(false)
    stubGetInstalled(installedStateFor('hello-app', { package_sha256: packageSha256 }))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.queryByTestId('run-section')).toBeNull()
    expect(screen.queryByTestId('run-button')).toBeNull()
  })

  it('offers no run once for an installed app whose v1 profile the state protocol cannot prove', async () => {
    expect(businessUiCatalog['mystery-app']).toBeUndefined()
    expect(confirmedV1SingleRunPackageDigests.has('ee55')).toBe(false)
    expect(runOnceEligible('mystery-app', 'ee55')).toBe(false)
    stubGetInstalled(installedStateFor('mystery-app', { version_major: 1, version_minor: 0, version_patch: 0 }))
    renderPage()
    await waitFor(() => {
      const { view } = useAppManagementStore.getState()
      expect(view?.kind === 'installed' && view.record.appId).toBe('mystery-app')
    })
    expect(screen.getByTestId('slot-manage')).toBeTruthy()
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('mystery-app')
    expect(screen.queryByTestId('run-section')).toBeNull()
    expect(screen.queryByTestId('run-button')).toBeNull()
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(false)
  })
})

describe('consumed server API contract', () => {
  it('matches the four high-privilege app routes with their documented methods', async () => {
    const mod = await import('@/services/api/appManagement')
    expect(mod.APP_PACKAGE_MAX_BYTES).toBe(4211176)
    expect(typeof mod.default.getInstalledState).toBe('function')
    expect(typeof mod.default.installPackage).toBe('function')
    expect(typeof mod.default.uninstallApp).toBe('function')
    expect(typeof mod.default.runApp).toBe('function')
  })
})
