import request from '../request'

/** Day/night ISP mode (matches firmware IMAGE_ISP_MODE_DAY/NIGHT/AUTO). */
export const ISP_MODE_DAY = 0
export const ISP_MODE_NIGHT = 1
export const ISP_MODE_AUTO = 2
export type IspMode = typeof ISP_MODE_DAY | typeof ISP_MODE_NIGHT | typeof ISP_MODE_AUTO

export type DayNightSource = 'time' | 'light_sensor' | 'isp_stats'

export interface DayNightConfig {
    mode: 'day' | 'night' | 'auto'
    auto_source: DayNightSource
    custom_schedule: {
        start_hour: number
        start_minute: number
        end_hour: number
        end_minute: number
    }
    light_sensor: {
        night_threshold: number
        day_threshold: number
    }
    isp_stats: {
        night_enter_lux: number
        day_enter_lux: number
        night_enter_exposure_us: number
        night_enter_gain_mdb: number
        night_enter_avgl: number
        day_enter_exposure_us: number
        day_enter_gain_mdb: number
        day_enter_avgl: number
        ema_alpha_num: number
        ema_alpha_den: number
    }
    ir_brightness: number
}

export interface DayNightStatus {
    configured: boolean
    mode: 'day' | 'night' | 'auto'
    control: 'manual' | 'auto'
    source: DayNightSource
    effective_mode: 'day' | 'night' | 'auto'
    ir_on: boolean
    ir_brightness: number
    ircut_mode: number
    metrics: {
        valid: boolean
        lux: number
        lux_ema: number
        avg_l: number
        avg_l_ema: number
        exposure_us: number
        gain_mdb: number
        light_sensor_percent: number
    }
}

export interface SetHardwareInfoReq {
    brightness: number;
    contrast: number;
    horizontal_flip: boolean;
    vertical_flip: boolean;
    aec: number;
    isp_mode: IspMode;
    fast_capture_skip_frames: number;
    fast_capture_resolution: number;
    fast_capture_jpeg_quality: number;
    capture_disable_comm: boolean;
    capture_storage_ai: boolean;
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
    getDayNightConfigReq: () => request.get<DayNightConfig>('/api/v1/device/day-night/config'),
    setDayNightConfigReq: (data: Partial<DayNightConfig>) => request.post('/api/v1/device/day-night/config', data),
    getDayNightStatusReq: () => request.get<DayNightStatus>('/api/v1/device/day-night/status'),
}

export default hardwareManagement;