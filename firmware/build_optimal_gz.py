import gzip

# Super clean, compact HTML with all features
html = """<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>NFC Studio</title><style>
*{box-sizing:border-box}body{font:14px sans-serif;background:#0f172a;color:#f8fafc;padding:12px;margin:0 auto;max-width:380px}
.c{background:#1e293b;padding:12px;border-radius:8px;border:1px solid #334155;margin-bottom:10px}
.h{color:#38bdf8;font-weight:700;display:flex;justify-content:space-between;margin-bottom:6px}
.v{font-family:monospace;color:#38bdf8;font-weight:700}
.p{background:#0b1120;padding:8px;border-radius:6px;word-break:break-all;font-size:13px;margin:6px 0}
input{background:#0b1120;border:1px solid #334155;color:#fff;padding:8px;border-radius:6px;width:100%;margin-bottom:8px}
button{background:#38bdf8;color:#0f172a;border:none;padding:8px 12px;border-radius:6px;font-weight:700;cursor:pointer}
#arm{background:#b45309;padding:8px;border-radius:6px;display:none;margin-bottom:8px;text-align:center}
</style></head><body>
<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:10px"><h3 style="margin:0;color:#38bdf8">📡 NFC Studio</h3><span id="st" style="font-size:12px;color:#94a3b8">Ready</span></div>
<div class="c"><div class="h"><span>Tag Inspector</span><span id="typ" class="v"></span></div>
<div>UID: <b id="uid" class="v">--:--:--:--</b></div>
<div class="p">Data: <span id="pay">No tag scanned</span></div>
<a id="lnk" href="#" target="_blank" style="display:none;color:#38bdf8;font-weight:700">Open Link ↗</a></div>
<div class="c"><div class="h"><span>Write to Tag</span></div>
<div id="arm">ARMED: Tap tag now <button onclick="fetch('/api/cancel',{method:'POST'})" style="padding:2px 6px">Cancel</button></div>
<input id="in" value="https://">
<div style="display:flex;gap:6px"><button onclick="arm('url')">Write URL</button><button onclick="arm('text')">Write Text</button><button onclick="arm('wipe')" style="background:#ef4444;color:#fff">Wipe</button></div>
<div id="msg" style="font-size:13px;margin-top:6px;font-weight:700"></div></div>
<div style="font-size:12px;color:#94a3b8;display:flex;justify-content:space-between"><span>RC522: <b id="rv">--</b></span><label><input type="checkbox" id="snd" checked style="width:auto"> Sound</label></div>
<script>
let sid=0,ax;
function bp(f){if(!document.getElementById('snd').checked)return;try{ax=ax||new(window.AudioContext||window.webkitAudioContext)();let o=ax.createOscillator(),g=ax.createGain();o.frequency.value=f;g.gain.setValueAtTime(.1,ax.currentTime);g.gain.exponentialRampToValueAtTime(.001,ax.currentTime+.08);o.connect(g);g.connect(ax.destination);o.start();o.stop(ax.currentTime+.08)}catch(e){}}
async function arm(t){let v=document.getElementById('in').value.trim();if(t!='wipe'&&!v)return alert('Enter text/URL');await fetch('/api/arm',{method:'POST',body:new URLSearchParams({type:t,payload:v})})}
async function poll(){try{let d=await(await fetch('/api/status')).json();document.getElementById('rv').textContent=d.rc522||'OK';document.getElementById('st').textContent=d.card_present?'● Tag on Reader':(d.scan_id>0?'○ Tag Lifted':'Ready');document.getElementById('st').style.color=d.card_present?'#10b981':'#94a3b8';if(d.tag&&d.tag.uid&&d.tag.uid!='--:--:--:--'){document.getElementById('uid').textContent=d.tag.uid;document.getElementById('typ').textContent=d.tag.type;let p=d.tag.payload||'(Empty)';document.getElementById('pay').textContent=p;let l=document.getElementById('lnk');if(p.startsWith('http://')||p.startsWith('https://')){l.style.display='inline';l.href=p}else l.style.display='none'}if(d.scan_id>sid){sid=d.scan_id;bp(d.last_action_status&&d.last_action_status.includes('Success')?1320:880)}document.getElementById('arm').style.display=d.armed?'block':'none';let m=document.getElementById('msg');if(d.last_action_status&&d.last_action_status!='Idle'){m.textContent=d.last_action_status;m.style.color=d.last_action_status.includes('Success')?'#10b981':'#ef4444'}}catch(e){}setTimeout(poll,1200)}poll();
</script></body></html>"""

print("Original length:", len(html))
gz = gzip.compress(html.encode('utf-8'), compresslevel=9)
print("Gzipped length:", len(gz))

hex_bytes = [f"0x{b:02x}" for b in gz]
chunks = [", ".join(hex_bytes[i:i+16]) for i in range(0, len(hex_bytes), 16)]
c_array = "const uint8_t INDEX_HTML_GZ[] PROGMEM = {\n  " + ",\n  ".join(chunks) + "\n};\n"
c_array += f"const size_t INDEX_HTML_GZ_LEN = {len(gz)};\n"

with open('src/html_gz.h', 'w', encoding='utf-8') as f:
    f.write(c_array)

print("Saved src/html_gz.h successfully!")
