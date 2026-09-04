/**
 * RTMP 推流控制 React 组件示例
 * 
 * 依赖: React 18+, TypeScript
 * 安装: npm install react react-dom
 */

import React, { useState, useEffect, useCallback } from 'react';

// ==================== 类型定义 ====================

interface RtmpConnection {
  url: string;
  stream_key: string;
  auto_start: boolean;
  auto_reconnect: boolean;
  reconnect_interval_ms: number;
  max_reconnect_attempts: number;
  connection_timeout_ms: number;
}

interface RtmpVideo {
  width: number;
  height: number;
  fps: number;
  bitrate_kbps: number;
  gop_size: number;
}

interface RtmpStatus {
  initialized: boolean;
  running: boolean;
  streaming: boolean;
  stream_state: StreamState;
  url?: string;
  version: string;
}

interface RtmpStatistics {
  frames_sent: number;
  bytes_sent: number;
  keyframes_sent: number;
  dropped_frames: number;
  errors: number;
  reconnect_count: number;
  current_bitrate_kbps: number;
  avg_frame_size: number;
  stream_duration_sec: number;
}

interface RtmpConfig {
  connection: RtmpConnection;
  video: RtmpVideo;
  status: RtmpStatus;
}

interface RtmpStatusResponse {
  status: RtmpStatus;
  statistics: RtmpStatistics;
}

type StreamState = 'idle' | 'connecting' | 'streaming' | 'reconnecting' | 'stopping' | 'error';

// ==================== API 服务 ====================

const API_BASE = '/api/v1';

class RtmpApiService {
  private authToken: string;

  constructor(authToken: string) {
    this.authToken = authToken;
  }

  private async request<T>(method: string, path: string, body?: object): Promise<T> {
    const response = await fetch(`${API_BASE}${path}`, {
      method,
      headers: {
        'Authorization': `Bearer ${this.authToken}`,
        'Content-Type': 'application/json',
      },
      body: body ? JSON.stringify(body) : undefined,
    });

    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    return response.json();
  }

  async getConfig(): Promise<RtmpConfig> {
    return this.request('GET', '/apps/rtmp/config');
  }

  async setConfig(config: Partial<{ connection: Partial<RtmpConnection>; video: Partial<RtmpVideo> }>): Promise<{ success: boolean; message: string }> {
    return this.request('POST', '/apps/rtmp/config', config);
  }

  async startStream(url?: string, streamKey?: string): Promise<{ success: boolean; message: string; stream_state: StreamState }> {
    const body: { url?: string; stream_key?: string } = {};
    if (url) body.url = url;
    if (streamKey) body.stream_key = streamKey;
    return this.request('POST', '/apps/rtmp/start', Object.keys(body).length > 0 ? body : undefined);
  }

  async stopStream(): Promise<{ success: boolean; message: string; stream_state: StreamState }> {
    return this.request('POST', '/apps/rtmp/stop');
  }

  async getStatus(): Promise<RtmpStatusResponse> {
    return this.request('GET', '/apps/rtmp/status');
  }
}

// ==================== Hooks ====================

function useRtmpApi(authToken: string) {
  const [api] = useState(() => new RtmpApiService(authToken));
  return api;
}

function useRtmpStatus(api: RtmpApiService, enabled: boolean, interval = 1000) {
  const [status, setStatus] = useState<RtmpStatusResponse | null>(null);
  const [error, setError] = useState<Error | null>(null);

  useEffect(() => {
    if (!enabled) return;

    const fetchStatus = async () => {
      try {
        const data = await api.getStatus();
        setStatus(data);
        setError(null);
      } catch (err) {
        setError(err as Error);
      }
    };

    fetchStatus();
    const id = setInterval(fetchStatus, interval);
    return () => clearInterval(id);
  }, [api, enabled, interval]);

  return { status, error };
}

// ==================== 组件 ====================

interface StreamControlProps {
  authToken: string;
}

