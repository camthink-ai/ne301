import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import {
  render,
  screen,
  cleanup,
  fireEvent,
  waitFor,
} from '@testing-library/preact';
import { I18nWrapper } from '@/i18n';
import {
  firstSlotFor,
  sidecarProjectionServable,
  businessUiCatalog,
} from '@/pages/applicationManagement/appSlot';
import SidecarAppFrame from '@/pages/applicationManagement/sidecarAppFrame';
import ApplicationManagement from '@/pages/applicationManagement';
import {
  useAppManagementStore,
  type AppView,
  type SidecarView,
} from '@/store/appManagement';
import {
  requestMock,
  envelopeOk,
  envelopeFail,
  resetAppStore,
  resetBusinessCatalog,
  stubGetInstalled,
  stubPath,
  setupSuite,
  teardownSuite,
  installedState,
} from './helpers';

import { SidecarBroker } from '@/services/sidecarBridge';

vi.mock('@/services/request', () => ({
  default: { get: vi.fn(), post: vi.fn(), put: vi.fn(), delete: vi.fn() },
}));

vi.mock('sonner', () => ({
  toast: { error: vi.fn(), success: vi.fn(), info: vi.fn() },
}));

vi.mock('@/pages/applicationManagement/mqtt-module', () => ({
  default: () => <div data-testid="mqtt-module-stub">MQTT module</div>,
}));

vi.mock('@/pages/applicationManagement/webhook-module', () => ({
  default: () => <div data-testid="webhook-module-stub">Webhook module</div>,
}));

vi.mock('@/services/sidecarBridge', async importOriginal => {
  const actual =    await importOriginal<typeof import('@/services/sidecarBridge')>();
  return {
    ...actual,
    SidecarBroker: vi.fn(),
  };
});

const flushAsync = async (): Promise<void> => {
  await Array.from({ length: 25 }).reduce(
    (pending: Promise<void>) => pending.then(() => undefined),
    Promise.resolve()
  );
};

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
};

const installed: AppView = {
  kind: 'installed',
  record: {
    publisherId: 'pub-alpha',
    appId: 'hello-app',
    version: 'v1.2.3',
    packageLen: 1024,
    packageSha256: 'f9'.repeat(32),
    tbsSha256: 'ee'.repeat(32),
  },
  appRunning: false,
};

describe('firstSlotFor trusted slot decision', () => {
  afterEach(() => {
    resetBusinessCatalog();
  });

  it('keeps the first slot for deploy, manage and status states', () => {
    expect(
      firstSlotFor({ kind: 'not_installed', appRunning: false }, null).kind
    ).toBe('deploy');
    expect(firstSlotFor(installed, null).kind).toBe('manage');
    expect(
      firstSlotFor({ kind: 'undetermined', reason: null }, null).kind
    ).toBe('status');
    expect(firstSlotFor(installed, 'unauthorized').kind).toBe('status');
    expect(firstSlotFor(null, null).kind).toBe('status');
    expect(
      firstSlotFor(
        { kind: 'cleanup_pending', record: null, appRunning: false },
        null
      ).kind
    ).toBe('status');
  });

  it('opens the trusted sidecar slot only for trusted ACTIVE, matching app and cross-site origin', () => {
    const sidecarActive: SidecarView = {
      kind: 'active',
      projection: activeProjection,
    };
    expect(firstSlotFor(installed, null, sidecarActive).kind).toBe('sidecar');
    expect(firstSlotFor(installed, null, { kind: 'unknown' }).kind).toBe(
      'manage'
    );
    expect(
      firstSlotFor(installed, null, {
        kind: 'unavailable',
        reason: 'SIDECAR_NOT_ACTIVE',
      }).kind
    ).toBe('manage');
    const stagedView: AppView = installed;
    expect(firstSlotFor(stagedView, null, sidecarActive).kind).toBe('sidecar');
  });

  it('never opens the sidecar slot for a different app identity or same origin', () => {
    const sidecarActive: SidecarView = {
      kind: 'active',
      projection: activeProjection,
    };
    const otherApp: AppView = {
      ...installed,
      record: { ...installed.record, appId: 'other-app' },
    };
    expect(firstSlotFor(otherApp, null, sidecarActive).kind).toBe('manage');
    const sameOrigin: SidecarView = {
      kind: 'active',
      projection: {
        ...activeProjection,
        app_site_origin: window.location.origin,
      },
    };
    expect(firstSlotFor(installed, null, sameOrigin).kind).toBe('manage');
    const portSwap: SidecarView = {
      kind: 'active',
      projection: {
        ...activeProjection,
        app_site_origin: 'http://127.0.0.1:9090',
      },
    };
    expect(
      sidecarProjectionServable(
        portSwap.projection,
        'hello-app',
        'http://127.0.0.1:8080'
      )
    ).toBe(true);
  });

  it('refuses projections with wrong host_ui_abi or missing base path, allows null origin', () => {
    expect(
      sidecarProjectionServable(
        { ...activeProjection, host_ui_abi: 2 },
        'hello-app',
        'http://127.0.0.1:8080'
      )
    ).toBe(false);
    expect(
      sidecarProjectionServable(
        { ...activeProjection, app_site_origin: null },
        'hello-app',
        'http://127.0.0.1:8080'
      )
    ).toBe(true);
    expect(
      sidecarProjectionServable(
        { ...activeProjection, ui_base_path: undefined },
        'hello-app',
        'http://127.0.0.1:8080'
      )
    ).toBe(false);
    expect(
      sidecarProjectionServable(
        {
          ...activeProjection,
          app_site_origin: 'http://127.0.0.1:48080/x/path',
        },
        'hello-app',
        'http://127.0.0.1:8080'
      )
    ).toBe(false);
  });

  it('keeps a registered in-process business catalog ahead of the sidecar slot', () => {
    businessUiCatalog['hello-app'] = {
      labelKey: 'sys.app_management.slot.manage',
      render: () => null,
    };
    const sidecarUnknown: SidecarView = { kind: 'unknown' };
    expect(firstSlotFor(installed, null, sidecarUnknown).kind).toBe('business');
  });
});

