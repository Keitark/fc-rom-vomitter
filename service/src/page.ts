export const visitorPage = `<!doctype html>
<html lang="ja">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ROM Vomitter FC — Expo Queue</title>
  <style>
    :root{color-scheme:dark;--bg:#071018;--card:#101d29;--ink:#eef6ff;--muted:#9db0c2;--blue:#38bdf8;--amber:#fbbf24;--line:#253749}
    *{box-sizing:border-box}body{margin:0;min-height:100vh;background:radial-gradient(circle at 20% 0,#123451 0,#071018 42%);color:var(--ink);font:16px/1.55 system-ui,sans-serif}
    main{width:min(720px,calc(100% - 32px));margin:0 auto;padding:48px 0}header{margin-bottom:22px}.kicker{color:var(--blue);font-weight:800;letter-spacing:.16em;text-transform:uppercase}
    h1{font-size:clamp(2rem,7vw,4.5rem);line-height:.95;margin:.25em 0}.sub{color:var(--muted);max-width:55ch}.card{background:color-mix(in srgb,var(--card) 92%,transparent);border:1px solid var(--line);border-radius:18px;padding:24px;box-shadow:0 24px 70px #0008}
    label{display:block;font-weight:700;margin:12px 0 7px}input[type=file]{width:100%;padding:14px;border:1px dashed #49647d;border-radius:12px;background:#09141f}.check{display:flex;gap:10px;align-items:flex-start;font-weight:500;color:var(--muted)}
    button{width:100%;margin-top:18px;border:0;border-radius:12px;padding:14px 18px;background:linear-gradient(100deg,#0ea5e9,#2563eb);color:white;font:inherit;font-weight:800;cursor:pointer}button:disabled{opacity:.45;cursor:wait}
    pre{white-space:pre-wrap;word-break:break-word;background:#071018;border:1px solid var(--line);border-radius:12px;padding:14px;min-height:72px;color:#c9e7ff}.note{font-size:.9rem;color:var(--muted)}.warn{color:var(--amber)}
  </style>
</head>
<body><main><header><div class="kicker">AI Nandemo Expo queue</div><h1>ROM Vomitter FC</h1><p class="sub">対応するhomebrew ROMを検査して展示カートリッジへ送ります。ChatGPTへのログインは不要です。</p></header>
  <section class="card"><form id="upload"><label for="rom">Mapper-0 iNES</label><input id="rom" name="rom" type="file" accept=".nes,application/octet-stream" required>
    <label class="check"><input name="authorized" type="checkbox" required><span>私はこのROMを使用・送信する権利を持っています。</span></label><button>Validate and queue</button></form>
    <p class="note">16/32 KiB PRG + 8 KiB CHR。ROMは非公開で保存され、約1時間で削除されます。</p><pre id="status">Ready.</pre></section>
</main><script>
const form=document.querySelector('#upload'),out=document.querySelector('#status'),button=form.querySelector('button');let timer;
function show(value){out.textContent=typeof value==='string'?value:JSON.stringify(value,null,2)}
async function poll(url){clearTimeout(timer);const r=await fetch(url,{cache:'no-store'});const data=await r.json();show(data);if(!['installed','unchanged','failed','expired','cancelled'].includes(data.state))timer=setTimeout(()=>poll(url),1500)}
form.addEventListener('submit',async event=>{event.preventDefault();button.disabled=true;show('Validating…');try{const body=new FormData(form);const r=await fetch('/api/public/jobs',{method:'POST',body});const data=await r.json();if(!r.ok)throw new Error(data.message||data.error||('HTTP '+r.status));history.replaceState(null,'','#job='+data.status_token);await poll(data.status_url)}catch(error){show('Rejected: '+error.message)}finally{button.disabled=false}});
const token=location.hash.startsWith('#job=')?location.hash.slice(5):'';if(/^[A-Za-z0-9_-]{24,96}$/.test(token))poll('/api/public/jobs/'+token);
</script></body></html>`;
