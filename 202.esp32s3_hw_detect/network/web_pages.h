#ifndef NETWORK_WEB_PAGES_H_
#define NETWORK_WEB_PAGES_H_

#pragma once
#include <Arduino.h>

/**
 * @file web_pages.h
 * @brief Single-page dashboard HTML (served by WebServerManager at "/").
 *
 * Vanilla HTML/CSS/JS + WebSocket. No build step, minimal flash footprint.
 * Connects to ws://<host>:81 and renders UART / ADC / GPIO / System / Config /
 * OTA / Logs. Dark, low-contrast UI per project convention.
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
  .row{display:flex;gap:8px;flex-wrap:wrap;align-items:center;margin:6px 0}
  label{color:var(--mut);min-width:96px;display:inline-block}
  input,select,textarea{background:var(--inp);color:var(--txt);border:1px solid var(--bd);border-radius:4px;padding:6px 8px;font:inherit}
  input[type=text],input[type=password],input[type=number]{min-width:220px}
  button.act{background:var(--inp);color:var(--txt);border:1px solid var(--bd);padding:6px 12px;border-radius:4px;cursor:pointer}
  #console{width:100%;height:320px;background:#151515;color:var(--txt);border:1px solid var(--bd);border-radius:4px;padding:8px;overflow:auto;white-space:pre-wrap;word-break:break-all}
  #logs{width:100%;height:240px;background:#151515;color:var(--txt);border:1px solid var(--bd);border-radius:4px;padding:8px;overflow:auto;white-space:pre-wrap}
  table{border-collapse:collapse;width:100%}
  td,th{border:1px solid var(--bd);padding:6px 8px;text-align:left}
  .kv{color:var(--mut)}
  .pill{color:var(--txt)}
  canvas{background:#151515;border:1px solid var(--bd);border-radius:4px}
  .hint{color:var(--mut);font-size:12px}
</style>
</head>
<body>
<header><h1>ESP32-S3 Remote Hardware Debugger</h1><span id="conn">connecting...</span></header>
<nav>
  <button data-p="dash" class="active">Dashboard</button>
  <button data-p="uart">UART</button>
  <button data-p="adc">ADC</button>
  <button data-p="gpio">GPIO</button>
  <button data-p="sys">System</button>
  <button data-p="cfg">Config</button>
  <button data-p="ota">OTA</button>
  <button data-p="logs">Logs</button>
</nav>
<main>
  <section id="dash" class="pane active"><div class="card" id="dashCard"></div></section>

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

  <section id="adc" class="pane">
    <div class="card">
      <table id="adcTable"><tr><th>CH</th><th>Voltage</th><th>Raw</th></tr></table>
      <canvas id="adcChart" width="600" height="160"></canvas>
    </div>
  </section>

  <section id="gpio" class="pane"><div class="card" id="gpioCard"></div></section>

  <section id="sys" class="pane"><div class="card" id="sysCard"></div></section>

  <section id="cfg" class="pane">
    <div class="card">
      <div class="row"><label>Web password</label><input type="password" id="pw" placeholder="admin"></div>
      <h3>WiFi (STA)</h3>
      <div class="row"><label>SSID</label><input type="text" id="wSsid"></div>
      <div class="row"><label>Password</label><input type="password" id="wPass"></div>
      <div class="row"><button class="act" id="wSave">Save WiFi</button><span class="hint">empty SSID -> AP mode</span></div>
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
const wsProto = location.protocol === 'https:' ? 'wss' : 'ws';
const ws = new WebSocket(wsProto + '://' + location.hostname + ':81/');
const $ = id => document.getElementById(id);
let paused = false, hist = [];

ws.onopen = () => { $('conn').textContent = 'ws connected'; };
ws.onclose = () => { $('conn').textContent = 'ws disconnected'; setTimeout(()=>location.reload(), 3000); };
ws.onmessage = e => {
  let m; try { m = JSON.parse(e.data); } catch { return; }
  if (m.type === 'uart' || m.type === 'uart_tx') {
    let line = (m.encoding === 'hex' ? '[' + m.encoding + '] ' : '') + m.data;
    pushUart('[' + (m.timestamp/1000).toFixed(3) + '] ' + line);
  } else if (m.type === 'adc') {
    updateAdc(m.channel, m.voltage, m.raw);
  } else if (m.type === 'gpio') {
    updateGpio(m.gpio, m.state);
  } else if (m.type === 'log') {
    $('logs').value += m.line + '\n'; $('logs').scrollTop = $('logs').scrollHeight;
  } else if (m.type === 'system') {
    renderSystem(m);
  }
};

function pushUart(line){
  const f = $('uFilter').value;
  if (f && !line.includes(f)) return;
  if (paused) return;
  hist.push(line); if (hist.length > 4000) hist.shift();
  const c = $('console'); c.value += line + '\n';
  if ($('uAuto').checked) c.scrollTop = c.scrollHeight;
  if (c.value.length > 200000) c.value = c.value.slice(-150000);
}
function updateAdc(ch, v, raw){
  let td = $('adcTable').rows[ch+1];
  if (!td){ td = $('adcTable').insertRow(); td.insertCell().textContent='CH'+ch;
    td.insertCell(); td.insertCell(); }
  td.cells[1].textContent = v.toFixed(3) + ' V';
  td.cells[2].textContent = raw;
  drawChart(ch, v);
}
const adcHist = [[],[],[],[]];
function drawChart(ch, v){
  const h = adcHist[ch]; h.push(v); if (h.length > 100) h.shift();
  const cv = $('adcChart'), ctx = cv.getContext('2d'); ctx.clearRect(0,0,cv.width,cv.height);
  ctx.strokeStyle = '#9a9a9a';
  for (let c=0;c<4;c++){
    const data = adcHist[c]; if (!data.length) continue;
    ctx.beginPath();
    for (let i=0;i<data.length;i++){
      const x = i/(100-1)*cv.width;
      const y = cv.height - (data[i]/6.2)*cv.height;
      i?ctx.lineTo(x,y):ctx.moveTo(x,y);
    }
    ctx.stroke();
  }
}
function updateGpio(pin, st){
  const el = $('g'+pin); if (el) el.textContent = st ? 'HIGH' : 'LOW';
}
function renderSystem(m){
  $('dashCard').innerHTML = sysHtml(m);
  $('sysCard').innerHTML = sysHtml(m);
}
function sysHtml(m){
  return '<div class="kv">Device</div><div class="pill">'+m.device+'</div>'+
    '<div class="kv">Firmware</div><div class="pill">'+m.firmware+'</div>'+
    '<div class="kv">Uptime</div><div class="pill">'+m.uptime+' s</div>'+
    '<div class="kv">Free Heap</div><div class="pill">'+m.free_heap+'</div>'+
    '<div class="kv">Min Free</div><div class="pill">'+m.min_heap+'</div>'+
    '<div class="kv">WiFi</div><div class="pill">'+m.wifi+'</div>'+
    '<div class="kv">RSSI</div><div class="pill">'+m.wifi_rssi+' dBm</div>'+
    '<div class="kv">IP</div><div class="pill">'+m.ip+'</div>'+
    '<div class="kv">MQTT</div><div class="pill">'+m.mqtt+'</div>'+
    '<div class="kv">UART drops</div><div class="pill">'+m.uart_drops+'</div>'+
    '<div class="kv">ADC drops</div><div class="pill">'+m.adc_drops+'</div>';
}

document.querySelectorAll('nav button').forEach(b=>b.onclick=()=>{
  document.querySelectorAll('nav button').forEach(x=>x.classList.remove('active'));
  document.querySelectorAll('.pane').forEach(x=>x.classList.remove('active'));
  b.classList.add('active'); document.getElementById(b.dataset.p).classList.add('active');
});
$('uPause').onclick = () => { paused = !paused; $('uPause').textContent = paused?'Resume':'Pause'; };
$('uClear').onclick = () => { $('console').value=''; hist=[]; };
$('uSend').onclick = () => { const v=$('uTx').value; if(v) ws.send(JSON.stringify({cmd:'uart_tx',data:v})); };
$('uExport').onclick = () => { const a=document.createElement('a'); a.href=URL.createObjectURL(new Blob([hist.join('\n')],{type:'text/plain'})); a.download='uart.log'; a.click(); };

// GPIO controls (rendered on load)
fetch('/api/gpio').then(r=>r.json()).then(j=>{
  const c = $('gpioCard'); c.innerHTML='';
  for (const g of j.gpios){
    const row = document.createElement('div'); row.className='row';
    row.innerHTML = '<label>GPIO'+g.pin+'</label><span id="g'+g.pin+'" class="pill">'+(g.state?'HIGH':'LOW')+'</span>';
    const set = document.createElement('button'); set.className='act'; set.textContent='SET';
    const clr = document.createElement('button'); clr.className='act'; clr.textContent='CLEAR';
    set.onclick=()=>ws.send(JSON.stringify({cmd:'gpio_set',gpio:g.pin,value:1}));
    clr.onclick=()=>ws.send(JSON.stringify({cmd:'gpio_set',gpio:g.pin,value:0}));
    row.appendChild(set); row.appendChild(clr); c.appendChild(row);
  }
});

// config load
function loadCfg(){
  const pw = $('pw').value;
  fetch('/api/status').then(r=>r.json()).then(m=>{
    $('wSsid').value=m.wifi_ssid||''; $('mBroker').value=m.mqtt_broker||'';
    $('mPort').value=m.mqtt_port||1883; $('mUser').value=m.mqtt_user||'';
    $('mKeep').value=m.mqtt_keep||30; $('mTls').checked=!!m.mqtt_tls;
    $('uBaud').value=m.uart_baud||115200; $('uData').value=m.uart_data||8;
    $('uStop').value=m.uart_stop||1; $('uPar').value=m.uart_par||0;
    $('aFsr').value=m.adc_fsr||6.144;
  });
  fetch('/api/adc/config').then(r=>r.json()).then(m=>{
    $('aD0').value=m.divider0; $('aD1').value=m.divider1; $('aD2').value=m.divider2; $('aD3').value=m.divider3;
  });
}
$('wSave').onclick = () => postCfg('/api/wifi', {ssid:$('wSsid').value,pass:$('wPass').value});
$('mSave').onclick = () => postCfg('/api/mqtt', {broker:$('mBroker').value,port:+$('mPort').value,user:$('mUser').value,pass:$('mPass').value,keep:+$('mKeep').value,tls:$('mTls').checked});
$('uCfgSave').onclick = () => postCfg('/api/uart/config', {baud:+$('uBaud').value,data:+$('uData').value,stop:+$('uStop').value,par:+$('uPar').value});
$('aSave').onclick = () => postCfg('/api/adc/config', {fsr:+$('aFsr').value,d0:+$('aD0').value,d1:+$('aD1').value,d2:+$('aD2').value,d3:+$('aD3').value});
$('pwSave').onclick = () => postCfg('/api/webpw', {pw:$('pw').value,newpw:$('pw').value});
function postCfg(url, body){
  const pw = $('pw').value;
  fetch(url+'?pw='+encodeURIComponent(pw),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.text()).then(t=>alert(t)).catch(e=>alert('err '+e));
}
$('oUp').onclick = () => {
  const pw=$('oPw').value, f=$('oFile').files[0]; if(!f) return;
  const fd=new FormData(); fd.append('firmware', f);
  fetch('/update?pw='+encodeURIComponent(pw),{method:'POST',body:fd}).then(r=>r.text()).then(t=>{$('oMsg').textContent=t;});
};
$('oReset').onclick = () => fetch('/api/reset?pw='+encodeURIComponent($('oPw').value)).then(r=>r.text()).then(t=>$('oMsg').textContent=t);
$('lClear').onclick = () => $('logs').value='';
loadCfg();
setInterval(loadCfg, 15000);
</script>
</body>
</html>
)";
} // namespace RHD

#endif // NETWORK_WEB_PAGES_H_