describe('sidecar store state mapping', () => {
  beforeEach(() => {
    setupSuite();
    resetAppStore();
    stubGetInstalled(installedState(false));
  });

  afterEach(() => {
    teardownSuite();
  });

  it('maps an active trusted projection into the store', async () => {
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return Promise.resolve(installedState(false));
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(envelopeOk(activeProjection));
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    await useAppManagementStore.getState().refreshSidecar();
    const { sidecar } = useAppManagementStore.getState();
    expect(sidecar.kind).toBe('active');
    if (sidecar.kind === 'active') {
      expect(sidecar.projection.app_id).toBe('hello-app');
      expect(sidecar.projection.app_site_origin).toBe(
        'https://app.ne301.example.com'
      );
    }
  });

  it('maps a non-ACTIVE sidecar state to an unavailable view with reason', async () => {
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return Promise.resolve(installedState(false));
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(
          envelopeOk({ state: 'unavailable', reason: 'SIDECAR_NOT_ACTIVE' })
        );
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    await useAppManagementStore.getState().refreshSidecar();
    const { sidecar } = useAppManagementStore.getState();
    expect(sidecar.kind).toBe('unavailable');
    if (sidecar.kind === 'unavailable') {
      expect(sidecar.reason).toBe('SIDECAR_NOT_ACTIVE');
    }
  });

  it('maps an ACTIVE projection with null app site origin into the same-origin slot', async () => {
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return Promise.resolve(installedState(false));
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(
          envelopeOk({ ...activeProjection, app_site_origin: null })
        );
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    await useAppManagementStore.getState().refreshSidecar();
    const { sidecar } = useAppManagementStore.getState();
    expect(sidecar.kind).toBe('active');
  });

  it('maps query failures to an unavailable sidecar view without touching the native view', async () => {
    stubGetInstalled(installedState(false));
    await useAppManagementStore.getState().refresh();
    stubPath('get', '/api/v1/app/sidecar/ui', () => Promise.reject(new Error('network down')));
    await useAppManagementStore.getState().refreshSidecar();
    const state = useAppManagementStore.getState();
    expect(state.sidecar.kind).toBe('unavailable');
    expect(state.view?.kind).toBe('installed');
  });

  it('maps an auth failure to the unknown sidecar view so the auth block wins', async () => {
    stubGetInstalled(envelopeFail('UNAUTHORIZED'));
    await useAppManagementStore.getState().refresh();
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(envelopeFail('UNAUTHORIZED'));
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('unknown');
    expect(useAppManagementStore.getState().authBlock).toBe('unauthorized');
  });
});

