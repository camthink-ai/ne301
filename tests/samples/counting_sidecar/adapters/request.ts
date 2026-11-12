import { requestCapability } from './bridge'

const routes: Record<string, string> = {
  'GET /api/v1/apps/line-counting/config': 'lineCounting.config.read',
  'POST /api/v1/apps/line-counting/config': 'lineCounting.config.write',
  'GET /api/v1/apps/line-counting/status': 'lineCounting.status.read',
  'GET /api/v1/apps/line-counting/stats': 'lineCounting.stats.read',
  'GET /api/v1/apps/line-counting/events': 'lineCounting.events.read',
  'GET /api/v1/apps/line-counting/tracks': 'lineCounting.tracks.read',
  'POST /api/v1/apps/line-counting/reset': 'lineCounting.stats.reset',
  'POST api/v1/preview/start': 'preview.start',
  'POST api/v1/preview/stop': 'preview.stop'
}

const call = async (method: string, path: string, payload?: unknown) => {
  const capability = routes[`${method} ${path}`]
  if (!capability) throw Object.assign(new Error('ROUTE_DENIED'), { code: 'ROUTE_DENIED' })
  return requestCapability(capability as never, payload as never)
}

const request = {
  get: async (path: string) => ({ data: await call('GET', path) }),
  post: async (path: string, payload?: unknown) => ({ data: await call('POST', path, payload) })
}

export const logStreamError = (api: string, phase: string, code: string, message: string) => {
  window.dispatchEvent(new CustomEvent('fixture-stream-event', { detail: { api, phase, code, message } }))
}

export default request
