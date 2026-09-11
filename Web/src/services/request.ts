import axios from 'axios'
import { setItem } from '@/utils/storage';
import { i18n } from '@lingui/core';
import { toast } from 'sonner';
import { debounce } from 'throttle-debounce';

// Interfaces that need longer timeout (long-running tasks)
const longTimeTaskList = [
  '/api/v1/system/ota/export',
  '/api/v1/device/config/export',
  '/api/v1/system/ota/upload',
  '/api/v1/system/ota/upgrade-local',
  '/api/v1/system/restart',
  '/api/v1/files/upload',
  '/api/v1/files/download',
  '/api/v1/files/list',       // may be slow after upload (LittleFS metadata flush)
  '/api/v1/device/storage/format',  // lfs_format + lfs_mount on full flash
]

const debouncedTimeoutError = debounce(2000, (message: string) => {
  toast.error(message);
}, { atBegin: true });

// Frontend network diagnostics: API path in error toasts + the failure-log
// viewer card on the device information page. Flip to true (and rebuild) when
// debugging field reports; false keeps toasts clean and hides the viewer.
export const NET_DIAG = false

// Failure log: last 50 failed requests kept in localStorage for on-site diagnosis.
// Dump with __netlog() in the browser console. Key fields:
//   code: ERR_NETWORK = socket died instantly; ECONNABORTED = timed out
//   elapsedMs: <1000 means the socket was rejected/already dead, not a timeout
const NETLOG_KEY = 'netlog'

// Low-level writer shared by the HTTP layer (logNetError) and the WS/video
// pipeline (logStreamError) so the device-information page's failure viewer
// shows both kinds of failure.
const pushNetLog = (entry: Record<string, unknown>) => {
  console.error('[netlog]', entry)
  try {
    const list = JSON.parse(localStorage.getItem(NETLOG_KEY) || '[]')
    list.push(entry)
    localStorage.setItem(NETLOG_KEY, JSON.stringify(list.slice(-50)))
  } catch {
    /* storage full/unavailable - console.error above still fired */
  }
}

const logNetError = (phase: string, error: any) => {
  if (!NET_DIAG) return
  const cfg = error?.config || error?.request?.config
  pushNetLog({
    time: new Date().toISOString(),
    api: `${String(cfg?.method || 'get').toUpperCase()} ${cfg?.url || '?'}`,
    phase, // 'no-response' | 'http' | 'request-setup'
    code: error?.code || '',
    message: error?.message || String(error),
    status: error?.response?.status,
    elapsedMs: cfg?.reqT0 != null ? Date.now() - cfg.reqT0 : undefined,
    retries: cfg?.retriesDone || 0,
  })
}

// WebSocket / video-stream failures captured from the player pipeline and
// written into the same netlog so the on-device viewer shows them too.
// `api` is a short source tag ('WS' / 'VIDEO'); `phase` names the failure.
export const logStreamError = (api: string, phase: string, code: string, message: string) => {
  if (!NET_DIAG) return
  pushNetLog({
    time: new Date().toISOString(),
    api,
    phase,
    code: code || '',
    message: message || '',
    status: undefined,
    elapsedMs: undefined,
    retries: 0,
  })
}
if (NET_DIAG) {
  (window as any).dumpNetLog = () => {
    console.table(JSON.parse(localStorage.getItem(NETLOG_KEY) || '[]'))
  }
}
// In-page viewer (device information page) - mobile has no devtools
export const getNetLog = (): any[] => {
  try {
    return JSON.parse(localStorage.getItem(NETLOG_KEY) || '[]')
  } catch {
    return []
  }
}
export const clearNetLog = () => localStorage.removeItem(NETLOG_KEY)

// Append the failing API to error toasts so testers can locate the request
const apiTag = (config?: any): string => {
  if (!NET_DIAG) return ''
  const url = config?.url
  if (!url) return ''
  return ` [${String(config.method || 'get').toUpperCase()} ${url}]`
}

const request = axios.create({
  baseURL: '/',
  timeout: 20000,
  headers: { 'Content-Type': 'application/json' },
})

// Request interceptor
request.interceptors.request.use(
  (config) => {
    // Add token to request header
    const token = localStorage.getItem('token')
    if (token) {
      const cleanToken = token.replace(/^"(.*)"$/, '$1');
      (config.headers as any).Authorization = cleanToken;
    }

    // If FormData, delete Content-Type to let browser set it automatically
    if (config.data instanceof FormData) {
      delete (config.headers as any)['Content-Type'];
    }

    // Add timestamp to prevent caching
    if (config.method === 'get') {
      config.params = {
        ...config.params,
        _t: Date.now(),
      }
    }
    // Dynamically set timeout: 10min for long tasks (upload/download), 30s for others
    const url = (config.url || '') as string
    const isLongTask = longTimeTaskList.some((p) => url.includes(p))
    config.timeout = isLongTask ? 900000 : 30000

    const reqCfg = config as any
    reqCfg.reqT0 = Date.now()

    return config
  },
  (error) => {
    logNetError('request-setup', error)
    return Promise.reject(error)
  }
)

