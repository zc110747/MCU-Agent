/* API contract with the STM32F429 firmware (next step).
 *
 *   GET  /api/hardware  -> { mcu, clock, ap3216c:{lux,ps,ir}, mpu9250:{ax,ay,az,gx,gy,gz,mx,my,mz}, led, beep }
 *   GET  /api/network   -> { ip, mask, gw, mac }
 *   POST /api/control   -> body { led?: 0|1, beep?: 0|1 }  -> { ok }
 *   POST /api/network   -> body { ip, mask, gw, mac }      -> { ok }
 *   POST /api/reset     -> body {}                         -> { ok } (reboots)
 *
 * All responses are JSON. Errors are thrown with the HTTP status.
 */

async function request(path, options = {}) {
  const resp = await fetch(path, options)
  if (!resp.ok) {
    let detail = ''
    try {
      const j = await resp.json()
      detail = j.error || ''
    } catch (_) { /* non-JSON body */ }
    throw new Error(`HTTP ${resp.status} ${detail}`.trim())
  }
  return resp.json()
}

export function getHardware() {
  return request('/api/hardware')
}

export function getNetwork() {
  return request('/api/network')
}

export function controlDevice(patch) {
  return request('/api/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(patch)
  })
}

export function setNetwork(params) {
  return request('/api/network', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params)
  })
}

export function resetDevice() {
  return request('/api/reset', { method: 'POST' })
}
