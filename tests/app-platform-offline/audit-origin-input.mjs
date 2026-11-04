import { mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { extractNeui } from '../../tests/sidecar-origin-study/extract.mjs'

const output = mkdtempSync(path.join(tmpdir(), 'ne301-app-platform-offline-'))
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..')

try {
  const report = extractNeui(
    path.join(root, 'tests/sidecar-origin-study/input/line-crossing.neui'),
    path.join(root, 'tests/sidecar-origin-study/input/test-publisher.spki.der'),
    output,
  )
  if (
    report.bundleBytes !== 422963 ||
    report.bundleSha256 !== '3cd01519af881eb114c2831d67b4f0542815c98140e06deeba14e597d947771c' ||
    report.nativeTbsSha256 !== 'd793a455e3ccda715c8ee9ae95a718cf0fc2627c0ff20969f23e7ea9349b349e' ||
    report.uiTbsSha256 !== '59f695608f3caa2a06ffae486e6288b3e3052c277a87c5a8a0a3cfb3a049e7a6' ||
    report.publisherKeySha256 !== '5cd252fb0ce8932436faf8ccd1040981b89ee4ad6b9fe9e2a2b7e71aacb27cd3' ||
    report.publisherId !== 'test-publisher' ||
    report.appId !== 'line-crossing' ||
    report.version !== '1.0.0' ||
    report.hostUiAbi !== 1 ||
    report.fileCount !== 7
  ) {
    throw new Error('verified App #25 artifact metadata differs from the pinned evidence')
  }
  console.log(`PASS #99 offline asset source audit: ${report.bundleSha256} bytes=${report.bundleBytes} files=${report.fileCount} signature=${report.signature}`)
} finally {
  rmSync(output, { recursive: true, force: true })
}
