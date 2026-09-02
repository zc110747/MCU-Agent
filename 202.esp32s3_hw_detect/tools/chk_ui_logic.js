/**
 * Headless smoke test for the dashboard JS in network/web_pages.h.
 *
 * Extracts the <script> block, runs it in a `vm` sandbox with a minimal DOM
 * stub, then injects the device's `state` WebSocket messages and asserts that
 * every Interface widget updates from the pushed state.
 *
 * This validates the four UI requirements without hardware:
 *   1. GPIO level text follows the pushed state (HIGH/LOW + IN/OUT)
 *   2. WS2812 shows mode + live output, with no accent colour
 *   3. PWM shows the applied pin/period/duty/frequency
 *   4. ADC table + chart update from the pushed voltages
 *
 * Run: node tools/chk_ui_logic.js
 */
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

// ---------------------------------------------------------------- extract JS
const header = fs.readFileSync(path.join(__dirname, '..', 'network', 'web_pages.h'), 'utf8');
const raw = header.match(/DASHBOARD_HTML = R"\(([\s\S]*?)\n\)";/);
if (!raw) {
  console.error('FAIL: DASHBOARD_HTML raw string not found');
  process.exit(1);
}
const scriptMatch = raw[1].match(/<script>([\s\S]*?)<\/script>/);
const code = scriptMatch[1];

// --------------------------------------------------------------- DOM stub
class ClassList {
  constructor(el) { this.el = el; this.set = new Set(); }
  add(c) { this.set.add(c); this.sync(); }
  remove(c) { this.set.delete(c); this.sync(); }
  toggle(c, on) {
    const want = (on === undefined) ? !this.set.has(c) : !!on;
    if (want) this.set.add(c); else this.set.delete(c);
    this.sync();
  }
  contains(c) { return this.set.has(c); }
  sync() { this.el.className = Array.from(this.set).join(' '); }
}

class El {
  constructor(tag, id) {
    this.tagName = tag;
    this._id = '';
    this.children = [];
    this.rows = [];
    this.cells = [];
    this.textContent = '';
    this._innerHTML = '';
    this.className = '';
    this.value = '';
    this.checked = false;
    this.style = {};
    this.dataset = {};
    this.classList = new ClassList(this);
    this.onclick = null;
    if (id) this.id = id;          // registers itself in doc._byId
  }

  // Assigning an id registers the element, like a real DOM does.
  get id() { return this._id; }
  set id(v) {
    this._id = v || '';
    if (this._id) doc._byId[this._id] = this;
  }
  get innerHTML() { return this._innerHTML; }
  set innerHTML(v) {
    this._innerHTML = String(v);
    // Register any id="..." children so getElementById() resolves them.
    const re = /<(\w+)([^>]*)\bid="([^"]+)"[^>]*>([^<]*)<\/\1>/g;
    let m;
    while ((m = re.exec(this._innerHTML)) !== null) {
      const child = doc.register(m[3], m[1]);
      child.textContent = m[4];
      const cls = /class="([^"]*)"/.exec(m[2]);
      if (cls) child.className = cls[1];
    }
  }
  appendChild(c) { this.children.push(c); c.parentNode = this; return c; }
  insertRow() { const r = new El('tr'); this.rows.push(r); return r; }
  insertCell() { const c = new El('td'); this.cells.push(c); return c; }
  getContext() {
    return {
      clearRect() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {}, fillText() {},
      set strokeStyle(v) {}, set lineWidth(v) {}, set fillStyle(v) {}
    };
  }
}

const doc = {
  _byId: Object.create(null),
  register(id, tag) {
    if (!this._byId[id]) this._byId[id] = new El(tag || 'div', id);
    return this._byId[id];
  },
  getElementById(id) {
    if (!this._byId[id]) this._byId[id] = new El('div', id);
    return this._byId[id];
  },
  createElement(tag) { return new El(tag); },
  querySelectorAll() { return []; },
  activeElement: null
};

// Pre-register every id declared in the static HTML.
const idRe = /\bid="([^"]+)"/g;
let idm;
while ((idm = idRe.exec(raw[1])) !== null) doc.register(idm[1]);

