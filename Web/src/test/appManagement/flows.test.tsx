import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { render, screen, waitFor, fireEvent, cleanup } from '@testing-library/preact'
import { I18nWrapper } from '@/i18n'
import ApplicationManagement from '@/pages/applicationManagement'
import { useAppManagementStore } from '@/store/appManagement'
import { businessUiCatalog } from '@/pages/applicationManagement/appSlot'
import {
  requestMock,
  installedState,
  installedStateFor,
  installedStateRevision,
  noneState,
  unavailableState,
  stubGetInstalled,
  stubPath,
  setupSuite,
  teardownSuite,
  envelopeOk,
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

const renderPage = () => render(
    <I18nWrapper>
      <ApplicationManagement />
    </I18nWrapper>,
  )

const openDeployDialog = async () => {
  await waitFor(() => {
    expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
  })
  fireEvent.click(screen.getByTestId('slot-deploy'))
  await screen.findByTestId('deploy-confirm')
}

const selectFile = async (bytes = [1, 2, 3, 4]) => {
  const input = screen.getByTestId('neapp-input') as HTMLInputElement
  const file = makeNeappFile(bytes, 'hello-v2.neapp')
  selectNeappFile(input, file)
  await waitFor(() => {
    expect(screen.getByTestId('package-file')).toBeTruthy()
    expect((screen.getByTestId('deploy-confirm') as HTMLButtonElement).disabled).toBe(false)
  })
  return file
}

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

describe('deploy flow inside the single dialog', () => {
  it('sends the raw .neapp body and switches to manage after device-confirmed install', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await openDeployDialog()
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(
        envelopeOk({
          status: 'installed',
          publisher_id: 'pub-alpha',
          app_id: 'hello-app',
          version_major: 1,
          version_minor: 2,
          version_patch: 3,
          package_len: 4,
          package_sha256: 'aa',
          tbs_sha256: 'bb',
          executable: false,
        }),
      ),)
    await selectFile([77, 88])
    fireEvent.click(screen.getByTestId('deploy-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().installOutcome).not.toBeNull()
    })
    expect(requestMock.post).toHaveBeenCalledTimes(1)
    const [url, body, config] = requestMock.post.mock.calls[0]
    expect(String(url)).toBe('/api/v1/app/install')
    expect(body).toBeInstanceOf(File)
    expect((body as File).size).toBe(2)
    expect((config as { headers: Record<string, string> }).headers['Content-Type'])
      .toBe('application/octet-stream')
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('hello-app')
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    })
    expect(screen.getByTestId('install-outcome').textContent).toContain('installed')
  })

  it('never fabricates a business tab when the platform has no business UI for the app', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await openDeployDialog()
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false })),)
    await selectFile()
    fireEvent.click(screen.getByTestId('deploy-confirm'))
    await screen.findByTestId('manage-identity')
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.queryByText('Line Counting')).toBeNull()
  })

  it('shows the registered business UI in place after a device-confirmed install without a fourth tab', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await openDeployDialog()
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => <div data-testid="business-stub">business</div>,
    }
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false })),)
    await selectFile()
    fireEvent.click(screen.getByTestId('deploy-confirm'))
    expect(await screen.findByTestId('business-stub')).toBeTruthy()
    expect(screen.getByTestId('app-status-panel').contains(screen.getByTestId('business-stub'))).toBe(true)
    expect(screen.queryByTestId('slot-business')).toBeNull()
    expect(screen.getByTestId('business-manage')).toBeTruthy()
    await waitFor(() => {
      expect(screen.queryByTestId('deploy-confirm')).toBeNull()
    })
  })

  it('treats an upload without server confirmation as unknown and never claims success', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await openDeployDialog()
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/install')) {
        return Promise.reject(new Error('timeout of 20000ms exceeded'))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    await selectFile()
    fireEvent.click(screen.getByTestId('deploy-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().lastError?.kind).toBe('indeterminate')
    })
    expect(useAppManagementStore.getState().pendingVerification).toBe(false)
    expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    expect(useAppManagementStore.getState().installOutcome).toBeNull()
    expect(screen.queryByTestId('install-outcome')).toBeNull()
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('shows the device text for verify and content-conflict refusals in the deploy dialog', async () => {
    const cases = [
      ['CONTENT_CONFLICT', 'content of the committed app'],
      ['NATIVE_VERIFY_FAILED', 'native part of the package failed verification'],
      ['UI_VERIFY_FAILED', 'UI part of the package failed verification'],
    ] as const
    for (const [reason, fragment] of cases) {
      setupSuite()
      stubGetInstalled(noneState())
      renderPage()
      await openDeployDialog()
      stubPath('post', '/api/v1/app/install', () => Promise.resolve(
          envelopeOk({ status: 'refused', reason, executable: false }),
        ),)
      await selectFile()
      fireEvent.click(screen.getByTestId('deploy-confirm'))
      const notice = await screen.findByTestId('last-error')
      expect(notice.textContent).toContain(fragment)
      expect(notice.textContent).not.toContain('unrecognized failure')
      cleanup()
    }
  })

  it('keeps the pending verification notice while the verifying query cannot complete', async () => {
    let releaseQuery!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseQuery = resolve
    })
    let call = 0
    requestMock.get.mockImplementation((url: string) => {
      call += 1
      if (String(url).includes('/api/v1/app/installed')) {
        if (call === 1) {
          return Promise.resolve(noneState())
        }
        return new Promise((resolve) => {
          gate.then(() => resolve(noneState()))
        })
      }
      return Promise.reject(new Error(`unexpected GET ${url}`))
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/install')) {
        return Promise.reject(new Error('timeout of 20000ms exceeded'))
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    renderPage()
    await openDeployDialog()
    await selectFile()
    fireEvent.click(screen.getByTestId('deploy-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().pendingVerification).toBe(true)
    })
    expect(useAppManagementStore.getState().installOutcome).toBeNull()
    expect(await screen.findByTestId('pending-verification')).toBeTruthy()
    releaseQuery()
  })

  it('rejects oversized files locally without any device write', async () => {
    stubGetInstalled(noneState())
    renderPage()
    await openDeployDialog()
    const big = makeNeappFile([], 'big.neapp')
    Object.defineProperty(big, 'size', { value: 4211177 })
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, big)
    await waitFor(() => {
      expect(screen.getByTestId('package-file')).toBeTruthy()
    })
    fireEvent.click(screen.getByTestId('deploy-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().lastError?.kind).toBe('file')
    })
    expect(requestMock.post).not.toHaveBeenCalled()
  })
})

