import request from "../request";

export type CaptureMode = 'instant' | 'batch' | 'scheduled' | 'local_only';
export type CaptureStorage = 'auto' | 'flash' | 'sd' | 'none';
export type StoragePolicy = 'wrap' | 'stop';
export type UploadProto = 'mqtt' | 'webhook';

export interface CaptureUploadConfig {
  version: number;
  mode: CaptureMode;
  storage: CaptureStorage;
  policy: StoragePolicy;
  upload_protocol: UploadProto;
  retry_enable: boolean;
  retry_max_attempts: number;
  batch_count: number;
  schedule_minutes: number[];
  keep_sent_hours: number;
  max_pending_records: number;
  /** 'default' = use system comm-pref (init all netifs); else a specific netif */
  upload_network: 'default' | 'wifi' | 'halow' | 'cellular' | 'poe';
  /** Compile-time flash record cap (0 = no limit / SD). Exposed for UI display. */
  flash_max_records: number;
}

export type RecordState = 'pending' | 'sent' | 'failed' | 'local';

export interface RecordInfo {
  id: string;
  state: RecordState;
  timestamp: number;
  size: number;
  retry_count: number;
  trigger: string;
  last_error: string;
  has_inference: boolean;
}

export interface RecordListResponse {
  state: string;
  offset: number;
  limit: number;
  total: number;
  records: RecordInfo[];
}

export interface RecordListParams {
  state: RecordState;
  offset?: number;
  limit?: number;
  /** Unix timestamp — return records with timestamp >= from */
  from?: number;
  /** Unix timestamp — return records with timestamp <= to */
  to?: number;
  /** Sort direction: "asc" (oldest first, default) or "desc" (newest first) */
  sort?: 'asc' | 'desc';
}

export interface QueueStatus {
  mode: CaptureMode;
  storage: CaptureStorage;
  actual_fs: 'sd' | 'flash';
  initialized: boolean;
  running: boolean;
  storage_full: boolean;
  pending_count: number;
  sent_count: number;
  failed_count: number;
  local_count: number;
  bytes_used_kb: number;
  bytes_available_kb: number;
  next_scheduled_at: number;
}

const captureSettings = {
  /** Get capture-upload config */
  getUploadConfig: () => request.get<CaptureUploadConfig>('/api/v1/capture/upload-config'),

  /** Set capture-upload config */
  setUploadConfig: (data: Partial<CaptureUploadConfig>) => request.post('/api/v1/capture/upload-config', data),

  /** Get queue/coordinator status */
  getQueueStatus: () => request.get<QueueStatus>('/api/v1/capture/queue'),

  /** List records by state, with optional time-range filter */
  listRecords: (params: RecordListParams) => request.get<RecordListResponse>('/api/v1/capture/records', {
      params: {
        state: params.state,
        offset: String(params.offset ?? 0),
        limit: String(params.limit ?? 50),
        ...(params.from ? { from: String(params.from) } : {}),
        ...(params.to ? { to: String(params.to) } : {}),
        ...(params.sort ? { sort: params.sort } : {}),
      },
    }),

  /** Retry one record */
  retryRecord: (id: string) => request.post('/api/v1/capture/records/retry', { id }),

  /** Retry selected records (batch) */
  retryRecords: (ids: string[]) => request.post('/api/v1/capture/records/retry', { ids }),

  /** Retry all failed records */
  retryAllFailed: () => request.post('/api/v1/capture/records/retry', { all: true }),

  /** Delete a single record */
  deleteRecord: (id: string) => request.delete('/api/v1/capture/records', { params: { id } }),

  /** Delete selected records (batch) */
  deleteRecords: (ids: string[]) => request.post('/api/v1/capture/records/delete', { ids }),
};

export default captureSettings;
