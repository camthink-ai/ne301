import { describe, it, expect } from 'vitest';
import { isCrossSite } from '@/services/sidecarBridge';
import { sidecarProjectionServable } from '@/pages/applicationManagement/appSlot';
import type { SidecarUiProjection } from '@/types/sidecar';

const base: SidecarUiProjection = {
  state: 'active',
  app_id: 'line-crossing',
  app_site_origin: 'http://192.168.93.112',
  ui_base_path: '/appsite/g7/',
  host_ui_abi: 1,
} as unknown as SidecarUiProjection;

describe('origin-level app site scheme matrix (real modules)', () => {
  it('admin by IP, app on second IP: cross-site and servable', () => {
    expect(isCrossSite('http://192.168.93.111', 'http://192.168.93.112')).toBe(
      true
    );
    expect(
      sidecarProjectionServable(base, 'line-crossing', 'http://192.168.93.111')
    ).toBe(true);
    expect(
      sidecarProjectionServable(
        base,
        'line-crossing',
        'http://192.168.93.111:8080'
      )
    ).toBe(true);
  });

  it('admin by hostname, app on second IP: cross-site and servable', () => {
    expect(isCrossSite('http://ne301-2a1509', 'http://192.168.93.112')).toBe(
      true
    );
    expect(
      isCrossSite('http://ne301-2a1509.local', 'http://192.168.93.112')
    ).toBe(true);
    expect(
      sidecarProjectionServable(
        base,
        'line-crossing',
        'http://ne301-2a1509.local'
      )
    ).toBe(true);
  });

  it('rejected traps: port-only, admin subdomain, same origin, null origin', () => {
    expect(
      isCrossSite('http://192.168.93.111', 'http://192.168.93.111:8080')
    ).toBe(false);
    expect(
      isCrossSite('http://cam-1234.local', 'http://app.cam-1234.local')
    ).toBe(false);
    expect(
      sidecarProjectionServable(base, 'line-crossing', 'http://192.168.93.112')
    ).toBe(false);
    const noOrigin = {
      ...base,
      app_site_origin: null,
    } as unknown as SidecarUiProjection;
    expect(
      sidecarProjectionServable(
        noOrigin,
        'line-crossing',
        'http://192.168.93.111'
      )
    ).toBe(true);
    const port80 = {
      ...base,
      app_site_origin: 'http://192.168.93.112:80',
    } as unknown as SidecarUiProjection;
    expect(
      sidecarProjectionServable(
        port80,
        'line-crossing',
        'http://192.168.93.111'
      )
    ).toBe(false);
  });

  it('fails closed on inactive state, app id mismatch, abi mismatch, non-canonical origin', () => {
    expect(
      sidecarProjectionServable(
        { ...base, state: 'stopped' } as unknown as SidecarUiProjection,
        'line-crossing',
        'http://192.168.93.111'
      )
    ).toBe(false);
    expect(
      sidecarProjectionServable(base, 'other-app', 'http://192.168.93.111:80')
    ).toBe(false);
    expect(
      sidecarProjectionServable(
        { ...base, host_ui_abi: 2 } as unknown as SidecarUiProjection,
        'line-crossing',
        'http://192.168.93.111'
      )
    ).toBe(false);
    expect(
      sidecarProjectionServable(
        {
          ...base,
          app_site_origin: 'http://192.168.93.112:80/',
        } as SidecarUiProjection,
        'line-crossing',
        'http://192.168.93.111'
      )
    ).toBe(false);
  });
});
