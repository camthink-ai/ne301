import assert from 'node:assert/strict'
import { createHash, generateKeyPairSync, sign } from 'node:crypto'
import { execFileSync } from 'node:child_process'
import { existsSync, mkdirSync, readFileSync, readdirSync, realpathSync, rmSync, symlinkSync, writeFileSync } from 'node:fs'
import path from 'node:path'
import preact from '@preact/preset-vite'
import tailwindcss from '@tailwindcss/vite'
import { build } from 'vite'
import { canonicalize, sidecarProbeDomain, verifySidecarEnvelope } from './binding.mjs'
import { distRoot, fixtureRoot, issue80Commit, publicRoot, repoRoot, sourceCommit, sourceRoot, tempRoot, videoPath } from './paths.mjs'

const sourcePaths = [
  'Web/src/pages/applicationManagement/line-counting-module.tsx',
  'Web/src/pages/applicationManagement/lineCounting/VideoPreview.tsx',
  'Web/src/pages/applicationManagement/lineCounting/ConfigPage.tsx',
  'Web/src/pages/applicationManagement/lineCounting/StatsAndEvents.tsx',
  'Web/src/pages/applicationManagement/lineCounting/LineToolbar.tsx',
  'Web/src/pages/applicationManagement/lineCounting/AdvancedPage.tsx',
  'Web/src/pages/applicationManagement/lineCounting/fields.tsx',
  'Web/src/services/api/line-counting.ts',
  'Web/src/services/api/deviceTool.ts',
  'Web/src/components/ui/card.tsx',
  'Web/src/components/ui/button.tsx',
  'Web/src/components/ui/input.tsx',
  'Web/src/components/ui/label.tsx',
  'Web/src/components/ui/select.tsx',
  'Web/src/components/ui/switch.tsx',
  'Web/src/components/android-UI/simple-android-switch.tsx',
  'Web/src/components/android-UI/device-detector.ts',
  'Web/src/lib/utils.ts',
  'Web/src/styles/index.css',
  'Web/src/styles/globals.css',
  'Web/src/utils/index.ts',
  'Web/src/lib/MSE/h264Player.ts',
  'Web/src/lib/MSE/media.ts',
  'Web/src/lib/MSE/constant.ts',
  'Web/src/lib/MSE/utils/myBrowser.ts',
  'Web/src/lib/MSE/websocket-init.ts',
  'Web/src/lib/MSE/videoWorker.ts',
  'Web/src/locales/en/sys.json',
  'Web/src/locales/en/com.json'
]
const publicPaths = ['Web/public/libs/FileSaver.min.js', 'Web/public/libs/jmuxer.min.js']
const oldFiles = [
  'tests/app_ui_sidecar/binding.mjs',
  'tests/app_ui_sidecar/binding.test.mjs',
  'tests/app_ui_sidecar/native-v3.test.mjs',
  'tests/app_ui_sidecar/browser.test.mjs',
  'tests/app_ui_sidecar/adapters/bridge.ts'
]
const oldNativePaths = [
  'tests/app_admission/test_app_admission_v3.c',
  'Custom/Services/AppHost/app_admission.c',
  'tests/app_admission/gen_v3_vectors.py'
]
const behaviorBaselinePaths = [
  'Web/src/test/lineCountingApi.test.ts',
  'Web/src/test/lineCountingNavigation.test.tsx',
  'Web/src/test/lineCountingPage.test.tsx'
]
const adapters = [
  'adapters/bridge.ts',
  'adapters/request.ts',
  'adapters/lingui.ts',
  'adapters/sonner.ts',
  'adapters/utils.ts',
  'src/main.tsx',
  'src/fixture.css',
  'index.html'
]
const sha256 = value => createHash('sha256').update(value).digest('hex')
const gitShow = (commit, file) => execFileSync('git', ['show', `${commit}:${file}`], { cwd: repoRoot, encoding: 'utf8' })
const writeSource = (file, data) => {
  const destination = path.join(sourceRoot, file.replace(/^Web\/src\//, ''))
  mkdirSync(path.dirname(destination), { recursive: true })
  writeFileSync(destination, data)
}
const collectFiles = directory => readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
  const file = path.join(directory, entry.name)
  return entry.isDirectory() ? collectFiles(file) : [file]
})
const relativeFiles = directory => collectFiles(directory).map(file => ({ path: path.relative(directory, file).split(path.sep).join('/'), bytes: readFileSync(file) })).sort((a, b) => a.path.localeCompare(b.path))
const writeJson = (file, value) => writeFileSync(file, `${JSON.stringify(value, null, 2)}\n`)

