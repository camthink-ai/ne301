import request from '../request';

interface SetSystemTimeReq {
  timestamp: number;
  timezone: string;
}

export interface DeviceInfo {
  battery_percent: number;
  camera_module: string;
  communication_type: string;
  device_name: string;
  extension_modules: string;
  hardware_version: string;
  mac_address: string;
  power_supply_type: string;
  serial_number: string;
  software_version: string;
  storage_card_info: string;
  storage_usage_percent: number;
}
export interface UpdateOTAReq {
  filename: string;
  firmware_type: string;
  validate_crc32: boolean;
  validate_signature: boolean;
  allow_downgrade: boolean;
  auto_upgrade: boolean;
}
export interface ExportFirmwareRes {
  filename: string;
  firmware_type: string;
}
interface ExportFirmwareReq {
  firmware_type: string;
  filename: string;
}
export type FirmwareType = 'app' | 'web' | 'ai' | 'fsbl' | 'wifi';

/** One firmware sub-package inside an OTA bundle, as planned by the device.
 *  Every entry burns unless the user skips it — skipping is offered for
 *  same_version entries only and is re-validated by the device at begin. */
export interface BundlePlanEntry {
  type: 'fsbl' | 'app' | 'web' | 'ai' | 'wifi' | 'unknown';
  fw_type: number;
  /** same version AND unmoved partition → user may skip this entry
   *  (re-validated by the device at begin) */
  skippable: boolean;
  cur_version: string;
  new_version: string;
  size: number;
  /** byte offset of this sub-package within the bundle file (after the 4KB header) */
  offset: number;
  /** absolute flash address resolved by the device */
  addr?: number;
}
/** One partition that differs between the bundle table and the device's
 *  compile-time table (-1 base/size = absent from the bundle table) */
export interface BundleLayoutDiff {
  part: string;
  device_base: number;
  device_size: number;
  bundle_base: number;
  bundle_size: number;
}
export interface BundlePreCheckRes {
  layout_changed: boolean;
  /** present (possibly empty) when layout_changed — which partitions differ */
  layout_diff?: BundleLayoutDiff[];
  total_bytes: number;
  entries: BundlePlanEntry[];
}

const systemApis = {
  getDeviceInfoReq: (config?: { skipErrorToast?: boolean; signal?: AbortSignal }) => request.get('/api/v1/device/info', config),
  setSystemTimeReq: (data: SetSystemTimeReq) => request.post('/api/v1/system/time', data),
  setDeviceNameReq: (data: { device_name: string }) => request.post('/api/v1/device/name', data),
  uploadOTAFileReq: (
    file: Blob,
    firmwareType: FirmwareType,
    config?: {
      /** bundle direct-address mode (partition migration) */
      direct?: boolean;
      addr?: number;
      onUploadProgress?: (progressEvent: { loaded: number; total?: number }) => void;
    },
  ) => request.post(
    '/api/v1/system/ota/upload',
    file,
    {
      headers: { 'Content-Type': 'application/octet-stream' },
      params: {
        firmwareType,
        ...(config?.direct
          ? { direct: 1, addr: `0x${(config.addr ?? 0).toString(16)}` }
          : {}),
      },
      onUploadProgress: config?.onUploadProgress,
    },
  ),
  preCheckReq: (file: Blob, firmwareType: FirmwareType) => request.post(
    '/api/v1/system/ota/precheck',
    file,
    { headers: { 'Content-Type': 'application/octet-stream' }, params: { firmwareType } },
  ),
  /** OTA bundle (one-click full upgrade): send the 4096-byte bundle header,
   *  the device answers with the burn plan */
  bundlePreCheckReq: (header: Blob) => request.post(
    '/api/v1/system/ota/bundle/precheck',
    header,
    { headers: { 'Content-Type': 'application/octet-stream' } },
  ),
  /** begin the burn session; `skip` lists same-version entries the user chose
   *  not to re-burn (device rejects anything else) */
  bundleBeginReq: (skip?: string[]) => request.post(
    '/api/v1/system/ota/bundle/begin',
    skip && skip.length > 0 ? { skip } : undefined,
  ),
  bundleFinishReq: (result: 'ok' | 'abort') => request.post('/api/v1/system/ota/bundle/finish', { result }),
  reloadModelReq: () => request.post('/api/v1/model/reload'),
  updateOTAReq: (data: UpdateOTAReq) => request.post('/api/v1/system/ota/upgrade-local', data),
  uploadDeviceFileReq: (data: any) => request.post('/api/v1/device/config/import', data),
  exportDeviceFileReq: () => request.get('/api/v1/device/config/export'),
  exportFirmwareReq: (data: ExportFirmwareReq) => request.post('/api/v1/system/ota/export', data, { responseType: 'blob' as any }),
  restartDevice: (
    data: { delay_seconds: number },
    config?: { skipErrorToast?: boolean },
  ) => request.post('/api/v1/system/restart', data, config),
  getLogsReq: () => request.get('/api/v1/system/logs'),
  exportLogsReq: () => request.get('/api/v1/system/logs/export'),
  getVersionsReq: () => request.get('/api/v1/device/firmware-versions'),
  versionCheckReq: () => request.get('/api/v1/device/version-check'),
};

export default systemApis;
