#ifndef NETWORK_WEB_PAGES_H_
#define NETWORK_WEB_PAGES_H_

#pragma once
#include <Arduino.h>

/**
 * @file web_pages.h
 * @brief Single-page dashboard HTML (served by WebServerManager at "/").
 *
 * Vanilla HTML/CSS/JS + WebSocket. No build step, minimal flash footprint.
 * Connects to ws://<host>:81 and renders Hardware / UART / Interface
 * (GPIO + WS2812 + PWM + ADC) / Config / OTA / Logs.
 *
 * LIVE STATE MODEL
 * ----------------
 * The device pushes a `state` snapshot every 400 ms (see
 * AppConfig::STATE_PUSH_INTERVAL_MS) carrying the *actual* hardware state:
 *   { type:"state", ts, gpio:[{pin,state,dir}], led:{mode,mode_str,on,r,g,b},
 *     pwm:{active,pin,period,duty,freq}, adc:[{ch,raw,voltage}] }
 * Every widget on the Interface page is driven from that snapshot, so GPIO
 * levels, the WS2812 live output, the applied PWM parameters and the ADC
 * voltages always mirror the board -- the browser never has to poll.
 *
 * UI convention: monochrome dark theme. The selected WS2812 mode is marked with
 * a neutral outline only (no accent colours).
 */
