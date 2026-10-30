import { createHash } from 'node:crypto'
import { realpathSync } from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'

export const fixtureRoot = path.dirname(fileURLToPath(import.meta.url))
export const repoRoot = path.resolve(fixtureRoot, '../..')
export const tempName = createHash('sha256').update(realpathSync(fixtureRoot)).digest('hex').slice(0, 12)
export const tempRoot = path.join(os.tmpdir(), `ne301-counting-sidecar-${tempName}`)
export const sourceRoot = path.join(tempRoot, 'source')
export const distRoot = path.join(tempRoot, 'dist')
export const publicRoot = path.join(tempRoot, 'public')
export const videoPath = path.join(tempRoot, 'line-counting-h264.h264')
export const sourceCommit = 'de25a6f1f431a631a37ff1cda87f616e293e8836'
export const issue80Commit = 'f819e1e9'
