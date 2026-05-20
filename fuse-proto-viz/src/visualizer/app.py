import os
import json
import subprocess
import threading
from flask import Flask, render_template_string
from flask_sock import Sock

app = Flask(__name__)
sock = Sock(app)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
FUSE_VIZ_BIN = os.path.join(BASE_DIR, "..", "..", "fuse_viz")

_subscribers = set()
_subscribers_lock = threading.Lock()

def _broadcast(msg: str):
    with _subscribers_lock:
        dead = set()
        for ws in _subscribers:
            try:
                ws.send(msg)
            except Exception:
                dead.add(ws)
        # IMPORTANT: Do NOT use "_subscribers -= dead" here!
        # The -= operator creates a local variable assignment in Python's
        # scope, which causes UnboundLocalError on the first read of
        # _subscribers in the for-loop above.  Use difference_update()
        # which modifies the set in-place without reassignment.
        _subscribers.difference_update(dead)

def _run_interceptor():
    try:
        proc = subprocess.Popen(
            [FUSE_VIZ_BIN],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            cwd=os.path.dirname(FUSE_VIZ_BIN),
        )
        print(f"[*] Started C interceptor (pid={proc.pid})")
        for line in proc.stdout:
            line = line.rstrip()
            if not line:
                continue
            if line.startswith("{"):
                _broadcast(line)
            else:
                print(f"[C-LOG] {line}")
    except FileNotFoundError:
        print(f"[!] fuse_viz binary not found at {FUSE_VIZ_BIN}. "
              "Build it with `make` first, or run from fuse-proto-viz/ dir.")
    except Exception as e:
        print(f"[!] Interceptor error: {e}")

def _start_interceptor_thread():
    t = threading.Thread(target=_run_interceptor, daemon=True)
    t.start()

DASHBOARD_HTML = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"/>
<meta name="viewport" content="width=device-width, initial-scale=1.0"/>
<title>fuse-proto-viz</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link href="https://fonts.googleapis.com/css2?family=JetBrains+Mono:wght@400;600&family=Space+Grotesk:wght@400;500;600&display=swap" rel="stylesheet">
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0}

:root{
  --bg:#0a0c0f;
  --surface:#111318;
  --surface2:#181b22;
  --border:#1e2330;
  --border2:#252a38;
  --text:#e2e8f0;
  --muted:#64748b;
  --dim:#8896aa;
  --accent:#38bdf8;
  --accent-dim:#0ea5e940;
  --green:#4ade80;
  --amber:#fbbf24;
  --coral:#fb7185;
  --purple:#a78bfa;
  --teal:#2dd4bf;
  --mono:'JetBrains Mono',monospace;
  --sans:'Space Grotesk',sans-serif;
}

html,body{height:100%;background:var(--bg);color:var(--text);font-family:var(--sans);font-size:14px;line-height:1.5}

/* Layout */
.shell{display:grid;grid-template-rows:52px 1fr;grid-template-columns:220px 1fr 280px;height:100vh;gap:0}

/* Top bar */
.topbar{grid-column:1/-1;display:flex;align-items:center;gap:16px;padding:0 20px;
  border-bottom:1px solid var(--border);background:var(--surface)}
.logo{font-family:var(--mono);font-weight:600;font-size:13px;letter-spacing:.04em;color:var(--accent)}
.logo span{color:var(--muted)}
.status-dot{width:8px;height:8px;border-radius:50%;background:var(--muted);flex-shrink:0;
  transition:background .3s}
.status-dot.live{background:var(--green);box-shadow:0 0 6px var(--green)}
#status-label{font-size:12px;color:var(--muted);font-family:var(--mono)}
.spacer{flex:1}
.stat-chip{display:flex;flex-direction:column;align-items:flex-end;gap:1px}
.stat-chip .val{font-family:var(--mono);font-size:18px;font-weight:600;color:var(--text);line-height:1}
.stat-chip .lbl{font-size:10px;color:var(--muted);text-transform:uppercase;letter-spacing:.08em}
.divider{width:1px;height:28px;background:var(--border2)}

/* Sidebar */
.sidebar{border-right:1px solid var(--border);background:var(--surface);padding:16px 0;overflow-y:auto}
.sidebar-section{padding:0 16px 16px}
.sidebar-title{font-size:10px;text-transform:uppercase;letter-spacing:.1em;color:var(--muted);
  margin-bottom:10px;font-weight:600}