export function RtmpStreamControl({ authToken }: StreamControlProps) {
  const api = useRtmpApi(authToken);
  
  const [url, setUrl] = useState('');
  const [streamKey, setStreamKey] = useState('');
  const [isStreaming, setIsStreaming] = useState(false);
  const [streamState, setStreamState] = useState<StreamState>('idle');
  const [loading, setLoading] = useState(false);
  const [message, setMessage] = useState<{ type: 'success' | 'error'; text: string } | null>(null);

  const { status } = useRtmpStatus(api, isStreaming);

  // 加载初始配置
  useEffect(() => {
    const loadConfig = async () => {
      try {
        const config = await api.getConfig();
        setUrl(config.connection.url || '');
        setStreamKey(config.connection.stream_key || '');
        setIsStreaming(config.status.streaming);
        setStreamState(config.status.stream_state);
      } catch (err) {
        console.error('Failed to load config:', err);
      }
    };
    loadConfig();
  }, [api]);

  // 更新状态
  useEffect(() => {
    if (status) {
      setIsStreaming(status.status.streaming);
      setStreamState(status.status.stream_state);
    }
  }, [status]);

  const handleStart = useCallback(async () => {
    if (!url) {
      setMessage({ type: 'error', text: '请输入RTMP服务器地址' });
      return;
    }

    setLoading(true);
    try {
      const result = await api.startStream(url, streamKey);
      setMessage({ type: result.success ? 'success' : 'error', text: result.message });
      setStreamState(result.stream_state);
      if (result.success) {
        setIsStreaming(true);
      }
    } catch (err) {
      setMessage({ type: 'error', text: (err as Error).message });
    } finally {
      setLoading(false);
    }
  }, [api, url, streamKey]);

  const handleStop = useCallback(async () => {
    setLoading(true);
    try {
      const result = await api.stopStream();
      setMessage({ type: result.success ? 'success' : 'error', text: result.message });
      setStreamState(result.stream_state);
      setIsStreaming(false);
    } catch (err) {
      setMessage({ type: 'error', text: (err as Error).message });
    } finally {
      setLoading(false);
    }
  }, [api]);

  const formatBytes = (bytes: number) => {
    if (bytes < 1024) return `${bytes} B`;
    if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
    return `${(bytes / 1024 / 1024).toFixed(2)} MB`;
  };

  const formatDuration = (seconds: number) => {
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    const s = seconds % 60;
    return h > 0 ? `${h}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}` 
                 : `${m}:${s.toString().padStart(2, '0')}`;
  };

  return (
    <div style={styles.container}>
      <h2 style={styles.title}>RTMP 推流控制</h2>

      {/* 状态显示 */}
      <div style={styles.statusBar}>
        <span style={{ ...styles.badge, ...getBadgeStyle(streamState) }}>
          {streamState.toUpperCase()}
        </span>
        {isStreaming && <span style={styles.liveIndicator}>● LIVE</span>}
      </div>

      {/* 消息提示 */}
      {message && (
        <div style={{ ...styles.message, ...(message.type === 'error' ? styles.messageError : styles.messageSuccess) }}>
          {message.text}
        </div>
      )}

      {/* 输入表单 */}
      <div style={styles.form}>
        <div style={styles.formGroup}>
          <label style={styles.label}>RTMP 服务器地址</label>
          <input
            type="text"
            value={url}
            onChange={(e) => setUrl(e.target.value)}
            placeholder="rtmp://live.example.com/live"
            style={styles.input}
            disabled={isStreaming}
          />
        </div>
        <div style={styles.formGroup}>
          <label style={styles.label}>推流密钥</label>
          <input
            type="text"
            value={streamKey}
            onChange={(e) => setStreamKey(e.target.value)}
            placeholder="your-stream-key"
            style={styles.input}
            disabled={isStreaming}
          />
        </div>
      </div>

      {/* 控制按钮 */}
      <div style={styles.buttonGroup}>
        <button
          onClick={handleStart}
          disabled={loading || isStreaming}
          style={{ ...styles.button, ...styles.buttonSuccess, ...(isStreaming && styles.buttonDisabled) }}
        >
          {loading && !isStreaming ? '启动中...' : '开始推流'}
        </button>
        <button
          onClick={handleStop}
          disabled={loading || !isStreaming}
          style={{ ...styles.button, ...styles.buttonDanger, ...(!isStreaming && styles.buttonDisabled) }}
        >
          {loading && isStreaming ? '停止中...' : '停止推流'}
        </button>
      </div>

      {/* 统计信息 */}
      {status?.statistics && (
        <div style={styles.statsGrid}>
          <StatCard label="发送帧数" value={status.statistics.frames_sent.toLocaleString()} />
          <StatCard label="发送数据" value={formatBytes(status.statistics.bytes_sent)} />
          <StatCard label="推流时长" value={formatDuration(status.statistics.stream_duration_sec)} />
          <StatCard label="当前码率" value={`${status.statistics.current_bitrate_kbps || status.statistics.avg_frame_size} kbps`} />
          <StatCard label="丢帧数" value={status.statistics.dropped_frames.toString()} />
          <StatCard label="错误数" value={status.statistics.errors.toString()} />
        </div>
      )}
    </div>
  );
}