describe('sidecar demotion debounce', () => {
  const nonActiveUi = () => envelopeOk({ state: 'unavailable', reason: 'SIDECAR_NOT_ACTIVE' });

  const stubSidecarUi = (envelope: unknown) => {
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return Promise.resolve(installedState(false));
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(envelope);
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
  };

  beforeEach(() => {
    setupSuite();
    resetAppStore();
    stubGetInstalled(installedState(false));
  });

  afterEach(() => {
    teardownSuite();
  });

  it('keeps an active sidecar after a single non-active ui response and demotes on two consecutive', async () => {
    stubSidecarUi(envelopeOk(activeProjection));
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('active');

    stubSidecarUi(nonActiveUi());
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('active');

    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('unavailable');
    const demoted = useAppManagementStore.getState().sidecar;
    if (demoted.kind === 'unavailable') {
      expect(demoted.reason).toBe('SIDECAR_NOT_ACTIVE');
    }
  });

  it('resets the consecutive counter when an active response arrives between non-active samples', async () => {
    stubSidecarUi(envelopeOk(activeProjection));
    await useAppManagementStore.getState().refreshSidecar();

    stubSidecarUi(nonActiveUi());
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('active');

    stubSidecarUi(envelopeOk(activeProjection));
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('active');

    stubSidecarUi(nonActiveUi());
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('active');

    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('unavailable');
  });

  it('still demotes an active sidecar immediately on an auth failure', async () => {
    stubSidecarUi(envelopeOk(activeProjection));
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('active');

    stubSidecarUi(envelopeFail('FORBIDDEN'));
    await useAppManagementStore.getState().refreshSidecar();
    expect(useAppManagementStore.getState().sidecar.kind).toBe('unknown');
  });
});