.opcode-list{display:flex;flex-direction:column;gap:4px}
.opcode-row{display:flex;align-items:center;justify-content:space-between;
  padding:6px 10px;border-radius:6px;cursor:pointer;transition:background .15s;border:1px solid transparent}
.opcode-row:hover{background:var(--surface2);border-color:var(--border2)}
.opcode-row.active{background:var(--accent-dim);border-color:var(--accent)40}
.opcode-name{font-family:var(--mono);font-size:11px;font-weight:600}
.opcode-bar-wrap{width:60px;height:4px;background:var(--border2);border-radius:2px;overflow:hidden}
.opcode-bar{height:100%;border-radius:2px;transition:width .4s}
.opcode-count{font-family:var(--mono);font-size:10px;color:var(--muted);min-width:24px;text-align:right}

.filter-row{display:flex;flex-direction:column;gap:6px}
.filter-label{display:flex;align-items:center;gap:8px;font-size:12px;color:var(--dim);cursor:pointer}
.filter-label input{accent-color:var(--accent)}

/* Main event log */
.main{background:var(--bg);display:flex;flex-direction:column;overflow:hidden}
.log-header{display:flex;align-items:center;gap:10px;padding:12px 16px;
  border-bottom:1px solid var(--border)}
.log-header-title{font-size:11px;text-transform:uppercase;letter-spacing:.1em;color:var(--muted);font-weight:600}
.controls{display:flex;gap:8px;align-items:center;margin-left:auto}
.btn{background:transparent;border:1px solid var(--border2);color:var(--dim);
  font-family:var(--mono);font-size:11px;padding:4px 10px;border-radius:5px;cursor:pointer;
  transition:all .15s}
.btn:hover{border-color:var(--accent);color:var(--accent)}
.btn.danger:hover{border-color:var(--coral);color:var(--coral)}
.search-input{background:var(--surface);border:1px solid var(--border2);color:var(--text);
  font-family:var(--mono);font-size:11px;padding:4px 10px;border-radius:5px;width:160px;outline:none}
.search-input:focus{border-color:var(--accent)}

.log-body{flex:1;overflow-y:auto;font-family:var(--mono);font-size:12px}
.log-body::-webkit-scrollbar{width:6px}
.log-body::-webkit-scrollbar-track{background:transparent}
.log-body::-webkit-scrollbar-thumb{background:var(--border2);border-radius:3px}

.event-row{display:grid;grid-template-columns:90px 110px 100px 1fr;align-items:center;
  gap:0;padding:0 16px;height:32px;border-bottom:1px solid var(--border)10;
  transition:background .1s;cursor:default}
.event-row:hover{background:var(--surface)}
.event-row.new{animation:fadeIn .25s ease}
@keyframes fadeIn{from{opacity:0;transform:translateY(-4px)}to{opacity:1;transform:none}}

.col-ts{color:var(--muted);font-size:10px}
.col-op .badge{display:inline-block;padding:2px 7px;border-radius:4px;
  font-size:10px;font-weight:600;letter-spacing:.02em}
.col-node{color:var(--dim);font-size:11px}
.col-desc{color:var(--text);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}