describe('update flow stays inside the same dialog', () => {
  it('expands the update picker in place and returns to manage after device-confirmed update', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openManageDialog()
    fireEvent.click(screen.getByTestId('update-row'))
    await screen.findByTestId('update-confirm')
    expect(screen.getByTestId('back-to-manage')).toBeTruthy()
    stubGetInstalled(
      envelopeOk({
        status: 'installed',
        publisher_id: 'pub-alpha',
        app_id: 'hello-app',
        version_major: 2,
        version_minor: 0,
        version_patch: 0,
        package_len: 2048,
        package_sha256: 'ff99',
        tbs_sha256: 'ff88',
        ui: { status: 'active' },
        executable: false,
        app_running: false,
      }),
    )
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false })),)
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, makeNeappFile([9, 9], 'hello-v2.neapp'))
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
    fireEvent.click(screen.getByTestId('update-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().installOutcome).not.toBeNull()
    })
    await screen.findByTestId('manage-identity')
    expect(screen.queryByTestId('update-confirm')).toBeNull()
    expect(screen.getByTestId('manage-app-id').textContent).toBe('hello-app')
  })
})

describe('uninstall flow', () => {
  it('requires explicit in-dialog confirmation before DELETE is sent', async () => {
    stubGetInstalled(installedState(false))
    stubPath('delete', '/api/v1/app/installed', () => Promise.resolve(
        envelopeOk({
          status: 'uninstalled',
          publisher_id: 'pub-alpha',
          app_id: 'hello-app',
          version_major: 1,
          version_minor: 2,
          version_patch: 3,
          package_len: 1024,
          package_sha256: 'aa11',
          tbs_sha256: 'bb22',
          executable: false,
        }),
      ),)
    renderPage()
    await openManageDialog()
    fireEvent.click(screen.getByTestId('uninstall-row'))
    expect(await screen.findByText('Uninstall the current app?')).toBeTruthy()
    expect(requestMock.delete).not.toHaveBeenCalled()
    fireEvent.click(screen.getByTestId('back-to-manage'))
    await screen.findByTestId('manage-identity')
    expect(requestMock.delete).not.toHaveBeenCalled()
    fireEvent.click(screen.getByTestId('uninstall-row'))
    await screen.findByTestId('confirm-uninstall')
    stubGetInstalled(noneState())
    fireEvent.click(screen.getByTestId('confirm-uninstall'))
    await waitFor(() => {
      expect(requestMock.delete).toHaveBeenCalledTimes(1)
    })
    expect(String(requestMock.delete.mock.calls[0][0])).toBe('/api/v1/app/installed')
    expect((requestMock.delete.mock.calls[0] as unknown[])[1]).toMatchObject({
      data: { confirm_delete_data: true },
    })
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect(screen.queryByTestId('confirm-uninstall')).toBeNull()
    expect(screen.getByTestId('slot-deploy')).toBeTruthy()
  })

  it('keeps the committed app visible when uninstall is refused BUSY', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openManageDialog()
    stubPath('delete', '/api/v1/app/installed', () => Promise.resolve({ success: true, data: { status: 'refused', reason: 'BUSY', executable: false } }),)
    await useAppManagementStore.getState().uninstallApp()
    const s = useAppManagementStore.getState()
    expect(s.uninstallOutcome).toBeNull()
    expect(s.lastError).toEqual({ scope: 'uninstall', kind: 'refused', reason: 'BUSY' })
    expect(s.view?.kind).toBe('installed')
  })
})

