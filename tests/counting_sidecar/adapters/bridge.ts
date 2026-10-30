export type Capability =
  | 'lineCounting.config.read'
  | 'lineCounting.config.write'
  | 'lineCounting.status.read'
  | 'lineCounting.stats.read'
  | 'lineCounting.events.read'
  | 'lineCounting.tracks.read'
  | 'lineCounting.stats.reset'
  | 'preview.start'
  | 'preview.stop'

type PayloadMap = {
  'lineCounting.config.read': undefined
  'lineCounting.config.write': Record<string, unknown>
  'lineCounting.status.read': undefined
  'lineCounting.stats.read': undefined
  'lineCounting.events.read': undefined
  'lineCounting.tracks.read': undefined
  'lineCounting.stats.reset': undefined
  'preview.start': undefined
  'preview.stop': undefined
}

type Pending = {
  resolve: (value: unknown) => void
  reject: (reason: Error & { code?: string }) => void
  timer: number
}

const pending = new Map<string, Pending>()
const referrer = document.referrer
const hostOrigin = new URLSearchParams(window.location.search).get('hostOrigin') || (referrer ? new URL(referrer).origin : window.location.origin)
const buildId = document.querySelector<HTMLMetaElement>('meta[name="app-build-id"]')?.content || ''
let nonce = ''
let sequence = 0
let resolveReady: (() => void) | undefined
let rejectReady: ((reason: Error & { code?: string }) => void) | undefined
let bound = false
const ready = new Promise<void>((resolve, reject) => {
  resolveReady = resolve
  rejectReady = reject
})

const fail = (code: string) => Object.assign(new Error(code), { code })

const receive = (event: MessageEvent) => {
  if (event.source !== window.parent || event.origin !== hostOrigin) return
  const message = event.data
  if (!message || typeof message !== 'object') return
  if (message.type === 'bootstrap-rejected') {
    if (!bound && message.buildId === buildId) rejectReady?.(fail(message.code || 'APP_REJECTED'))
    return
  }
  if (message.type === 'bootstrap') {
    if (bound || message.buildId !== buildId || typeof message.nonce !== 'string' || message.nonce.length < 32) return
    nonce = message.nonce
    bound = true
    window.parent.postMessage({ type: 'ready', buildId, nonce }, hostOrigin)
    resolveReady?.()
    return
  }
  if (!bound || message.type !== 'response' || message.nonce !== nonce || typeof message.requestId !== 'string') return
  const request = pending.get(message.requestId)
  if (!request) return
  window.clearTimeout(request.timer)
  pending.delete(message.requestId)
  if (message.ok === true) request.resolve(message.result)
  else request.reject(fail(message.code || 'REQUEST_REJECTED'))
}

window.addEventListener('message', receive)
window.parent.postMessage({ type: 'app-hello', buildId }, hostOrigin)

const send = async <T extends Capability>(capability: T, payload: PayloadMap[T]) => {
  await ready
  if (!bound || !nonce) throw fail('HOST_NOT_BOUND')
  const requestId = `line-${++sequence}`
  return new Promise<unknown>((resolve, reject) => {
    const timer = window.setTimeout(() => {
      pending.delete(requestId)
      reject(fail('REQUEST_TIMEOUT'))
    }, 2000)
    pending.set(requestId, { resolve, reject, timer })
    window.parent.postMessage({ type: 'request', requestId, nonce, capability, payload }, hostOrigin)
  })
}

export const requestCapability = <T extends Capability>(capability: T, payload: PayloadMap[T]) => send(capability, payload)

export const installFixtureDebug = () => {
  const debug = {
    buildId,
    get nonce() { return nonce },
    requestUnsafe: (capability: string, payload?: unknown) => send(capability as Capability, payload as never),
    sendRaw: (message: unknown) => window.parent.postMessage(message, hostOrigin),
    getHostOrigin: () => hostOrigin
  }
  ;(window as Window & { __fixtureDebug?: typeof debug }).__fixtureDebug = debug
}