describe('uninstall flow states per the approved wireframe', () => {
  beforeEach(() => {
    setupSuite();
    resetAppStore();
    stubGetInstalled({
      status: 'installed',
      publisher_id: 'pub-alpha',
      app_id: 'hello-app',
      version_major: 1,
      version_minor: 2,
      version_patch: 3,
      package_len: 1024,
      package_sha256: 'f9'.repeat(32),
      tbs_sha256: 'ee'.repeat(32),
      ui: { status: 'active' },
      executable: false,
      app_running: false,
    });
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return Promise.resolve(
          envelopeOk({
            status: 'installed',
            publisher_id: 'pub-alpha',
            app_id: 'hello-app',
            version_major: 1,
            version_minor: 2,
            version_patch: 3,
            package_len: 1024,
            package_sha256: 'f9'.repeat(32),
            tbs_sha256: 'ee'.repeat(32),
            ui: { status: 'active' },
            executable: false,
            app_running: false,
          })
        );
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(envelopeFail('FORBIDDEN'));
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/revoke')) {
        return Promise.resolve(envelopeOk({ status: 'revoked', count: 1 }));
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
    (
      requestMock as unknown as { delete: ReturnType<typeof vi.fn> }
    ).delete.mockImplementation(() => Promise.reject(new Error('unexpected DELETE')));
  });

  afterEach(() => {
    cleanup();
    teardownSuite();
  });

  const openManageAndUninstall = async () => {
    render(
      <I18nWrapper>
        <ApplicationManagement />
      </I18nWrapper>
    );
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('installed');
    });
    fireEvent.click(screen.getByTestId('slot-manage'));
    await screen.findByTestId('manage-identity');
    fireEvent.click(screen.getByTestId('uninstall-row'));
    await screen.findByTestId('confirm-uninstall');
  };

  it('shows the processing copy while the uninstall is in flight and a success state on completion', async () => {
    let uninstalled = false;
    const deleteResolver: { current: ((value: unknown) => void) | null } = {
      current: null,
    };
    const deleteMock = (
      requestMock as unknown as { delete: ReturnType<typeof vi.fn> }
    ).delete;
    deleteMock.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return new Promise(resolve => {
          deleteResolver.current = (value: unknown) => {
            uninstalled = true;
            resolve(value);
          };
        });
      }
      return Promise.reject(new Error(`unexpected DELETE ${url}`));
    });
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        if (!uninstalled) {
          return Promise.resolve(
            envelopeOk({
              status: 'installed',
              publisher_id: 'pub-alpha',
              app_id: 'hello-app',
              version_major: 1,
              version_minor: 2,
              version_patch: 3,
              package_len: 1024,
              package_sha256: 'f9'.repeat(32),
              tbs_sha256: 'ee'.repeat(32),
              ui: { status: 'active' },
              executable: false,
              app_running: false,
            })
          );
        }
        return Promise.resolve(
          envelopeOk({ state: 'none', executable: false, app_running: false })
        );
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(envelopeFail('FORBIDDEN'));
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    await openManageAndUninstall();
    fireEvent.click(screen.getByTestId('confirm-uninstall'));
    await waitFor(() => {
      expect(useAppManagementStore.getState().uninstalling).toBe(true);
    });
    await flushAsync();
    expect(screen.queryByTestId('uninstall-progress')).toBeTruthy();
    deleteResolver.current?.(
      envelopeOk({
        status: 'uninstalled',
        publisher_id: 'pub-alpha',
        app_id: 'hello-app',
      })
    );
    await waitFor(() => {
      expect(useAppManagementStore.getState().uninstallOutcome).not.toBeNull();
    });
    await waitFor(() => {
      expect(useAppManagementStore.getState().view?.kind).toBe('not_installed');
    });
    await screen.findByTestId('uninstall-success', undefined, {
      timeout: 3000,
    });
    expect(screen.getByTestId('uninstall-success').textContent).toContain(
      'Uninstalled'
    );
    expect(requestMock.post).toHaveBeenCalledWith(
      '/api/v1/app/sidecar/bridge/revoke',
      { all: true },
      expect.objectContaining({ skipErrorToast: true })
    );
  });

  it('lands on the abnormal state when cleanup cannot be confirmed', async () => {
    let uninstalled = false;
    const deleteMock = (
      requestMock as unknown as { delete: ReturnType<typeof vi.fn> }
    ).delete;
    deleteMock.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        uninstalled = true;
        return Promise.resolve(
          envelopeOk({
            status: 'cleanup_pending',
            publisher_id: 'pub-alpha',
            app_id: 'hello-app',
            reason: 'PURGE_UNCONFIRMED_OR_STORAGE_UNKNOWN',
          })
        );
      }
      return Promise.reject(new Error(`unexpected DELETE ${url}`));
    });
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        if (!uninstalled) {
          return Promise.resolve(
            envelopeOk({
              status: 'installed',
              publisher_id: 'pub-alpha',
              app_id: 'hello-app',
              version_major: 1,
              version_minor: 2,
              version_patch: 3,
              package_len: 1024,
              package_sha256: 'f9'.repeat(32),
              tbs_sha256: 'ee'.repeat(32),
              ui: { status: 'active' },
              executable: false,
              app_running: false,
            })
          );
        }
        return Promise.resolve(
          envelopeOk({
            state: 'cleanup_pending',
            executable: false,
            app_running: false,
          })
        );
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        return Promise.resolve(envelopeFail('FORBIDDEN'));
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    await openManageAndUninstall();
    fireEvent.click(screen.getByTestId('confirm-uninstall'));
    await screen.findByTestId('uninstall-abnormal');
    expect(screen.queryByTestId('uninstall-success')).toBeNull();
  });
});

