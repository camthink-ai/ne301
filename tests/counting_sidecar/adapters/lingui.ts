import sys from '@source/locales/en/sys.json'
import common from '@source/locales/en/com.json'

const lookup = (root: unknown, path: string) => path.split('.').reduce<unknown>((value, key) => {
  if (!value || typeof value !== 'object') return undefined
  return (value as Record<string, unknown>)[key]
}, root)

export const useLingui = () => ({
  i18n: {
    _(key: string) {
      const value = lookup(key.startsWith('sys.') ? sys : common, key)
      return typeof value === 'string' ? value : key
    }
  }
})