// ------------------------------------------------------------ network stubs
const RESPONSES = {
  '/api/gpio': {
    gpios: [
      { pin: 4, state: 0, dir: 0 }, { pin: 5, state: 1, dir: 0 },
      { pin: 6, state: 0, dir: 0 }, { pin: 7, state: 0, dir: 0 }
    ],
    led_mode: 'off',
    led_pin: 48,
    led_state: { mode: 0, mode_str: 'off', on: 0, r: 0, g: 0, b: 0 },
    pwm: { active: false, pin: -1, period: 0, duty: 0, freq: 0 },
    adc: [
      { ch: 0, raw: 0, voltage: 0 }, { ch: 1, raw: 0, voltage: 0 },
      { ch: 2, raw: 0, voltage: 0 }, { ch: 3, raw: 0, voltage: 0 }
    ],
    adc_src: 'internal-adc1'
  },
  '/api/status': {
    chip: 'ESP32-S3', cpu_mhz: 240, ip: '192.168.4.1', ws_port: 81,
    mqtt: 'down', mqtt_broker: '192.168.10.1', mqtt_port: 1883,
    uart_ready: 1, adc_ready: 1, adc_src: 'internal-adc1', gpio_ready: 1,
    led_ready: 1, pwm_active: 0, device: 'esp32s3-AABBCCDD', firmware: '1.0.0',
    uptime: 12, free_heap: 200000, min_heap: 190000,
    wifi_ssid: '', mqtt_user: '', mqtt_keep: 30, mqtt_tls: false,
    uart_baud: 115200, uart_data: 8, uart_stop: 1, uart_par: 0, adc_fsr: 6.144
  },
  '/api/adc/config': { fsr: 6.144, divider0: 1, divider1: 1, divider2: 1, divider3: 1 }
};

function fakeFetch(url) {
  const body = RESPONSES[url.split('?')[0]] || {};
  return Promise.resolve({
    json: () => Promise.resolve(body),
    text: () => Promise.resolve(JSON.stringify(body))
  });
}

const sent = [];
class FakeWebSocket {
  constructor(url) { this.url = url; this.readyState = 1; this.onmessage = null; }
  send(s) { sent.push(JSON.parse(s)); }
  close() { this.readyState = 3; }
}

const sandbox = {
  document: doc,
  location: { hostname: '192.168.4.1', protocol: 'http:' },
  WebSocket: FakeWebSocket,
  fetch: fakeFetch,
  URL: { createObjectURL: () => 'blob:stub' },
  Blob: function () {},
  FormData: function () { this.append = () => {}; },
  alert: () => {},
  setTimeout: (fn) => { void fn; return 0; },
  console
};
sandbox.window = sandbox;
sandbox.globalThis = sandbox;

vm.createContext(sandbox);
vm.runInContext(code, sandbox);

// ------------------------------------------------------------------- tests
let pass = 0, fail = 0;
function check(desc, ok, detail) {
  console.log('[%s] %s%s', ok ? 'PASS' : 'FAIL', desc, detail ? '  (' + detail + ')' : '');
  ok ? pass++ : fail++;
}

const flush = () => new Promise((r) => setImmediate(r));

