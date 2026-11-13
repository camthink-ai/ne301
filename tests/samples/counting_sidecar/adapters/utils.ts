export const getWebSocketUrl = (path = '/stream', port = 8081) => {
  const fixturePort = new URLSearchParams(window.location.search).get('streamPort')
  return `ws://${window.location.hostname}:${fixturePort || port}${path}`
}
export const sleep = (milliseconds: number) => new Promise(resolve => window.setTimeout(resolve, milliseconds))
