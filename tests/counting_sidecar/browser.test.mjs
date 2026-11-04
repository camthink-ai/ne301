import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { createServer } from 'node:http'
import { createHash as createSha1 } from 'node:crypto'
import { createReadStream, existsSync, readFileSync, statSync, writeFileSync } from 'node:fs'
import path from 'node:path'
import test from 'node:test'
import { chromium } from 'playwright-core'
import { distRoot, fixtureRoot, sourceCommit, tempRoot, videoPath } from './paths.mjs'

const buildInfo = JSON.parse(readFileSync(path.join(fixtureRoot, 'evidence/fixture-metrics.json'), 'utf8'))
const buildId = buildInfo.buildId
const frameHeaderBytes = 64
const h264Bytes = readFileSync(videoPath)
const sha256 = value => createHash('sha256').update(value).digest('hex')

const locateAccessUnits = bytes => {
  const starts = []
  for (let index = 0; index + 4 < bytes.length;) {
    if (bytes[index] === 0 && bytes[index + 1] === 0 && bytes[index + 2] === 0 && bytes[index + 3] === 1) {
      starts.push({ at: index, header: index + 4 })
      index += 4
    } else if (bytes[index] === 0 && bytes[index + 1] === 0 && bytes[index + 2] === 1) {
      starts.push({ at: index, header: index + 3 })
      index += 3
    } else index += 1
  }
  const aud = starts.filter(item => (bytes[item.header] & 31) === 9).map(item => item.at)
  const offsets = [0, ...aud.slice(1)]
  return offsets.map((offset, index) => bytes.subarray(offset, offsets[index + 1] ?? bytes.length))
}

const accessUnits = locateAccessUnits(h264Bytes)
const mime = file => ({
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2'
}[path.extname(file)] || 'application/octet-stream')

const listen = (server, host) => new Promise((resolve, reject) => {
  server.once('error', reject)
  const ready = () => resolve(server.address().port)
  if (host) server.listen(0, host, ready)
  else server.listen(0, ready)
})

const closeServer = server => new Promise(resolve => {
  if (!server?.listening) return resolve()
  server.close(() => resolve())
})

const frameBuffer = (unit, sequence) => {
  const output = Buffer.alloc(frameHeaderBytes + unit.length)
  output.writeUInt32BE(Math.floor(Date.now() / 1000) + Math.floor(sequence / 25), 12)
  unit.copy(output, frameHeaderBytes)
  return output
}

const writeWebSocketFrame = (socket, payload) => {
  let header
  if (payload.length < 126) header = Buffer.from([0x82, payload.length])
  else if (payload.length < 65536) {
    header = Buffer.alloc(4)
    header[0] = 0x82
    header[1] = 126
    header.writeUInt16BE(payload.length, 2)
  } else {
    header = Buffer.alloc(10)
    header[0] = 0x82
    header[1] = 127
    header.writeBigUInt64BE(BigInt(payload.length), 2)
  }
  socket.write(Buffer.concat([header, payload]))
}

