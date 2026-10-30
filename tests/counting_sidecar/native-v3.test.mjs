import assert from 'node:assert/strict'
import { createHash, generateKeyPairSync } from 'node:crypto'
import { chmodSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const fixtureRoot = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(fixtureRoot, '../..')
const tempRoot = path.join(tmpdir(), `ne301-v3-probe-${process.pid}`)
const buildRoot = path.join(tempRoot, 'build')
const mbedtls = path.join(repoRoot, 'Custom/Common/Lib/MbedTLS')
const includeArgs = [
  `-I${path.join(repoRoot, 'tests/sign_spike/shim')}`,
  `-I${path.join(repoRoot, 'tests/storage_gate/shim')}`,
  `-I${path.join(repoRoot, 'tests/app_admission')}`,
  `-I${path.join(repoRoot, 'Custom/Services/AppHost')}`,
  `-I${path.join(repoRoot, 'Custom/Services/Web')}`,
  `-I${path.join(repoRoot, 'Custom/Services/Web/api')}`,
  `-I${path.join(repoRoot, 'Custom/Common/Inc')}`,
  `-I${path.join(repoRoot, 'Custom/Common/Utils')}`,
  `-I${path.join(repoRoot, 'Custom/Common/Lib')}`,
  `-I${path.join(repoRoot, 'Custom/Common/Lib/cJSON')}`,
  `-I${path.join(mbedtls, 'include')}`,
  `-I${path.join(mbedtls, 'configs')}`,
  `-I${path.join(repoRoot, 'tests/sign_spike')}`,
  `-I${tempRoot}`
]

const run = (command, args, options = {}) => execFileSync(command, args, { encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'], ...options })
const hex = value => Buffer.from(value).toString('hex')
const toC = bytes => [...bytes].map(value => `0x${value.toString(16).padStart(2, '0')}`).join(', ')

rmSync(tempRoot, { recursive: true, force: true })
mkdirSync(buildRoot, { recursive: true })

try {
  const trusted = generateKeyPairSync('ec', {
    namedCurve: 'prime256v1',
    privateKeyEncoding: { type: 'sec1', format: 'pem' },
    publicKeyEncoding: { type: 'spki', format: 'der' }
  })
  const foreign = generateKeyPairSync('ec', {
    namedCurve: 'prime256v1',
    privateKeyEncoding: { type: 'sec1', format: 'pem' },
    publicKeyEncoding: { type: 'spki', format: 'der' }
  })
  const trustedPrivate = path.join(tempRoot, 'trusted-test-key.pem')
  const foreignPrivate = path.join(tempRoot, 'foreign-test-key.pem')
  writeFileSync(trustedPrivate, trusted.privateKey)
  writeFileSync(foreignPrivate, foreign.privateKey)
  chmodSync(trustedPrivate, 0o600)
  chmodSync(foreignPrivate, 0o600)
  const testAnchorSha = createHash('sha256').update(trusted.publicKey).digest('hex')
  for (const name of ['anchor_keygen.py', 'gen_v3_vectors.py']) {
    const original = readFileSync(path.join(repoRoot, 'tests/app_admission', name), 'utf8')
    const source = name === 'gen_v3_vectors.py'
      ? original.replace(/ANCHOR_SHA256 = "[a-f0-9]{64}"/, `ANCHOR_SHA256 = "${testAnchorSha}"`)
      : original
    writeFileSync(path.join(tempRoot, name), source)
  }
  run('python3', [path.join(tempRoot, 'gen_v3_vectors.py'), '--key', trustedPrivate, '--foreign-key', foreignPrivate, '--output', path.join(tempRoot, 'v3_vectors.h')])
  const stub = [
    '#include "app_trust_anchor.h"',
    `static const uint8_t probe_spki[] = { ${toC(trusted.publicKey)} };`,
    `static const uint8_t probe_sha[] = { ${toC(Buffer.from(testAnchorSha, 'hex'))} };`,
    'static const app_trust_anchor_t probe_anchor = { APP_TRUST_ANCHOR_FORMAT_VERSION, "ne301-exp-pub", 13u, probe_spki, sizeof(probe_spki), probe_sha };',
    'const app_trust_anchor_t *app_trust_anchor_get(void) { return &probe_anchor; }',
    'app_trust_anchor_status_t app_trust_anchor_status(void) { return APP_TRUST_ANCHOR_PROVISIONED; }',
    'const char *app_trust_anchor_status_str(app_trust_anchor_status_t status) { return status == APP_TRUST_ANCHOR_PROVISIONED ? "provisioned" : "unknown"; }'
  ].join('\n')
  const stubPath = path.join(tempRoot, 'app_trust_anchor_test.c')
  writeFileSync(stubPath, stub)
  const commonFlags = ['-std=c11', '-O1', '-g', '-DMBEDTLS_CONFIG_FILE="host_user_config.h"', '-DMBEDTLS_ALLOW_PRIVATE_ACCESS', ...includeArgs]
  const objects = []
  const strictSources = [
    path.join(repoRoot, 'tests/app_admission/test_app_admission_v3.c'),
    path.join(repoRoot, 'Custom/Services/AppHost/app_admission.c'),
    path.join(repoRoot, 'Custom/Common/Utils/generic_math.c'),
    stubPath
  ]
  for (const [index, source] of strictSources.entries()) {
    const object = path.join(buildRoot, `strict-${index}.o`)
    run(process.env.CC || 'cc', [...commonFlags, '-Wall', '-Wextra', '-Werror', '-c', source, '-o', object])
    objects.push(object)
  }
  for (const name of readdirSync(path.join(mbedtls, 'library')).filter(name => name.endsWith('.c'))) {
    const object = path.join(buildRoot, `${name.slice(0, -2)}.o`)
    run(process.env.CC || 'cc', [...commonFlags, '-w', '-c', path.join(mbedtls, 'library', name), '-o', object])
    objects.push(object)
  }
  const executable = path.join(buildRoot, 'test-app-admission-v3')
  run(process.env.CC || 'cc', [...objects, '-o', executable])
  const output = run(executable, [])
  assert.match(output, /total=61 fail=0/)
  const evidenceRoot = path.join(fixtureRoot, 'evidence')
  mkdirSync(evidenceRoot, { recursive: true })
  writeFileSync(path.join(evidenceRoot, 'native-v3-existing-harness.txt'), output)
  process.stdout.write(output)
} finally {
  rmSync(tempRoot, { recursive: true, force: true })
}