describe('SidecarAppFrame sandbox lifecycle', () => {
  const mockedBroker = SidecarBroker as unknown as ReturnType<typeof vi.fn>;

  beforeEach(() => {
    setupSuite();
    resetAppStore();
    mockedBroker.mockReset();
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
      })
    );
    requestMock.get.mockImplementation(() => Promise.reject(new Error('unexpected GET')));
    requestMock.post.mockImplementation(() => Promise.reject(new Error('unexpected POST')));
  });

  afterEach(() => {
    cleanup();
    teardownSuite();
  });

  const renderFrame = () => render(
      <I18nWrapper>
        <SidecarAppFrame projection={activeProjection} appId="hello-app" />
      </I18nWrapper>
    );

  it('renders the sandboxed iframe with generation-bound src and a download-free sandbox', async () => {
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(
          envelopeOk({
            status: 'granted',
            capabilities: ['snapshot.read'],
            grant: {
              grant_id: 'b'.repeat(64),
              expires_at_ms: 123456,
              issued_at_ms: 1000,
              identity: {
                publisher_id: 'cnt-publisher',
                app_id: 'hello-app',
                publisher_key_sha256: 'dd'.repeat(32),
                native_tbs_sha256: 'ee'.repeat(32),
                ui_tbs_sha256: 'ff'.repeat(32),
                build_id: '1.0.0',
                host_boot_generation: 42,
                native_session_generation: 3,
                ui_generation: 11,
                sidecar_service_generation: 42,
              },
            },
          })
        );
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
    const { container } = renderFrame();
    const frame = await waitFor(() => {
      const el = container.querySelector('[data-testid="sidecar-frame"]');
      expect(el).toBeTruthy();
      return el as HTMLIFrameElement;
    });
    expect(frame.getAttribute('sandbox')).toBe(
      'allow-scripts allow-same-origin'
    );
    expect(frame.getAttribute('sandbox')).not.toContain('allow-downloads');
    expect(frame.getAttribute('src')).toBe(
      'https://app.ne301.example.com/appsite/g42/index.html'
    );
    fireEvent.load(frame);
    await waitFor(() => {
      expect(mockedBroker).toHaveBeenCalledTimes(1);
    });
    const options = mockedBroker.mock.calls[0][0] as {
      appOrigin: string;
      grant: { grant_id: string; identity: Record<string, unknown> };
    };
    expect(options.appOrigin).toBe('https://app.ne301.example.com');
    expect(options.grant.grant_id).toBe('b'.repeat(64));
    expect(options.grant.identity.build_id).toBe('1.0.0');
    expect(options.grant.identity.sidecar_service_generation).toBe(42);
  });

  it('refuses to render any iframe when the grant is refused and shows trusted status', async () => {
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(envelopeFail('FORBIDDEN'));
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
    const { container } = renderFrame();
    await waitFor(() => {
      expect(
        container.querySelector('[data-testid="sidecar-revoked"]')
      ).toBeTruthy();
    });
    expect(container.querySelector('[data-testid="sidecar-frame"]')).toBeNull();
  });

  it('auto-retries the bridge grant after a session revoke and recovers the frame', async () => {
    let grantCalls = 0;
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        grantCalls += 1;
        return Promise.resolve(
          envelopeOk({
            status: 'granted',
            capabilities: ['snapshot.read'],
            grant: {
              grant_id: 'b'.repeat(64),
              expires_at_ms: 123456,
              issued_at_ms: 1000,
              identity: {
                publisher_id: 'cnt-publisher',
                app_id: 'hello-app',
                publisher_key_sha256: 'dd'.repeat(32),
                native_tbs_sha256: 'ee'.repeat(32),
                ui_tbs_sha256: 'ff'.repeat(32),
                build_id: '1.0.0',
                host_boot_generation: 42,
                native_session_generation: 3,
                ui_generation: 11,
                sidecar_service_generation: 42,
              },
            },
          })
        );
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
    const { container } = renderFrame();
    const frame = await waitFor(() => {
      const el = container.querySelector('[data-testid="sidecar-frame"]');
      expect(el).toBeTruthy();
      return el as HTMLIFrameElement;
    });
    fireEvent.load(frame);
    await waitFor(() => {
      expect(mockedBroker).toHaveBeenCalledTimes(1);
    });
    const options = mockedBroker.mock.calls[0][0] as {
      onRevoke: (reason: string) => void;
    };
    options.onRevoke('IDENTITY_OR_GENERATION_CHANGED');
    await waitFor(() => {
      expect(container.querySelector('[data-testid="sidecar-revoked"]')).toBeTruthy();
    });
    await new Promise((resolve) => { setTimeout(resolve, 3600); });
    const reloaded = await waitFor(() => {
      const el = container.querySelector('[data-testid="sidecar-frame"]');
      expect(el).toBeTruthy();
      return el as HTMLIFrameElement;
    });
    fireEvent.load(reloaded);
    await waitFor(() => {
      expect(mockedBroker).toHaveBeenCalledTimes(2);
    });
    expect(grantCalls).toBeGreaterThanOrEqual(2);
  });

  it('refuses to render any iframe when the grant carries no trusted identity', async () => {
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(
          envelopeOk({
            status: 'granted',
            grant: {
              grant_id: 'b'.repeat(64),
              capabilities: ['snapshot.read'],
            },
          })
        );
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
    const { container } = renderFrame();
    await waitFor(() => {
      expect(
        container.querySelector('[data-testid="sidecar-revoked"]')
      ).toBeTruthy();
    });
    expect(container.querySelector('[data-testid="sidecar-frame"]')).toBeNull();
    expect(mockedBroker).not.toHaveBeenCalled();
  });

  it('keeps a fixed minimum-height class when ResizeObserver or contentDocument is unavailable', async () => {
    const { container } = renderFrame();
    const frame = await waitFor(() => {
      const el = container.querySelector('[data-testid="sidecar-frame"]');
      expect(el).toBeTruthy();
      return el as HTMLIFrameElement;
    });
    expect(frame.className).toContain('min-h-[560px]');
    fireEvent.load(frame);
    await flushAsync();
    expect(frame.style.height).toBe('');
  });

  it('revokes the broker session on unmount so late port messages cannot resume it', async () => {
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(
          envelopeOk({
            status: 'granted',
            capabilities: ['snapshot.read'],
            grant: {
              grant_id: 'b'.repeat(64),
              expires_at_ms: 123456,
              issued_at_ms: 1000,
              identity: {
                publisher_id: 'cnt-publisher',
                app_id: 'hello-app',
                publisher_key_sha256: 'dd'.repeat(32),
                native_tbs_sha256: 'ee'.repeat(32),
                ui_tbs_sha256: 'ff'.repeat(32),
                build_id: '1.0.0',
                host_boot_generation: 42,
                native_session_generation: 3,
                ui_generation: 11,
                sidecar_service_generation: 42,
              },
            },
          })
        );
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
    const { container, unmount } = renderFrame();
    const frame = await waitFor(() => {
      const el = container.querySelector('[data-testid="sidecar-frame"]');
      expect(el).toBeTruthy();
      return el as HTMLIFrameElement;
    });
    fireEvent.load(frame);
    await waitFor(() => {
      expect(mockedBroker).toHaveBeenCalledTimes(1);
    });
    const instance = mockedBroker.mock.results[0].value as {
      revoke: (r: string) => void;
    };
    const revokeSpy = vi.spyOn(instance, 'revoke');
    unmount();
    expect(revokeSpy).toHaveBeenCalledWith('FRAME_UNMOUNT');
    expect(container.querySelector('[data-testid="sidecar-frame"]')).toBeNull();
  });
});