/* opcode colors */
.op-FUSE_INIT    .badge{background:#a78bfa20;color:#a78bfa;border:1px solid #a78bfa40}
.op-FUSE_GETATTR .badge{background:#38bdf820;color:#38bdf8;border:1px solid #38bdf840}
.op-FUSE_LOOKUP  .badge{background:#4ade8020;color:#4ade80;border:1px solid #4ade8040}
.op-FUSE_OPEN    .badge{background:#fbbf2420;color:#fbbf24;border:1px solid #fbbf2440}
.op-FUSE_READ    .badge{background:#2dd4bf20;color:#2dd4bf;border:1px solid #2dd4bf40}
.op-FUSE_READDIR .badge{background:#34d39920;color:#34d399;border:1px solid #34d39940}
.op-FUSE_WRITE   .badge{background:#fb718520;color:#fb7185;border:1px solid #fb718540}
.op-FUSE_RELEASE .badge{background:#f9731620;color:#f97316;border:1px solid #f9731640}
.op-unknown      .badge{background:#64748b20;color:#94a3b8;border:1px solid #64748b40}

/* Right panel: sequence + detail */
.panel{border-left:1px solid var(--border);background:var(--surface);display:flex;flex-direction:column;overflow:hidden}
.panel-tabs{display:flex;border-bottom:1px solid var(--border)}
.tab{flex:1;padding:10px 0;font-size:11px;text-align:center;cursor:pointer;
  color:var(--muted);text-transform:uppercase;letter-spacing:.08em;font-weight:600;
  transition:all .15s;border-bottom:2px solid transparent}
.tab.active{color:var(--accent);border-bottom-color:var(--accent)}
.tab-content{flex:1;overflow:hidden;display:none}
.tab-content.active{display:flex;flex-direction:column}

/* sequence diagram */
#seq-canvas-wrap{flex:1;overflow-y:auto;padding:16px}
#seq-canvas-wrap::-webkit-scrollbar{width:4px}
#seq-canvas-wrap::-webkit-scrollbar-thumb{background:var(--border2);border-radius:2px}
svg.seq{width:100%;font-family:var(--mono)}

/* detail pane */
.detail-pane{flex:1;padding:16px;overflow-y:auto}
.detail-pane .empty{color:var(--muted);font-size:12px;text-align:center;margin-top:40px}
.detail-card{background:var(--surface2);border:1px solid var(--border2);border-radius:8px;padding:12px;margin-bottom:10px}
.detail-card .title{font-size:10px;text-transform:uppercase;letter-spacing:.1em;color:var(--muted);margin-bottom:8px;font-weight:600}
.detail-field{display:flex;justify-content:space-between;padding:4px 0;border-bottom:1px solid var(--border)50;font-size:12px}
.detail-field:last-child{border:none}
.detail-field .key{color:var(--muted)}
.detail-field .val{color:var(--text);font-family:var(--mono)}

.empty-state{display:flex;flex-direction:column;align-items:center;justify-content:center;
  height:100%;color:var(--muted);font-size:12px;gap:8px}
.empty-state svg{opacity:.2}

/* Rate sparkline */
#sparkline{display:block}

/* pause indicator */
.paused-banner{background:var(--amber)15;border-top:1px solid var(--amber)40;
  padding:4px 16px;font-size:11px;color:var(--amber);font-family:var(--mono);
  display:none;text-align:center;letter-spacing:.04em}
.paused-banner.show{display:block}
</style>
</head>
<body>
<div class="shell">

<!-- Top bar -->
<header class="topbar">
  <div class="logo">fuse<span>-proto-</span>viz</div>
  <div class="status-dot" id="dot"></div>
  <span id="status-label">connecting...</span>
  <div class="spacer"></div>
  <div class="stat-chip"><span class="val" id="total-count">0</span><span class="lbl">events</span></div>
  <div class="divider"></div>
  <div class="stat-chip"><span class="val" id="rate-val">0</span><span class="lbl">ev/s</span></div>
  <div class="divider"></div>
  <canvas id="sparkline" width="80" height="28"></canvas>
</header>

<!-- Sidebar -->
<nav class="sidebar">
  <div class="sidebar-section">
    <div class="sidebar-title">opcodes</div>
    <div class="opcode-list" id="opcode-list"></div>
  </div>
  <div class="sidebar-section" style="border-top:1px solid var(--border);padding-top:16px">
    <div class="sidebar-title">filter</div>
    <div class="filter-row" id="filter-row"></div>
  </div>
</nav>

<!-- Event log -->
<main class="main">
  <div class="log-header">
    <span class="log-header-title">event log</span>
    <div class="controls">
      <input class="search-input" id="search" placeholder="search desc..." oninput="applySearch()"/>
      <button class="btn" id="scroll-btn" onclick="toggleScroll()">pause</button>
      <button class="btn danger" onclick="clearLog()">clear</button>
      <button class="btn" onclick="exportLog()">export</button>
    </div>
  </div>
  <div class="paused-banner" id="paused-banner">PAUSED - scroll up to review, click "resume" to continue</div>
  <div class="log-body" id="log-body">
    <div class="empty-state" id="empty-state">
      <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1">
        <circle cx="12" cy="12" r="10"/><path d="M12 6v6l4 2"/>
      </svg>
      waiting for FUSE events...
    </div>
  </div>
</main>

<!-- Right panel -->
<aside class="panel">
  <div class="panel-tabs">
    <div class="tab active" onclick="switchTab('seq')">sequence</div>
    <div class="tab" onclick="switchTab('detail')">detail</div>
  </div>
  <div class="tab-content active" id="tab-seq">
    <div id="seq-canvas-wrap">
      <svg class="seq" id="seq-svg" viewBox="0 0 260 40" xmlns="http://www.w3.org/2000/svg">
        <text x="130" y="24" text-anchor="middle" font-size="11" fill="#64748b" font-family="JetBrains Mono,monospace">no events yet</text>
      </svg>
    </div>
  </div>
  <div class="tab-content" id="tab-detail">
    <div class="detail-pane" id="detail-pane">
      <p class="empty">click an event row to inspect</p>
    </div>
  </div>
</aside>

</div>

<script>
const OPCODE_MAP={1:'FUSE_LOOKUP',3:'FUSE_GETATTR',14:'FUSE_OPEN',15:'FUSE_READ',
  16:'FUSE_WRITE',18:'FUSE_RELEASE',26:'FUSE_INIT',28:'FUSE_READDIR'};
const COLORS={FUSE_INIT:'#a78bfa',FUSE_GETATTR:'#38bdf8',FUSE_LOOKUP:'#4ade80',
  FUSE_OPEN:'#fbbf24',FUSE_READ:'#2dd4bf',FUSE_READDIR:'#34d399',
  FUSE_WRITE:'#fb7185',FUSE_RELEASE:'#f97316',unknown:'#94a3b8'};

let events=[], opStats={}, filtered=[], autoScroll=true, activeFilter=new Set(), searchVal='';
const MAX_EVENTS=2000, MAX_SEQ=40;
let rateHistory=Array(30).fill(0), lastRateTick=Date.now(), tickCount=0;

/* WebSocket */
const proto=location.protocol==='https:'?'wss':'ws';
const ws=new WebSocket(`${proto}://${location.host}/ws/events`);
ws.onopen=()=>{setStatus(true)};
ws.onclose=()=>{setStatus(false)};
ws.onerror=()=>{setStatus(false)};
ws.onmessage=e=>{
  let ev;
  try{ev=JSON.parse(e.data)}catch{return}
  ingestEvent(ev);
};

function setStatus(live){
  document.getElementById('dot').classList.toggle('live',live);
  document.getElementById('status-label').textContent=live?'live':'disconnected';
}

/* Ingest */
function ingestEvent(ev){
  const name=OPCODE_MAP[ev.op]||'unknown';
  ev._name=name;
  ev._ts_rel=ev.ts;
  if(events.length>0) ev._delta=ev.ts-events[events.length-1].ts;
  else ev._delta=0;
  events.push(ev);
  if(events.length>MAX_EVENTS) events.shift();

  opStats[name]=(opStats[name]||0)+1;
  tickCount++;

  if(passesFilter(ev)){
    filtered.push(ev);
    if(filtered.length>MAX_EVENTS) filtered.shift();
    renderRow(ev);
  }
  updateSidebar();
  updateSeq(ev,name);
  document.getElementById('total-count').textContent=events.length;
}

/* Filter logic */
function passesFilter(ev){
  if(activeFilter.size>0 && !activeFilter.has(ev._name)) return false;
  if(searchVal && !ev.desc.toLowerCase().includes(searchVal)) return false;
  return true;
}
function toggleFilter(name){
  if(activeFilter.has(name)) activeFilter.delete(name);
  else activeFilter.add(name);
  rebuildLog();
}
function applySearch(){
  searchVal=document.getElementById('search').value.toLowerCase();
  rebuildLog();
}
function rebuildLog(){
  filtered=events.filter(passesFilter);
  const body=document.getElementById('log-body');
  body.innerHTML='';
  if(filtered.length===0){
    body.innerHTML='<div class="empty-state" id="empty-state">no matching events</div>';
    return;
  }
  filtered.forEach(ev=>renderRow(ev,false));
  if(autoScroll) body.scrollTop=body.scrollHeight;
}

/* Render row */
function renderRow(ev,animate=true){
  const body=document.getElementById('log-body');
  const old=document.getElementById('empty-state');
  if(old) old.remove();

  const tsMs=(ev.ts/1e6).toFixed(1);
  const name=ev._name;
  const row=document.createElement('div');
  row.className=`event-row op-${name}${animate?' new':''}`;
  row.dataset.idx=events.indexOf(ev);
  row.onclick=()=>showDetail(ev);
  row.innerHTML=`
    <span class="col-ts">${tsMs}ms</span>
    <span class="col-op"><span class="badge">${name}</span></span>
    <span class="col-node">node:${ev.node}</span>
    <span class="col-desc">${escHtml(ev.desc)}</span>`;
  body.appendChild(row);
  if(autoScroll) body.scrollTop=body.scrollHeight;
}

function escHtml(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;')}

/* Scroll control */
function toggleScroll(){
  autoScroll=!autoScroll;
  const btn=document.getElementById('scroll-btn');
  const banner=document.getElementById('paused-banner');
  btn.textContent=autoScroll?'pause':'resume';
  banner.classList.toggle('show',!autoScroll);
  if(autoScroll){const b=document.getElementById('log-body');b.scrollTop=b.scrollHeight}
}
function clearLog(){events=[];filtered=[];opStats={};document.getElementById('log-body').innerHTML='<div class="empty-state" id="empty-state">log cleared</div>';document.getElementById('total-count').textContent='0';updateSidebar();resetSeq()}
function exportLog(){
  const blob=new Blob([events.map(e=>JSON.stringify(e)).join('\n')],{type:'application/json'});
  const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='fuse_events.ndjson';a.click();
}

/* Sidebar */
function updateSidebar(){
  const total=Object.values(opStats).reduce((a,b)=>a+b,0)||1;
  const list=document.getElementById('opcode-list');
  const fr=document.getElementById('filter-row');
  list.innerHTML='';fr.innerHTML='';
  Object.entries(opStats).sort((a,b)=>b[1]-a[1]).forEach(([name,count])=>{
    const pct=Math.round(count/total*100);
    const color=COLORS[name]||COLORS.unknown;
    const active=activeFilter.has(name);
    const row=document.createElement('div');
    row.className=`opcode-row${active?' active':''}`;
    row.onclick=()=>{toggleFilter(name);updateSidebar()};
    row.innerHTML=`<span class="opcode-name" style="color:${color}">${name.replace('FUSE_','')}</span>
      <div class="opcode-bar-wrap"><div class="opcode-bar" style="width:${pct}%;background:${color}40;border:1px solid ${color}60"></div></div>
      <span class="opcode-count">${count}</span>`;
    list.appendChild(row);

    const lbl=document.createElement('label');
    lbl.className='filter-label';
    lbl.innerHTML=`<input type="checkbox" ${active?'checked':''} onchange="toggleFilter('${name}')"> <span style="color:${color}">${name}</span>`;
    fr.appendChild(lbl);
  });
}

/* Detail panel */
function showDetail(ev){
  switchTab('detail');
  const pane=document.getElementById('detail-pane');
  const delta=ev._delta?(ev._delta/1e6).toFixed(3)+'ms after prev':'first event';
  pane.innerHTML=`
    <div class="detail-card">
      <div class="title">event</div>
      <div class="detail-field"><span class="key">opcode name</span><span class="val" style="color:${COLORS[ev._name]||COLORS.unknown}">${ev._name}</span></div>
      <div class="detail-field"><span class="key">opcode num</span><span class="val">${ev.op}</span></div>
      <div class="detail-field"><span class="key">node id</span><span class="val">${ev.node}</span></div>
      <div class="detail-field"><span class="key">timestamp</span><span class="val">${(ev.ts/1e6).toFixed(3)} ms</span></div>
      <div class="detail-field"><span class="key">delta</span><span class="val">${delta}</span></div>
    </div>
    <div class="detail-card">
      <div class="title">description</div>
      <div style="font-family:var(--mono);font-size:11px;color:var(--text);line-height:1.7;white-space:pre-wrap">${escHtml(ev.desc)}</div>
    </div>
    <div class="detail-card">
      <div class="title">raw</div>
      <div style="font-family:var(--mono);font-size:10px;color:var(--muted);line-height:1.7;white-space:pre-wrap">${escHtml(JSON.stringify(ev,null,2))}</div>
    </div>`;
}

/* Tab switcher */
function switchTab(id){
  document.querySelectorAll('.tab').forEach((t,i)=>{t.classList.toggle('active',['seq','detail'][i]===id)});
  document.querySelectorAll('.tab-content').forEach(c=>c.classList.remove('active'));
  document.getElementById('tab-'+id).classList.add('active');
}

/* Sequence diagram */
const SEQ_W=260, ROW_H=24, HEADER_H=36;
const ACTORS=['Kernel','fuse_viz','VFS'];
const ACTOR_X=[30,130,220];

let seqEvents=[];
function resetSeq(){seqEvents=[];drawSeq()}
function updateSeq(ev,name){
  seqEvents.push({name,node:ev.node,desc:ev.desc.substring(0,22)});
  if(seqEvents.length>MAX_SEQ) seqEvents.shift();
  drawSeq();
}
function drawSeq(){
  const svg=document.getElementById('seq-svg');
  const H=HEADER_H+seqEvents.length*ROW_H+20;
  svg.setAttribute('viewBox',`0 0 ${SEQ_W} ${H}`);
  let out='';

  ACTORS.forEach((a,i)=>{
    out+=`<line x1="${ACTOR_X[i]}" y1="${HEADER_H}" x2="${ACTOR_X[i]}" y2="${H-10}" stroke="#1e2330" stroke-width="1"/>`;
    out+=`<rect x="${ACTOR_X[i]-32}" y="6" width="64" height="20" rx="4" fill="#111318" stroke="#252a38" stroke-width="0.5"/>`;
    out+=`<text x="${ACTOR_X[i]}" y="20" text-anchor="middle" font-size="9" fill="#64748b" font-family="JetBrains Mono,monospace" font-weight="600">${a}</text>`;
  });

  seqEvents.forEach((ev,i)=>{
    const y=HEADER_H+i*ROW_H+ROW_H/2;
    const color=COLORS[ev.name]||COLORS.unknown;
    const isSend=ev.name.includes('INIT')||ev.name.includes('ATTR')||ev.name.includes('OPEN');
    const x1=isSend?ACTOR_X[1]:ACTOR_X[0], x2=isSend?ACTOR_X[2]:ACTOR_X[1];
    const dir=x2>x1?1:-1;
    out+=`<line x1="${x1+dir*4}" y1="${y}" x2="${x2-dir*8}" y2="${y}" stroke="${color}" stroke-width="1" opacity="0.7"/>`;
    out+=`<polygon points="${x2-dir*8},${y-3} ${x2-dir*2},${y} ${x2-dir*8},${y+3}" fill="${color}" opacity="0.7"/>`;
    const lx=(x1+x2)/2, ly=y-3;
    out+=`<text x="${lx}" y="${ly}" text-anchor="middle" font-size="8" fill="${color}" font-family="JetBrains Mono,monospace" opacity="0.9">${ev.name.replace('FUSE_','')}</text>`;
  });

  if(seqEvents.length===0){
    out+=`<text x="130" y="24" text-anchor="middle" font-size="11" fill="#64748b" font-family="JetBrains Mono,monospace">no events yet</text>`;
  }
  svg.innerHTML=out;

  const wrap=document.getElementById('seq-canvas-wrap');
  if(autoScroll) wrap.scrollTop=wrap.scrollHeight;
}

/* Sparkline rate */
setInterval(()=>{
  const now=Date.now();
  const dt=(now-lastRateTick)/1000;
  const rate=Math.round(tickCount/dt);
  rateHistory.push(rate);rateHistory.shift();
  tickCount=0;lastRateTick=now;
  document.getElementById('rate-val').textContent=rate;
  drawSparkline();
},1000);

function drawSparkline(){
  const c=document.getElementById('sparkline');
  const ctx=c.getContext('2d');
  ctx.clearRect(0,0,80,28);
  const max=Math.max(...rateHistory,1);
  ctx.beginPath();
  rateHistory.forEach((v,i)=>{
    const x=i*(80/29),y=28-(v/max)*22;
    i===0?ctx.moveTo(x,y):ctx.lineTo(x,y);
  });
  ctx.strokeStyle='#38bdf8';ctx.lineWidth=1.5;ctx.stroke();
  ctx.lineTo(80,28);ctx.lineTo(0,28);
  ctx.fillStyle='#38bdf815';ctx.fill();
}
drawSparkline();
</script>
</body>
</html>"""

@app.get("/")
def index():
    return DASHBOARD_HTML

@sock.route("/ws/events")
def ws_events(ws):
    with _subscribers_lock:
        _subscribers.add(ws)
    try:
        while True:
            ws.receive(timeout=30)
    except Exception:
        pass
    finally:
        with _subscribers_lock:
            _subscribers.discard(ws)

if __name__ == "__main__":
    print("[*] Starting C Interceptor:", FUSE_VIZ_BIN)
    _start_interceptor_thread()
    print("[*] Starting Flask WebSocket Server...")
    app.run(host="0.0.0.0", port=5000, debug=False)