namespace RHD {
const char* DASHBOARD_HTML = R"(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32-S3 Remote Hardware Debugger</title>
<style>
  :root{ --bg:#1b1b1b; --card:#262626; --txt:#e6e6e6; --mut:#9a9a9a; --bd:#333; --inp:#1f1f1f; }
  *{box-sizing:border-box}
  body{margin:0;background:var(--bg);color:var(--txt);font:14px/1.5 ui-monospace,Menlo,Consolas,monospace}
  header{padding:12px 16px;border-bottom:1px solid var(--bd);display:flex;justify-content:space-between;align-items:center}
  header h1{font-size:15px;margin:0;font-weight:600}
  #conn{font-size:12px;color:var(--mut)}
  nav{display:flex;flex-wrap:wrap;gap:6px;padding:8px 12px;border-bottom:1px solid var(--bd)}
  nav button{background:var(--card);color:var(--txt);border:1px solid var(--bd);padding:6px 12px;cursor:pointer;border-radius:4px}
  nav button.active{outline:1px solid var(--mut)}
  main{padding:14px}
  .pane{display:none}
  .pane.active{display:block}
  .card{background:var(--card);border:1px solid var(--bd);border-radius:6px;padding:12px;margin-bottom:12px}
  .card h3{margin:2px 0 10px;font-size:13px;font-weight:600;color:var(--txt)}
  .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
  label{color:var(--mut);min-width:96px;display:inline-block}
  input,select,textarea{background:var(--inp);color:var(--txt);border:1px solid var(--bd);border-radius:4px;padding:6px 8px;font:inherit}
  input[type=text],input[type=password],input[type=number]{min-width:220px}
  button.act{background:var(--inp);color:var(--txt);border:1px solid var(--bd);padding:6px 12px;border-radius:4px;cursor:pointer}
  button.act.sel{outline:1px solid var(--txt)}
  #console{width:100%;height:320px;background:#151515;color:var(--txt);border:1px solid var(--bd);border-radius:4px;padding:8px;overflow:auto;white-space:pre-wrap;word-break:break-all}
  #logs{width:100%;height:240px;background:#151515;color:var(--txt);border:1px solid var(--bd);border-radius:4px;padding:8px;overflow:auto;white-space:pre-wrap}
  table{border-collapse:collapse;width:100%}
  td,th{border:1px solid var(--bd);padding:6px 8px;text-align:left}
  .kv{color:var(--mut)}
  .pill{color:var(--txt)}
  .mono{font-variant-numeric:tabular-nums}
  canvas{background:#151515;border:1px solid var(--bd);border-radius:4px;max-width:100%}
  .hint{color:var(--mut);font-size:12px}
  .grid2{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:2px 18px}
  #ledWrap{display:flex;gap:6px;flex-wrap:wrap}
</style>
</head>
<body>
<header><h1>ESP32-S3 Remote Hardware Debugger</h1><span id="conn">connecting...</span></header>
<nav>
  <button data-p="dash" class="active">Hardware</button>
  <button data-p="uart">UART</button>
  <button data-p="gpio">Interface</button>
  <button data-p="cfg">Config</button>
  <button data-p="ota">OTA</button>
  <button data-p="logs">Logs</button>
</nav>
<main>
  <section id="dash" class="pane active"><div class="card" id="hwCard"></div></section>

  <section id="uart" class="pane">
    <div class="card">
      <div class="row">
        <button class="act" id="uPause">Pause</button>
        <button class="act" id="uClear">Clear</button>
        <label style="min-width:auto">Filter</label><input type="text" id="uFilter" placeholder="substring">
        <label style="min-width:auto">Hex</label><input type="checkbox" id="uHex">
        <label style="min-width:auto">Auto-scroll</label><input type="checkbox" id="uAuto" checked>
      </div>
      <textarea id="console" readonly></textarea>
      <div class="row">
        <input type="text" id="uTx" placeholder="AT+RST">
        <button class="act" id="uSend">Send</button>
        <button class="act" id="uExport">Export</button>
      </div>
    </div>
  </section>

  <section id="gpio" class="pane">
    <div class="card">
      <h3>GPIO Monitor</h3>
      <div id="gpioCard"></div>
      <div class="hint">SET / CLEAR promotes the pin to output. Levels are pushed by the device (400 ms).</div>
    </div>

    <div class="card">
      <h3>WS2812 Status LED</h3>
      <div class="row">
        <label>WS2812</label>
        <span id="ledWrap"></span>
        <span id="ledCur" class="pill">-</span>
      </div>
      <div class="hint">GPIO48 on-board LED. Mode = selected pattern, state = live output right now.</div>
    </div>

    <div class="card">
      <h3>PWM Output</h3>
      <div class="row">
        <label>Pin</label>
        <select id="pwmPin"></select>
        <input type="number" id="pwmPeriod" value="1000" min="25" step="1" style="min-width:110px">
        <input type="number" id="pwmDuty" value="50" min="0" max="100" step="1" style="min-width:80px">
        <button class="act" id="pwmApply">Apply</button>
        <button class="act" id="pwmStop">Stop</button>
        <span id="pwmCur" class="pill">off</span>
      </div>
      <div class="hint">Period in microseconds, duty in percent. Applied values are echoed back by the device.</div>
    </div>

    <div class="card">
      <h3>ADC Read <span class="hint" id="adcSrc"></span></h3>
      <table id="adcTable">
        <tr><th>CH</th><th>Voltage</th><th>Raw</th></tr>
      </table>
      <canvas id="adcChart" width="600" height="160"></canvas>
      <div class="hint">Sampled continuously by the device and pushed over WebSocket.</div>
    </div>
  </section>

  <section id="cfg" class="pane">
    <div class="card">
      <div class="row"><label>Web password</label><input type="password" id="pw" placeholder="admin"></div>
      <h3>WiFi (STA)</h3>
      <div class="row"><label>SSID</label><input type="text" id="wSsid"></div>
      <div class="row"><label>Password</label><input type="password" id="wPass"></div>
      <div class="row"><button class="act" id="wSave">Save WiFi</button><span class="hint">empty SSID -&gt; AP mode</span></div>
      <h3>MQTT</h3>
      <div class="row"><label>Broker</label><input type="text" id="mBroker"></div>
      <div class="row"><label>Port</label><input type="number" id="mPort"></div>
      <div class="row"><label>User</label><input type="text" id="mUser"></div>
      <div class="row"><label>Password</label><input type="password" id="mPass"></div>
      <div class="row"><label>KeepAlive(s)</label><input type="number" id="mKeep"></div>
      <div class="row"><label>TLS</label><input type="checkbox" id="mTls"></div>
      <div class="row"><button class="act" id="mSave">Save MQTT</button></div>
      <h3>UART</h3>
      <div class="row"><label>Baud</label><input type="number" id="uBaud"></div>
      <div class="row"><label>Data/Stop/Parity</label>
        <input type="number" id="uData" style="min-width:60px">
        <input type="number" id="uStop" style="min-width:60px">
        <input type="number" id="uPar" style="min-width:60px"></div>
      <div class="row"><button class="act" id="uCfgSave">Save UART</button></div>
      <h3>ADC</h3>
      <div class="row"><label>FSR(V)</label><input type="number" step="0.001" id="aFsr"></div>
      <div class="row"><label>Divider 0..3</label>
        <input type="number" step="0.001" id="aD0" style="min-width:60px">
        <input type="number" step="0.001" id="aD1" style="min-width:60px">
        <input type="number" step="0.001" id="aD2" style="min-width:60px">
        <input type="number" step="0.001" id="aD3" style="min-width:60px"></div>
      <div class="row"><button class="act" id="aSave">Save ADC</button></div>
      <div class="row"><button class="act" id="pwSave">Change Web Password</button></div>
    </div>
  </section>

  <section id="ota" class="pane">
    <div class="card">
      <div class="row"><label>Web password</label><input type="password" id="oPw"></div>
      <div class="row"><input type="file" id="oFile"><button class="act" id="oUp">Upload</button></div>
      <div class="row"><span id="oMsg" class="hint"></span></div>
      <div class="row"><button class="act" id="oReset">Reboot Device</button></div>
    </div>
  </section>

  <section id="logs" class="pane"><div class="card"><textarea id="logs" readonly></textarea>
    <div class="row"><button class="act" id="lClear">Clear</button></div></div></section>
</main>

<script>
var ws = null;
var paused = false;
var hist = [];
var LED_MODES = [['off','OFF'],['r','R BLINK'],['g','G BLINK'],['b','B BLINK'],['cycle','RGB CYCLE']];
var LED_NAMES = {off:'OFF', r:'R BLINK', g:'G BLINK', b:'B BLINK', cycle:'RGB CYCLE'};
var PWM_PINS = [3,10,11,12,13,14,15,16,21,33,34,35,36,37,38,39,40,41,42,43,44,47];

function $(id){ return document.getElementById(id); }

function connect(){
  var proto = (location.protocol === 'https:') ? 'wss' : 'ws';
  ws = new WebSocket(proto + '://' + location.hostname + ':81/');
  ws.onopen = function(){ $('conn').textContent = 'ws connected'; };
  ws.onclose = function(){ $('conn').textContent = 'ws disconnected - retrying'; setTimeout(connect, 3000); };
  ws.onerror = function(){ $('conn').textContent = 'ws error'; };
  ws.onmessage = function(e){
    var m; try { m = JSON.parse(e.data); } catch (err) { return; }
    onMsg(m);
  };
}
function send(obj){ if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj)); }

function onMsg(m){
  if (m.type === 'uart' || m.type === 'uart_tx') {
    pushUart('[' + (m.timestamp / 1000).toFixed(3) + '] ' + (m.encoding === 'hex' ? '[hex] ' : '') + m.data);
  } else if (m.type === 'adc') {
    updateAdc(m.channel, m.voltage, m.raw);
  } else if (m.type === 'gpio') {
    updateGpio(m.gpio, m.state, null);
  } else if (m.type === 'led') {
    updateLed(m);
  } else if (m.type === 'pwm') {
    updatePwm(m);
  } else if (m.type === 'state') {
    applyState(m);
  } else if (m.type === 'log') {
    $('logs').value += m.line + '\n';
    $('logs').scrollTop = $('logs').scrollHeight;
  } else if (m.type === 'system') {
    renderHardware(m);
  }
}

function applyState(m){
  if (m.gpio) for (var i = 0; i < m.gpio.length; i++) updateGpio(m.gpio[i].pin, m.gpio[i].state, m.gpio[i].dir);
  if (m.led)  updateLed(m.led);
  if (m.pwm)  updatePwm(m.pwm);
  if (m.adc)  for (var j = 0; j < m.adc.length; j++) updateAdc(m.adc[j].ch, m.adc[j].voltage, m.adc[j].raw);
  if (m.adc_src) $('adcSrc').textContent = '[' + m.adc_src + ']';
}

function updateGpio(pin, st, dir){
  var el = $('g' + pin);
  if (el) el.textContent = st ? 'HIGH' : 'LOW';
  if (dir !== null && dir !== undefined) {
    var d = $('gd' + pin);
    if (d) d.textContent = dir ? 'OUT' : 'IN';
  }
}
function buildGpioRows(list){
  var c = $('gpioCard');
  c.innerHTML = '';
  for (var i = 0; i < list.length; i++){
    var g = list[i];
    var row = document.createElement('div');
    row.className = 'row';
    row.innerHTML = '<label>GPIO' + g.pin + '</label>'
      + '<span id="g' + g.pin + '" class="pill mono">' + (g.state ? 'HIGH' : 'LOW') + '</span>'
      + '<span id="gd' + g.pin + '" class="hint">' + (g.dir ? 'OUT' : 'IN') + '</span>';
    var set = document.createElement('button');
    set.className = 'act'; set.textContent = 'SET';
    set.onclick = (function(p){ return function(){ send({cmd:'gpio_set', gpio:p, value:1}); }; })(g.pin);
    var clr = document.createElement('button');
    clr.className = 'act'; clr.textContent = 'CLEAR';
    clr.onclick = (function(p){ return function(){ send({cmd:'gpio_set', gpio:p, value:0}); }; })(g.pin);
    row.appendChild(set);
    row.appendChild(clr);
    c.appendChild(row);
  }
}

function buildLed(){
  var w = $('ledWrap');
  for (var i = 0; i < LED_MODES.length; i++){
    var key = LED_MODES[i][0];
    var b = document.createElement('button');
    b.className = 'act';
    b.id = 'led_' + key;
    b.textContent = LED_MODES[i][1];
    b.onclick = (function(k){ return function(){ send({cmd:'ws2812_set', mode:k}); }; })(key);
    w.appendChild(b);
  }
}
function updateLed(l){
  var key = (typeof l.mode_str === 'string') ? l.mode_str : (['off','r','g','b','cycle'][l.mode] || 'off');
  for (var i = 0; i < LED_MODES.length; i++){
    var b = $('led_' + LED_MODES[i][0]);
    if (b) b.className = (LED_MODES[i][0] === key) ? 'act sel' : 'act';
  }
  var live = l.on ? ('ON rgb(' + (l.r | 0) + ',' + (l.g | 0) + ',' + (l.b | 0) + ')') : 'OFF';
  $('ledCur').textContent = (LED_NAMES[key] || key) + ' | ' + live;
}

function buildPwm(){
  var sel = $('pwmPin');
  for (var i = 0; i < PWM_PINS.length; i++){
    var o = document.createElement('option');
    o.value = PWM_PINS[i];
    o.textContent = 'GPIO' + PWM_PINS[i];
    sel.appendChild(o);
  }
  $('pwmApply').onclick = function(){
    send({cmd:'pwm_set', pin: +$('pwmPin').value, period: +$('pwmPeriod').value, duty: +$('pwmDuty').value});
  };
  $('pwmStop').onclick = function(){ send({cmd:'pwm_set', active:false}); };
}
function fmtFreq(hz){
  if (!hz) return '-';
  if (hz >= 1000) return (hz / 1000).toFixed(hz >= 10000 ? 0 : 2) + ' kHz';
  return hz + ' Hz';
}
function updatePwm(p){
  if (p.active) {
    $('pwmCur').textContent = 'GPIO' + p.pin + ' ' + p.period + 'us ' + p.duty + '% (' + fmtFreq(p.freq) + ')';
    if (document.activeElement !== $('pwmPin'))    $('pwmPin').value = p.pin;
    if (document.activeElement !== $('pwmPeriod')) $('pwmPeriod').value = p.period;
    if (document.activeElement !== $('pwmDuty'))   $('pwmDuty').value = p.duty;
  } else {
    $('pwmCur').textContent = 'off';
  }
}

var adcHist = [[],[],[],[]];
function buildAdc(){
  var t = $('adcTable');
  for (var ch = 0; ch < 4; ch++){
    var r = t.insertRow();
    r.insertCell().textContent = 'CH' + ch;
    var v = r.insertCell(); v.id = 'adcV' + ch; v.className = 'mono'; v.textContent = '-';
    var x = r.insertCell(); x.id = 'adcR' + ch; x.className = 'mono'; x.textContent = '-';
  }
}
function updateAdc(ch, v, raw){
  var cv = $('adcV' + ch), cr = $('adcR' + ch);
  if (cv) cv.textContent = (typeof v === 'number' ? v.toFixed(3) : '-') + ' V';
  if (cr) cr.textContent = (raw === undefined || raw === null) ? '-' : raw;
  if (typeof v !== 'number') return;
  var h = adcHist[ch];
  h.push(v);
  if (h.length > 100) h.shift();
  drawChart();
}
function drawChart(){
  var cvs = $('adcChart'), ctx = cvs.getContext('2d');
  ctx.clearRect(0, 0, cvs.width, cvs.height);
  var peak = 1.0;
  for (var c = 0; c < 4; c++){
    for (var i = 0; i < adcHist[c].length; i++) if (adcHist[c][i] > peak) peak = adcHist[c][i];
  }
  var scale = peak * 1.15;
  ctx.strokeStyle = '#9a9a9a';
  ctx.lineWidth = 1;
  for (var c2 = 0; c2 < 4; c2++){
    var data = adcHist[c2];
    if (!data.length) continue;
    ctx.beginPath();
    for (var k = 0; k < data.length; k++){
      var x = k / 99 * cvs.width;
      var y = cvs.height - (data[k] / scale) * cvs.height;
      if (k) ctx.lineTo(x, y); else ctx.moveTo(x, y);
    }
    ctx.stroke();
  }
  ctx.fillStyle = '#9a9a9a';
  ctx.fillText('peak ' + peak.toFixed(2) + ' V', 6, 14);
}

function pushUart(line){
  var f = $('uFilter').value;
  if (f && line.indexOf(f) < 0) return;
  if (paused) return;
  hist.push(line);
  if (hist.length > 4000) hist.shift();
  var c = $('console');
  c.value += line + '\n';
  if ($('uAuto').checked) c.scrollTop = c.scrollHeight;
  if (c.value.length > 200000) c.value = c.value.slice(-150000);
}

function kv(k, v){ return '<div class="kv">' + k + '</div><div class="pill">' + v + '</div>'; }
function stat(b){ return b ? 'ready' : 'down'; }
function renderHardware(m){
  var parts = [];
  parts.push(kv('Chip', m.chip || '-'));
  parts.push(kv('Clock', (m.cpu_mhz || 0) + ' MHz'));
  parts.push(kv('Web', 'http://' + (m.ip || '-') + ':' + (m.ws_port || 80)));
  parts.push(kv('MQTT', (m.mqtt_broker || '-') + ':' + (m.mqtt_port || 1883) + ' (' + (m.mqtt || '-') + ')'));
  parts.push(kv('UART', stat(m.uart_ready) + ' (GPIO17/18)'));
  parts.push(kv('ADC', stat(m.adc_ready) + ' [' + (m.adc_src || '-') + ']'));
  parts.push(kv('GPIO', stat(m.gpio_ready) + ' (GPIO4..7)'));
  parts.push(kv('LED', stat(m.led_ready) + ' (GPIO48)'));
  parts.push(kv('PWM', m.pwm_active ? ('GPIO' + m.pwm_pin + ' ' + m.pwm_period + 'us ' + m.pwm_duty + '%') : 'off'));
  parts.push(kv('Device', m.device || '-'));
  parts.push(kv('Firmware', m.firmware || '-'));
  parts.push(kv('Uptime', (m.uptime || 0) + ' s'));
  parts.push(kv('Free Heap', m.free_heap || '-'));
  parts.push(kv('Min Free', m.min_heap || '-'));
  $('hwCard').innerHTML = '<div class="grid2">' + parts.join('') + '</div>';
}

document.querySelectorAll('nav button').forEach(function(b){
  b.onclick = function(){
    document.querySelectorAll('nav button').forEach(function(x){ x.classList.remove('active'); });
    document.querySelectorAll('.pane').forEach(function(x){ x.classList.remove('active'); });
    b.classList.add('active');
    document.getElementById(b.dataset.p).classList.add('active');
  };
});
$('uPause').onclick = function(){ paused = !paused; $('uPause').textContent = paused ? 'Resume' : 'Pause'; };
$('uClear').onclick = function(){ $('console').value = ''; hist = []; };
$('uSend').onclick = function(){ var v = $('uTx').value; if (v) send({cmd:'uart_tx', data:v}); };
$('uExport').onclick = function(){
  var a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([hist.join('\n')], {type:'text/plain'}));
  a.download = 'uart.log';
  a.click();
};
$('lClear').onclick = function(){ $('logs').value = ''; };

