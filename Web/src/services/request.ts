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

    return config
  },
  (error) => Promise.reject(error)
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
      toast.error(i18n._(`errors.business.${data.error_code}`))
    }
    return Promise.reject(response)
  },
  (error) => {
    const isCanceled = axios.isCancel(error) || error.code === 'ERR_CANCELED'
    if (isCanceled) {
      return Promise.reject(error)
    }
    const skipErrorToast = error.config?.skipErrorToast
    if (!error.response) {
      const errorMessage = String(error)
      if (!skipErrorToast) {
        debouncedTimeoutError(errorMessage)
      }
      return Promise.reject(error)
    }
    const { status } = error.response
    switch (status) {
      case 401:
        // Unauthorized, clear token and redirect to login page
        localStorage.removeItem('token')
        // 'login route
        if (!window.location.pathname.includes('/login') && window.location.pathname !== '/') {
          if (!skipErrorToast) {
            toast.error(i18n._('errors.http.401'))
          }
          window.location.href = '/login'
        }
        return Promise.reject(error.response)
      case 403:
        if (!skipErrorToast) {
          toast.error(i18n._('errors.http.403'))
        }
        return Promise.reject(error.response)

      case 404:
        if (!skipErrorToast) {
          toast.error(i18n._('errors.http.404'))
        }
        return Promise.reject(error.response)

      case 500:
        if (!skipErrorToast) {
          debouncedTimeoutError(i18n._('errors.http.500'))
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