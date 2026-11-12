import { createHash, createPublicKey, verify } from 'node:crypto'

export const sidecarProbeDomain = 'com.camthink.ne301.sidecar-probe'

export const canonicalize = value => JSON.stringify(order(value))

const order = value => {
  if (Array.isArray(value)) return value.map(order)
  if (value && typeof value === 'object') {
    return Object.fromEntries(Object.keys(value).sort().map(key => [key, order(value[key])]))
  }
  return value
}

const fail = code => {
  const error = new Error(code)
  error.code = code
  throw error
}

const sha256 = value => createHash('sha256').update(value).digest('hex')
const identity = native => `${native.publisherId}\u0000${native.appId}`
const digestPattern = /^[a-f0-9]{64}$/
const namePattern = /^[a-z0-9][a-z0-9._-]{0,63}$/

const validatePath = path => {
  if (typeof path !== 'string' || path.length === 0 || path.length > 240) fail('ASSET_PATH_INVALID')
  if (path.startsWith('/') || path.includes('\\') || path.includes('%') || path.includes(':') || path.includes('\u0000')) fail('ASSET_PATH_INVALID')
  if (!/^[A-Za-z0-9._/-]+$/.test(path)) fail('ASSET_PATH_INVALID')
  const segments = path.split('/')
  if (segments.some(segment => segment === '' || segment === '.' || segment === '..')) fail('ASSET_PATH_INVALID')
}

const validateBinding = (binding, native) => {
  if (!native || native.trusted !== true) fail('NATIVE_IDENTITY_UNTRUSTED')
  const fields = ['packageSha256', 'publisherId', 'appId', 'keySha256']
  if (fields.some(field => typeof native[field] !== 'string' || native[field].length === 0)) fail('NATIVE_IDENTITY_INVALID')
  if (fields.some(field => binding[field] !== native[field])) fail('NATIVE_BINDING_MISMATCH')
}

const verifyAssets = (manifest, files) => {
  if (!Array.isArray(manifest.assets) || manifest.assets.length === 0 || manifest.assets.length > 4096) fail('ASSET_MANIFEST_INVALID')
  if (!files || typeof files !== 'object' || Array.isArray(files)) fail('ASSET_SET_INVALID')
  const names = new Set()
  const foldedNames = new Set()
  const verified = []
  for (const entry of manifest.assets) {
    if (!entry || typeof entry !== 'object') fail('ASSET_MANIFEST_INVALID')
    validatePath(entry.path)
    const folded = entry.path.toLowerCase()
    if (names.has(entry.path) || foldedNames.has(folded)) fail('ASSET_PATH_DUPLICATE')
    names.add(entry.path)
    foldedNames.add(folded)
    if (!Number.isSafeInteger(entry.size) || entry.size < 0 || !digestPattern.test(entry.sha256)) fail('ASSET_MANIFEST_INVALID')
    if (!Object.hasOwn(files, entry.path)) fail('ASSET_MISSING')
    const data = Buffer.isBuffer(files[entry.path]) ? files[entry.path] : Buffer.from(files[entry.path])
    if (data.length !== entry.size) fail('ASSET_SIZE_MISMATCH')
    if (sha256(data) !== entry.sha256) fail('ASSET_DIGEST_MISMATCH')
    verified.push({ path: entry.path, size: data.length, sha256: entry.sha256 })
  }
  const keys = Object.keys(files)
  if (keys.length !== names.size || keys.some(path => !names.has(path))) fail('ASSET_SET_MISMATCH')
  return verified
}

export function verifySidecarEnvelope(envelope, native, trustedKeys) {
  if (!envelope || !envelope.manifest || !envelope.signature) fail('SIDECAR_ENVELOPE_INVALID')
  const { manifest, signature } = envelope
  if (manifest.format !== 'sidecar-probe-v1' || !namePattern.test(manifest.sidecarId) || !namePattern.test(manifest.name)) fail('SIDECAR_MANIFEST_INVALID')
  if (!manifest.binding || typeof manifest.binding !== 'object') fail('NATIVE_BINDING_INVALID')
  validateBinding(manifest.binding, native)
  if (signature.domain !== sidecarProbeDomain || signature.algorithm !== 'ed25519') fail('SIGNATURE_DOMAIN_UNSUPPORTED')
  if (!digestPattern.test(signature.keySha256) || typeof signature.value !== 'string') fail('SIGNATURE_INVALID')
  const publicKey = trustedKeys?.get(signature.keySha256)
  if (!publicKey) fail('SIGNING_KEY_UNTRUSTED')
  const keyObject = publicKey.type === 'public' ? publicKey : createPublicKey(publicKey)
  const publicDer = keyObject.export({ format: 'der', type: 'spki' })
  if (sha256(publicDer) !== signature.keySha256) fail('SIGNING_KEY_ID_MISMATCH')
  const signatureBytes = Buffer.from(signature.value, 'base64')
  if (!verify(null, Buffer.from(canonicalize(manifest)), publicKey, signatureBytes)) fail('SIGNATURE_INVALID')
  const assets = verifyAssets(manifest, envelope.files)
  return {
    state: 'verified',
    sidecarId: manifest.sidecarId,
    name: manifest.name,
    binding: { ...manifest.binding },
    signerKeySha256: signature.keySha256,
    assets
  }
}

export class SidecarMockHost {
  #trustedKeys
  #native = new Map()
  #sidecars = new Map()

  constructor(trustedKeys) {
    this.#trustedKeys = trustedKeys
  }

  installNative(record) {
    if (!record || record.trusted !== true) fail('NATIVE_IDENTITY_UNTRUSTED')
    this.#native.set(identity(record), { ...record })
  }

  installSidecar(envelope) {
    const binding = envelope?.manifest?.binding
    if (!binding) fail('NATIVE_BINDING_INVALID')
    const installed = this.#native.get(identity(binding))
    if (!installed) fail('NATIVE_NOT_INSTALLED')
    const verified = verifySidecarEnvelope(envelope, installed, this.#trustedKeys)
    const key = `${identity(installed)}\u0000${installed.packageSha256}\u0000${verified.name}`
    if (this.#sidecars.has(key)) fail('SIDECAR_NAME_DUPLICATE')
    this.#sidecars.set(key, { verified, state: 'active' })
    return verified
  }

  upgradeNative(nextRecord) {
    if (!nextRecord || nextRecord.trusted !== true || !this.#native.has(identity(nextRecord))) fail('NATIVE_UPGRADE_INVALID')
    this.#native.set(identity(nextRecord), { ...nextRecord })
  }

  uninstallNative(record) {
    this.#native.delete(identity(record))
  }

  openSidecar(record) {
    const current = this.#native.get(identity(record))
    if (!current) fail('NATIVE_NOT_INSTALLED')
    if (current.packageSha256 !== record.packageSha256) fail('NATIVE_PACKAGE_STALE')
    const candidates = [...this.#sidecars.values()].filter(item =>
      item.verified.binding.publisherId === current.publisherId &&
      item.verified.binding.appId === current.appId
    )
    const match = candidates.find(item =>
      item.verified.binding.packageSha256 === current.packageSha256 &&
      item.verified.binding.keySha256 === current.keySha256
    )
    if (match) return { state: 'mounted', sidecarId: match.verified.sidecarId, assets: match.verified.assets }
    if (candidates.length > 0) fail('SIDECAR_BINDING_STALE')
    fail('SIDECAR_NOT_FOUND')
  }
}