function postCfg(url, body){
  fetch(url + '?pw=' + encodeURIComponent($('pw').value),
        {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)})
    .then(function(r){ return r.text(); })
    .then(function(t){ alert(t); })
    .catch(function(e){ alert('err ' + e); });
}
$('wSave').onclick = function(){ postCfg('/api/wifi', {ssid:$('wSsid').value, pass:$('wPass').value}); };
$('mSave').onclick = function(){ postCfg('/api/mqtt', {broker:$('mBroker').value, port:+$('mPort').value, user:$('mUser').value, pass:$('mPass').value, keep:+$('mKeep').value, tls:$('mTls').checked}); };
$('uCfgSave').onclick = function(){ postCfg('/api/uart/config', {baud:+$('uBaud').value, data:+$('uData').value, stop:+$('uStop').value, par:+$('uPar').value}); };
$('aSave').onclick = function(){ postCfg('/api/adc/config', {fsr:+$('aFsr').value, d0:+$('aD0').value, d1:+$('aD1').value, d2:+$('aD2').value, d3:+$('aD3').value}); };
$('pwSave').onclick = function(){ postCfg('/api/webpw', {pw:$('pw').value, newpw:$('pw').value}); };
$('oUp').onclick = function(){
  var f = $('oFile').files[0];
  if (!f) return;
  var fd = new FormData();
  fd.append('firmware', f);
  fetch('/update?pw=' + encodeURIComponent($('oPw').value), {method:'POST', body:fd})
    .then(function(r){ return r.text(); })
    .then(function(t){ $('oMsg').textContent = t; });
};
$('oReset').onclick = function(){
  fetch('/api/reset?pw=' + encodeURIComponent($('oPw').value))
    .then(function(r){ return r.text(); })
    .then(function(t){ $('oMsg').textContent = t; });
};