describe('running-app gating for dangerous operations', () => {
  const openUpdateDialog = async () => {
    await openManageDialog()
    fireEvent.click(screen.getByTestId('update-row'))
    await screen.findByTestId('update-confirm')
  }

  it('disables update and uninstall entries with a stated reason while the installed app is running', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await openManageDialog()
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('running-block-note')).toBeTruthy()
    expect(requestMock.post).not.toHaveBeenCalled()
    expect(requestMock.delete).not.toHaveBeenCalled()
  })

  it('disables update and uninstall entries while an unavailable app is reported running', async () => {
    stubGetInstalled(unavailableState('LOAD_FAILED', true))
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('status-reason')
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(true)
    expect((screen.getByTestId('uninstall-row') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('running-block-note')).toBeTruthy()
  })

  it('fails the parked update confirmation closed when the app starts running', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openUpdateDialog()
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false })),)
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, makeNeappFile([1], 'hello-v2.neapp'))
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(screen.getByTestId('running-block-note')).toBeTruthy()
    fireEvent.click(screen.getByTestId('update-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().installing).toBe(false)
    })
    expect(requestMock.post).not.toHaveBeenCalled()
  })

  it('fails the parked update confirmation closed when the committed app disappears', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openUpdateDialog()
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false })),)
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, makeNeappFile([1], 'hello-v2.neapp'))
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
    stubGetInstalled(noneState())
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('state-changed-note')).toBeTruthy()
    fireEvent.click(screen.getByTestId('update-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().installing).toBe(false)
    })
    expect(requestMock.post).not.toHaveBeenCalled()
  })

  it('lets the user re-confirm an update against the new unavailable state after a failed-closed reject', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openUpdateDialog()
    stubGetInstalled(unavailableState('LOAD_FAILED'))
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('state-changed-note')).toBeTruthy()
    fireEvent.click(screen.getByTestId('back-to-manage'))
    await screen.findByTestId('manage-identity')
    expect((screen.getByTestId('update-row') as HTMLButtonElement).disabled).toBe(false)
    fireEvent.click(screen.getByTestId('update-row'))
    await screen.findByTestId('update-confirm')
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false })),)
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, makeNeappFile([1], 'hello-v2.neapp'))
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
    expect(screen.queryByTestId('state-changed-note')).toBeNull()
  })

  it('fails the uninstall confirmation closed when the app starts running or the record disappears', async () => {
    stubGetInstalled(installedState(false))
    stubPath('delete', '/api/v1/app/installed', () => Promise.resolve(envelopeOk({ status: 'uninstalled', executable: false })),)
    renderPage()
    await openManageDialog()
    fireEvent.click(screen.getByTestId('uninstall-row'))
    await screen.findByTestId('confirm-uninstall')
    expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(false)
    stubGetInstalled(installedState(true))
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(screen.getByTestId('running-block-note')).toBeTruthy()
    stubGetInstalled(noneState())
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed')
    })
    expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('state-changed-note')).toBeTruthy()
    fireEvent.click(screen.getByTestId('confirm-uninstall'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().uninstalling).toBe(false)
    })
    expect(requestMock.delete).not.toHaveBeenCalled()
  })
})