(async () => {
  await flush(); await flush(); await flush();

  const $ = (id) => doc.getElementById(id);

  check('GPIO rows built from /api/gpio', doc.getElementById('gpioCard').children.length === 4,
    'rows=' + doc.getElementById('gpioCard').children.length);
  check('initial GPIO4 shows LOW', $('g4').textContent === 'LOW', $('g4').textContent);
  check('initial GPIO4 direction IN', $('gd4').textContent === 'IN', $('gd4').textContent);
  check('WS2812 buttons built', doc.getElementById('ledWrap').children.length === 5,
    'buttons=' + doc.getElementById('ledWrap').children.length);
  check('PWM pin options built', doc.getElementById('pwmPin').children.length === 22,
    'options=' + doc.getElementById('pwmPin').children.length);
  check('ADC table has 4 rows + header', doc.getElementById('adcTable').rows.length === 4,
    'rows=' + doc.getElementById('adcTable').rows.length);

  // ---- simulate the device pushing a live state snapshot ----
  sandbox.ws.onmessage({
    data: JSON.stringify({
      type: 'state', ts: 1234,
      gpio: [
        { pin: 4, state: 1, dir: 1 }, { pin: 5, state: 0, dir: 0 },
        { pin: 6, state: 1, dir: 0 }, { pin: 7, state: 0, dir: 1 }
      ],
      led: { pin: 48, mode: 4, mode_str: 'cycle', on: 1, r: 0, g: 255, b: 0, ready: 1 },
      pwm: { active: 1, pin: 21, period: 1000, duty: 25, freq: 1000, resolution: 12 },
      adc: [
        { ch: 0, raw: 2048, voltage: 3.3 }, { ch: 1, raw: 1024, voltage: 1.65 },
        { ch: 2, raw: 0, voltage: 0 }, { ch: 3, raw: 4095, voltage: 6.6 }
      ],
      adc_src: 'internal-adc1', adc_ready: 1
    })
  });
  await flush();

  check('GPIO4 -> HIGH after state push', $('g4').textContent === 'HIGH', $('g4').textContent);
  check('GPIO4 direction -> OUT', $('gd4').textContent === 'OUT', $('gd4').textContent);
  check('GPIO7 -> LOW with OUT direction', $('g7').textContent === 'LOW' && $('gd7').textContent === 'OUT',
    $('g7').textContent + '/' + $('gd7').textContent);
  check('GPIO6 -> HIGH from external input', $('g6').textContent === 'HIGH', $('g6').textContent);

  check('WS2812 shows mode + live output',
    $('ledCur').textContent === 'RGB CYCLE | ON rgb(0,255,0)', $('ledCur').textContent);
  check('WS2812 active button uses neutral outline only',
    $('led_cycle').className === 'act sel' && $('led_off').className === 'act',
    'cycle="' + $('led_cycle').className + '" off="' + $('led_off').className + '"');

  check('PWM status shows applied parameters',
    $('pwmCur').textContent === 'GPIO21 1000us 25% (1.00 kHz)', $('pwmCur').textContent);
  check('PWM inputs mirrored back',
    $('pwmPin').value === 21 && $('pwmPeriod').value === 1000 && $('pwmDuty').value === 25,
    'pin=' + $('pwmPin').value + ' period=' + $('pwmPeriod').value + ' duty=' + $('pwmDuty').value);

  check('ADC CH0 voltage rendered', $('adcV0').textContent === '3.300 V', $('adcV0').textContent);
  check('ADC CH0 raw rendered', String($('adcR0').textContent) === '2048', String($('adcR0').textContent));
  check('ADC source shown', $('adcSrc').textContent === '[internal-adc1]', $('adcSrc').textContent);

  // ---- led off + pwm stop ----
  sandbox.ws.onmessage({
    data: JSON.stringify({
      type: 'state', ts: 2233,
      gpio: [{ pin: 4, state: 0, dir: 1 }, { pin: 5, state: 0, dir: 0 },
             { pin: 6, state: 0, dir: 0 }, { pin: 7, state: 0, dir: 1 }],
      led: { pin: 48, mode: 0, mode_str: 'off', on: 0, r: 0, g: 0, b: 0, ready: 1 },
      pwm: { active: 0, pin: -1, period: 0, duty: 0, freq: 0, resolution: 0 },
      adc: [{ ch: 0, raw: 0, voltage: 0 }, { ch: 1, raw: 0, voltage: 0 },
            { ch: 2, raw: 0, voltage: 0 }, { ch: 3, raw: 0, voltage: 0 }],
      adc_src: 'internal-adc1', adc_ready: 1
    })
  });
  await flush();

  check('GPIO4 -> LOW after state push', $('g4').textContent === 'LOW', $('g4').textContent);
  check('WS2812 off state shown', $('ledCur').textContent === 'OFF | OFF', $('ledCur').textContent);
  check('PWM stopped shows off', $('pwmCur').textContent === 'off', $('pwmCur').textContent);

  // ---- commands actually go out over the socket ----
  sent.length = 0;
  const gpioRow = doc.getElementById('gpioCard').children[0];
  gpioRow.children[0].onclick();   // SET
  gpioRow.children[1].onclick();   // CLEAR
  doc.getElementById('led_g').onclick();
  doc.getElementById('pwmApply').onclick();
  doc.getElementById('pwmStop').onclick();
  const cmds = sent.map((s) => s.cmd + (s.mode ? ':' + s.mode : '') + (s.value !== undefined ? ':' + s.value : ''));
  check('SET/CLEAR/LED/PWM commands sent over WS',
    cmds.join(',') === 'gpio_set:1,gpio_set:0,ws2812_set:g,pwm_set,pwm_set',
    cmds.join(','));
  check('gpio_set carries the right pin', sent[0].gpio === 4 && sent[1].gpio === 4,
    'gpio=' + sent[0].gpio);

  console.log('='.repeat(56));
  console.log('chk_ui_logic: %d/%d passed, %d failed', pass, pass + fail, fail);
  process.exit(fail === 0 ? 0 : 1);
})();
