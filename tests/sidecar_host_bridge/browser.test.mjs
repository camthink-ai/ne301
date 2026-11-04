import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { spawnSync } from 'node:child_process'
import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import test from 'node:test'
import { chromium } from 'playwright-core'
import { homedir } from 'node:os'
import {
  createAppServer,
  createHostServer,
  materializeSignedAssets,
  setBehavior,
  setLiveBehavior,
  pushLiveCrossing,
  setLiveTracks,
  submitSnapshot,
  currentGeneration,
  grantCount,
  bridgeCalls,
  issuedConfirmTokens
} from './server.mjs'

const chromeExecutable = path.join(
  homedir(),
  'Library/Caches/ms-playwright/chromium-1243/chrome-mac-arm64/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing'
)
const launch = () => chromium.launch({ headless: true, executablePath: chromeExecutable })

const distRoot = path.join(path.dirname(new URL(import.meta.url).pathname), 'dist-host')
const signedRoot = mkdtempSync(path.join(tmpdir(), 'ne301-signed-app-'))
materializeSignedAssets(signedRoot)
const h264Path = path.join(signedRoot, '..', 'line-crossing-test.h264')

const listen = (server, host) =>
  new Promise((resolve, reject) => {
    server.once('error', reject)
    server.listen(0, host, () => resolve(server.address().port))
  })

const closeServer = (server) =>
  new Promise((resolve) => {
    if (!server?.listening) return resolve()
    server.close(() => resolve())
  })

const locateAccessUnits = (bytes) => {
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
  const aud = starts.filter((item) => (bytes[item.header] & 31) === 9).map((item) => item.at)
  const offsets = [0, ...aud.slice(1)]
  return offsets.map((offset, index) => bytes.subarray(offset, offsets[index + 1] ?? bytes.length))
}

const frameBuffer = (unit, sequence) => {
  const output = Buffer.alloc(64 + unit.length)
  output.writeUInt32BE(0x57534653, 0)
  output.writeUInt8(1, 4)
  output.writeUInt8(sequence % 25 === 0 ? 1 : 2, 5)
  output.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 1000) + Math.floor(sequence / 25)) * 1000000n, 8)
  output.writeUInt32BE(unit.length, 20)
  output.writeUInt32BE(64, 60)
  unit.copy(output, 64)
  return output
}

const websocketFrame = (payload) => {
  if (payload.length < 126) return Buffer.concat([Buffer.from([0x82, payload.length]), payload])
  if (payload.length < 65536) {
    const header = Buffer.alloc(4)
    header[0] = 0x82
    header[1] = 126
    header.writeUInt16BE(payload.length, 2)
    return Buffer.concat([header, payload])
  }
  const header = Buffer.alloc(10)
  header[0] = 0x82
  header[1] = 127
  header.writeBigUInt64BE(BigInt(payload.length), 2)
  return Buffer.concat([header, payload])
}

const waitUntil = async (predicate, timeoutMs, message) => {
  const deadline = Date.now() + timeoutMs
  for (;;) {
    if (predicate()) return
    if (Date.now() > deadline) throw new Error(message ?? 'condition never held')
    await new Promise((resolve) => setTimeout(resolve, 100))
  }
}

const generateH264Fixture = async () => {
  const generated = spawnSync('ffmpeg', [
    '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=25',
    '-t', '2', '-an', '-c:v', 'libx264', '-profile:v', 'baseline', '-preset', 'ultrafast', '-tune', 'zerolatency',
    '-g', '25', '-keyint_min', '25', '-sc_threshold', '0', '-x264-params', 'aud=1:repeat-headers=1',
    '-f', 'h264', '-y', h264Path,
  ], { encoding: 'utf8' })
  assert.equal(generated.status, 0, `ffmpeg fixture generation failed: ${generated.stderr}`)
  return locateAccessUnits((await import('node:fs')).readFileSync(h264Path))
}