const marker = path.join(tempRoot, '.fixture-marker')
if (existsSync(tempRoot)) {
  if (!existsSync(marker) || readFileSync(marker, 'utf8') !== realpathSync(fixtureRoot)) throw new Error(`Refusing to replace unmarked temporary path: ${tempRoot}`)
  rmSync(tempRoot, { recursive: true, force: true })
}
mkdirSync(sourceRoot, { recursive: true })
mkdirSync(publicRoot, { recursive: true })
writeFileSync(marker, realpathSync(fixtureRoot))
symlinkSync(path.join(fixtureRoot, 'node_modules'), path.join(tempRoot, 'node_modules'), 'dir')

const pinnedSources = sourcePaths.map(file => {
  const data = Buffer.from(gitShow(sourceCommit, file))
  writeSource(file, data)
  return { path: file, sha256: sha256(data), bytes: data.length }
})
const sourceCssPath = path.join(sourceRoot, 'styles/index.css')
const sourceCss = readFileSync(sourceCssPath, 'utf8')
const generatedSourceDirective = '@source "..";\n'
writeFileSync(sourceCssPath, `${generatedSourceDirective}${sourceCss}`)
const pinnedPublic = publicPaths.map(file => {
  const data = Buffer.from(gitShow(sourceCommit, file))
  const destination = path.join(publicRoot, file.replace(/^Web\/public\//, ''))
  mkdirSync(path.dirname(destination), { recursive: true })
  writeFileSync(destination, data)
  return { path: file, sha256: sha256(data), bytes: data.length }
})
const requestAdapter = readFileSync(path.join(fixtureRoot, 'adapters/request.ts'))
const requestAdapterSource = requestAdapter.toString().replace("from './bridge'", `from '${path.join(fixtureRoot, 'adapters/bridge').split(path.sep).join('/')}'`)
writeSource('Web/src/services/request.ts', requestAdapterSource)
const browserAdapter = `const agent = navigator.userAgent.toLowerCase(); export default { isBrowserFirefox: () => agent.includes('firefox'), isBrowserSafari: () => agent.includes('safari') && !agent.includes('chrome'), isBrowserChrome: () => agent.includes('chrome') && !agent.includes('edg') };\n`
const browserAdapterPath = path.join(sourceRoot, 'lib/MSE/utils/myBrowser.js')
writeFileSync(browserAdapterPath, browserAdapter)

const packageLock = readFileSync(path.join(fixtureRoot, 'package-lock.json'))
const buildId = sha256(Buffer.from(`${sourceCommit}\n${sha256(packageLock)}\nline-counting-fixture-v1`))
const htmlPath = path.join(fixtureRoot, 'index.html')
const html = readFileSync(htmlPath, 'utf8').replace('__APP_BUILD_ID__', buildId)
await build({
  root: fixtureRoot,
  publicDir: publicRoot,
  plugins: [
    preact({ reactAliases: true }),
    tailwindcss(),
    { name: 'counting-fixture-build-id', transformIndexHtml: value => value.replace('__APP_BUILD_ID__', buildId) }
  ],
  resolve: {
    alias: [
      { find: /^@\/utils(?:\/index\.js)?$/, replacement: path.join(fixtureRoot, 'adapters/utils.ts') },
      { find: '@lingui/react', replacement: path.join(fixtureRoot, 'adapters/lingui.ts') },
      { find: 'sonner', replacement: path.join(fixtureRoot, 'adapters/sonner.ts') },
      { find: '@source', replacement: sourceRoot },
      { find: '@', replacement: sourceRoot }
    ]
  },
  build: {
    outDir: distRoot,
    emptyOutDir: true,
    sourcemap: false,
    rollupOptions: { output: { manualChunks: undefined } }
  }
})

const builtFiles = relativeFiles(distRoot)
const assetFiles = Object.fromEntries(builtFiles.map(file => [file.path, file.bytes]))
const nativeIdentity = {
  trusted: true,
  packageSha256: sha256(Buffer.from('test-only-line-counting-native-package')),
  publisherId: 'fixture-publisher',
  appId: 'line-counting',
  keySha256: sha256(Buffer.from('test-only-line-counting-native-key'))
}
const keyPair = generateKeyPairSync('ed25519')
const publicDer = keyPair.publicKey.export({ format: 'der', type: 'spki' })
const signingKeySha256 = sha256(publicDer)
const manifest = {
  format: 'sidecar-probe-v1',
  sidecarId: 'line-counting-ui',
  name: 'line-counting-ui',
  binding: {
    packageSha256: nativeIdentity.packageSha256,
    publisherId: nativeIdentity.publisherId,
    appId: nativeIdentity.appId,
    keySha256: nativeIdentity.keySha256
  },
  assets: builtFiles.map(file => ({ path: file.path, size: file.bytes.length, sha256: sha256(file.bytes) }))
}
const signatureBytes = sign(null, Buffer.from(canonicalize(manifest)), keyPair.privateKey)
const envelope = {
  manifest,
  files: assetFiles,
  signature: {
    domain: sidecarProbeDomain,
    algorithm: 'ed25519',
    keySha256: signingKeySha256,
    value: signatureBytes.toString('base64')
  }
}
const verified = verifySidecarEnvelope(envelope, nativeIdentity, new Map([[signingKeySha256, keyPair.publicKey]]))
assert.equal(verified.state, 'verified')

const packageRoot = path.join(tempRoot, 'signed-package')
mkdirSync(packageRoot, { recursive: true })
for (const file of builtFiles) {
  const destination = path.join(packageRoot, file.path)
  mkdirSync(path.dirname(destination), { recursive: true })
  writeFileSync(destination, file.bytes)
}
const manifestBytes = Buffer.from(canonicalize(manifest))
writeFileSync(path.join(packageRoot, 'manifest.json'), manifestBytes)
writeFileSync(path.join(packageRoot, 'manifest.sig'), signatureBytes)
const tarBytes = execFileSync('tar', ['-cf', '-', '-C', packageRoot, '.'])
const archiveBytes = execFileSync('gzip', ['-n', '-9', '-c'], { input: tarBytes })
const expandedBytes = relativeFiles(packageRoot).reduce((total, file) => total + file.bytes.length, 0)
const outputBytes = builtFiles.reduce((total, file) => total + file.bytes.length, 0)
const metrics = {
  buildId,
  sourceCommit,
  outputFileCount: builtFiles.length,
  outputBytes,
  concatenatedAssetsGzipBytes: execFileSync('gzip', ['-n', '-9', '-c'], { input: Buffer.concat(builtFiles.map(file => file.bytes)) }).length,
  archiveBytes: archiveBytes.length,
  manifestBytes: manifestBytes.length,
  signatureBytes: signatureBytes.length,
  expandedBundleBytes: expandedBytes,
  installStagePeakBytes: archiveBytes.length + expandedBytes,
  archiveMethod: 'tar archive with gzip -n -9; a fixture staging proxy, not an NE301 package format',
  assets: builtFiles.map(file => ({ path: file.path, bytes: file.bytes.length, sha256: sha256(file.bytes) }))
}

const h264Source = path.join(tempRoot, 'line-counting-source.h264')
execFileSync('ffmpeg', [
  '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x180:rate=25',
  '-t', '2', '-an', '-c:v', 'libx264', '-profile:v', 'baseline', '-preset', 'ultrafast', '-tune', 'zerolatency',
  '-g', '25', '-keyint_min', '25', '-sc_threshold', '0', '-x264-params', 'aud=1:repeat-headers=1',
  '-f', 'h264', '-y', h264Source
])
const videoBytes = readFileSync(h264Source)
const starts = []
for (let index = 0; index + 4 < videoBytes.length;) {
  if (videoBytes[index] === 0 && videoBytes[index + 1] === 0 && videoBytes[index + 2] === 0 && videoBytes[index + 3] === 1) {
    starts.push({ at: index, header: index + 4 })
    index += 4
  } else if (videoBytes[index] === 0 && videoBytes[index + 1] === 0 && videoBytes[index + 2] === 1) {
    starts.push({ at: index, header: index + 3 })
    index += 3
  } else index += 1
}
const aud = starts.filter(item => (videoBytes[item.header] & 31) === 9).map(item => item.at)
assert.ok(aud.length > 1)
const frameStarts = [0, ...aud.slice(1)]
const accessUnits = frameStarts.map((start, index) => videoBytes.subarray(start, frameStarts[index + 1] ?? videoBytes.length))
assert.ok(accessUnits.every(unit => unit.length > 68 && unit[0] === 0))
writeFileSync(videoPath, videoBytes)
const videoInfo = {
  encoder: execFileSync('ffmpeg', ['-version'], { encoding: 'utf8' }).split('\n')[0],
  generation: 'local ffmpeg lavfi testsrc2, H.264 Baseline Annex-B, 320x180, 25 fps, 2 seconds',
  sourceBytes: videoBytes.length,
  sourceSha256: sha256(videoBytes),
  accessUnitCount: accessUnits.length,
  accessUnitBytes: accessUnits.map(unit => unit.length),
  width: 320,
  height: 180,
  transport: '64-byte test frame header followed by the access unit over loopback WebSocket'
}

const sourceHashes = [...pinnedSources, ...pinnedPublic]
const adapterHashes = adapters.map(file => {
  const data = readFileSync(path.join(fixtureRoot, file))
  return { path: file, sha256: sha256(data), bytes: data.length }
})
adapterHashes.push({ path: 'generated/source/services/request.ts', sha256: sha256(Buffer.from(requestAdapterSource)), bytes: Buffer.byteLength(requestAdapterSource) })
adapterHashes.push({ path: 'generated/source/lib/MSE/utils/myBrowser.js', sha256: sha256(browserAdapter), bytes: Buffer.byteLength(browserAdapter) })
adapterHashes.push({ path: 'generated/source/styles/index.css', sha256: sha256(Buffer.from(`${generatedSourceDirective}${sourceCss}`)), bytes: Buffer.byteLength(`${generatedSourceDirective}${sourceCss}`), reason: 'fixture-only Tailwind v4 source scan root for the extracted counting closure' })
const pr81Materials = oldFiles.map(file => {
  const data = Buffer.from(gitShow(issue80Commit, file))
  return { commit: issue80Commit, path: file, sha256: sha256(data), bytes: data.length }
})
const behaviorBaselineTests = behaviorBaselinePaths.map(file => {
  const data = Buffer.from(gitShow(sourceCommit, file))
  return { commit: sourceCommit, path: file, sha256: sha256(data), bytes: data.length }
})
const nativeV3Hashes = oldNativePaths.map(file => {
  const data = readFileSync(path.join(repoRoot, file))
  return { baseline: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: repoRoot, encoding: 'utf8' }).trim(), path: file, sha256: sha256(data), bytes: data.length }
})
const lockHash = sha256(packageLock)
const closure = {
  sourceCommit,
  issue80Commit: execFileSync('git', ['rev-parse', issue80Commit], { cwd: repoRoot, encoding: 'utf8' }).trim(),
  actualEntry: 'Web/src/pages/applicationManagement/line-counting-module.tsx',
  sourceFiles: sourceHashes,
  fixtureAdapters: adapterHashes,
  behaviorBaselineTests,
  pr81Materials,
  frozenNativeV3Sources: nativeV3Hashes,
  dependencies: JSON.parse(readFileSync(path.join(fixtureRoot, 'package.json'), 'utf8')),
  packageLockSha256: lockHash,
  buildId,
  transportAdaptations: [
    'The source line-counting API and deviceTool callers remain exact; Web/src/services/request.ts is replaced only by a local typed capability transport adapter.',
    'The source getWebSocketUrl and sleep exports are substituted with loopback-only equivalents so the original H264Player worker path remains active.',
    'The source browser detector import is mapped to a behavior-equivalent JavaScript probe for the original H264Player.',
    'Lingui and Sonner hooks are local English/toast adapters; all counting page and lineCounting component TSX remains pinned source.'
  ]
}

writeJson(path.join(fixtureRoot, 'evidence/source-closure.json'), closure)
writeJson(path.join(fixtureRoot, 'evidence/fixture-metrics.json'), metrics)
writeJson(path.join(fixtureRoot, 'evidence/h264-fixture.json'), videoInfo)
process.stdout.write(`${JSON.stringify({ buildId, sourceCommit, outputFileCount: metrics.outputFileCount, outputBytes, concatenatedAssetsGzipBytes: metrics.concatenatedAssetsGzipBytes, archiveBytes: metrics.archiveBytes, manifestBytes: metrics.manifestBytes, signatureBytes: metrics.signatureBytes, installStagePeakBytes: metrics.installStagePeakBytes, h264SourceBytes: videoBytes.length, accessUnitCount: accessUnits.length, distRoot }, null, 2)}\n`)
