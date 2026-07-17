import request from '../request';

export interface WorkModeSwitchReq {
  mode: 'image' | 'video_stream';
}

export interface AiParams {
  nms_threshold: number;
  confidence_threshold: number;
}
export interface VideoStreamPushReq {
  enabled: boolean;
  server_url?: string;
}
// export interface ImageTriggerReq {
//     io_trigger?: boolean,
//     pir_trigger?: boolean,
//     timer_trigger?: boolean
// }

export interface PhotoCaptureReq {
  enable_ai: boolean;
  chunk_size: number;
  store_to_sd: boolean;
}

export interface RtmpConfigReq {
  url: string;
  stream_key: string;
  enabled: boolean;
}
export interface RtmpStartReq {
  url?: string;
  stream_key?: string;
}

export type RtspAuthMode = 'none' | 'digest';

export interface RtspConfigReq {
  enabled: boolean;
  port: number;
  auth_mode: RtspAuthMode;
  username: string;
  password: string;
}

export interface RtspStatus {
  running: boolean;
  stream_url: string;
}

export interface RtspClient {
  id: string;
  ip: string;
  connected_duration: string;
}

export interface PirConfigReq {
  pir_trigger: {
    enable: boolean;
    trigger_type:
      | 'rising_edge'
      | 'falling_edge'
      | 'high_level'
      | 'low_level'
      | 'both_edges';
    sensitivity_level: number; // 10-255
    ignore_time_s: number; // 0-15
    pulse_count: number; // 1-4
    window_time_s: number; // 0-3
    disable_in_preview: boolean; // Disable PIR capture during preview (default: true)
  };
  timer_trigger?: {
    enable: boolean;
    capture_mode: string;
    interval_sec: number;
    time_node_count: number;
    time_node: string[];
    weekdays: number[];
    interval_mode: 'normal' | 'scheduled';
    start_time: string;
  };
  remote_trigger?: {
    enable: boolean;
  };
}
const deviceTool = {
  // Whether to enable video stream
  startVideoStreamReq: () => request.post('api/v1/preview/start'),
  stopVideoStreamReq: () => request.post('api/v1/preview/stop'),

  // Get work mode status
  getWorkModeStatusReq: () => request.get('/api/v1/work-mode/status'),
  // Switch work mode
  switchWorkModeReq: (data: WorkModeSwitchReq) => request.post('/api/v1/work-mode/switch', data),

  // Configure video stream push
  configVideoStreamPushReq: (data: VideoStreamPushReq) => request.post('/api/v1/work-mode/video-stream/config', data),

  // Configure image mode trigger
  configTriggerConfigReq: (data: PirConfigReq) => request.post('/api/v1/work-mode/triggers', data),

  getTriggerConfigReq: () => request.get('/api/v1/work-mode/triggers'),

  // Switch power mode
  switchPowerModeReq: (data: { mode: string }) => request.post('api/v1/power-mode/switch', data),
  getPowerModeReq: () => request.get('api/v1/power-mode/status'),
  // -----------------

  // Get AI status
  getAiStatusReq: () => request.get('/api/v1/ai/status'),
  toggleAiReq: (data: { ai_enabled: boolean }) => request.post('/api/v1/ai/toggle', data),
  getTriggerMethodReq: () => request.get('/api/v1/trigger-method/status'),

  // AI parameters
  getAiParamsReq: () => request.get('/api/v1/ai/params'),
  setAiParamsReq: (data: AiParams) => request.post('/api/v1/ai/params', data),

  // Photo capture
  photoCaptureReq: (data: PhotoCaptureReq) => request.post('/api/v1/device/capture', data),

  // RTMP
  getRtmpConfigReq: () => request.get('/api/v1/apps/rtmp/config'),
  setRtmpConfigReq: (data: RtmpConfigReq) => request.post('/api/v1/apps/rtmp/config', data),
  startRtmpReq: (data: RtmpStartReq) => request.post('/api/v1/apps/rtmp/start', data),
  stopRtmpReq: () => request.post('/api/v1/apps/rtmp/stop'),
  getRtmpStatusReq: () => request.get('/api/v1/apps/rtmp/status'),

  // RTSP
  getRtspConfigReq: () => request.get('/api/v1/apps/rtsp/config'),
  setRtspConfigReq: (data: RtspConfigReq) => request.put('/api/v1/apps/rtsp/config', data),
  getRtspStatusReq: () => request.get('/api/v1/apps/rtsp/status'),
  getRtspClientsReq: () => request.get('/api/v1/apps/rtsp/clients'),
  kickRtspClientReq: (id: string) => request.delete(`/api/v1/apps/rtsp/clients/${id}`),

  getStreamTabPrefReq: () => (request.get('/api/v1/device/preference/stream_tab')),
  setStreamTabPrefReq: (data: { stream_tab: string }) => (request.post('/api/v1/device/preference/stream_tab', data)),

  // // PIR
  // getPirConfigReq: () => request.get('/api/v1/work-mode/triggers'),
  // setPirConfigReq: (data: PirConfigReq) => request.post('/api/v1/work-mode/triggers', data),
};

export default deviceTool;
