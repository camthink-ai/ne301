import request from '../request';

export interface CameraStreamConfig {
  width: number;
  height: number;
  fps: number;
  format?: string;
}

export interface CameraInfo {
  type: 'native' | 'uvc';
  present: boolean;
  model?: string;
  product?: string;
  vid?: string;
  pid?: string;
  bulk?: boolean;
  configs: CameraStreamConfig[];
}

export interface UvcStatus {
  active: boolean;
  streaming: boolean;
  width: number;
  height: number;
  fps: number;
  fps_measured?: number;
  frames: number;
  frames_bad: number;
  reconnects: number;
}

export interface CameraListData {
  active: 'native' | 'uvc';
  cameras: CameraInfo[];
  uvc_status: UvcStatus;
}

export interface CameraConfigData {
  source: 'native' | 'uvc';
  native_stream_res: number;
  uvc_stream: { width: number; height: number; fps: number };
  restart_required: boolean;
}

export interface CameraConfigReq {
  source?: 'native' | 'uvc';
  native_stream_res?: number;
  uvc_stream?: { width?: number; height?: number; fps?: number };
}

const camera = {
  getCameraList: () => request.get<CameraListData>('api/v1/camera/list'),
  detectCameras: () => request.post<CameraListData>('api/v1/camera/detect', {}),
  getCameraConfig: () => request.get<CameraConfigData>('api/v1/camera/config'),
  setCameraConfig: (data: CameraConfigReq) => request.put('api/v1/camera/config', data),
};

export default camera;