function loadCfg(){
  fetch('/api/status').then(function(r){ return r.json(); }).then(function(m){
    $('wSsid').value = m.wifi_ssid || '';
    $('mBroker').value = m.mqtt_broker || '';
    $('mPort').value = m.mqtt_port || 1883;
    $('mUser').value = m.mqtt_user || '';
    $('mKeep').value = m.mqtt_keep || 30;
    $('mTls').checked = !!m.mqtt_tls;
    $('uBaud').value = m.uart_baud || 115200;
    $('uData').value = m.uart_data || 8;
    $('uStop').value = m.uart_stop || 1;
    $('uPar').value = m.uart_par || 0;
    $('aFsr').value = m.adc_fsr || 6.144;
    renderHardware(m);
  }).catch(function(){});
  fetch('/api/adc/config').then(function(r){ return r.json(); }).then(function(m){
    $('aD0').value = m.divider0;
    $('aD1').value = m.divider1;
    $('aD2').value = m.divider2;
    $('aD3').value = m.divider3;
  }).catch(function(){});
}

buildLed();
buildPwm();
buildAdc();
fetch('/api/gpio').then(function(r){ return r.json(); }).then(function(j){
  buildGpioRows(j.gpios || []);
  if (j.led_state) updateLed(j.led_state);
  else if (typeof j.led_mode === 'string') updateLed({mode_str:j.led_mode, on:0, r:0, g:0, b:0});
  if (j.pwm) updatePwm(j.pwm);
  if (j.adc) for (var i = 0; i < j.adc.length; i++) updateAdc(j.adc[i].ch, j.adc[i].voltage, j.adc[i].raw);
  if (j.adc_src) $('adcSrc').textContent = '[' + j.adc_src + ']';
}).catch(function(){});
loadCfg();
connect();
</script>
</body>
</html>
)";
} // namespace RHD

#endif // NETWORK_WEB_PAGES_H_
