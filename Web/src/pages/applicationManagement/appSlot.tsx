import type { ComponentChildren } from 'preact';
import type {
  AppAuthBlock,
  AppRecord,
  AppView,
  SidecarView,
} from '@/store/appManagement';
import type { SidecarUiProjection } from '@/types/sidecar';

export interface BusinessUiEntry {
  labelKey: string;
  render: (record: AppRecord) => ComponentChildren;
}

export const businessUiCatalog: Record<string, BusinessUiEntry> = {};

export function businessUiFor(appId: string): BusinessUiEntry | null {
  return businessUiCatalog[appId] || null;
}

export const confirmedV1SingleRunPackageDigests: ReadonlySet<string> = new Set([
  'f98811f8f5eab265ad9e0df532bf71b1e54bdb2b8cc111866c2c4c87f1b7dcd8',
  '2a86a2982c042385850daac8962b3cef1d49f838e03a503032b0c6a5874a644f',
  'd803bfe64a59f39b3472ef6af1c29e85fa98a6634246460db3466e38fa641a02',
]);

export function runOnceEligible(appId: string, packageSha256: string): boolean {
  if (businessUiFor(appId) !== null) return false;
  return confirmedV1SingleRunPackageDigests.has(packageSha256);
}

export type FirstSlot =
  | { kind: 'deploy'; entry: null }
  | { kind: 'manage'; entry: null }
  | { kind: 'business'; entry: BusinessUiEntry }
  | { kind: 'sidecar'; projection: SidecarUiProjection }
  | { kind: 'status'; entry: null };

export function sidecarProjectionServable(
  projection: SidecarUiProjection,
  appId: string,
  adminOrigin: string
): boolean {
  if (projection.state !== 'active') return false;
  if (!projection.ui_base_path) return false;
  if (projection.app_id !== appId) return false;
  if (projection.host_ui_abi !== 1) return false;
  if (projection.app_site_origin == null) return true;
  if (!projection.app_site_origin) return false;
  let appOrigin: string;
  try {
    appOrigin = new URL(projection.app_site_origin).origin;
  } catch {
    return false;
  }
  if (appOrigin !== projection.app_site_origin) return false;
  if (adminOrigin === appOrigin) return false;
  return true;
}

export function firstSlotFor(
  view: AppView | null,
  authBlock: AppAuthBlock,
  sidecar?: SidecarView
): FirstSlot {
  if (authBlock !== null || view === null) {
    return { kind: 'status', entry: null };
  }
  if (view.kind === 'not_installed') {
    return { kind: 'deploy', entry: null };
  }
  if (view.kind === 'installed') {
    if (
      sidecar &&
      sidecar.kind === 'active' &&
      sidecarProjectionServable(
        sidecar.projection,
        view.record.appId,
        window.location.origin
      )
    ) {
      return { kind: 'sidecar', projection: sidecar.projection };
    }
    const entry = businessUiFor(view.record.appId);
    return entry
      ? { kind: 'business', entry }
      : { kind: 'manage', entry: null };
  }
  return { kind: 'status', entry: null };
}
