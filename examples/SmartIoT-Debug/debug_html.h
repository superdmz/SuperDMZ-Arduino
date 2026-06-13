#pragma once
// Auto-extracted dashboard HTML.
// Kept in a separate header on purpose: the Arduino .ino sketch prototype
// generator mis-parses large multiline C++11 raw string literals, leaking the
// embedded JavaScript into code context (error: 'function' does not name a type).
// Header files are NOT run through that generator, so the raw string is safe here.
#include <Arduino.h>

const char DEBUG_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>SuperDMZ ESP32 — Debug Console</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,Segoe UI,Roboto,sans-serif;background:#0a0f1e;color:#e5e7eb;padding:1rem;max-width:980px;margin:0 auto}
h1{font-size:1.15rem;color:#60a5fa;margin-bottom:.4rem}
.sub{color:#94a3b8;font-size:.8rem;margin-bottom:1.2rem}
.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:.65rem;margin-bottom:1.1rem}
.card{background:#1e293b;border:1px solid #334155;border-radius:.5rem;padding:.85rem 1rem}
.card h2{font-size:.7rem;color:#94a3b8;text-transform:uppercase;letter-spacing:.07em;font-weight:700;margin-bottom:.55rem;display:flex;justify-content:space-between;align-items:center}
.kv{display:flex;justify-content:space-between;padding:.28rem 0;border-bottom:1px solid #334155;font-size:.78rem}
.kv:last-child{border-bottom:0}
.kv .k{color:#94a3b8}
.kv .v{color:#e5e7eb;font-family:ui-monospace,Consolas,monospace;text-align:right;word-break:break-all;max-width:70%}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:.3rem;vertical-align:middle}
.on{background:#34d399;box-shadow:0 0 8px rgba(52,211,153,.55)}
.off{background:#f87171}
.pill{display:inline-block;font-size:.62rem;font-weight:700;padding:1px 7px;border-radius:9px;letter-spacing:.04em;text-transform:uppercase}
.pill.green{background:rgba(52,211,153,.15);color:#34d399;border:1px solid rgba(52,211,153,.4)}
.pill.red{background:rgba(248,113,113,.15);color:#f87171;border:1px solid rgba(248,113,113,.4)}
.pill.gray{background:rgba(148,163,184,.15);color:#94a3b8;border:1px solid rgba(148,163,184,.4)}
.section{background:#1e293b;border:1px solid #334155;border-radius:.5rem;padding:.85rem 1rem;margin-bottom:1.1rem}
.section h2{font-size:.7rem;color:#94a3b8;text-transform:uppercase;letter-spacing:.07em;font-weight:700;margin-bottom:.55rem;display:flex;justify-content:space-between;align-items:center}
label{display:block;margin-top:.4rem;font-size:.72rem;color:#94a3b8;text-transform:uppercase;letter-spacing:.05em;font-weight:600}
input,select{width:100%;padding:.5rem .6rem;background:#0f172a;border:1px solid #334155;color:#e5e7eb;border-radius:.35rem;font-size:.85rem;box-sizing:border-box;height:38px}
input:focus,select:focus{outline:none;border-color:#60a5fa}
.ssid-row{display:flex;gap:.4rem;align-items:stretch}
.ssid-row select{flex:1}
.ssid-row .btn{margin-top:0;height:38px;flex:0 0 auto;white-space:nowrap}
.token-row{display:flex;gap:.4rem;align-items:stretch}
.token-row input{flex:1}
.token-row .btn{margin-top:0;height:38px;flex:0 0 auto;white-space:nowrap}
.btn{margin-top:.5rem;padding:.55rem .9rem;background:#2563eb;color:#fff;border:0;border-radius:.35rem;font-weight:600;font-size:.82rem;cursor:pointer}
.btn:hover{background:#1d4ed8}
.btn.gray{background:#334155}.btn.gray:hover{background:#475569}
.btn.red{background:#7f1d1d}.btn.red:hover{background:#991b1b}
.btn.green{background:#047857}.btn.green:hover{background:#059669}
.actions{display:flex;gap:.4rem;flex-wrap:wrap;margin-top:.6rem}
.grid2{display:grid;grid-template-columns:1fr 1fr;gap:.5rem}
#log{background:#020617;border:1px solid #1e293b;border-radius:.5rem;padding:.7rem .8rem;
     font-family:ui-monospace,Consolas,monospace;font-size:.72rem;color:#cbd5e1;
     white-space:pre;overflow:auto;max-height:480px;line-height:1.45;
     scrollbar-width:thin;scrollbar-color:#475569 #020617}
.logbar{display:flex;justify-content:space-between;align-items:center;margin-bottom:.4rem;font-size:.78rem;color:#94a3b8}
table.probes{width:100%;border-collapse:collapse;font-size:.78rem}
table.probes td{padding:.35rem .25rem;border-bottom:1px solid #334155}
.ota-bar{display:flex;align-items:center;gap:.5rem;flex-wrap:wrap}
.ota-bar input[type=file]{flex:1;padding:.3rem;background:#0f172a;border:1px solid #334155;color:#e5e7eb;border-radius:.35rem;font-size:.75rem}
.flag-wrap{display:inline-flex;align-items:center;gap:.35rem}
.flag-wrap img{width:18px;height:13px;border-radius:2px;vertical-align:middle}
#ota_prog{width:100%;height:6px;background:#0f172a;border-radius:3px;margin-top:.5rem;overflow:hidden;display:none}
#ota_prog .bar{height:100%;background:#34d399;width:0;transition:width .15s}
#ota_msg{margin-top:.4rem;font-size:.78rem;color:#94a3b8}
</style></head><body>
<h1>SuperDMZ ESP32 — Debug Console</h1>
<p class="sub">AP is intentionally kept ON. Connect to <code>SuperDMZ-Debug-XXXX</code> any time to read live state without disturbing the running tunnel.</p>

<div class="row">
  <div class="card">
    <h2>SuperDMZ <span id="tn_pill" class="pill gray">…</span></h2>
    <div class="kv"><span class="k">State</span><span class="v" id="tn_state">—</span></div>
    <div class="kv"><span class="k">Public URL</span><span class="v" id="tn_url">—</span></div>
    <div class="kv"><span class="k">Node</span><span class="v" id="tn_node">—</span></div>
    <div class="kv"><span class="k">Bytes in/out</span><span class="v" id="tn_b">—</span></div>
  </div>
  <div class="card">
    <h2>WiFi (STA) <span id="wf_pill" class="pill gray">…</span></h2>
    <div class="kv"><span class="k">SSID</span><span class="v" id="wf_ssid">—</span></div>
    <div class="kv"><span class="k">IP</span><span class="v" id="wf_ip">—</span></div>
    <div class="kv"><span class="k">RSSI</span><span class="v" id="wf_rssi">—</span></div>
    <div class="kv"><span class="k">Internet</span><span class="v" id="wf_inet">—</span></div>
    <div class="kv"><span class="k">MAC</span><span class="v" id="wf_mac">—</span></div>
  </div>
  <div class="card">
    <h2>System <span id="sy_pill" class="pill gray">…</span></h2>
    <div class="kv"><span class="k">Chip</span><span class="v" id="sy_chip">—</span></div>
    <div class="kv"><span class="k">Firmware</span><span class="v" id="sy_fw">—</span></div>
    <div class="kv"><span class="k">Free heap</span><span class="v" id="sy_heap">—</span></div>
    <div class="kv"><span class="k">Uptime</span><span class="v" id="sy_up">—</span></div>
    <div class="kv"><span class="k">NTP</span><span class="v" id="sy_ntp">—</span></div>
    <div class="kv"><span class="k">Date</span><span class="v" id="sy_date">—</span></div>
  </div>
</div>

<div class="section">
  <h2>WiFi configuration <span class="pill gray" id="wifi_status">…</span></h2>
  <div class="grid2">
    <div>
      <label>WiFi SSID</label>
      <div class="ssid-row">
        <select id="ssid" required><option value="">Pick a network…</option></select>
        <button type="button" class="btn gray" onclick="scan()">Scan</button>
      </div>
    </div>
    <div>
      <label>WiFi password</label>
      <input id="pass" type="password" autocomplete="off" placeholder="(leave empty for open networks)">
    </div>
  </div>
  <div class="actions">
    <button class="btn green" onclick="saveWifi()">Save WiFi</button>
  </div>
</div>

<div class="section" id="token_section">
  <h2>SuperDMZ token <span class="pill gray" id="token_label">…</span></h2>
  <div class="token-row">
    <input id="token" autocomplete="off" pattern="[a-f0-9]{32,64}" placeholder="paste your hex token here">
    <button type="button" class="btn green" onclick="saveToken()" id="token_btn">Save token</button>
  </div>
  <p style="color:#94a3b8;font-size:.72rem;margin-top:.45rem">Get a token from <a href="https://superdmz.com/login/?page=tunnels" target="_blank" style="color:#60a5fa">superdmz.com / Tunnels</a>. Paste here and click <b>Save</b> to replace the current one without rebooting.</p>
</div>

<div class="section">
  <h2>Network probes <span id="probe_age" class="pill gray">…</span></h2>
  <p style="color:#94a3b8;font-size:.72rem;margin-bottom:.5rem">DNS + TCP reachability to internet (Google DNS, Cloudflare DNS), panel and relay. Auto-runs every 120 s.</p>
  <table class="probes" id="probes"><tbody></tbody></table>
  <div class="actions">
    <button class="btn" type="button" id="probeBtn" onclick="runProbesNow()">Run probes now</button>
  </div>
</div>

<div class="section">
  <h2>OTA firmware update</h2>
  <p style="color:#94a3b8;font-size:.72rem;margin-bottom:.55rem">Pick a compiled <code>.bin</code> file (Sketch → Export compiled binary). Board flashes and reboots automatically.</p>
  <div class="ota-bar">
    <input type="file" id="ota_file" accept=".bin">
    <button class="btn" type="button" onclick="otaUpload()" id="ota_btn">Upload &amp; flash</button>
  </div>
  <div id="ota_prog"><div class="bar"></div></div>
  <div id="ota_msg"></div>
</div>

<div class="section">
  <h2>Device actions</h2>
  <div class="actions">
    <button class="btn red" type="button" onclick="confirmAction('Reboot device?','/api/reboot','POST')">Reboot</button>
    <button class="btn gray" type="button" onclick="confirmAction('Factory reset (wipes WiFi+token)?','/api/factory','POST')">Factory reset</button>
  </div>
</div>

<div class="logbar">
  <span><span class="pill green" id="log_pulse">LIVE</span> <span id="log_count">0</span> lines · auto-refresh 2 s</span>
  <span><a href="/log" style="color:#60a5fa;text-decoration:none" target="_blank">raw /log →</a></span>
</div>
<div id="log">loading…</div>

<script>
function fmt(b){if(b==null||isNaN(b))return '0 B';if(b<1024)return b+' B';if(b<1048576)return (b/1024).toFixed(1)+' KB';if(b<1073741824)return (b/1048576).toFixed(1)+' MB';return (b/1073741824).toFixed(1)+' GB';}
function up(s){var h=Math.floor(s/3600),m=Math.floor((s%3600)/60),x=s%60;return h+'h '+m+'m '+x+'s';}
function setPill(id, cls, txt){var e=document.getElementById(id);if(!e)return;e.className='pill '+cls;e.textContent=txt;}
function confirmAction(msg, url, method){
  if(!confirm(msg))return;
  fetch(url,{method:method}).then(r=>r.text()).then(t=>console.log(t));
}
function flagImg(code){
  if(!code) return '';
  return '<img src="https://flagcdn.com/' + code.toLowerCase() + '.svg" alt="' + code + '">';
}
function scan(){
  fetch('/scan').then(r=>r.json()).then(nets=>{
    var sel=document.getElementById('ssid');sel.innerHTML='<option value="">Pick a network…</option>';
    nets.sort((a,b)=>b.rssi-a.rssi).forEach(n=>{
      var bars=n.rssi>-55?'▂▄▆█':n.rssi>-70?'▂▄▆_':n.rssi>-80?'▂▄__':'▂___';
      var o=document.createElement('option');o.value=n.ssid;
      o.textContent=n.ssid+'  '+bars+'  '+n.rssi+'dBm'+(n.secure?' 🔒':'');
      sel.appendChild(o);
    });
  });
}
function saveWifi(){
  var s = document.getElementById('ssid').value;
  var p = document.getElementById('pass').value;
  if(!s){alert('Pick an SSID first (Scan).');return;}
  var fd = new FormData(); fd.append('ssid', s); fd.append('pass', p);
  fetch('/save/wifi',{method:'POST', body: fd}).then(r=>r.text()).then(_=>{
    alert('WiFi saved. The board will reconnect (AP stays up).');
  });
}
function saveToken(){
  var t = document.getElementById('token').value.trim().toLowerCase();
  if(!/^[a-f0-9]{32,64}$/.test(t)){alert('Token must be 32–64 hex chars.');return;}
  var fd = new FormData(); fd.append('token', t);
  fetch('/save/token',{method:'POST', body: fd}).then(r=>r.text()).then(_=>{
    alert('Token saved. Tunnel will reconnect.');
    // status refreshes on the next poll cycle (kept single-request for the tunnel)
  });
}
function renderProbes(d){
  var tbody = document.querySelector('#probes tbody');
  tbody.innerHTML = '';
  var age = document.getElementById('probe_age');
  if (!d.ran) { tbody.innerHTML='<tr><td style="color:#64748b;padding:.5rem">Probes not run yet.</td></tr>'; setPill('probe_age','gray','not run'); return; }
  if (d.error) { tbody.innerHTML='<tr><td style="color:#f87171;padding:.5rem">'+d.error+'</td></tr>'; setPill('probe_age','red','error'); return; }
  var anyFail = false;
  d.tests.forEach(function(t){
    if(!t.ok) anyFail = true;
    var tr = document.createElement('tr');
    tr.innerHTML = '<td style="font-family:ui-monospace,monospace;color:#cbd5e1">'+t.name+'</td>'
                 + '<td style="text-align:right;color:'+(t.ok?'#34d399':'#f87171')+';font-weight:600">'+(t.ok?'OK':'FAIL')+'</td>'
                 + '<td style="text-align:right;color:#94a3b8;font-family:ui-monospace,monospace;width:60px">'+t.ms+'ms</td>'
                 + '<td style="color:#94a3b8;font-family:ui-monospace,monospace;padding-left:.6rem">'+t.detail+'</td>';
    tbody.appendChild(tr);
  });
  setPill('probe_age', anyFail?'red':'green', anyFail?'fail':'ok');
}
function runProbesNow(){
  var btn = document.getElementById('probeBtn');
  btn.disabled = true; btn.textContent = 'Running…';
  fetch('/api/probe',{method:'POST'}).then(r=>r.json()).then(d=>{
    renderProbes(d); btn.disabled=false; btn.textContent='Run probes now';
  }).catch(e=>{ btn.disabled=false; btn.textContent='Run probes now'; alert('Probe failed: '+e); });
}
function otaUpload(){
  var f = document.getElementById('ota_file').files[0];
  if (!f) { alert('Pick a .bin file first.'); return; }
  if (!confirm('Flash '+f.name+' ('+fmt(f.size)+')? Board will reboot when done.')) return;
  var prog = document.getElementById('ota_prog');
  var bar  = prog.querySelector('.bar');
  var msg  = document.getElementById('ota_msg');
  var btn  = document.getElementById('ota_btn');
  btn.disabled = true; btn.textContent = 'Flashing…';
  prog.style.display = 'block'; bar.style.width = '0%'; msg.textContent = 'Uploading…';
  var fd = new FormData(); fd.append('firmware', f);
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/ota', true);
  xhr.upload.onprogress = function(e){
    if (e.lengthComputable){ bar.style.width = ((e.loaded/e.total)*100).toFixed(1)+'%'; }
  };
  xhr.onload = function(){
    if (xhr.status === 200) {
      msg.textContent = 'Flash OK. Rebooting…'; msg.style.color = '#34d399';
      setTimeout(function(){ location.reload(); }, 8000);
    } else {
      msg.textContent = 'Flash FAILED: ' + xhr.responseText;
      msg.style.color = '#f87171';
      btn.disabled = false; btn.textContent = 'Upload & flash';
    }
  };
  xhr.onerror = function(){
    msg.textContent = 'Upload error.'; msg.style.color = '#f87171';
    btn.disabled = false; btn.textContent = 'Upload & flash';
  };
  xhr.send(fd);
}
function applyStatus(d){
  setPill('tn_pill', d.online?'green':'red', d.online?'online':'offline');
  document.getElementById('tn_state').innerHTML = '<span class="dot '+(d.online?'on':'off')+'"></span>'+(d.online?'ONLINE':'OFFLINE');
  document.getElementById('tn_url').textContent  = d.public_url || '—';
  if (d.node && d.node.country_code) {
    document.getElementById('tn_node').innerHTML =
      '<span class="flag-wrap">' + flagImg(d.node.country_code)
      + ' <b>' + (d.node.country_code||'').toUpperCase() + '</b> · '
      + (d.node.city||'') + '</span>';
  } else {
    document.getElementById('tn_node').textContent = '—';
  }
  document.getElementById('tn_b').textContent    = fmt(d.bytes_in)+' / '+fmt(d.bytes_out);

  var staOk = d.wifi_status === 3;
  setPill('wf_pill', staOk?'green':'red', staOk?'connected':'disconnected');
  setPill('wifi_status', staOk?'green':'red', staOk?'connected':'disconnected');
  document.getElementById('wf_ssid').textContent = d.wifi_ssid || '—';
  document.getElementById('wf_ip').textContent   = d.wifi_ip || '—';
  document.getElementById('wf_rssi').textContent = (d.rssi||0)+' dBm';
  document.getElementById('wf_mac').textContent  = d.mac;
  var inet = '';
  if (d.inet_google) inet += '<span style="color:#34d399">8.8.8.8</span> ';
  else               inet += '<span style="color:#f87171">8.8.8.8</span> ';
  if (d.inet_cf)     inet += '<span style="color:#34d399">1.1.1.1</span>';
  else               inet += '<span style="color:#f87171">1.1.1.1</span>';
  document.getElementById('wf_inet').innerHTML = inet;

  setPill('sy_pill','green','ok');
  document.getElementById('sy_chip').textContent = d.chip;
  document.getElementById('sy_fw').textContent = d.fw_version || '—';
  document.getElementById('sy_heap').textContent = fmt(d.free_heap);
  document.getElementById('sy_up').textContent   = up(d.uptime);
  document.getElementById('sy_ntp').innerHTML = '<span class="pill '+(d.ntp_ok?'green':'red')+'">'+(d.ntp_ok?'ON':'OFF')+'</span>';
  document.getElementById('sy_date').textContent = d.date_utc || '—';

  // Token UI state
  var has = !!d.has_token;
  setPill('token_label', has?'green':'gray', has?'configured':'not set');
  var tokBtn = document.getElementById('token_btn');
  tokBtn.textContent = has ? 'Renew token' : 'Add token';
  if (has && !document.getElementById('token').value) {
    document.getElementById('token').placeholder = d.token_preview + ' · paste new to renew';
  }
}
function applyLog(d){
  var el=document.getElementById('log');
  el.textContent = d.lines.join('\n');
  el.scrollTop = el.scrollHeight;
  document.getElementById('log_count').textContent = d.lines.length;
  var p = document.getElementById('log_pulse');
  p.style.opacity = '.3'; setTimeout(_=>p.style.opacity='1', 250);
}
// IMPORTANT: through the SuperDMZ tunnel every HTTP request opens its own
// loopback socket on the ESP32 (relay -> new_conn -> 127.0.0.1:80). The board's
// WebServer is single-client and lwIP has a tiny socket pool, so firing several
// fetches at once (as a LAN browser tolerates) exhausts sockets and every poll
// fails silently — the dashboard then shows blanks behind the public URL.
// So we keep STRICTLY ONE request in flight at a time, chained sequentially,
// and only schedule the next cycle after the current one finishes.
var _polling = false;
function refresh(){
  if(_polling) return;                       // never overlap requests over the tunnel
  _polling = true;
  var opt = {cache:'no-store'};
  fetch('/api/status',opt).then(r=>r.json()).then(applyStatus).catch(_=>{})
    .then(()=> fetch('/api/probe',opt).then(r=>r.json()).then(renderProbes).catch(_=>{}))
    .then(()=> fetch('/log.json',opt).then(r=>r.json()).then(applyLog).catch(_=>{}))
    .then(()=>{ _polling=false; setTimeout(refresh,2000); })
    .catch(()=>{ _polling=false; setTimeout(refresh,2000); });
}
refresh();
</script></body></html>
)HTML";