const startStreamServer = async (frameUnits) => {
  const { createServer } = await import('node:http')
  const streams = new Map()
  const server = createServer(() => undefined)
  server.on('upgrade', (request, socket) => {
    if (request.url?.split('?')[0] !== '/stream' || typeof request.headers['sec-websocket-key'] !== 'string') {
      socket.destroy()
      return
    }
    const accept = createHash('sha1')
      .update(`${request.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
      .digest('base64')
    socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n')
    socket.write(`Sec-WebSocket-Accept: ${accept}\r\n\r\n`)
    socket.setNoDelay(true)
    let sequence = 0
    const timer = setInterval(() => {
      if (socket.destroyed || !socket.writable) return
      try {
        socket.write(websocketFrame(frameBuffer(frameUnits[sequence % frameUnits.length], sequence)))
      } catch {
        clearInterval(timer)
        socket.destroy()
      }
      sequence += 1
    }, 40)
    streams.set(socket, timer)
    socket.on('error', () => undefined)
    socket.on('close', () => {
      clearInterval(timer)
      streams.delete(socket)
    })
  })
  const port = await listen(server, '127.0.0.1')
  return { server, port, url: `ws://127.0.0.1:${port}/stream`, streams }
}

const closeStreamServer = async (stream) => {
  for (const [socket, timer] of stream.streams) {
    clearInterval(timer)
    socket.destroy()
  }
  await closeServer(stream.server)
}

async function startStack({ timeoutMs } = {}) {
  const appServer = createAppServer({
    appRoot: signedRoot,
    probeRoot: path.join(path.dirname(new URL(import.meta.url).pathname), 'download'),
    allowedHostOrigin: '*',
    hostOriginForFrame: ''
  })
  const appPort = await listen(appServer, 'localhost')
  const appOrigin = `http://localhost:${appPort}`
  const hostServer = createHostServer({ distRoot, getAppSiteOrigin: () => appOrigin })
  const hostPort = await listen(hostServer, '127.0.0.1')
  const hostOrigin = `http://127.0.0.1:${hostPort}`
  const params = new URLSearchParams()
  if (timeoutMs) params.set('timeoutMs', String(timeoutMs))
  params.set('livePollMs', '100')
  params.set('appOrigin', appOrigin)
  const hostUrl = `${hostOrigin}/?${params.toString()}`
  return { hostServer, appServer, hostOrigin, appOrigin, hostUrl }
}

async function grantAndAuthorize(stack, browser) {
  const page = await browser.newPage({ viewport: { width: 1280, height: 900 }, locale: 'en-US' })
  await page.goto(stack.hostUrl)
  await page.waitForFunction(
    () => document.querySelector('[data-testid="host-status"]')?.textContent === 'granted',
    undefined,
    { timeout: 20000 }
  )
  return page
}

test.describe = test

test('real signed app #32 runtime handshakes with the host broker and consumes typed results', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    submitSnapshot()
    setBehavior({ staleReceipt: false })
    const page = await grantAndAuthorize(stack, browser)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="lc-module"]').waitFor({ timeout: 30000 })
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 30000 })

    const sandbox = await page.getAttribute('[data-testid="host-frame"]', 'sandbox')
    assert.equal(sandbox, 'allow-scripts allow-same-origin')
    assert.ok(!sandbox.includes('allow-downloads'), 'production sandbox never grants downloads')
    const frameSrc = await page.getAttribute('[data-testid="host-frame"]', 'src')
    assert.ok(frameSrc.startsWith(stack.appOrigin), 'frame src points at the distinct app site')

    const asset = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/assets/index-B-RCOpzx.js`
    )
    assert.equal(asset.status(), 200)
    assert.match(asset.headers()['cache-control'] || '', /no-store/)
    const workerAsset = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/assets/videoWorker-CwSEKqUk.js`
    )
    assert.equal(workerAsset.status(), 200)
    const jmuxerAsset = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/assets/jmuxer.min.js`
    )
    assert.equal(jmuxerAsset.status(), 200)

    const missing = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/unknown.js`
    )
    assert.equal(missing.status(), 404)
    const traversal = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/..%2findex.html`
    )
    assert.equal(traversal.status(), 404)
    const staleGeneration = await page.request.get(`${stack.hostOrigin}/appsite/g1/index.html`)
    assert.equal(staleGeneration.status(), 404)
    const tampered = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/assets/index-B-RCOpzx.js?tamper=yes`
    )
    assert.equal(tampered.status(), 404)

    await frame.locator('[data-testid="lc-tab-advanced"]').click()
    await frame.locator('[data-testid="lc-host-settings-unavailable"]').waitFor({ timeout: 20000 })
    const hostSettings = await frame.locator('[data-testid="lc-host-settings-unavailable"]').textContent()
    assert.match(String(hostSettings), /CAPABILITY_NOT_REVIEWED|unavailable/i)

    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'snapshot.read' && entry.outcome === 'APPLIED'),
      undefined,
      { timeout: 20000 }
    )
    const calls = bridgeCalls()
    const snapshotCall = calls.find((entry) => entry.op === 'snapshot.read')
    assert.ok(snapshotCall, 'the real runtime issued snapshot.read REQUESTs')
    assert.ok(snapshotCall.request_id > 0)
    assert.equal(grantCount() >= 1, true)
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('video.open media grant drives the real worker/JMuxer/MSE pipeline inside the sandboxed frame', async () => {
  const stack = await startStack()
  const browser = await launch()
  let streamServer = null
  let streamPort = 0
  const streams = new Map()
  try {
    const generated = spawnSync('ffmpeg', [
      '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=25',
      '-t', '2', '-an', '-c:v', 'libx264', '-profile:v', 'baseline', '-preset', 'ultrafast', '-tune', 'zerolatency',
      '-g', '25', '-keyint_min', '25', '-sc_threshold', '0', '-x264-params', 'aud=1:repeat-headers=1',
      '-f', 'h264', '-y', h264Path,
    ], { encoding: 'utf8' })
    assert.equal(generated.status, 0, `ffmpeg fixture generation failed: ${generated.stderr}`)
    const frameUnits = locateAccessUnits((await import('node:fs')).readFileSync(h264Path))
    assert.ok(frameUnits.length > 1)

    const { createServer } = await import('node:http')
    streamServer = createServer(() => undefined)
    streamServer.on('upgrade', (request, socket) => {
      if (request.url?.split('?')[0] !== '/stream' || typeof request.headers['sec-websocket-key'] !== 'string') {
        socket.destroy()
        return
      }
      const accept = createHash('sha1')
        .update(`${request.headers['sec-websocket-key']}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
        .digest('base64')
      socket.write('HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n')
      socket.write(`Sec-WebSocket-Accept: ${accept}\r\n\r\n`)
      socket.setNoDelay(true)
      let sequence = 0
      const timer = setInterval(() => {
        if (socket.destroyed || !socket.writable) return
        try {
          socket.write(websocketFrame(frameBuffer(frameUnits[sequence % frameUnits.length], sequence)))
        } catch {
          clearInterval(timer)
          socket.destroy()
        }
        sequence += 1
      }, 40)
      streams.set(socket, timer)
      socket.on('error', () => undefined)
      socket.on('close', () => {
        clearInterval(timer)
        streams.delete(socket)
      })
    })
    streamPort = await listen(streamServer, '127.0.0.1')
    setBehavior({ videoLiveUrl: `ws://127.0.0.1:${streamPort}/stream` })

    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const videoFrame = page.frameLocator('[data-testid="host-frame"]')
    await videoFrame.locator('[data-testid="lc-video-retry"]').waitFor({ timeout: 30000 })
    await videoFrame.locator('[data-testid="lc-video-retry"]').click()
    try {
      await videoFrame.locator('[data-testid="lc-video-state"][data-state="live"]').waitFor({ timeout: 30000 })
    } catch (failure) {
      const state = await videoFrame.locator('[data-testid="lc-video-state"]').textContent()
      const calls = bridgeCalls().map((entry) => entry.op)
      throw new Error(`video never went live (state=${state}, calls=${calls.join(',')})`)
    }
    const playback = await videoFrame
      .locator('#lcVideoPlayer')
      .evaluate((video) => ({ width: video.videoWidth, height: video.videoHeight, time: video.currentTime }))
    assert.deepEqual([playback.width, playback.height], [320, 180])
    assert.ok(playback.time > 0, 'MSE is actually playing decoded frames')

    const videoCall = bridgeCalls().find((entry) => entry.op === 'video.open')
    assert.ok(videoCall, 'the real runtime requested video.open through the broker')
    assert.equal(videoCall.payload.session_generation, 3)
    const workerFetch = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration()}/assets/videoWorker-CwSEKqUk.js`
    )
    assert.equal(workerFetch.status(), 200)
  } finally {
    for (const [socket, timer] of streams) {
      clearInterval(timer)
      socket.destroy()
    }
    rmSync(h264Path, { force: true })
    if (streamServer) await closeServer(streamServer)
    await browser.close().catch(() => undefined)
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('video.open stays refused while the video source is offline and no stream socket opens', async () => {
  const stack = await startStack()
  const browser = await launch()
  let stream = null
  try {
    const frameUnits = await generateH264Fixture()
    stream = await startStreamServer(frameUnits)
    setBehavior({ videoLiveUrl: stream.url, videoSourceActive: false })
    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const videoFrame = page.frameLocator('[data-testid="host-frame"]')
    await videoFrame.locator('[data-testid="lc-video-retry"]').waitFor({ timeout: 30000 })
    await videoFrame.locator('[data-testid="lc-video-retry"]').click()
    await videoFrame.locator('[data-testid="lc-video-state"][data-state="unavailable"]').waitFor({ timeout: 20000 })
    await page.waitForFunction(
      () => (window.results ?? []).some(
        (entry) => entry.op === 'video.open' && entry.outcome === 'UNAVAILABLE' && entry.payload?.reason === 'VIDEO_SOURCE_UNAVAILABLE'
      ),
      undefined,
      { timeout: 20000 }
    )
    const videoCalls = bridgeCalls().filter((entry) => entry.op === 'video.open')
    assert.equal(videoCalls.length, 1)
    assert.equal(videoCalls[0].payload.session_generation, 3)
    await new Promise((resolve) => setTimeout(resolve, 700))
    assert.equal(stream.streams.size, 0, 'no stream socket may open while the source is offline')
    await videoFrame.locator('[data-testid="lc-video-state"][data-state="live"]').waitFor({ timeout: 1500 }).then(
      () => assert.fail('the app must never claim live without a video source'),
      () => undefined
    )
    await page.close()
  } finally {
    rmSync(h264Path, { force: true })
    if (stream) await closeStreamServer(stream)
    await browser.close().catch(() => undefined)
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('the legacy LAN stream entry still serves grant-free H264 frames to ordinary clients', async () => {
  const frameUnits = await generateH264Fixture()
  const stream = await startStreamServer(frameUnits)
  try {
    const ws = new WebSocket(stream.url)
    const payload = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error('no frame within 5s from the legacy entry')), 5000)
      ws.addEventListener('message', (event) => {
        clearTimeout(timer)
        resolve(event.data)
      })
      ws.addEventListener('error', () => {
        clearTimeout(timer)
        reject(new Error('legacy stream entry refused the plain connection'))
      })
    })
    const bytes = payload instanceof ArrayBuffer
      ? new Uint8Array(payload)
      : new Uint8Array(await payload.arrayBuffer())
    assert.deepEqual([...bytes.subarray(0, 4)], [0x57, 0x53, 0x46, 0x53], 'frames keep the WSFS header format')
    assert.ok(bytes.length > 64, 'the frame carries an H264 access unit after the header')
    ws.close()
  } finally {
    rmSync(h264Path, { force: true })
    await closeStreamServer(stream)
  }
})

test('a dropped stream socket drives reconnecting and recovers live without a new video.open', async () => {
  const stack = await startStack()
  const browser = await launch()
  let stream = null
  try {
    const frameUnits = await generateH264Fixture()
    stream = await startStreamServer(frameUnits)
    setBehavior({ videoLiveUrl: stream.url })
    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const videoFrame = page.frameLocator('[data-testid="host-frame"]')
    await videoFrame.locator('[data-testid="lc-video-retry"]').waitFor({ timeout: 30000 })
    await videoFrame.locator('[data-testid="lc-video-retry"]').click()
    await videoFrame.locator('[data-testid="lc-video-state"][data-state="live"]').waitFor({ timeout: 30000 })
    await waitUntil(() => stream.streams.size >= 1, 10000, 'the app never attached to the stream server')
    const openCallsBefore = bridgeCalls().filter((entry) => entry.op === 'video.open').length
    for (const [socket, timer] of stream.streams) {
      clearInterval(timer)
      socket.destroy()
    }
    await new Promise((resolve) => stream.server.close(() => resolve(undefined)))
    await videoFrame.locator('[data-testid="lc-video-state"][data-state="reconnecting"]').waitFor({ timeout: 20000 })
    await new Promise((resolve) => stream.server.listen(stream.port, '127.0.0.1', () => resolve(undefined)))
    await videoFrame.locator('[data-testid="lc-video-state"][data-state="live"]').waitFor({ timeout: 30000 })
    const openCallsAfter = bridgeCalls().filter((entry) => entry.op === 'video.open').length
    assert.equal(openCallsAfter, openCallsBefore, 'recovery must reuse the granted URL, not request a new grant')
    await page.close()
  } finally {
    rmSync(h264Path, { force: true })
    if (stream) await closeStreamServer(stream)
    await browser.close().catch(() => undefined)
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('admin revoke tears down the trusted frame, drops the app socket and no video reconnect follows', async () => {
  const stack = await startStack()
  const browser = await launch()
  let stream = null
  try {
    const frameUnits = await generateH264Fixture()
    stream = await startStreamServer(frameUnits)
    setBehavior({ videoLiveUrl: stream.url })
    submitSnapshot()
    setBehavior({ staleReceipt: false })
    const page = await grantAndAuthorize(stack, browser)
    const videoFrame = page.frameLocator('[data-testid="host-frame"]')
    await videoFrame.locator('[data-testid="lc-video-retry"]').waitFor({ timeout: 30000 })
    await videoFrame.locator('[data-testid="lc-video-retry"]').click()
    await videoFrame.locator('[data-testid="lc-video-state"][data-state="live"]').waitFor({ timeout: 30000 })
    await waitUntil(() => stream.streams.size >= 1, 10000, 'the app never attached to the stream server')
    const openCallsBefore = bridgeCalls().filter((entry) => entry.op === 'video.open').length
    const revokeResponse = await page.request.post(`${stack.hostOrigin}/api/v1/app/sidecar/bridge/revoke`, {
      data: { all: true }
    })
    assert.ok(revokeResponse.ok(), 'admin revoke endpoint answered')
    await page.waitForFunction(
      () => document.querySelector('[data-testid="host-status"]')?.textContent === 'revoked',
      undefined,
      { timeout: 30000 }
    )
    assert.equal(await page.locator('[data-testid="host-frame"]').count(), 0, 'the revoked frame is destroyed')
    await waitUntil(() => stream.streams.size === 0, 10000, 'the app stream socket must drop on iframe teardown')
    await new Promise((resolve) => setTimeout(resolve, 3000))
    const openCallsAfter = bridgeCalls().filter((entry) => entry.op === 'video.open').length
    assert.equal(openCallsAfter, openCallsBefore, 'a revoked app must not request video again')
  } finally {
    rmSync(h264Path, { force: true })
    if (stream) await closeStreamServer(stream)
    await browser.close().catch(() => undefined)
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('config.apply conflict and counters.reset admin confirm EVENT complete the write loop', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    submitSnapshot()
    setBehavior({ staleReceipt: true })
    const page = await grantAndAuthorize(stack, browser)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 30000 })

    await frame.locator('[data-testid="lc-tab-config"]').click()
    await frame.locator('[data-testid="lc-page-config"]').waitFor({ timeout: 20000 })
    await frame.locator('[data-testid="lc-counter-name"]').fill('Renamed Counter')
    await frame.locator('[data-testid="lc-save-config"]').click()
    await page.waitForFunction(
      () => {
        const calls = (window.results ?? []).filter((entry) => entry.op === 'config.apply')
        return calls.length >= 1 && calls[calls.length - 1].outcome === 'CONFLICT'
      },
      undefined,
      { timeout: 30000 }
    )
    const applyCall = bridgeCalls().find((entry) => entry.op === 'config.apply')
    assert.ok(applyCall, 'config.apply REQUEST reached the Host server')
    assert.equal(applyCall.expected_config_generation, 6)
    assert.equal(applyCall.payload.session_generation, 3)
    assert.ok(applyCall.payload.candidate && applyCall.payload.candidate.counter_name === 'Renamed Counter')
    const conflictResult = await page.evaluate(
      () => (window.results ?? []).filter((entry) => entry.op === 'config.apply').at(-1)
    )
    assert.equal(conflictResult.outcome, 'CONFLICT')
    assert.equal(conflictResult.payload.reason, 'STALE_CONFIG_GENERATION')

    setBehavior({ staleReceipt: false })
    await frame.locator('[data-testid="lc-tab-realtime"]').click()
    await frame.locator('[data-testid="lc-reset-stats"]').click()
    await frame.locator('[data-testid="lc-reset-dialog"]').waitFor()
    await frame.locator('[data-testid="lc-reset-dialog"] button').last().click()
    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'counters.reset' && entry.outcome === 'REJECTED'),
      undefined,
      { timeout: 30000 }
    )
    const rejectedReset = await page.evaluate(
      () => (window.results ?? []).filter((entry) => entry.op === 'counters.reset').at(-1)
    )
    assert.equal(rejectedReset.outcome, 'REJECTED')
    assert.equal(rejectedReset.payload.reason, 'ADMIN_CONFIRM_REQUIRED')
    const rejectedCalls = bridgeCalls().filter((entry) => entry.op === 'counters.reset')
    assert.equal(rejectedCalls.length, 1)
    assert.equal(rejectedCalls[0].confirm_token, false, 'the unconfirmed retry carries no token')
    assert.deepEqual(rejectedCalls[0].payload, { session_generation: 3 })

    await page.locator('[data-testid="host-confirm-dangerous"]').waitFor()
    await page.locator('[data-testid="host-confirm-dangerous-button"]').click()
    await page.waitForFunction(() => window.confirmDelivered === true, undefined, { timeout: 20000 })
    const tokens = issuedConfirmTokens()
    assert.equal(tokens.length, 1, 'the confirm endpoint issued exactly one bearer token')
    const token = tokens[0]
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 20000 })

    await frame.locator('[data-testid="lc-reset-stats"]').click()
    await frame.locator('[data-testid="lc-reset-dialog"]').waitFor()
    await frame.locator('[data-testid="lc-reset-dialog"] button').last().click()
    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'counters.reset' && entry.outcome === 'APPLIED'),
      undefined,
      { timeout: 30000 }
    )
    const resetCalls = bridgeCalls().filter((entry) => entry.op === 'counters.reset')
    assert.ok(resetCalls.length >= 2, 'the App retried counters.reset after the admin confirm')
    assert.equal(resetCalls[1].confirm_token, true, 'the host attached the armed token to its own HTTP body')
    assert.deepEqual(resetCalls[1].payload, { session_generation: 3 }, 'the app payload never carries the token')

    const appFrame = page.frames().find((entry) => entry.url().startsWith(stack.appOrigin))
    assert.ok(appFrame, 'the signed app frame is present')
    const appDom = await appFrame.evaluate(() => document.documentElement.outerHTML)
    assert.ok(!appDom.includes(token), 'the one-shot token never reaches the app frame DOM')
    const hostResults = await page.evaluate(() => JSON.stringify(window.results ?? []))
    assert.ok(!hostResults.includes(token), 'the one-shot token never reaches the app RESULT stream')

    await frame.locator('[data-testid="lc-reset-stats"]').click()
    await frame.locator('[data-testid="lc-reset-dialog"]').waitFor()
    await frame.locator('[data-testid="lc-reset-dialog"] button').last().click()
    await page.waitForFunction(
      () => {
        const entries = (window.results ?? []).filter((entry) => entry.op === 'counters.reset')
        return entries.length >= 3 && entries.at(-1).outcome === 'REJECTED'
      },
      undefined,
      { timeout: 30000 }
    )
    const replayCalls = bridgeCalls().filter((entry) => entry.op === 'counters.reset')
    assert.equal(replayCalls.at(-1).confirm_token, false, 'the consumed token is never re-attached')
    assert.deepEqual(replayCalls.at(-1).payload, { session_generation: 3 })
    const replayResult = await page.evaluate(
      () => (window.results ?? []).filter((entry) => entry.op === 'counters.reset').at(-1)
    )
    assert.equal(replayResult.outcome, 'REJECTED')
    assert.equal(replayResult.payload.reason, 'ADMIN_CONFIRM_REQUIRED')
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 20000 })
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('enable.read projects the trusted state and enable.apply applies only after the trusted admin confirm', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 30000 })
    await frame.locator('[data-testid="lc-tab-config"]').click()
    await frame.locator('[data-testid="lc-page-config"]').waitFor({ timeout: 20000 })
    await frame.locator('[data-testid="lc-business-enable-switch"][data-state="checked"]').waitFor({ timeout: 30000 })

    await frame.locator('[data-testid="lc-business-enable-switch"]').click()
    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'enable.apply' && entry.outcome === 'REJECTED'),
      undefined,
      { timeout: 30000 }
    )
    const rejected = await page.evaluate(
      () => (window.results ?? []).filter((entry) => entry.op === 'enable.apply').at(-1)
    )
    assert.equal(rejected.payload.reason, 'ADMIN_CONFIRM_REQUIRED')
    const refusedCalls = bridgeCalls().filter((entry) => entry.op === 'enable.apply')
    assert.equal(refusedCalls.length, 1)
    assert.deepEqual(refusedCalls[0].payload, { session_generation: 3, value: false })
    assert.equal(refusedCalls[0].confirm_token, false)

    await page.locator('[data-testid="host-confirm-dangerous"]').waitFor()
    await page.locator('[data-testid="host-confirm-dangerous-button"]').click()
    await page.waitForFunction(() => window.confirmDelivered === true, undefined, { timeout: 20000 })

    await frame.locator('[data-testid="lc-business-enable-switch"]').click()
    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'enable.apply' && entry.outcome === 'APPLIED'),
      undefined,
      { timeout: 30000 }
    )
    const applied = await page.evaluate(
      () => (window.results ?? []).filter((entry) => entry.op === 'enable.apply').at(-1)
    )
    assert.deepEqual(applied.payload, { session_generation: 3, value: false })
    const confirmedCalls = bridgeCalls().filter((entry) => entry.op === 'enable.apply')
    assert.equal(confirmedCalls[1].confirm_token, true, 'the host attached the armed token to its own HTTP body')
    await frame.locator('[data-testid="lc-business-enable-switch"][data-state="unchecked"]').waitFor({ timeout: 30000 })
    const appFrame = page.frames().find((entry) => entry.url().startsWith(stack.appOrigin))
    assert.ok(appFrame, 'the signed app frame is present')
    const token = issuedConfirmTokens()[0]
    const appDom = await appFrame.evaluate(() => document.documentElement.outerHTML)
    assert.ok(!appDom.includes(token), 'the enable confirm token never reaches the app frame DOM')

    setBehavior({ enableStateConflict: true })
    await frame.locator('[data-testid="lc-business-enable-switch"]').click()
    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'enable.apply' && entry.outcome === 'REJECTED'),
      undefined,
      { timeout: 30000 }
    )
    await page.locator('[data-testid="host-confirm-dangerous"]').waitFor()
    await page.locator('[data-testid="host-confirm-dangerous-button"]').click()
    await page.waitForFunction(() => window.confirmDelivered === true, undefined, { timeout: 20000 })
    await frame.locator('[data-testid="lc-business-enable-switch"]').click()
    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'enable.apply' && entry.outcome === 'CONFLICT'),
      undefined,
      { timeout: 30000 }
    )
    const conflict = await page.evaluate(
      () => (window.results ?? []).filter((entry) => entry.op === 'enable.apply').at(-1)
    )
    assert.equal(conflict.payload.reason, 'STATE_CONFLICT')
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('live.subscribe pumps typed crossing facts into the signed Realtime UI and a new source gap fails it closed', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 30000 })
    await frame.locator('[data-testid="lc-tab-realtime"]').click()
    await frame.locator('[data-testid="lc-page-realtime"]').waitFor({ timeout: 20000 })
    await page.waitForFunction(
      () => (window.results ?? []).some((entry) => entry.op === 'live.subscribe' && entry.outcome === 'APPLIED'),
      undefined,
      { timeout: 30000 }
    )
    assert.equal(bridgeCalls().find((entry) => entry.op === 'live.subscribe').payload.stream, 'live')

    pushLiveCrossing({ direction: 'in' })
    pushLiveCrossing({ direction: 'out', trackId: 12 })
    await frame.locator('[data-testid="lc-event-list"] [data-testid="lc-event-class"]').first().waitFor({ timeout: 30000 })
    const classes = await frame.locator('[data-testid="lc-event-class"]').allTextContents()
    assert.ok(classes.length >= 2)
    for (const label of classes) {
      assert.equal(String(label).trim(), 'person')
    }

    setLiveTracks([
      {
        track_id: 9,
        point_count: 2,
        class_index: 0,
        state: 1,
        flags: 0,
        confidence_q16: 60000,
        points: [
          { ts_ms: 1000, x_q16: 32768, y_q16: 16384 },
          { ts_ms: 1100, x_q16: 49152, y_q16: 65535 }
        ]
      }
    ])
    await new Promise((resolve) => setTimeout(resolve, 700))
    assert.equal(await frame.locator('[data-testid="lc-events-unavailable"]').count(), 0, 'a valid tracks frame never fails the live view')

    setLiveBehavior({ sourceGap: 3 })
    await frame.locator('[data-testid="lc-events-unavailable"]').waitFor({ timeout: 30000 })
    assert.equal(
      await frame.locator('[data-testid="lc-event-list"] [data-testid="lc-event-class"]').count(),
      0,
      'no crossing facts stay visible after the live stream fails closed'
    )
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('live.subscribe without a trusted native producer stays unavailable for the signed Realtime UI', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    setLiveBehavior({ available: false })
    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 30000 })
    await frame.locator('[data-testid="lc-tab-realtime"]').click()
    await frame.locator('[data-testid="lc-page-realtime"]').waitFor({ timeout: 20000 })
    await page.waitForFunction(
      () => (window.results ?? []).some(
        (entry) => entry.op === 'live.subscribe' && entry.outcome === 'UNAVAILABLE' && entry.payload?.reason === 'LIVE_UNAVAILABLE'
      ),
      undefined,
      { timeout: 30000 }
    )
    await frame.locator('[data-testid="lc-events-unavailable"]').waitFor({ timeout: 30000 })
    assert.equal(await frame.locator('[data-testid="lc-event-list"] [data-testid="lc-event-class"]').count(), 0)
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('generation switch revokes the grant, delivers REVOKE and tears down the old frame', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 30000 })
    await page.request.get(`${stack.hostOrigin}/trigger-generation-switch`)
    await page.waitForFunction(
      () => document.querySelector('[data-testid="host-status"]')?.textContent === 'revoked',
      undefined,
      { timeout: 30000 }
    )
    const reason = await page.textContent('[data-testid="host-revoked"]')
    assert.match(String(reason), /IDENTITY_OR_GENERATION_CHANGED|GENERATION/)
    const frameCount = await page.locator('[data-testid="host-frame"]').count()
    assert.equal(frameCount, 0, 'old-generation frame is destroyed')
    const staleAsset = await page.request.get(
      `${stack.hostOrigin}/appsite/g${currentGeneration() - 1}/index.html`
    )
    assert.equal(staleAsset.status(), 404)
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('forged window messages from the frame never touch the trusted session', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    submitSnapshot()
    const page = await grantAndAuthorize(stack, browser)
    const frame = page.frameLocator('[data-testid="host-frame"]')
    await frame.locator('[data-testid="lc-app-state"][data-state="available"]').waitFor({ timeout: 30000 })
    const hostOrigin = stack.hostOrigin
    try {
      await frame.locator('body').evaluate((_, origin) => {
        window.parent.postMessage(
          {
            protocol: 'ne301.sidecar.bridge.v1',
            type: 'REQUEST',
            seq: 9999,
            grant_id: 'f'.repeat(64),
            identity: {},
            request_id: 500,
            op: 'config.apply',
            payload: {}
          },
          origin
        )
      }, hostOrigin)
    } catch (evalFailure) {
      throw new Error(`forged postMessage evaluate failed: ${evalFailure.message}`)
    }
    try {
      await page
        .locator('[data-testid="host-forged-rejected"]')
        .filter({ hasText: 'ignored-window-message' })
        .waitFor({ timeout: 15000 })
    } catch (waitFailure) {
      const markerText = await page.locator('[data-testid="host-forged-rejected"]').textContent()
      const status = await page.textContent('[data-testid="host-status"]')
      throw new Error(`marker never fired (text=${JSON.stringify(markerText)}, status=${status}): ${waitFailure.message}`)
    }
    const status = await page.textContent('[data-testid="host-status"]')
    assert.equal(status, 'granted', 'forged traffic never changes the trusted session state')
    assert.equal(await page.locator('[data-testid="host-frame"]').count(), 1)
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('same-site app origin is refused by the host gate before any iframe is created', async () => {
  const stack = await startStack()
  const browser = await launch()
  try {
    const page = await browser.newPage()
    await page.goto(`${stack.hostOrigin}/?appOrigin=${encodeURIComponent(stack.hostOrigin)}`)
    await page.waitForFunction(
      () => document.querySelector('[data-testid="host-status"]')?.textContent === 'blocked-same-site',
      undefined,
      { timeout: 15000 }
    )
    assert.equal(await page.locator('[data-testid="host-frame"]').count(), 0)
    await page.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})

test('sandbox download gate denies downloads in the trusted frame and allows them only in the allow-downloads control', async () => {
  const stack = await startStack()
  const browser = await launch({ args: ['--autoplay-policy=no-user-gesture-required'] })
  try {
    const context = await browser.newContext({ acceptDownloads: true })
    const page = await context.newPage()

    await page.goto(`${stack.appOrigin}/denied-frame.html`)
    const deniedProbe = page.locator('#probe').contentFrame().locator('#go')
    await deniedProbe.waitFor()
    const deniedSandbox = await page.locator('#probe').getAttribute('sandbox')
    assert.equal(deniedSandbox, 'allow-scripts allow-same-origin')
    let deniedDownload = null
    try {
      await Promise.all([
        page.waitForEvent('download', { timeout: 4000 }),
        deniedProbe.click()
      ]).then(([download]) => { deniedDownload = download })
    } catch {
      deniedDownload = null
    }
    assert.equal(deniedDownload, null, 'sandbox without allow-downloads blocks the blob anchor download')

    await page.goto(`${stack.appOrigin}/allowed-frame.html`)
    const allowedProbe = page.locator('#probe').contentFrame().locator('#go')
    await allowedProbe.waitFor()
    const allowedSandbox = await page.locator('#probe').getAttribute('sandbox')
    assert.equal(allowedSandbox, 'allow-scripts allow-same-origin allow-downloads')
    const [download] = await Promise.all([
      page.waitForEvent('download', { timeout: 8000 }),
      allowedProbe.click()
    ])
    assert.equal(download.suggestedFilename(), 'probe-download.bin', 'the allow-downloads control proves the same action can download')
    await context.close()
  } finally {
    await browser.close()
    await closeServer(stack.hostServer)
    await closeServer(stack.appServer)
  }
})
