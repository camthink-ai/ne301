const KNOWN_REASONS = new Set([
  'PARAM',
  'BUSY',
  'BAD_PACKAGE',
  'LEGACY_SIGNED_FORMAT_REJECTED',
  'LEGACY_SIGNED_STATE_PRESERVED',
  'LEGACY_SIGNED_PAIR_STATE_PRESERVED',
  'UNKNOWN_FORMAT_REJECTED',
  'TARGET_INCOMPATIBLE',
  'ABI_INCOMPATIBLE',
  'RESOURCE_LIMIT',
  'STORAGE_STATE_UNKNOWN',
  'STORAGE_FULL',
  'WRITE_FAILED',
  'COMMIT_FAILED',
  'VERSION_CONFLICT',
  'CONTENT_CONFLICT',
  'NATIVE_VERIFY_FAILED',
  'UI_VERIFY_FAILED',
  'DOWNGRADE_REFUSED',
  'PUBLISHER_CONFLICT',
  'IDENTITY_CONFLICT',
  'NOT_FOUND',
  'UNAVAILABLE',
  'NOT_INSTALLED',
  'LOAD_FAILED',
  'RUNTIME_UNAVAILABLE',
  'UNKNOWN',
])

const KNOWN_ENVELOPE_CODES = new Set([
  'INVALID_REQUEST',
  'METHOD_NOT_ALLOWED',
  'INTERNAL_ERROR',
  'SERVICE_UNAVAILABLE',
  'TIMEOUT',
  'TOO_MANY_REQUESTS',
])

export function appReasonKey(reason: string): string {
  const normalized = String(reason || '').toUpperCase()
  if (KNOWN_REASONS.has(normalized)) {
    return `sys.app_management.reasons.${normalized}`
  }
  return 'sys.app_management.reasons.UNKNOWN'
}

export function appEnvelopeKey(code: string): string {
  const normalized = String(code || '').toUpperCase()
  if (KNOWN_ENVELOPE_CODES.has(normalized)) {
    return `sys.app_management.envelope.${normalized}`
  }
  return 'sys.app_management.envelope.GENERIC'
}

export function appFileErrorKey(reason: string | null): string {
  if (reason === 'EMPTY_FILE') {
    return 'sys.app_management.file.empty'
  }
  return 'sys.app_management.file.too_large'
}