describe('SidecarAppFrame iframe height auto-sizing', () => {
  const mockedBroker = SidecarBroker as unknown as ReturnType<typeof vi.fn>;

  type StubObserver = {
    callback: ResizeObserverCallback;
    observe: ReturnType<typeof vi.fn>;
    unobserve: ReturnType<typeof vi.fn>;
    disconnect: ReturnType<typeof vi.fn>;
  };

  let observers: StubObserver[];

  const grantOk = () => {
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(
          envelopeOk({
            status: 'granted',
            capabilities: ['snapshot.read'],
            grant: {
              grant_id: 'b'.repeat(64),
              expires_at_ms: 123456,
              issued_at_ms: 1000,
              identity: {
                publisher_id: 'cnt-publisher',
                app_id: 'hello-app',
                publisher_key_sha256: 'dd'.repeat(32),
                native_tbs_sha256: 'ee'.repeat(32),
                ui_tbs_sha256: 'ff'.repeat(32),
                build_id: '1.0.0',
                host_boot_generation: 42,
                native_session_generation: 3,
                ui_generation: 11,
                sidecar_service_generation: 42,
              },
            },
          })
        );
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
  };

  const renderSameOriginFrame = () => render(
    <I18nWrapper>
      <SidecarAppFrame
        projection={{ ...activeProjection, app_site_origin: null }}
        appId="hello-app"
      />
    </I18nWrapper>
  );

  const getFrame = async (container: Element) => waitFor(() => {
    const el = container.querySelector(
      '[data-testid="sidecar-frame"]'
    ) as HTMLIFrameElement | null;
    expect(el).toBeTruthy();
    return el as HTMLIFrameElement;
  });

  const setFrameScrollHeight = (frame: HTMLIFrameElement, value: number) => {
    const doc = frame.contentDocument;
    expect(doc).toBeTruthy();
    if (!doc!.documentElement) {
      const html = doc!.createElement('html');
      html.appendChild(doc!.createElement('body'));
      doc!.appendChild(html);
    }
    Object.defineProperty(doc!.documentElement, 'scrollHeight', {
      configurable: true,
      value,
    });
  };

  beforeEach(() => {
    setupSuite();
    resetAppStore();
    mockedBroker.mockReset();
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
      })
    );
    grantOk();
    observers = [];
    (globalThis as Record<string, unknown>).ResizeObserver = class {
      callback: ResizeObserverCallback;

      observe = vi.fn();

      unobserve = vi.fn();

      disconnect = vi.fn();

      constructor(callback: ResizeObserverCallback) {
        this.callback = callback;
        observers.push(this as unknown as StubObserver);
      }
    };
  });

  afterEach(() => {
    cleanup();
    teardownSuite();
    delete (globalThis as Record<string, unknown>).ResizeObserver;
  });

  it('stretches the iframe to the document height and skips sub-threshold changes', async () => {
    const { container } = renderSameOriginFrame();
    const frame = await getFrame(container);
    setFrameScrollHeight(frame, 1400);
    fireEvent.load(frame);
    await waitFor(() => {
      expect(observers.length).toBe(1);
    });
    const observer = observers[0];
    expect(observer.observe).toHaveBeenCalled();
    observer.callback([], observer as unknown as ResizeObserver);
    expect(frame.style.height).toBe('1400px');
    setFrameScrollHeight(frame, 1405);
    observer.callback([], observer as unknown as ResizeObserver);
    expect(frame.style.height).toBe('1400px');
    setFrameScrollHeight(frame, 900);
    observer.callback([], observer as unknown as ResizeObserver);
    expect(frame.style.height).toBe('900px');
  });

  it('clamps small documents to the minimum height and disconnects the observer on unmount', async () => {
    const { container, unmount } = renderSameOriginFrame();
    const frame = await getFrame(container);
    setFrameScrollHeight(frame, 300);
    fireEvent.load(frame);
    await waitFor(() => {
      expect(observers.length).toBe(1);
    });
    const observer = observers[0];
    observer.callback([], observer as unknown as ResizeObserver);
    expect(frame.style.height).toBe('480px');
    expect(frame.className).toContain('min-h-[560px]');
    unmount();
    expect(observer.disconnect).toHaveBeenCalled();
  });

  it('reattaches to the fresh document when the frame reloads for a new generation', async () => {
    const { container } = renderSameOriginFrame();
    const frame = await getFrame(container);
    setFrameScrollHeight(frame, 1200);
    fireEvent.load(frame);
    await waitFor(() => {
      expect(observers.length).toBe(1);
    });
    observers[0].callback([], observers[0] as unknown as ResizeObserver);
    expect(frame.style.height).toBe('1200px');
    expect(observers[0].disconnect).not.toHaveBeenCalled();
    fireEvent.load(frame);
    expect(observers[0].disconnect).toHaveBeenCalledTimes(1);
    expect(observers.length).toBe(2);
    setFrameScrollHeight(frame, 1500);
    observers[1].callback([], observers[1] as unknown as ResizeObserver);
    expect(frame.style.height).toBe('1500px');
  });
});

