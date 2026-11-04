const dispatch = (level: string, message: string) => window.dispatchEvent(new CustomEvent('fixture-toast', { detail: { level, message } }))

export const toast = {
  success: (message: string) => dispatch('success', message),
  error: (message: string) => dispatch('error', message)
}