// Response interceptor
request.interceptors.response.use(
  (response) => {
    const { data } = response

    // If response is file/binary, return raw data directly
    const contentType = ((response.headers || {}) as any)['content-type'] as string | undefined
    const isBlob = typeof Blob !== 'undefined' && data instanceof Blob
    const isArrayBuffer = typeof ArrayBuffer !== 'undefined' && data instanceof ArrayBuffer
    const isBinaryContentType = typeof contentType === 'string' && /octet-stream|application\/pdf|image\/.+|video\/.+|audio\/.+|zip|gzip/i.test(contentType)
    const isBinaryResponseType = (response.request && response.request.responseType) === 'blob' || (response.request && response.request.responseType) === 'arraybuffer'

    if (isBlob || isArrayBuffer || isBinaryContentType || isBinaryResponseType) {
      return data
    }

    if (data.success) {
      setItem('lastRequestTime', Date.now().toString());
      return data
    }
    //  special case for unauthorized error
    if (data.error_code === 'UNAUTHORIZED' && window.location.pathname.includes('/login') && window.location.pathname !== '/') {
      return Promise.reject(response)
    }
    if (!response.config?.skipErrorToast) {
      toast.error(i18n._(`errors.business.${data.error_code}`) + apiTag(response.config))
    }
    return Promise.reject(response)
  },
  (error) => {
    const isCanceled = axios.isCancel(error) || error.code === 'ERR_CANCELED'
    if (isCanceled) {
      return Promise.reject(error)
    }
    // Radio-hole retry: on the phone WiFi link a traffic burst can black-hole
    // packets for ~10s (device serves the request, phone never sees bytes -
    // see netlog ERR_NETWORK entries). GET is idempotent, so retry silently:
    // 1s then 8s backoff crosses the hole; only the exhausted failure reaches
    // the toast/netlog. POST etc. never retry.
    const cfg = error.config || {}
    const retriesDone: number = cfg.retriesDone || 0
    if (
      !error.response
      && error.code === 'ERR_NETWORK'
      && String(cfg.method || 'get').toLowerCase() === 'get'
      && retriesDone < 2
    ) {
      cfg.retriesDone = retriesDone + 1
      const delay = retriesDone === 0 ? 1000 : 8000
      return new Promise((resolve) => {
        setTimeout(() => {
          resolve(request(cfg))
        }, delay)
      })
    }
    const skipErrorToast = error.config?.skipErrorToast
    if (!error.response) {
      logNetError('no-response', error)
      const errorMessage = String(error)
      if (!skipErrorToast) {
        debouncedTimeoutError(errorMessage + apiTag(error.config))
      }
      return Promise.reject(error)
    }
    logNetError('http', error)
    const { status } = error.response
    switch (status) {
      case 401:
        // Unauthorized, clear token and redirect to login page
        localStorage.removeItem('token')
        // 'login route
        if (!window.location.pathname.includes('/login') && window.location.pathname !== '/') {
          if (!skipErrorToast) {
            toast.error(i18n._('errors.http.401') + apiTag(error.config))
          }
          window.location.href = '/login'
        }
        return Promise.reject(error.response)
      case 403:
        if (!skipErrorToast) {
          toast.error(i18n._('errors.http.403') + apiTag(error.config))
        }
        return Promise.reject(error.response)

      case 404:
        if (!skipErrorToast) {
          toast.error(i18n._('errors.http.404') + apiTag(error.config))
        }
        return Promise.reject(error.response)

      case 500:
        if (!skipErrorToast) {
          debouncedTimeoutError(i18n._('errors.http.500') + apiTag(error.config))
        }
        return Promise.reject(error.response)

      default: {
        // Handle business errors
        return Promise.reject(error.response)
      }
    }
  }
)

// Export request methods
export const http = {
  // Write business path directly, e.g., '/login'
  get: (url: string, params?: any) => request.get(url, { params }),
  post: (url: string, data?: any) => request.post(url, data),
  put: (url: string, data?: any) => request.put(url, data),
  delete: (url: string) => request.delete(url),
  patch: (url: string, data?: any) => request.patch(url, data),
}

// Export axios instance
export default request