describe('SidecarAppFrame stale generation auto-recovery', () => {
  const mockedBroker = SidecarBroker as unknown as ReturnType<typeof vi.fn>;
  let generationBumped = false;
  let sidecarUiCalls = 0;

  const grantOk = () => {
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(
          envelopeOk({
            status: 'granted',
            capabilities: ['snapshot.read'],
            grant: {
              grant_id: 'b'.repeat(64),
              expires_at_ms: 123456,
              issued_at_ms: 1000,
              identity: {
                publisher_id: 'cnt-publisher',
                app_id: 'hello-app',
                publisher_key_sha256: 'dd'.repeat(32),
                native_tbs_sha256: 'ee'.repeat(32),
                ui_tbs_sha256: 'ff'.repeat(32),
                build_id: '1.0.0',
                host_boot_generation: 42,
                native_session_generation: 3,
                ui_generation: 11,
                sidecar_service_generation: 42,
              },
            },
          })
        );
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
  };

  beforeEach(() => {
    setupSuite();
    resetAppStore();
    mockedBroker.mockReset();
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
      })
    );
    generationBumped = false;
    sidecarUiCalls = 0;
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/installed')) {
        return Promise.resolve(installedState(false));
      }
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        sidecarUiCalls += 1;
        return Promise.resolve(
          envelopeOk(
            generationBumped
              ? {
                  ...activeProjection,
                  sidecar_service_generation: '43',
                  ui_base_path: '/appsite/g43/',
                }
              : activeProjection
          )
        );
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    grantOk();
  });

  afterEach(() => {
    cleanup();
    teardownSuite();
  });

  it('re-queries the projection on auto-retry so the frame remounts at the new generation', async () => {
    const { container } = render(
      <I18nWrapper>
        <ApplicationManagement />
      </I18nWrapper>
    );
    const frame = await waitFor(() => {
      const el = container.querySelector(
        '[data-testid="sidecar-frame"]'
      ) as HTMLIFrameElement | null;
      expect(el).toBeTruthy();
      return el as HTMLIFrameElement;
    });
    expect(frame.getAttribute('src')).toBe(
      'https://app.ne301.example.com/appsite/g42/index.html'
    );
    await waitFor(() => {
      expect(requestMock.post).toHaveBeenCalled();
    });
    fireEvent.load(frame);
    await waitFor(() => {
      expect(mockedBroker).toHaveBeenCalledTimes(1);
    });
    const options = mockedBroker.mock.calls[0][0] as {
      onRevoke: (reason: string) => void;
    };
    generationBumped = true;
    options.onRevoke('IDENTITY_OR_GENERATION_CHANGED');
    await waitFor(() => {
      expect(
        container.querySelector('[data-testid="sidecar-revoked"]')
      ).toBeTruthy();
    });
    await new Promise((resolve) => { setTimeout(resolve, 3600); });
    const healed = await waitFor(() => {
      const el = container.querySelector(
        '[data-testid="sidecar-frame"]'
      ) as HTMLIFrameElement | null;
      expect(el).toBeTruthy();
      expect(el!.getAttribute('src')).toBe(
        'https://app.ne301.example.com/appsite/g43/index.html'
      );
      return el as HTMLIFrameElement;
    });
    expect(healed.getAttribute('src')).toContain('/appsite/g43/');
    expect(sidecarUiCalls).toBeGreaterThanOrEqual(2);
    fireEvent.load(healed);
    await waitFor(() => {
      expect(mockedBroker).toHaveBeenCalledTimes(2);
    });
  });
});