function StatCard({ label, value }: { label: string; value: string }) {
  return (
    <div style={styles.statCard}>
      <div style={styles.statValue}>{value}</div>
      <div style={styles.statLabel}>{label}</div>
    </div>
  );
}

function getBadgeStyle(state: StreamState): React.CSSProperties {
  const colors: Record<StreamState, string> = {
    idle: '#6b7280',
    connecting: '#f59e0b',
    streaming: '#10b981',
    reconnecting: '#f59e0b',
    stopping: '#6b7280',
    error: '#ef4444',
  };
  return { backgroundColor: colors[state] };
}

// ==================== 样式 ====================

const styles: Record<string, React.CSSProperties> = {
  container: {
    fontFamily: '-apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif',
    maxWidth: '600px',
    margin: '0 auto',
    padding: '24px',
    backgroundColor: '#1f2937',
    borderRadius: '12px',
    color: '#f3f4f6',
  },
  title: {
    margin: '0 0 24px 0',
    fontSize: '24px',
    fontWeight: 600,
    color: '#00d9ff',
  },
  statusBar: {
    display: 'flex',
    alignItems: 'center',
    gap: '12px',
    marginBottom: '20px',
  },
  badge: {
    padding: '4px 12px',
    borderRadius: '16px',
    fontSize: '12px',
    fontWeight: 600,
    color: '#fff',
  },
  liveIndicator: {
    color: '#ef4444',
    fontWeight: 600,
    fontSize: '14px',
    animation: 'blink 1s infinite',
  },
  message: {
    padding: '12px 16px',
    borderRadius: '8px',
    marginBottom: '16px',
    fontSize: '14px',
  },
  messageSuccess: {
    backgroundColor: 'rgba(16, 185, 129, 0.2)',
    color: '#10b981',
  },
  messageError: {
    backgroundColor: 'rgba(239, 68, 68, 0.2)',
    color: '#ef4444',
  },
  form: {
    marginBottom: '20px',
  },
  formGroup: {
    marginBottom: '16px',
  },
  label: {
    display: 'block',
    marginBottom: '6px',
    fontSize: '14px',
    color: '#9ca3af',
  },
  input: {
    width: '100%',
    padding: '10px 14px',
    border: '1px solid #374151',
    borderRadius: '8px',
    backgroundColor: '#111827',
    color: '#f3f4f6',
    fontSize: '14px',
    outline: 'none',
  },
  buttonGroup: {
    display: 'flex',
    gap: '12px',
    marginBottom: '24px',
  },
  button: {
    flex: 1,
    padding: '12px 24px',
    border: 'none',
    borderRadius: '8px',
    fontSize: '14px',
    fontWeight: 600,
    cursor: 'pointer',
    transition: 'opacity 0.2s',
  },
  buttonSuccess: {
    backgroundColor: '#10b981',
    color: '#fff',
  },
  buttonDanger: {
    backgroundColor: '#ef4444',
    color: '#fff',
  },
  buttonDisabled: {
    opacity: 0.5,
    cursor: 'not-allowed',
  },
  statsGrid: {
    display: 'grid',
    gridTemplateColumns: 'repeat(3, 1fr)',
    gap: '12px',
  },
  statCard: {
    backgroundColor: '#111827',
    padding: '16px',
    borderRadius: '8px',
    textAlign: 'center' as const,
  },
  statValue: {
    fontSize: '20px',
    fontWeight: 700,
    color: '#00d9ff',
  },
  statLabel: {
    fontSize: '12px',
    color: '#9ca3af',
    marginTop: '4px',
  },
};

// ==================== 使用示例 ====================

/*
// App.tsx
import { RtmpStreamControl } from './rtmp_react_example';

function App() {
  return (
    <div style={{ padding: '40px', backgroundColor: '#111827', minHeight: '100vh' }}>
      <RtmpStreamControl authToken="YOUR_AUTH_TOKEN" />
    </div>
  );
}

export default App;
*/

export default RtmpStreamControl;

