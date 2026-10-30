import { spawnSync } from 'node:child_process'
import { mkdirSync, writeFileSync } from 'node:fs'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

const fixtureRoot = path.dirname(fileURLToPath(import.meta.url))
const evidenceRoot = path.join(fixtureRoot, 'evidence')
const commands = [
  ['build', ['build-fixture.mjs']],
  ['binding-and-counting-adaptation', ['--test', 'binding.test.mjs', 'binding-counting.test.mjs']],
  ['frozen-native-v3-regression', ['native-v3.test.mjs']],
  ['real-chrome-browser', ['--test', 'browser.test.mjs']]
]
const entries = []
let failed = false

for (const [name, files] of commands) {
  const args = name === 'build' || name === 'frozen-native-v3-regression' ? files : ['--test', ...files.slice(1)]
  const result = spawnSync(process.execPath, args, { cwd: fixtureRoot, encoding: 'utf8', env: { ...process.env, npm_config_offline: 'true' } })
  const output = `${result.stdout || ''}${result.stderr || ''}`
  entries.push(`$ ${process.execPath} ${args.join(' ')}\n${output}\nexit=${result.status ?? 1}`)
  process.stdout.write(`\n[${name}]\n${output}`)
  if (result.status !== 0) {
    failed = true
    break
  }
}

mkdirSync(evidenceRoot, { recursive: true })
writeFileSync(path.join(evidenceRoot, 'test-all.log'), `${entries.join('\n\n')}\n`)
if (failed) process.exitCode = 1