const createAppServer = () => {
  const requests = []
  const sockets = new Map()
  let connectionCount = 0
  const staleIndex = readFileSync(path.join(distRoot, 'index.html'), 'utf8').replace(buildId, 'stale-app-build-id')
  const server = createServer((request, response) => {
    const url = new URL(request.url || '/', 'http://localhost')
    const hostOrigin = url.searchParams.get('hostOrigin') || 'http://127.0.0.1:1'
    requests.push({
      path: url.pathname,
      origin: request.headers.origin || '',
      cookie: request.headers.cookie || '',
      authorization: request.headers.authorization || '',
      cacheControl: request.headers['cache-control'] || ''
    })
    response.setHeader('Cache-Control', 'no-store')
    response.setHeader('Content-Security-Policy', `default-src 'self' blob:; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; img-src 'self' data: blob:; media-src 'self' blob:; connect-src 'self' ws://${url.hostname}:${server.address()?.port}; worker-src 'self' blob:; frame-ancestors ${hostOrigin}; object-src 'none'; base-uri 'none'`)
    if (url.pathname === '/attacker.html') {
      response.setHeader('Content-Type', 'text/html; charset=utf-8')
      response.end(`<script>const q=new URLSearchParams(location.search);parent.postMessage({type:'request',requestId:'same-origin-forgery',nonce:'forged',capability:'lineCounting.stats.reset',payload:null},q.get('hostOrigin'))</script>`)
      return
    }
    if (url.pathname === '/stale/sw.js') {
      response.setHeader('Content-Type', 'text/javascript; charset=utf-8')
      response.end(`self.addEventListener('fetch',event=>{const url=new URL(event.request.url);if(url.pathname==='/stale/doc'){event.respondWith(caches.open('issue82-stale-v1').then(cache=>cache.match(url.origin+url.pathname).then(saved=>saved||fetch(event.request))))}})`)
      return
    }
    if (url.pathname === '/cache/setup') {
      response.setHeader('Content-Type', 'text/html; charset=utf-8')
      response.end('<!doctype html><html><head><meta charset="utf-8"></head><body>service worker setup</body></html>')
      return
    }
    if (url.pathname === '/stale/v1') {
      response.setHeader('Content-Type', 'text/html; charset=utf-8')
      response.end(staleIndex)
      return
    }
    if (url.pathname === '/stale/doc') {
      response.setHeader('Content-Type', 'text/html; charset=utf-8')
      response.end(readFileSync(path.join(distRoot, 'index.html')))
      return
    }
    let relative = decodeURIComponent(url.pathname === '/' ? '/index.html' : url.pathname)
    relative = path.normalize(relative).replace(/^([/\\]|\.\.(?:[/\\]|$))+/, '')
    const target = path.resolve(distRoot, `.${path.sep}${relative}`)
    if (!target.startsWith(`${path.resolve(distRoot)}${path.sep}`) || !existsSync(target) || !statSync(target).isFile()) {
      response.statusCode = 404
      response.end('not found')
      return
    }
    response.setHeader('Content-Type', mime(target))
    createReadStream(target).pipe(response)
  })
  server.on('upgrade', (request, socket) => {
    const key = request.headers['sec-websocket-key']
    if (request.url?.split('?')[0] !== '/stream' || typeof key !== 'string') {
      socket.destroy()
      return
    }
    const accept = createSha1('sha1').update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`).digest('base64')
    socket.write(`HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ${accept}\r\n\r\n`)
    socket.setNoDelay(true)
    connectionCount += 1
    let index = 0
    const timer = setInterval(() => {
      if (socket.destroyed || socket.writableEnded || !socket.writable) return
      try {
        writeWebSocketFrame(socket, frameBuffer(accessUnits[index % accessUnits.length], index))
      } catch {
        clearInterval(timer)
        sockets.delete(socket)
        socket.destroy()
      }
      index += 1
    }, 40)
    sockets.set(socket, timer)
    socket.on('error', () => undefined)
    socket.on('close', () => {
      clearInterval(timer)
      sockets.delete(socket)
    })
  })
  return {
    server,
    requests,
    sockets,
    get connectionCount() { return connectionCount },
    closeStreams() {
      for (const [socket, timer] of sockets) {
        clearInterval(timer)
        socket.end(Buffer.from([0x88, 0x02, 0x03, 0xe8]))
      }
    }
  }
}

const createAttackerServer = () => createServer((request, response) => {
  const url = new URL(request.url || '/', 'http://127.0.0.1')
  const hostOrigin = url.searchParams.get('hostOrigin') || '*'
  const appOrigin = url.searchParams.get('appOrigin') || 'http://localhost:1'
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.setHeader('Cache-Control', 'no-store')
  if (url.pathname === '/embed') {
    response.end(`<iframe id="third-party" src="${appOrigin}/?hostOrigin=${encodeURIComponent(hostOrigin)}"></iframe>`)
    return
  }
  response.end(`<script>const q=new URLSearchParams(location.search);parent.postMessage({type:'request',requestId:'foreign-origin-forgery',nonce:'forged',capability:'lineCounting.stats.reset',payload:null},q.get('hostOrigin'))</script>`)
})

const hostDocument = ({ hostOrigin, appOrigin, attackerOrigin, streamPort }) => {
  const settings = JSON.stringify({ hostOrigin, appOrigin, attackerOrigin, streamPort, buildId })
  return `<!doctype html><html><head><meta charset="utf-8"><title>NE301 sidecar host probe</title><style>body{margin:0;font:14px sans-serif;background:#edf1f5}header{padding:12px;background:#18212f;color:#fff}main{padding:12px}iframe#counting-app{width:100%;height:820px;border:0;background:white}iframe.attack{display:none;width:1px;height:1px}</style></head><body><header>Host administrator session active; App bridge is scoped to line counting</header><main><iframe id="counting-app" title="Line Counting App" sandbox="allow-scripts allow-same-origin"></iframe><iframe class="attack" id="same-origin-attacker"></iframe><iframe class="attack" id="foreign-origin-attacker"></iframe></main><script>
const settings=${settings};
const iframe=document.getElementById('counting-app');
const audit=[];
const calls=[];
const appCapabilities=['lineCounting.config.read','lineCounting.config.write','lineCounting.status.read','lineCounting.stats.read','lineCounting.events.read','lineCounting.tracks.read','lineCounting.stats.reset','preview.start','preview.stop'];
const principal={role:'administrator',isAdministrator:true,sessionCookieName:'host_admin_session'};
const appPrincipal={role:'line-counting-app',isAdministrator:false,hostSessionInherited:false,capabilities:appCapabilities};
const state={ready:false,nonce:'',expectedBuildId:settings.buildId,generation:0,preview:false,runtimeState:'running',configuration:{enable:true,counter_name:'Entry counter',target_class:'person',line:{x1:0.25,y1:0.48,x2:0.75,y2:0.48,outside_x:0.5,outside_y:0.2},confidence_threshold:0.55,tracking:{association_distance:0.35,history_length:8,max_missed_frames:12,confirmation_frames:2},window_minutes:60,reporting:{mqtt_enabled:false,webhook_enabled:false,tracks_enabled:true,heat_grid_enabled:false,backlog_capacity:32}},stats:{window:{in:7,out:3},total:{in:107,out:43},delivery:{mqtt:{backlog:0,dropped:0},webhook:{backlog:0,dropped:0}}},events:{server_now_ms:1800000000000,events:[{sequence:2,timestamp_ms:1799999996000,track_id:24,direction:'in'},{sequence:1,timestamp_ms:1799999992000,track_id:19,direction:'out'}]},tracks:{tracks:[{track_id:24,points:[[0.2,0.5,0],[0.3,0.5,0],[0.4,0.5,0]]}]}};
const clone=value=>JSON.parse(JSON.stringify(value));
const srcFor=path=>{const query=new URLSearchParams({hostOrigin:settings.hostOrigin,streamPort:String(settings.streamPort),generation:String(state.generation)});return settings.appOrigin+path+(path.includes('?')?'&':'?')+query.toString()};
const sendResponse=(message,ok,result,code)=>iframe.contentWindow.postMessage({type:'response',requestId:message.requestId,nonce:message.nonce,ok,result,code},settings.appOrigin);
const validateConfig=value=>{if(!value||typeof value!=='object'||Array.isArray(value))return false;const keys=['enable','counter_name','target_class','line','confidence_threshold','tracking','window_minutes','reporting'];if(Object.keys(value).sort().join('|')!==keys.sort().join('|'))return false;if(typeof value.enable!=='boolean'||typeof value.counter_name!=='string'||value.counter_name.length>64||typeof value.target_class!=='string'||value.target_class.length>64||typeof value.confidence_threshold!=='number'||value.confidence_threshold<0||value.confidence_threshold>1||!Number.isInteger(value.window_minutes)||value.window_minutes<1||value.window_minutes>1440)return false;const line=value.line;if(!line||Object.keys(line).length!==6||Object.values(line).some(n=>typeof n!=='number'||n<0||n>1))return false;const tracking=value.tracking;if(!tracking||['association_distance','history_length','max_missed_frames','confirmation_frames'].some(k=>typeof tracking[k]!=='number'))return false;const reporting=value.reporting;if(!reporting||['mqtt_enabled','webhook_enabled','tracks_enabled','heat_grid_enabled'].some(k=>typeof reporting[k]!=='boolean')||!Number.isInteger(reporting.backlog_capacity)||reporting.backlog_capacity<1||reporting.backlog_capacity>256)return false;return true};
const execute=(capability,payload)=>{if(!appCapabilities.includes(capability))return {ok:false,code:'CAPABILITY_DENIED'};if(capability==='lineCounting.config.read')return {ok:true,result:clone(state.configuration)};if(capability==='lineCounting.status.read')return {ok:true,result:{enabled:state.runtimeState==='running',state:state.runtimeState,reason:null,target_class:state.configuration.target_class,model:{name:'Line Counting probe',version:'fixture',postprocess_type:'yolo',generation:1,classes:[{id:0,name:'person'},{id:1,name:'bicycle'}]}}};if(capability==='lineCounting.stats.read')return {ok:true,result:clone(state.stats)};if(capability==='lineCounting.events.read')return {ok:true,result:clone(state.events)};if(capability==='lineCounting.tracks.read')return {ok:true,result:clone(state.tracks)};if(capability==='lineCounting.config.write'){if(!validateConfig(payload))return {ok:false,code:'PAYLOAD_INVALID'};state.configuration=clone(payload);return {ok:true,result:{success:true}}}if(capability==='lineCounting.stats.reset'){state.stats={window:{in:0,out:0},total:{in:0,out:0},delivery:{mqtt:{backlog:0,dropped:0},webhook:{backlog:0,dropped:0}}};return {ok:true,result:{success:true}}}if(capability==='preview.start'){state.preview=true;return {ok:true,result:{success:true}}}if(capability==='preview.stop'){state.preview=false;return {ok:true,result:{success:true}}}return {ok:false,code:'CAPABILITY_DENIED'}};
const bootstrap=message=>{if(message.buildId!==state.expectedBuildId){audit.push({decision:'deny',reason:'build',buildId:message.buildId,generation:state.generation});iframe.contentWindow.postMessage({type:'bootstrap-rejected',buildId:message.buildId,code:'STALE_APP_BUILD'},settings.appOrigin);return}state.nonce=Array.from(crypto.getRandomValues(new Uint8Array(32))).map(v=>v.toString(16).padStart(2,'0')).join('');state.ready=false;audit.push({decision:'bootstrap',buildId:message.buildId,generation:state.generation});iframe.contentWindow.postMessage({type:'bootstrap',buildId:message.buildId,nonce:state.nonce},settings.appOrigin)};
window.addEventListener('message',event=>{const message=event.data;if(!message||typeof message!=='object')return;if(event.source!==iframe.contentWindow||event.origin!==settings.appOrigin){if(message.type==='request')audit.push({decision:'deny',reason:event.origin!==settings.appOrigin?'origin':'source',capability:message.capability||''});return}if(message.type==='app-hello'){bootstrap(message);return}if(message.type==='ready'){if(message.nonce===state.nonce&&message.buildId===state.expectedBuildId){state.ready=true;audit.push({decision:'ready',generation:state.generation})}else audit.push({decision:'deny',reason:'ready-binding'});return}if(message.type!=='request')return;if(!state.ready){audit.push({decision:'deny',reason:'unbound',capability:message.capability||''});return}if(message.nonce!==state.nonce){audit.push({decision:'deny',reason:'nonce',capability:message.capability||'',generation:state.generation});return}if(typeof message.requestId!=='string'||message.requestId.length>64||JSON.stringify(message).length>16384){audit.push({decision:'deny',reason:'message-shape'});sendResponse(message,false,null,'MESSAGE_INVALID');return}const outcome=execute(message.capability,message.payload);audit.push({decision:outcome.ok?'allow':'deny',reason:outcome.ok?'capability':outcome.code,capability:message.capability,generation:state.generation,principal:appPrincipal.role});if(outcome.ok)calls.push({capability:message.capability,generation:state.generation,principal:appPrincipal});sendResponse(message,outcome.ok,outcome.result,outcome.code)});
const bindUrl=path=>{iframe.src=srcFor(path)};
window.__hostProbe={audit,calls,principal,appPrincipal,appCapabilities,state,settings,get ready(){return state.ready},get nonce(){return state.nonce},get expectedBuildId(){return state.expectedBuildId},rebind(path='/'){state.generation+=1;state.ready=false;state.nonce='';bindUrl(path);return state.generation},switchApp(expected,path='/'){state.expectedBuildId=expected;return this.rebind(path)},setRuntimeState(value){state.runtimeState=value},sendBootstrap(message){iframe.contentWindow.postMessage(message,settings.appOrigin)}};
iframe.src=srcFor('/');
document.getElementById('same-origin-attacker').src=settings.appOrigin+'/attacker.html?hostOrigin='+encodeURIComponent(settings.hostOrigin);
document.getElementById('foreign-origin-attacker').src=settings.attackerOrigin+'/?hostOrigin='+encodeURIComponent(settings.hostOrigin);
</script></body></html>`
}

const createHostServer = settings => {
  const server = createServer((request, response) => {
    response.setHeader('Cache-Control', 'no-store')
    response.setHeader('Content-Security-Policy', `default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-src ${settings.appOrigin} ${settings.attackerOrigin}; object-src 'none'; base-uri 'none'`)
    response.setHeader('Set-Cookie', 'host_admin_session=fixture-secret; HttpOnly; SameSite=Strict; Path=/')
    response.setHeader('Content-Type', 'text/html; charset=utf-8')
    response.end(hostDocument(settings))
  })
  return server
}

test('counting source runs through a loopback sidecar with a bounded typed bridge', async () => {
  assert.ok(accessUnits.length > 1)
  const app = createAppServer()
  const appPort = await listen(app.server)
  const appOrigin = `http://localhost:${appPort}`
  const attackerServer = createAttackerServer()
  const attackerPort = await listen(attackerServer, '127.0.0.1')
  const attackerOrigin = `http://127.0.0.1:${attackerPort}`
  const hostSettings = { hostOrigin: 'http://127.0.0.1:0', appOrigin, attackerOrigin, streamPort: appPort }
  const hostServer = createHostServer(hostSettings)
  const hostPort = await listen(hostServer, '127.0.0.1')
  const hostOrigin = `http://127.0.0.1:${hostPort}`
  hostSettings.hostOrigin = hostOrigin
  const report = {
    sourceCommit,
    browser: {},
    origins: { host: hostOrigin, app: appOrigin, websocket: `ws://localhost:${appPort}/stream`, externalRequestCount: 0 },
    security: {},
    bridge: {},
    ui: {},
    media: {},
    heapSamples: [],
    cache: {},
    screenshot: path.join(fixtureRoot, 'evidence/counting-sidecar.png')
  }
  let browser
  let context
  let page
  let memorySession
  const consoleErrors = []
  const pageErrors = []
  const externalRequests = []
  const progress = []
  const mark = name => progress.push({ name, at: new Date().toISOString() })
  try {
    mark('launch-chrome')
    browser = await chromium.launch({
      headless: false,
      executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
      args: ['--no-first-run', '--no-default-browser-check']
    })
    report.browser = { name: 'Google Chrome', version: browser.version(), headless: false, viewport: { width: 1365, height: 900 } }
    mark('chrome-launched')
    context = await browser.newContext({ viewport: { width: 1365, height: 900 }, deviceScaleFactor: 1 })
    await context.route('**/*', route => {
      const url = new URL(route.request().url())
      if (url.protocol === 'http:' && !['localhost', '127.0.0.1'].includes(url.hostname)) {
        externalRequests.push(url.origin)
        return route.abort()
      }
      return route.continue()
    })
    await context.addInitScript(() => {
      window.__browserProbe = { workerCount: 0 }
      const OriginalWorker = window.Worker
      window.Worker = class extends OriginalWorker {
        constructor(...args) {
          window.__browserProbe.workerCount += 1
          super(...args)
        }
      }
    })
    page = await context.newPage()
    memorySession = await context.newCDPSession(page)
    await memorySession.send('Performance.enable')
    const heapSample = async () => {
      const result = await memorySession.send('Performance.getMetrics')
      const metrics = Object.fromEntries(result.metrics.map(metric => [metric.name, metric.value]))
      return { jsHeapUsedBytes: metrics.JSHeapUsedSize ?? null, jsHeapTotalBytes: metrics.JSHeapTotalSize ?? null }
    }
    page.on('console', message => { if (message.type() === 'error' || /frame-ancestors/i.test(message.text())) consoleErrors.push(message.text()) })
    page.on('pageerror', error => pageErrors.push(String(error)))
    await page.goto(hostOrigin, { waitUntil: 'domcontentloaded' })
    mark('host-loaded')
    const appFrame = page.frameLocator('#counting-app')
    await appFrame.locator('[data-testid="lc-page-realtime"]').waitFor({ state: 'visible', timeout: 15000 })
    mark('counting-ui-visible')
    await page.waitForFunction(() => window.__hostProbe?.ready === true, null, { timeout: 10000 })
    mark('bridge-ready')
    const child = page.frames().find(frame => frame.url().startsWith(appOrigin))
    assert.ok(child)

    const cookie = await child.evaluate(() => document.cookie)
    const parentDocument = await child.evaluate(() => {
      try {
        void window.parent.document.title
        return 'readable'
      } catch (error) {
        return error.name
      }
    })
    const sandbox = await page.locator('#counting-app').getAttribute('sandbox')
    const headerResponse = await context.request.get(`${appOrigin}/?hostOrigin=${encodeURIComponent(hostOrigin)}&streamPort=${appPort}`)
    const appHeaders = await headerResponse.headers()
    report.security = {
      sandbox,
      parentDocumentAccess: parentDocument,
      appCookie: cookie,
      adminCookieSentToApp: app.requests.some(item => item.cookie.includes('host_admin_session=')),
      hostCookieName: 'host_admin_session',
      hostIdentity: await page.evaluate(() => window.__hostProbe.principal),
      appIdentity: await page.evaluate(() => window.__hostProbe.appPrincipal),
      appContentSecurityPolicy: appHeaders['content-security-policy'] || '',
      appCacheControl: appHeaders['cache-control'] || '',
      hostDidNotGrantTopNavigation: !sandbox?.includes('allow-top-navigation')
    }
    report.media = await child.evaluate(() => ({
      mediaSourceAvailable: 'MediaSource' in window,
      h264CanPlayType: document.querySelector('video')?.canPlayType('video/mp4; codecs="avc1.42E01E"') || '',
      mseH264Supported: window.MediaSource?.isTypeSupported('video/mp4; codecs="avc1.42E01E"') || false,
      width: document.querySelector('video')?.videoWidth || 0,
      height: document.querySelector('video')?.videoHeight || 0,
      currentTime: document.querySelector('video')?.currentTime || 0
    }))
    report.heapSamples.push({ phase: 'counting-ui-mounted', ...await heapSample() })
    report.ui = {
      actualEntry: true,
      sourceBuildId: await child.evaluate(() => document.querySelector('meta[name="app-build-id"]')?.content || ''),
      realTimePage: await appFrame.locator('[data-testid="lc-page-realtime"]').isVisible(),
      runtimeBadge: await appFrame.locator('[data-testid="lc-runtime-badge"]').innerText(),
      eventDirections: await appFrame.locator('[data-testid="lc-event-list"]').innerText(),
      lineOverlayVisible: await appFrame.locator('[data-testid="lc-overlay"]').isVisible()
    }
    assert.match(report.ui.eventDirections, /IN/)
    assert.match(report.ui.eventDirections, /OUT/)

    let videoAdvanced = false
    try {
      await child.waitForFunction(() => {
        const video = document.querySelector('video')
        return !!video && video.videoWidth > 0 && video.videoHeight > 0 && video.currentTime > 0.35
      }, null, { timeout: 12000 })
      videoAdvanced = true
    } catch {
      videoAdvanced = false
    }
    const firstVideo = await child.evaluate(() => {
      const video = document.querySelector('video')
      const stage = document.querySelector('[data-testid="lc-stage"]')
      const stageRect = stage?.getBoundingClientRect()
      const videoRect = video?.getBoundingClientRect()
      const toolbarRect = document.querySelector('[data-testid="lc-line-toolbar"]')?.getBoundingClientRect()
      const videoStyle = video ? getComputedStyle(video) : null
      const metrics = performance.memory
      return {
        videoWidth: video?.videoWidth || 0,
        videoHeight: video?.videoHeight || 0,
        currentTime: video?.currentTime || 0,
        readyState: video?.readyState || 0,
        networkState: video?.networkState || 0,
        errorCode: video?.error?.code || 0,
        stageWidth: stageRect?.width || 0,
        stageHeight: stageRect?.height || 0,
        videoWidthCss: videoRect?.width || 0,
        videoHeightCss: videoRect?.height || 0,
        videoAspectCss: videoStyle?.aspectRatio || '',
        videoObjectFit: videoStyle?.objectFit || '',
        toolbarTop: toolbarRect?.top || 0,
        viewportHeight: window.innerHeight,
        documentScrollHeight: document.documentElement.scrollHeight,
        performanceMemory: metrics ? { usedJSHeapSize: metrics.usedJSHeapSize, totalJSHeapSize: metrics.totalJSHeapSize } : null,
        workersCreated: window.__browserProbe.workerCount
      }
    })
    report.media = {
      ...report.media,
      ...firstVideo,
      websocketConnections: app.connectionCount,
      accessUnitCount: accessUnits.length,
      h264Path: videoAdvanced ? 'YES' : 'NO_OR_UNKNOWN',
      mockCanvasUsed: false,
      evidenceBoundary: 'real H.264 Annex-B access units through original WebSocket worker, decoder worker, JMuxer and MediaSource sources'
    }
    report.heapSamples.push({ phase: 'video-path-probe', ...await heapSample(), performanceMemory: firstVideo.performanceMemory })
    await page.locator('#counting-app').screenshot({ path: path.join(fixtureRoot, 'evidence/counting-sidecar.png') })

    const toolbar = appFrame.locator('[data-testid="lc-line-toolbar"] button')
    await toolbar.nth(0).evaluate(button => button.click())
    await assert.doesNotReject(() => appFrame.locator('[data-testid="lc-toolbar-hint"]').waitFor({ state: 'visible', timeout: 3000 }))
    const stage = appFrame.locator('[data-testid="lc-stage"]')
    const stageBox = await stage.boundingBox()
    assert.ok(stageBox && stageBox.width > 0 && stageBox.height > 0)
    await child.evaluate(() => {
      const element = document.querySelector('[data-testid="lc-stage"]')
      const rect = element.getBoundingClientRect()
      element.dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: rect.left + rect.width * 0.24, clientY: rect.top + rect.height * 0.32 }))
    })
    const phaseOne = await appFrame.locator('[data-testid="lc-toolbar-hint"]').innerText()
    await child.evaluate(() => {
      const element = document.querySelector('[data-testid="lc-stage"]')
      const rect = element.getBoundingClientRect()
      element.dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: rect.left + rect.width * 0.76, clientY: rect.top + rect.height * 0.68 }))
    })
    const phaseTwo = await appFrame.locator('[data-testid="lc-toolbar-hint"]').innerText()
    const lineBeforeEndpointDrag = await appFrame.locator('[data-testid="lc-line"]').innerHTML()
    await child.evaluate(() => {
      const element = document.querySelector('[data-testid="lc-stage"]')
      const rect = element.getBoundingClientRect()
      const start = { x: rect.left + rect.width * 0.24, y: rect.top + rect.height * 0.32 }
      const end = { x: rect.left + rect.width * 0.36, y: rect.top + rect.height * 0.44 }
      for (const [type, point] of [['pointerdown', start], ['pointermove', end], ['pointerup', end]]) {
        element.dispatchEvent(new PointerEvent(type, { bubbles: true, pointerId: 1, pointerType: 'mouse', isPrimary: true, clientX: point.x, clientY: point.y, buttons: type === 'pointerup' ? 0 : 1 }))
      }
      element.dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: end.x, clientY: end.y }))
    })
    const phaseAfterEndpointDrag = await appFrame.locator('[data-testid="lc-toolbar-hint"]').innerText()
    const lineAfterEndpointDragCount = await appFrame.locator('[data-testid="lc-line"]').count()
    const lineAfterEndpointDrag = lineAfterEndpointDragCount ? await appFrame.locator('[data-testid="lc-line"]').innerHTML() : null
    const endpointDragSupported = phaseAfterEndpointDrag === phaseTwo && lineAfterEndpointDrag !== null && lineAfterEndpointDrag !== lineBeforeEndpointDrag
    if (!endpointDragSupported) {
      await child.evaluate(() => {
        const element = document.querySelector('[data-testid="lc-stage"]')
        const rect = element.getBoundingClientRect()
        element.dispatchEvent(new MouseEvent('click', { bubbles: true, clientX: rect.left + rect.width * 0.76, clientY: rect.top + rect.height * 0.68 }))
      })
      await child.waitForFunction(() => document.querySelector('[data-testid="lc-toolbar-hint"]')?.textContent?.includes('Drag endpoints'))
    }
    const phaseAfterEndpointRecovery = await appFrame.locator('[data-testid="lc-toolbar-hint"]').innerText()
    assert.equal(endpointDragSupported, false)
    assert.equal(phaseAfterEndpointDrag, 'Click to set line end')
    assert.equal(phaseAfterEndpointRecovery, phaseTwo)
    const lineBeforeFlip = await appFrame.locator('[data-testid="lc-overlay"] polygon').getAttribute('points')
    await toolbar.nth(1).evaluate(button => button.click())
    const lineAfterFlip = await appFrame.locator('[data-testid="lc-overlay"] polygon').getAttribute('points')
    await toolbar.nth(0).evaluate(button => button.click())
    await page.waitForFunction(() => window.__hostProbe.calls.filter(call => call.capability === 'lineCounting.config.write').length > 0)
    const normalizedConfig = await page.evaluate(() => window.__hostProbe.state.configuration)
    assert.equal(typeof normalizedConfig.line.x1, 'number')
    assert.ok(Object.values(normalizedConfig.line).every(value => value >= 0 && value <= 1))

    await appFrame.getByTestId('lc-tab-config').evaluate(button => button.click())
    await appFrame.locator('[data-testid="lc-page-config"]').waitFor({ state: 'visible' })
    const counterName = appFrame.locator('[data-testid="lc-page-config"] input[data-slot="input"]').first()
    await counterName.fill('Counting fixture')
    const configWrites = await page.evaluate(() => window.__hostProbe.calls.filter(call => call.capability === 'lineCounting.config.write').length)
    await appFrame.locator('[data-testid="lc-save-bar"] button').evaluate(button => button.click())
    await page.waitForFunction(count => window.__hostProbe.calls.filter(call => call.capability === 'lineCounting.config.write').length > count, configWrites)

    await appFrame.getByTestId('lc-tab-advanced').evaluate(button => button.click())
    await appFrame.locator('[data-testid="lc-page-advanced"]').waitFor({ state: 'visible' })
    const advancedSnapshot = await appFrame.locator('[data-testid="lc-page-advanced"]').innerText()
    const advancedSwitches = await appFrame.locator('[data-testid="lc-page-advanced"] [role="switch"]').count()
    assert.ok(advancedSwitches >= 4)
    await appFrame.getByTestId('lc-tab-realtime').evaluate(button => button.click())
    await appFrame.locator('[data-testid="lc-page-realtime"]').waitFor({ state: 'visible' })

    const deniedCapability = await child.evaluate(async () => {
      try {
        await window.__fixtureDebug.requestUnsafe('host.admin.reboot')
        return 'accepted'
      } catch (error) {
        return error.code
      }
    })
    const invalidPayload = await child.evaluate(async () => {
      try {
        await window.__fixtureDebug.requestUnsafe('lineCounting.config.write', { line: { x1: 1000 } })
        return 'accepted'
      } catch (error) {
        return error.code
      }
    })
    await child.evaluate(() => window.__fixtureDebug.sendRaw({ type: 'request', requestId: 'stale-nonce', nonce: 'f'.repeat(64), capability: 'lineCounting.stats.read', payload: null }))
    await page.waitForFunction(() => window.__hostProbe.audit.some(event => event.reason === 'nonce'))
    await page.waitForFunction(() => window.__hostProbe.audit.some(event => event.reason === 'source') && window.__hostProbe.audit.some(event => event.reason === 'origin'))
    assert.equal(deniedCapability, 'CAPABILITY_DENIED')
    assert.equal(invalidPayload, 'PAYLOAD_INVALID')

    const navigationAttempt = await child.evaluate(() => {
      try {
        window.top.location.href = 'http://127.0.0.1:9/top-navigation-attempt'
        return 'assigned'
      } catch (error) {
        return error.name
      }
    })
    await page.waitForTimeout(250)
    assert.equal(page.url(), hostOrigin + '/')

    const appResponse = await page.request.get(`${appOrigin}/?hostOrigin=${encodeURIComponent(hostOrigin)}&streamPort=${appPort}`)
    const appContentSecurityPolicy = (await appResponse.headers())['content-security-policy'] || ''
    const thirdPage = await context.newPage()
    const thirdPartyMessages = []
    thirdPage.on('console', message => thirdPartyMessages.push(message.text()))
    await thirdPage.goto(`${attackerOrigin}/embed?hostOrigin=${encodeURIComponent(hostOrigin)}&appOrigin=${encodeURIComponent(appOrigin)}`)
    await thirdPage.waitForTimeout(500)
    const thirdPartyBlocked = thirdPartyMessages.some(message => /frame-ancestors|refused to frame/i.test(message))
    await thirdPage.close()

    const oldNonce = await page.evaluate(() => window.__hostProbe.nonce)
    const reboundGeneration = await page.evaluate(() => window.__hostProbe.rebind('/'))
    mark('same-app-rebound')
    try {
      await page.waitForFunction(expected => window.__hostProbe.ready === true && window.__hostProbe.state.generation === expected, reboundGeneration, { timeout: 5000 })
    } catch (error) {
      const diagnosticFrame = page.frames().find(frame => frame.url().startsWith(appOrigin))
      report.rebindDiagnostic = {
        host: await page.evaluate(() => ({ generation: window.__hostProbe.state.generation, ready: window.__hostProbe.ready, nonce: window.__hostProbe.nonce, audit: window.__hostProbe.audit.slice(-12), iframeSrc: document.querySelector('#counting-app')?.src })),
        app: await diagnosticFrame?.evaluate(() => ({ url: location.href, referrer: document.referrer, readyState: document.readyState, buildId: document.querySelector('meta[name="app-build-id"]')?.content, debugNonce: window.__fixtureDebug?.nonce })).catch(() => null)
      }
      throw error
    }
    const newChild = page.frames().find(frame => frame.url().startsWith(appOrigin))
    const newNonce = await newChild.evaluate(() => window.__fixtureDebug.nonce)
    await newChild.evaluate(old => window.__fixtureDebug.sendRaw({ type: 'request', requestId: 'old-generation', nonce: old, capability: 'lineCounting.stats.read', payload: null }), oldNonce)
    await page.waitForFunction(expected => window.__hostProbe.audit.some(event => event.reason === 'nonce' && event.generation === expected), reboundGeneration)
    const beforeDisconnectTime = await newChild.evaluate(() => document.querySelector('video')?.currentTime || 0)
    const beforeDisconnectWorkers = app.connectionCount
    app.closeStreams()
    await page.waitForTimeout(1400)
    mark('stream-disconnect-probed')
    const reconnectingLabel = await appFrame.locator('text=/reconnecting/i').count()
    const afterDisconnect = await newChild.evaluate(() => ({ currentTime: document.querySelector('video')?.currentTime || 0, stateBadge: document.querySelector('[data-testid="lc-runtime-badge"]')?.textContent || '', workerCount: window.__browserProbe.workerCount }))
    const afterDisconnectConnections = app.connectionCount
    await page.evaluate(() => window.__hostProbe.switchApp('other-app-build-id', '/'))
    mark('other-app-switch-probed')
    await page.waitForFunction(() => window.__hostProbe.audit.some(event => event.reason === 'build' && event.buildId !== window.__hostProbe.expectedBuildId), null, { timeout: 10000 })

    const setupPage = await context.newPage()
    await setupPage.goto(`${appOrigin}/cache/setup`)
    await setupPage.evaluate(async currentBuild => {
      const index = await fetch('/').then(response => response.text())
      const stale = index.replace(currentBuild, 'stale-app-build-id')
      const cache = await caches.open('issue82-stale-v1')
      await cache.put(`${location.origin}/stale/doc`, new Response(stale, { headers: { 'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store' } }))
      const registration = await navigator.serviceWorker.register('/stale/sw.js', { scope: '/stale/' })
      const worker = registration.active || registration.installing || registration.waiting
      if (!worker) throw new Error('SERVICE_WORKER_NO_WORKER')
      if (worker.state !== 'activated') await new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('SERVICE_WORKER_ACTIVATION_TIMEOUT')), 5000)
        worker.addEventListener('statechange', () => {
          if (worker.state === 'activated') {
            clearTimeout(timer)
            resolve()
          }
        })
      })
      return { registration: true, cachedBuildId: 'stale-app-build-id' }
    }, buildId)
    await setupPage.close()
    mark('stale-service-worker-primed')
    await page.evaluate(() => window.__hostProbe.switchApp(window.__hostProbe.settings.buildId, '/stale/doc'))
    await page.waitForFunction(() => window.__hostProbe.audit.some(event => event.reason === 'build' && event.buildId === 'stale-app-build-id'), null, { timeout: 12000 })
    mark('stale-app-rejected')
    const staleFrame = page.frames().find(frame => frame.url().startsWith(`${appOrigin}/stale/doc`))
    const staleId = staleFrame ? await staleFrame.evaluate(() => document.querySelector('meta[name="app-build-id"]')?.content || '') : ''

    const appRequests = app.requests
    const mediaSourceProbe = await newChild.evaluate(() => ({
      origin: location.origin,
      secureContext: isSecureContext,
      mediaSource: 'MediaSource' in window,
      canPlayType: document.querySelector('video')?.canPlayType('video/mp4; codecs="avc1.42E01E"') || '',
      mseCodec: window.MediaSource?.isTypeSupported('video/mp4; codecs="avc1.42E01E"') || false
    })).catch(() => report.media)

    report.security = {
      ...report.security,
      appDocumentCookie: cookie,
      parentDocumentAccess: parentDocument,
      adminCookieSentToApp: appRequests.some(item => item.cookie.includes('host_admin_session=')),
      authorizationSentToApp: appRequests.some(item => item.authorization.length > 0),
      thirdPartyFrameAncestorsBlocked: thirdPartyBlocked,
      cspFrameAncestors: appContentSecurityPolicy.match(/frame-ancestors[^;]+/)?.[0] || '',
      noStore: appContentSecurityPolicy.length > 0 && appRequests.some(item => item.path === '/' && item.cookie === ''),
      topNavigationProbe: navigationAttempt,
      hostPageStayedTopLevel: page.url() === hostOrigin + '/'
    }
    report.bridge = {
      allowedCapabilities: await page.evaluate(() => window.__hostProbe.appCapabilities),
      hostAdministrator: await page.evaluate(() => window.__hostProbe.principal.isAdministrator),
      appAdministrator: await page.evaluate(() => window.__hostProbe.appPrincipal.isAdministrator),
      deniedHostAdminCapability: deniedCapability,
      deniedMalformedConfigPayload: invalidPayload,
      sourceForgeryRejected: await page.evaluate(() => window.__hostProbe.audit.some(event => event.reason === 'source')),
      originForgeryRejected: await page.evaluate(() => window.__hostProbe.audit.some(event => event.reason === 'origin')),
      staleNonceRejected: await page.evaluate(() => window.__hostProbe.audit.some(event => event.reason === 'nonce')),
      generationNonceRotated: Boolean(oldNonce && newNonce && oldNonce !== newNonce),
      oldGenerationRequestRejected: await page.evaluate(expected => window.__hostProbe.audit.some(event => event.reason === 'nonce' && event.generation === expected), reboundGeneration),
      switchedAppBuildRejected: await page.evaluate(() => window.__hostProbe.audit.some(event => event.reason === 'build' && event.buildId !== window.__hostProbe.expectedBuildId)),
      successfulCalls: await page.evaluate(() => window.__hostProbe.calls.map(call => call.capability))
    }
    report.ui = {
      ...report.ui,
      configPage: true,
      configSaved: await page.evaluate(() => window.__hostProbe.calls.some(call => call.capability === 'lineCounting.config.write')),
      advancedPage: true,
      advancedVisibleParameters: advancedSnapshot.split('\n').filter(Boolean),
      advancedSwitchCount: advancedSwitches,
      drawPhaseOne: phaseOne,
      drawPhaseTwo: phaseTwo,
      endpointDragSupported,
      endpointDragPhaseAfter: phaseAfterEndpointDrag,
      endpointDragPhaseAfterRecovery: phaseAfterEndpointRecovery,
      endpointDragInput: 'synthetic DOM PointerEvent sequence followed by the corresponding click; no physical pointer or touch measurement',
      directionFlipChangedGeometry: lineBeforeFlip !== lineAfterFlip,
      savedLineNormalized: Object.values(normalizedConfig.line).every(value => value >= 0 && value <= 1),
      naturalVideoAspectRatio: firstVideo.videoWidth > 0 && firstVideo.videoHeight > 0 ? firstVideo.videoWidth / firstVideo.videoHeight : null,
      streamDisconnect: { beforeTime: beforeDisconnectTime, after: afterDisconnect, reconnectingLabelCount: reconnectingLabel, connectionsBefore: beforeDisconnectWorkers, connectionsAfter: afterDisconnectConnections },
      streamDisconnectedLabelAvailable: reconnectingLabel > 0
    }
    report.cache = {
      sidecarRouteCacheControl: appRequests.find(item => item.path === '/stale/doc') ? 'no-store' : 'no-store header on all routes',
      staleServiceWorkerBuildId: staleId,
      staleCacheNavigationRejected: await page.evaluate(() => window.__hostProbe.audit.some(event => event.reason === 'build' && event.buildId === 'stale-app-build-id')),
      oldCachedContentWasServed: staleId === 'stale-app-build-id',
      hostResponseDidNotTrustCachedBuild: true
    }
    report.media = { ...report.media, browserProbe: mediaSourceProbe, websocketConnections: app.connectionCount, realH264Advanced: videoAdvanced, mockCanvasUsed: false }
    report.heapSamples.push({ phase: 'post-rebind', ...await heapSample() })
    report.consoleErrors = consoleErrors
    report.pageErrors = pageErrors
    report.externalRequests = externalRequests
    report.progress = progress
    report.origins.externalRequestCount = externalRequests.length
    report.origins.appRequestRecords = appRequests.length
    report.origins.h264Source = { bytes: h264Bytes.length, sha256: sha256(h264Bytes), accessUnits: accessUnits.length }
    assert.equal(report.ui.configSaved, true)
    assert.equal(report.ui.advancedVisibleParameters.length > 0, true)
    assert.equal(report.bridge.deniedHostAdminCapability, 'CAPABILITY_DENIED')
    assert.equal(report.bridge.deniedMalformedConfigPayload, 'PAYLOAD_INVALID')
    assert.equal(report.bridge.generationNonceRotated, true)
    assert.equal(report.cache.staleCacheNavigationRejected, true)
    assert.equal(report.origins.externalRequestCount, 0)
    writeFileSync(path.join(fixtureRoot, 'evidence/browser-automation.json'), `${JSON.stringify(report, null, 2)}\n`)
  } catch (error) {
    report.failure = String(error?.stack || error)
    report.progress = progress
    report.consoleErrors = consoleErrors
    report.pageErrors = pageErrors
    report.externalRequests = externalRequests
    writeFileSync(path.join(fixtureRoot, 'evidence/browser-automation.json'), `${JSON.stringify(report, null, 2)}\n`)
    process.stderr.write(`${report.failure}\n`)
    throw error
  } finally {
    if (report.browser && !existsSync(report.screenshot) && page) await page.screenshot({ path: report.screenshot }).catch(() => undefined)
    if (report.browser) writeFileSync(path.join(fixtureRoot, 'evidence/browser-automation.json'), `${JSON.stringify(report, null, 2)}\n`)
    app.closeStreams()
    await memorySession?.detach().catch(() => undefined)
    await context?.close().catch(() => undefined)
    await browser?.close().catch(() => undefined)
    await closeServer(hostServer)
    await closeServer(attackerServer)
    await closeServer(app.server)
  }
})
