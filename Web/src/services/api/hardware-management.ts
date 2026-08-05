import request from '../request'

export interface SetHardwareInfoReq {
    brightness: number;
    contrast: number;
    horizontal_flip: boolean;
    vertical_flip: boolean;
    aec: number;
    isp_mode: number;
    grayscale: boolean;
    /* The following are optional — backend only updates fields that are present
       in the request body. They are managed on the Capture Settings page. */
    fast_capture_skip_frames?: number;
    fast_capture_resolution?: number;
    fast_capture_jpeg_quality?: number;
    capture_disable_comm?: boolean;
    capture_storage_ai?: boolean;
}
export interface SetLightConfigReq {
    mode: 'auto' | 'custom' | 'off';
    brightness_level: number;
    connected?: boolean;
    custom_schedule: {
        start_hour: number;
        start_minute: number;
        end_hour: number;
        end_minute: number;
    }
}

export interface ControlLightReq {
    enable: boolean;
}

/** FSBL persisted profile ids (see fsbl_app_common.h) */
export type SysClkProfileId = 1 | 2 | 3 | 4;

export interface SysClkConfigRes {
    valid: boolean;
    sys_clk_profile: number;
}

export interface SetSysClkConfigReq {
    sys_clk_profile: SysClkProfileId;
}

const hardwareManagement = {
    getHardwareInfoReq: () => request.get('/api/v1/device/image/config'),
    setHardwareInfoReq: (data: SetHardwareInfoReq) => request.post('/api/v1/device/image/config', data),
    getSysClkConfigReq: () => request.get<SysClkConfigRes>('/api/v1/device/sys-clk/config'),
    setSysClkConfigReq: (data: SetSysClkConfigReq) => request.post('/api/v1/device/sys-clk/config', data),
    getIspProfileExportReq: () => request.get('/api/v1/isp/config/export'),
    postIspProfileImportReq: (body: Record<string, unknown>) => request.post('/api/v1/isp/config/import', body),
    getLightConfigReq: () => request.get('/api/v1/device/light/config'),
    setLightConfigReq: (data: SetLightConfigReq) => request.post('/api/v1/device/light/config', data),
    controlLightReq: (data: ControlLightReq) => request.post('/api/v1/device/light/control', data),
}

export default hardwareManagement;