describe('confirmation target invalidation on observed identity or revision change', () => {
  const openUninstallConfirm = async () => {
    await openManageDialog()
    fireEvent.click(screen.getByTestId('uninstall-row'))
    await screen.findByTestId('confirm-uninstall')
    expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(false)
  }

  const openUpdateConfirmWithFile = async () => {
    await openManageDialog()
    fireEvent.click(screen.getByTestId('update-row'))
    await screen.findByTestId('update-confirm')
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, makeNeappFile([1], 'hello-v2.neapp'))
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
  }

  const expectAppChangedOnly = () => {
    expect(screen.getByTestId('app-changed-note')).toBeTruthy()
    expect(screen.queryByTestId('state-changed-note')).toBeNull()
    expect(screen.queryByTestId('running-block-note')).toBeNull()
  }

  it('fails the parked uninstall confirmation closed when the same installed state now holds a different app', async () => {
    stubGetInstalled(installedState(false))
    stubPath('delete', '/api/v1/app/installed', () => Promise.resolve(envelopeOk({ status: 'uninstalled', executable: false })),)
    renderPage()
    await openUninstallConfirm()
    stubGetInstalled(installedStateFor('other-app'))
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    expectAppChangedOnly()
    fireEvent.click(screen.getByTestId('confirm-uninstall'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().uninstalling).toBe(false)
    })
    expect(requestMock.delete).not.toHaveBeenCalled()
    fireEvent.click(screen.getByTestId('back-to-manage'))
    await screen.findByTestId('manage-identity')
    expect(screen.getByTestId('manage-app-id').textContent).toBe('other-app')
    fireEvent.click(screen.getByTestId('uninstall-row'))
    await screen.findByTestId('confirm-uninstall')
    expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.queryByTestId('app-changed-note')).toBeNull()
    stubGetInstalled(noneState())
    fireEvent.click(screen.getByTestId('confirm-uninstall'))
    await waitFor(() => {
      expect(requestMock.delete).toHaveBeenCalledTimes(1)
    })
  })

  it('fails the parked uninstall confirmation closed when the same app reports a different committed revision', async () => {
    stubGetInstalled(installedState(false))
    stubPath('delete', '/api/v1/app/installed', () => Promise.resolve(envelopeOk({ status: 'uninstalled', executable: false })),)
    renderPage()
    await openUninstallConfirm()
    stubGetInstalled(installedStateRevision())
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    expectAppChangedOnly()
    fireEvent.click(screen.getByTestId('confirm-uninstall'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().uninstalling).toBe(false)
    })
    expect(requestMock.delete).not.toHaveBeenCalled()
  })

  it('fails the parked update confirmation closed when the same installed state now holds a different app', async () => {
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'other-app', executable: false })),)
    renderPage()
    await openUpdateConfirmWithFile()
    stubGetInstalled(installedStateFor('other-app'))
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    expectAppChangedOnly()
    fireEvent.click(screen.getByTestId('update-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().installing).toBe(false)
    })
    expect(requestMock.post).not.toHaveBeenCalled()
  })

  it('fails the parked update confirmation closed when the same app reports a different committed revision', async () => {
    stubGetInstalled(installedState(false))
    stubPath('post', '/api/v1/app/install', () => Promise.resolve(envelopeOk({ status: 'installed', app_id: 'hello-app', executable: false })),)
    renderPage()
    await openUpdateConfirmWithFile()
    stubGetInstalled(installedStateRevision())
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(useAppManagementStore.getState().view?.kind).toBe('installed')
    expectAppChangedOnly()
    fireEvent.click(screen.getByTestId('update-confirm'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().installing).toBe(false)
    })
    expect(requestMock.post).not.toHaveBeenCalled()
  })

  it('fails the parked uninstall confirmation closed when an unavailable record is replaced by a different unavailable app', async () => {
    stubGetInstalled(unavailableState('LEGACY_SIGNED_STATE_PRESERVED'))
    stubPath('delete', '/api/v1/app/installed', () => Promise.resolve(envelopeOk({ status: 'uninstalled', executable: false })),)
    renderPage()
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    })
    fireEvent.click(screen.getByTestId('slot-manage'))
    await screen.findByTestId('status-reason')
    fireEvent.click(screen.getByTestId('uninstall-row'))
    await screen.findByTestId('confirm-uninstall')
    expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(false)
    stubGetInstalled(
      envelopeOk({
        status: 'unavailable',
        publisher_id: 'pub-gamma',
        app_id: 'other-app',
        version_major: 2,
        version_minor: 1,
        version_patch: 0,
        package_len: 777,
        package_sha256: '1234',
        tbs_sha256: '5678',
        reason: 'LEGACY_SIGNED_STATE_PRESERVED',
        executable: false,
        app_running: false,
      }),
    )
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(true)
    })
    expect(useAppManagementStore.getState().view?.kind).toBe('unavailable')
    expectAppChangedOnly()
    fireEvent.click(screen.getByTestId('confirm-uninstall'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().uninstalling).toBe(false)
    })
    expect(requestMock.delete).not.toHaveBeenCalled()
  })

  it('keeps parked confirmations usable when a refresh reports the identical committed app', async () => {
    stubGetInstalled(installedState(false))
    stubPath('delete', '/api/v1/app/installed', () => Promise.resolve(envelopeOk({ status: 'uninstalled', executable: false })),)
    renderPage()
    await openUninstallConfirm()
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().querying).toBe(false)
    })
    expect((screen.getByTestId('confirm-uninstall') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.queryByTestId('app-changed-note')).toBeNull()
    expect(screen.queryByTestId('state-changed-note')).toBeNull()
    fireEvent.click(screen.getByTestId('back-to-manage'))
    await screen.findByTestId('manage-identity')
    fireEvent.click(screen.getByTestId('update-row'))
    await screen.findByTestId('update-confirm')
    const input = screen.getByTestId('neapp-input') as HTMLInputElement
    selectNeappFile(input, makeNeappFile([1], 'hello-v2.neapp'))
    await waitFor(() => {
      expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(false)
    })
    await useAppManagementStore.getState().refresh()
    await waitFor(() => {
      expect(useAppManagementStore.getState().querying).toBe(false)
    })
    expect((screen.getByTestId('update-confirm') as HTMLButtonElement).disabled).toBe(false)
    expect(screen.queryByTestId('app-changed-note')).toBeNull()
  })
})

