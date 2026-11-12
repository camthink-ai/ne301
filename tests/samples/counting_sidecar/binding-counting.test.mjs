import assert from 'node:assert/strict'
import { createHash, generateKeyPairSync, sign } from 'node:crypto'
import test from 'node:test'
import { SidecarMockHost, canonicalize, sidecarProbeDomain, verifySidecarEnvelope } from './binding.mjs'

const sha256 = value => createHash('sha256').update(value).digest('hex')
const keyHash = publicKey => sha256(publicKey.export({ format: 'der', type: 'spki' }))
const native = () => ({
  trusted: true,
  publisherId: 'fixture-publisher',
  appId: 'line-counting',
  packageSha256: sha256(Buffer.from('line-counting-native-package-probe')),
  keySha256: sha256(Buffer.from('line-counting-native-key-probe'))
})
const signer = () => {
  const pair = generateKeyPairSync('ed25519')
  return { ...pair, keySha256: keyHash(pair.publicKey) }
}
const envelope = (key, identity) => {
  const data = Buffer.from('<main data-testid="lc-page-realtime">counting</main>')
  const manifest = {
    format: 'sidecar-probe-v1',
    sidecarId: 'line-counting-ui',
    name: 'line-counting-ui',
    binding: {
      packageSha256: identity.packageSha256,
      publisherId: identity.publisherId,
      appId: identity.appId,
      keySha256: identity.keySha256
    },
    assets: [{ path: 'index.html', size: data.length, sha256: sha256(data) }]
  }
  return {
    manifest,
    files: { 'index.html': data },
    signature: {
      domain: sidecarProbeDomain,
      algorithm: 'ed25519',
      keySha256: key.keySha256,
      value: sign(null, Buffer.from(canonicalize(manifest)), key.privateKey).toString('base64')
    }
  }
}

test('line-counting identity tuple is accepted by the reused test-only binding model', () => {
  const identity = native()
  const key = signer()
  const result = verifySidecarEnvelope(envelope(key, identity), identity, new Map([[key.keySha256, key.publicKey]]))
  assert.equal(result.binding.appId, 'line-counting')
  assert.equal(result.sidecarId, 'line-counting-ui')
})

test('line-counting binding rejects package changes and the frozen neapp-v3 domain', () => {
  const identity = native()
  const key = signer()
  const host = new SidecarMockHost(new Map([[key.keySha256, key.publicKey]]))
  const signed = envelope(key, identity)
  host.installNative(identity)
  host.installSidecar(signed)
  const changed = { ...identity, packageSha256: sha256(Buffer.from('other-package')) }
  assert.throws(() => host.openSidecar(changed), error => error.code === 'NATIVE_PACKAGE_STALE')
  const oldDomain = envelope(key, identity)
  oldDomain.signature.domain = 'neapp-v3'
  assert.throws(() => verifySidecarEnvelope(oldDomain, identity, new Map([[key.keySha256, key.publicKey]])), error => error.code === 'SIGNATURE_DOMAIN_UNSUPPORTED')
})
