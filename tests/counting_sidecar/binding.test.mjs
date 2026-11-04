import assert from 'node:assert/strict'
import { createHash, generateKeyPairSync, sign } from 'node:crypto'
import test from 'node:test'
import { SidecarMockHost, canonicalize, sidecarProbeDomain, verifySidecarEnvelope } from './binding.mjs'

const bytes = value => Buffer.from(value)
const sha256 = value => createHash('sha256').update(value).digest('hex')
const keyHash = publicKey => sha256(publicKey.export({ format: 'der', type: 'spki' }))
const native = (overrides = {}) => ({
  trusted: true,
  publisherId: 'pub-alpha',
  appId: 'people-counting',
  packageSha256: sha256(bytes('native-package-v1')),
  keySha256: sha256(bytes('native-key-alpha')),
  ...overrides
})

const publisher = () => {
  const pair = generateKeyPairSync('ed25519')
  return { ...pair, keySha256: keyHash(pair.publicKey) }
}

const issue = (signer, binding, overrides = {}, files = { 'index.html': bytes('<main>sidecar</main>') }) => {
  const assets = Object.entries(files).map(([path, data]) => ({ path, size: data.length, sha256: sha256(data) }))
  const manifest = {
    format: 'sidecar-probe-v1',
    sidecarId: 'people-counting-ui',
    name: 'people-counting-ui',
    binding: {
      packageSha256: binding.packageSha256,
      publisherId: binding.publisherId,
      appId: binding.appId,
      keySha256: binding.keySha256
    },
    assets,
    ...overrides
  }
  const signature = sign(null, Buffer.from(canonicalize(manifest)), signer.privateKey).toString('base64')
  return {
    manifest,
    files,
    signature: {
      domain: sidecarProbeDomain,
      algorithm: 'ed25519',
      keySha256: signer.keySha256,
      value: signature
    }
  }
}

const trust = signer => new Map([[signer.keySha256, signer.publicKey]])
const expectCode = (fn, code) => assert.throws(fn, error => error.code === code)

test('accepts a test-root signed asset set bound to all trusted native identity fields', () => {
  const installed = native()
  const signer = publisher()
  const envelope = issue(signer, installed)
  const result = verifySidecarEnvelope(envelope, installed, trust(signer))
  assert.equal(result.binding.packageSha256, installed.packageSha256)
  assert.equal(result.binding.publisherId, installed.publisherId)
  assert.equal(result.binding.appId, installed.appId)
  assert.equal(result.binding.keySha256, installed.keySha256)
  assert.equal(result.assets.length, 1)
})

test('rejects altered asset bytes after signature and manifest verification', () => {
  const installed = native()
  const signer = publisher()
  const envelope = issue(signer, installed)
  envelope.files['index.html'] = bytes('<main>changed</main>')
  expectCode(() => verifySidecarEnvelope(envelope, installed, trust(signer)), 'ASSET_DIGEST_MISMATCH')
})

test('rejects a changed manifest, signature, or unlisted extra file', () => {
  const installed = native()
  const signer = publisher()
  const changedManifest = issue(signer, installed)
  changedManifest.manifest.name = 'replacement-ui'
  expectCode(() => verifySidecarEnvelope(changedManifest, installed, trust(signer)), 'SIGNATURE_INVALID')
  const changedSignature = issue(signer, installed)
  changedSignature.signature.value = Buffer.alloc(64).toString('base64')
  expectCode(() => verifySidecarEnvelope(changedSignature, installed, trust(signer)), 'SIGNATURE_INVALID')
  const extraFile = issue(signer, installed)
  extraFile.files['extra.js'] = bytes('unlisted')
  expectCode(() => verifySidecarEnvelope(extraFile, installed, trust(signer)), 'ASSET_SET_MISMATCH')
})

test('rejects absolute, traversal, encoded, and backslash asset paths', () => {
  const installed = native()
  const signer = publisher()
  for (const path of ['/index.html', '../index.html', 'assets/%2e%2e/secret', 'assets\\secret']) {
    const files = { [path]: bytes('x') }
    const envelope = issue(signer, installed, {}, files)
    expectCode(() => verifySidecarEnvelope(envelope, installed, trust(signer)), 'ASSET_PATH_INVALID')
  }
})

test('rejects duplicate asset paths and duplicate sidecar names for one native identity', () => {
  const installed = native()
  const signer = publisher()
  const duplicateAssets = issue(signer, installed)
  duplicateAssets.manifest.assets.push({ ...duplicateAssets.manifest.assets[0] })
  duplicateAssets.signature.value = sign(null, Buffer.from(canonicalize(duplicateAssets.manifest)), signer.privateKey).toString('base64')
  expectCode(() => verifySidecarEnvelope(duplicateAssets, installed, trust(signer)), 'ASSET_PATH_DUPLICATE')
  const host = new SidecarMockHost(trust(signer))
  host.installNative(installed)
  const envelope = issue(signer, installed)
  host.installSidecar(envelope)
  expectCode(() => host.installSidecar(envelope), 'SIDECAR_NAME_DUPLICATE')
})

test('rejects a valid sidecar signature that is bound to a different native package', () => {
  const installed = native()
  const other = native({ appId: 'other-app', packageSha256: sha256(bytes('other-native')) })
  const signer = publisher()
  const envelope = issue(signer, other)
  expectCode(() => verifySidecarEnvelope(envelope, installed, trust(signer)), 'NATIVE_BINDING_MISMATCH')
})

test('rejects the old signature domain without interpreting it as a sidecar signature', () => {
  const installed = native()
  const signer = publisher()
  const envelope = issue(signer, installed)
  envelope.signature.domain = 'neapp-v3'
  expectCode(() => verifySidecarEnvelope(envelope, installed, trust(signer)), 'SIGNATURE_DOMAIN_UNSUPPORTED')
})

test('rejects a mathematically valid signature from an unknown UI test root', () => {
  const installed = native()
  const signer = publisher()
  const unknown = publisher()
  const envelope = issue(unknown, installed)
  expectCode(() => verifySidecarEnvelope(envelope, installed, trust(signer)), 'SIGNING_KEY_UNTRUSTED')
})

test('upgrade makes a prior package binding unmountable until a newly bound sidecar is installed', () => {
  const signer = publisher()
  const installed = native()
  const host = new SidecarMockHost(trust(signer))
  host.installNative(installed)
  host.installSidecar(issue(signer, installed))
  assert.equal(host.openSidecar(installed).state, 'mounted')
  const upgraded = native({ packageSha256: sha256(bytes('native-package-v2')) })
  host.upgradeNative(upgraded)
  expectCode(() => host.openSidecar(upgraded), 'SIDECAR_BINDING_STALE')
})

test('uninstall removes the native identity and prevents a dangling sidecar from mounting', () => {
  const signer = publisher()
  const installed = native()
  const host = new SidecarMockHost(trust(signer))
  host.installNative(installed)
  host.installSidecar(issue(signer, installed))
  host.uninstallNative(installed)
  expectCode(() => host.openSidecar(installed), 'NATIVE_NOT_INSTALLED')
})