describe('manual single run flow inside the manage dialog', () => {
  it('shows the app entry result and session-bounded nature after a completed run', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openManageDialog()
    stubPath('post', '/api/v1/app/run', () => Promise.resolve(
        envelopeOk({
          status: 'completed',
          publisher_id: 'pub-alpha',
          app_id: 'hello-app',
          version_major: 1,
          version_minor: 2,
          version_patch: 3,
          package_len: 1024,
          package_sha256: 'aa11',
          tbs_sha256: 'bb22',
          executable: false,
          entry_returned: true,
          entry_result: 0,
          log: 'hello from app',
          log_truncated: false,
          app_running: false,
        }),
      ),)
    fireEvent.click(screen.getByTestId('run-button'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().runOutcome?.status).toBe('completed')
    })
    expect((screen.getByTestId('run-outcome') as HTMLElement).textContent).toContain('0')
    expect((screen.getByTestId('run-outcome') as HTMLElement).textContent).toContain('hello from app')
    expect((screen.getByTestId('run-outcome') as HTMLElement).textContent).toContain('browser session')
  })

  it('disables run while the device reports app_running and offers the stop control instead', async () => {
    stubGetInstalled(installedState(true))
    renderPage()
    await openManageDialog()
    expect((screen.getByTestId('run-button') as HTMLButtonElement).disabled).toBe(true)
    expect(screen.getByTestId('runtime-state').textContent).toBe('Running')
    expect((screen.getByTestId('stop-button') as HTMLButtonElement).disabled).toBe(false)
  })

  it('ignores overlapping run requests while one is in flight', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openManageDialog()
    let releaseRun!: () => void
    const gate = new Promise<void>((resolve) => {
      releaseRun = resolve
    })
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/run')) {
        return gate.then(() => Promise.resolve(
            envelopeOk({ status: 'completed', entry_returned: true, entry_result: 0, app_running: false }),
          ),)
      }
      return Promise.reject(new Error(`unexpected POST ${url}`))
    })
    const first = useAppManagementStore.getState().runOnce()
    const second = useAppManagementStore.getState().runOnce()
    await Promise.resolve()
    expect(useAppManagementStore.getState().running).toBe(true)
    releaseRun()
    await Promise.all([first, second])
    expect(requestMock.post).toHaveBeenCalledTimes(1)
  })

  it('shows host failure class separately from any app return value when run is refused', async () => {
    stubGetInstalled(installedState(false))
    renderPage()
    await openManageDialog()
    stubPath('post', '/api/v1/app/run', () => Promise.resolve({
        success: true,
        data: { status: 'refused', reason: 'LOAD_FAILED', detail: 'segment fault in load', executable: false },
      }),)
    fireEvent.click(screen.getByTestId('run-button'))
    await waitFor(() => {
      expect(useAppManagementStore.getState().runOutcome?.status).toBe('refused')
    })
    const panel = screen.getByTestId('run-outcome').textContent || ''
    expect(panel).toContain('failed to load the app')
    expect(panel).toContain('segment fault in load')
  })
})
