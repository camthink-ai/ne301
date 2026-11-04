import { build } from 'vite'
import { fileURLToPath } from 'node:url'
import path from 'node:path'
import { mkdirSync, rmSync } from 'node:fs'

const fixtureRoot = path.dirname(fileURLToPath(import.meta.url))
const repoRoot = path.resolve(fixtureRoot, '../..')
const outDir = path.join(fixtureRoot, 'dist-host')

rmSync(outDir, { recursive: true, force: true })
mkdirSync(outDir, { recursive: true })

await build({
  root: path.join(fixtureRoot, 'host'),
  base: './',
  logLevel: 'error',
  resolve: {
    alias: {
      '@': path.join(repoRoot, 'Web', 'src'),
    },
  },
  build: {
    outDir,
    emptyOutDir: true,
    target: 'chrome120',
  },
})

console.log('host fixture built to', outDir)
