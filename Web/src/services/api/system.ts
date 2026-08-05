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
const systemApis = {
  getDeviceInfoReq: (config?: { skipErrorToast?: boolean; signal?: AbortSignal }) => request.get('/api/v1/device/info', config),
  setSystemTimeReq: (data: SetSystemTimeReq) => request.post('/api/v1/system/time', data),
  setDeviceNameReq: (data: { device_name: string }) => request.post('/api/v1/device/name', data),
  uploadOTAFileReq: (file: Blob, firmwareType: FirmwareType) => request.post(
    '/api/v1/system/ota/upload',
    file,
    {
      headers: { 'Content-Type': 'application/octet-stream' },
      params: { firmwareType },
    },
  ),
  preCheckReq: (file: Blob, firmwareType: FirmwareType) => request.post(
    '/api/v1/system/ota/precheck',
    file,
    { headers: { 'Content-Type': 'application/octet-stream' }, params: { firmwareType } },
  ),
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