describe('SidecarAppFrame revoke tick refresh scheduling', () => {
  const mockedBroker = SidecarBroker as unknown as ReturnType<typeof vi.fn>;
  let sidecarUiCalls: number;

  beforeEach(() => {
    setupSuite();
    resetAppStore();
    mockedBroker.mockReset();
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
      })
    );
    sidecarUiCalls = 0;
    requestMock.get.mockImplementation((url: string) => {
      if (String(url).includes('/api/v1/app/sidecar/ui')) {
        sidecarUiCalls += 1;
        return Promise.resolve(envelopeOk(activeProjection));
      }
      return Promise.reject(new Error(`unexpected GET ${url}`));
    });
    requestMock.post.mockImplementation((url: string) => {
      if (String(url).includes('/app/sidecar/bridge/grant')) {
        return Promise.resolve(
          envelopeOk({
            status: 'granted',
            capabilities: ['snapshot.read'],
            grant: {
              grant_id: 'b'.repeat(64),
              expires_at_ms: 123456,
              issued_at_ms: 1000,
              identity: {
                publisher_id: 'cnt-publisher',
                app_id: 'hello-app',
                publisher_key_sha256: 'dd'.repeat(32),
                native_tbs_sha256: 'ee'.repeat(32),
                ui_tbs_sha256: 'ff'.repeat(32),
                build_id: '1.0.0',
                host_boot_generation: 42,
                native_session_generation: 3,
                ui_generation: 11,
                sidecar_service_generation: 42,
              },
            },
          })
        );
      }
      return Promise.reject(new Error(`unexpected POST ${url}`));
    });
  });

  afterEach(() => {
    cleanup();
    teardownSuite();
  });

  it('does not sample the sidecar ui endpoint inside the revoke tick and refreshes on the retry cadence', async () => {
    const { container } = render(
      <I18nWrapper>
        <SidecarAppFrame projection={activeProjection} appId="hello-app" />
      </I18nWrapper>
    );
    const frame = await waitFor(() => {
      const el = container.querySelector('[data-testid="sidecar-frame"]');
      expect(el).toBeTruthy();
      return el as HTMLIFrameElement;
    });
    fireEvent.load(frame);
    await waitFor(() => {
      expect(mockedBroker).toHaveBeenCalledTimes(1);
    });
    const options = mockedBroker.mock.calls[0][0] as {
      onRevoke: (reason: string) => void;
    };
    options.onRevoke('SIDECAR_NOT_ACTIVE');
    await waitFor(() => {
      expect(container.querySelector('[data-testid="sidecar-revoked"]')).toBeTruthy();
    });
    await flushAsync();
    await new Promise((resolve) => { setTimeout(resolve, 200); });
    expect(sidecarUiCalls).toBe(0);
    await new Promise((resolve) => { setTimeout(resolve, 3600); });
    expect(sidecarUiCalls).toBeGreaterThanOrEqual(1);
  });
});
