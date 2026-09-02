const fs = require('fs');
const path = require('path');
const file = path.join(__dirname, '..', 'network', 'web_pages.h');
const src = fs.readFileSync(file, 'utf8');
const m = src.match(/DASHBOARD_HTML = R"\(([\s\S]*?)\n\)";/);
if (!m) { console.error('RAW_STRING_NOT_FOUND'); process.exit(1); }
const html = m[1];
console.log('html_len =', html.length);
if (html.indexOf(')"') >= 0) { console.error('WARN: raw string contains a )" sequence'); }
const s = html.match(/<script>([\s\S]*?)<\/script>/);
if (!s) { console.error('SCRIPT_NOT_FOUND'); process.exit(1); }
const code = s[1];
try { new Function(code); console.log('JS_SYNTAX_OK len =', code.length); }
catch (e) { console.error('JS_SYNTAX_ERROR:', e.message); process.exit(1); }

const ids = ['conn', 'gpioCard', 'ledWrap', 'ledCur', 'pwmPin', 'pwmPeriod', 'pwmDuty',
  'pwmApply', 'pwmStop', 'pwmCur', 'adcTable', 'adcChart', 'adcSrc', 'console', 'logs',
  'hwCard', 'uFilter', 'uAuto', 'uTx', 'uPause', 'uClear', 'uSend', 'uExport', 'lClear',
  'pw', 'wSsid', 'wPass', 'mBroker', 'mPort', 'mUser', 'mPass', 'mKeep', 'mTls',
  'uBaud', 'uData', 'uStop', 'uPar', 'aFsr', 'aD0', 'aD1', 'aD2', 'aD3',
  'wSave', 'mSave', 'uCfgSave', 'aSave', 'pwSave', 'oPw', 'oFile', 'oUp', 'oMsg', 'oReset'];
const missing = ids.filter(id => html.indexOf('id="' + id + '"') < 0);
console.log(missing.length ? 'MISSING_IDS: ' + missing.join(',') : 'ALL_IDS_OK (' + ids.length + ')');

const panes = ['dash', 'uart', 'gpio', 'cfg', 'ota', 'logs'];
const mp = panes.filter(p => html.indexOf('id="' + p + '"') < 0);
console.log(mp.length ? 'MISSING_PANES: ' + mp.join(',') : 'ALL_PANES_OK');

const noColor = !/background\s*[:=]\s*['"]#2a7/i.test(html) && !/style\.background/.test(html);
console.log(noColor ? 'NO_INLINE_COLOR_OK (monochrome)' : 'WARN: inline colour styling found');
