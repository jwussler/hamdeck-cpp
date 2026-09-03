#include "admin_page.h"

// ⚠️ ONE PAGE, EMBEDDED. See admin_page.h for why it is not served from disk.
//
// ⚠️ THIS IS NOT AN OPERATING POSITION. The console manages accounts and ends a
// session that has left the transmitter locked open. It deliberately carries no
// VFO, no mode, no audio and no PTT-on: operating happens in the client, and a
// web page that could key the rig is a web page that keys the rig by accident.
// The only control here that touches the radio is the one that STOPS it.
//
// Colours and type are the tokens from ~/hamdeck-site/brand/BRAND.md, verbatim.
// tx-red is used for exactly one thing, because in this identity red means RF.
const char* const kAdminPageHtml = R"HTMLPAGE(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>HamDeck — Station Admin</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow+Condensed:wght@600;700&family=IBM+Plex+Mono:wght@400;500&family=IBM+Plex+Sans:wght@400;500&display=swap">
<style>
:root{
  --ground:#0E1013; --panel:#171A1F; --panel-deep:#141619; --line:#2A3038;
  --text:#E8EAED; --dim:#8A929C; --amber:#FFB020; --amber-dim:#8A6320;
  --cyan:#3B82F6; --cyan-fill:#1E3A6B; --tx-red:#B4232A; --ok-green:#32C765;
  --display:"Barlow Condensed","Arial Narrow",sans-serif;
  --body:"IBM Plex Sans",system-ui,sans-serif;
  --data:"IBM Plex Mono",ui-monospace,monospace;
}
*{box-sizing:border-box}
body{margin:0;background:var(--ground);color:var(--text);font-family:var(--body);
     font-size:15px;line-height:1.5;-webkit-text-size-adjust:100%}
.wrap{max-width:920px;margin:0 auto;padding:20px 16px 64px}
h1,h2{font-family:var(--display);text-transform:uppercase;letter-spacing:.05em;
      font-weight:700;margin:0}
h1{font-size:26px}
h2{font-size:17px;color:var(--dim);font-weight:600;margin:0 0 10px}
header{display:flex;align-items:baseline;gap:12px;flex-wrap:wrap;
       border-bottom:1px solid var(--line);padding-bottom:14px;margin-bottom:20px}
header .sub{font-family:var(--data);font-size:12px;color:var(--dim)}
section{background:var(--panel);border:1px solid var(--line);border-radius:8px;
        padding:16px;margin-bottom:16px}
table{width:100%;border-collapse:collapse;font-size:14px}
th{font-family:var(--display);text-transform:uppercase;letter-spacing:.05em;
   font-size:12px;color:var(--dim);text-align:left;font-weight:600;
   border-bottom:1px solid var(--line);padding:6px 8px 6px 0}
td{padding:8px 8px 8px 0;border-bottom:1px solid var(--line);vertical-align:middle}
tr:last-child td{border-bottom:none}
.u{font-family:var(--data);font-weight:500}
.scroll{overflow-x:auto}
button{font-family:var(--display);text-transform:uppercase;letter-spacing:.05em;
       font-weight:600;font-size:13px;cursor:pointer;border-radius:6px;
       border:1px solid var(--line);background:var(--panel-deep);color:var(--text);
       padding:7px 12px;min-height:36px}
button:hover{border-color:var(--cyan)}
button.on{background:var(--cyan-fill);border-color:var(--cyan);color:#fff}
button.ghost{color:var(--dim)}
input{font-family:var(--data);font-size:14px;background:var(--panel-deep);
      color:var(--text);border:1px solid var(--line);border-radius:6px;
      padding:9px 10px;min-height:38px;width:100%}
input:focus{outline:none;border-color:var(--cyan)}
label{font-family:var(--display);text-transform:uppercase;letter-spacing:.05em;
      font-size:12px;color:var(--dim);display:block;margin-bottom:4px}
.row{display:flex;gap:10px;flex-wrap:wrap;align-items:flex-end}
.row>div{flex:1 1 150px}
.flag{font-family:var(--data);font-size:12px;padding:2px 7px;border-radius:4px;
      border:1px solid var(--line);color:var(--dim);white-space:nowrap}
.flag.set{color:var(--amber);border-color:var(--amber-dim)}
.flag.holds{color:#FF7076;border-color:var(--tx-red);font-weight:500}
tr.holding td{background:#2A0F11}
.msg{font-family:var(--data);font-size:13px;padding:10px 12px;border-radius:6px;
     margin-bottom:14px;border:1px solid var(--line);background:var(--panel-deep)}
.msg.bad{border-color:var(--tx-red);color:#FFD9DA}
.msg.good{border-color:var(--ok-green);color:#CFF3DC}
/* ── RED MEANS RF. The only red on this page, and the only control that
      touches the radio. ─────────────────────────────────────────────── */
#air{border:2px solid var(--line);border-radius:8px;padding:16px;margin-bottom:16px;
     background:var(--panel-deep)}
#air.keyed{border-color:var(--tx-red);background:#2A0F11;animation:pulse 1s infinite}
@keyframes pulse{50%{border-color:#FF4A52}}
#airstate{font-family:var(--display);text-transform:uppercase;letter-spacing:.06em;
          font-weight:700;font-size:22px}
#air.keyed #airstate{color:#FF7076}
#airdetail{font-family:var(--data);font-size:13px;color:var(--dim);margin-top:2px}
#kill{width:100%;margin-top:14px;background:var(--tx-red);border-color:var(--tx-red);
      color:#fff;font-size:20px;padding:18px;min-height:64px;letter-spacing:.08em}
#kill:hover{background:#D22B33;border-color:#D22B33}
#login{max-width:340px;margin:12vh auto}
.hidden{display:none!important}
@media(max-width:520px){h1{font-size:22px}#kill{font-size:18px}}
</style>
</head>
<body>

<!-- ── LOGIN ─────────────────────────────────────────────────────────────── -->
<div id="login" class="wrap hidden">
  <header><h1>HamDeck</h1><span class="sub">station admin</span></header>
  <div id="loginmsg"></div>
  <section>
    <div style="margin-bottom:12px"><label for="lu">Username</label>
      <input id="lu" autocomplete="username" autocapitalize="off" autocorrect="off"></div>
    <div style="margin-bottom:14px"><label for="lp">Password</label>
      <input id="lp" type="password" autocomplete="current-password"></div>
    <button id="lgo" class="on" style="width:100%">Sign in</button>
  </section>
</div>

<!-- ── CONSOLE ───────────────────────────────────────────────────────────── -->
<div id="app" class="wrap hidden">
  <header>
    <h1>HamDeck</h1>
    <span class="sub">station admin</span>
    <span style="flex:1"></span>
    <span class="sub" id="who"></span>
    <button class="ghost" id="out">Sign out</button>
  </header>

  <div id="msg"></div>

  <div id="air">
    <div id="airstate">—</div>
    <div id="airdetail">reading the radio…</div>
    <button id="kill">Kill transmit</button>
  </div>

  <section>
    <h2>Sessions</h2>
    <div class="scroll"><table id="stbl"><thead><tr>
      <th>User</th><th>Token</th><th>Rights</th><th>Idle</th><th></th>
    </tr></thead><tbody></tbody></table></div>
  </section>

  <section>
    <h2>Users</h2>
    <div class="scroll"><table id="utbl"><thead><tr>
      <th>User</th><th>Admin</th><th>Transmit</th><th>Station</th><th></th>
    </tr></thead><tbody></tbody></table></div>
  </section>

  <section>
    <h2>Add a user</h2>
    <div class="row">
      <div><label for="nu">Username</label><input id="nu" autocapitalize="off"></div>
      <div><label for="np">Password</label><input id="np" type="password"></div>
      <div style="flex:0 0 auto"><button id="nadmin">Admin</button></div>
      <div style="flex:0 0 auto"><button id="ntx" class="on">Transmit</button></div>
      <div style="flex:0 0 auto"><button id="nadd" class="on">Add</button></div>
    </div>
    <div class="sub" style="font-family:var(--data);font-size:12px;color:var(--dim);margin-top:10px">
      A new account is never a station account. Grant that separately, in the table above.
    </div>
  </section>

  <section>
    <h2>Lockdown</h2>
    <div class="row" style="align-items:center">
      <div style="flex:1 1 auto;color:var(--dim);font-size:14px" id="lockword">—</div>
      <div style="flex:0 0 auto"><button id="lock">Admins only</button></div>
      <div style="flex:0 0 auto"><button id="unlock">All users</button></div>
    </div>
  </section>
</div>

<script>
"use strict";
const $ = s => document.querySelector(s);
let newAdmin = false, newTx = true;

function say(box, text, ok) {
  box.innerHTML = "";
  if (!text) return;
  const d = document.createElement("div");
  d.className = "msg " + (ok ? "good" : "bad");
  d.textContent = text;           // textContent, never innerHTML: usernames come
  box.appendChild(d);             // back from the host and are not markup.
}

async function api(path, opts) {
  const r = await fetch(path, Object.assign({credentials: "same-origin"}, opts || {}));
  let j = null;
  try { j = await r.json(); } catch (e) { /* a non-JSON body is still a status */ }
  return {ok: r.ok, status: r.status, body: j || {}};
}

// ── The kill button ───────────────────────────────────────────────────────
// ⚠️ It reports what the RIG said, not what was sent. "Sent TX0" is not "the
// transmitter is off", and an operator who walks away from an open carrier
// believing they closed it is the whole failure this page exists to prevent.
$("#kill").onclick = async () => {
  const b = $("#kill");
  b.disabled = true; b.textContent = "Stopping…";
  const r = await api("/api/admin/unkey");
  b.disabled = false; b.textContent = "Kill transmit";
  if (r.body.confirmed) {
    say($("#msg"), "Transmitter off — the rig confirmed it.", true);
  } else {
    say($("#msg"), "COULD NOT CONFIRM THE RIG UNKEYED. " +
        (r.body.message || "Go to the radio.") + " The watchdog will still drop " +
        "PTT, but do not assume the carrier is down.", false);
  }
  refresh();
};

async function tick() {
  const r = await api("/api/status");
  if (r.status === 401) { show(false); return; }
  const s = r.body, air = $("#air");
  if (s.tx) {
    air.classList.add("keyed");
    $("#airstate").textContent = "ON AIR";
    $("#airdetail").textContent = "transmitting" +
      (s.tx_timeout_in > 0 ? " — watchdog drops PTT in " + s.tx_timeout_in + " s" : "");
  } else {
    air.classList.remove("keyed");
    $("#airstate").textContent = s.connected ? "RECEIVING" : "RIG NOT CONNECTED";
    $("#airdetail").textContent = s.connected
      ? ((s.freq / 1e6).toFixed(5) + " MHz  " + (s.mode || "") + "  " + (s.power || 0) + " W")
      : "no answer from the radio";
  }
}

function flag(on, text) {
  const s = document.createElement("span");
  s.className = "flag" + (on ? " set" : "");
  s.textContent = text;
  return s;
}
function btn(text, cls, fn) {
  const b = document.createElement("button");
  b.textContent = text; if (cls) b.className = cls; b.onclick = fn;
  return b;
}
function cell(tr, node) {
  const td = document.createElement("td");
  if (node) td.appendChild(node);
  tr.appendChild(td);
  return td;
}

async function act(path, confirmText) {
  if (confirmText && !confirm(confirmText)) return;
  const r = await api(path);
  say($("#msg"), r.body.message || (r.ok ? "Done." : "That did not work."), r.ok);
  refresh();
}

async function refresh() {
  const [u, s, l] = await Promise.all([
    api("/api/admin/users"), api("/api/admin/sessions"), api("/api/admin/lockdown/status")
  ]);
  if (u.status === 401 || u.status === 403) { show(false); return; }

  const ub = $("#utbl tbody"); ub.innerHTML = "";
  (u.body.users || []).forEach(x => {
    const tr = document.createElement("tr");
    const n = document.createElement("span"); n.className = "u"; n.textContent = x.username;
    cell(tr, n);
    cell(tr, flag(x.is_admin, x.is_admin ? "admin" : "—"));
    cell(tr, btn(x.can_transmit ? "Transmit" : "Denied", x.can_transmit ? "on" : "ghost",
      () => act("/api/admin/user/tx/" + (x.can_transmit ? "disable" : "enable") + "/" +
                encodeURIComponent(x.username))));
    cell(tr, btn(x.is_station ? "Station" : "No", x.is_station ? "on" : "ghost",
      () => act("/api/admin/user/station/" + (x.is_station ? "disable" : "enable") + "/" +
                encodeURIComponent(x.username),
                x.is_station ? null :
                "Grant '" + x.username + "' the STATION right?\n\nThat is permission to " +
                "start an unattended ten-second carrier into the amplifier.")));
    cell(tr, btn("Remove", "ghost",
      () => act("/api/admin/user/remove/" + encodeURIComponent(x.username),
                "Remove the account '" + x.username + "' and end its sessions?")));
    ub.appendChild(tr);
  });

  const sb = $("#stbl tbody"); sb.innerHTML = "";
  const rows = s.body.sessions || [];
  if (!rows.length) {
    const tr = document.createElement("tr");
    const td = cell(tr, null); td.colSpan = 5; td.style.color = "var(--dim)";
    td.textContent = "Nobody is signed in.";
    sb.appendChild(tr);
  }
  rows.forEach(x => {
    const tr = document.createElement("tr");
    const n = document.createElement("span"); n.className = "u"; n.textContent = x.username;
    cell(tr, n);
    const t = document.createElement("span"); t.className = "u";
    t.style.color = "var(--dim)"; t.textContent = x.token_short;
    cell(tr, t);
    // ⚠️ ONLY THE RIGHTS THE SESSION ACTUALLY HAS. Rendering every label and
    // greying the ones that are off printed "admin tx station" beside an account
    // that had none of them - which at a glance reads as an account that has all
    // three. An audit view that has to be read carefully to avoid the opposite
    // conclusion is worse than no audit view.
    const w = document.createElement("span");
    let any = false;
    [[x.is_admin, "admin"], [x.can_transmit, "tx"], [x.is_station, "station"]]
      .forEach(([on, name]) => {
        if (!on) return;
        if (any) w.appendChild(document.createTextNode(" "));
        w.appendChild(flag(true, name));
        any = true;
      });
    if (!any) w.appendChild(flag(false, "receive only"));
    // ⚠️ WHICH SESSION IS HOLDING THE TRANSMITTER. Without it the only safe move
    // is to kick everybody, including the people operating correctly.
    if (x.holds_transmitter) {
      tr.classList.add("holding");
      const h = document.createElement("span");
      h.className = "flag holds"; h.textContent = "HOLDS TX";
      w.appendChild(document.createTextNode(" "));
      w.appendChild(h);
      any = true;
    }
    cell(tr, w);
    const i = document.createElement("span"); i.className = "u";
    i.textContent = x.idle_seconds + "s";
    cell(tr, i);
    cell(tr, btn("Kick", "ghost",
      () => act("/api/admin/kick/" + encodeURIComponent(x.username),
                "End every session for '" + x.username + "'?\n\nThis signs them out. " +
                "It does NOT stop a transmitter that is already keyed — use KILL TRANSMIT " +
                "for that.")));
    sb.appendChild(tr);
  });

  const on = !!(l.body && l.body.admin_only_login);
  $("#lockword").textContent = on
    ? "Only admins may sign in."
    : "Every account with a password may sign in.";
  $("#lock").className = on ? "on" : "";
  $("#unlock").className = on ? "" : "on";
}

$("#lock").onclick   = () => act("/api/admin/lockdown/on");
$("#unlock").onclick = () => act("/api/admin/lockdown/off");
$("#nadmin").onclick = e => { newAdmin = !newAdmin; e.target.className = newAdmin ? "on" : ""; };
$("#ntx").onclick    = e => { newTx = !newTx; e.target.className = newTx ? "on" : ""; };
$("#nadd").onclick = async () => {
  const user = $("#nu").value.trim(), pass = $("#np").value;
  if (!user || !pass) { say($("#msg"), "A username and a password are both required.", false); return; }
  const r = await api("/api/admin/user/add", {
    method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({username: user, password: pass,
                          is_admin: newAdmin, can_transmit: newTx})
  });
  say($("#msg"), r.body.message || "That did not work.", r.ok);
  if (r.ok) { $("#nu").value = ""; $("#np").value = ""; }
  refresh();
};

$("#lgo").onclick = async () => {
  const r = await api("/api/auth/login", {
    method: "POST", headers: {"Content-Type": "application/json"},
    body: JSON.stringify({username: $("#lu").value.trim(), password: $("#lp").value})
  });
  if (r.ok) { $("#lp").value = ""; say($("#loginmsg"), "", true); start(); }
  else say($("#loginmsg"), r.body.message || "Sign-in failed.", false);
};
$("#lp").onkeydown = e => { if (e.key === "Enter") $("#lgo").click(); };
$("#out").onclick = async () => {
  await api("/api/auth/logout", {method: "POST"});
  show(false);
};

function show(signedIn) {
  $("#app").classList.toggle("hidden", !signedIn);
  $("#login").classList.toggle("hidden", signedIn);
  if (!signedIn && window.__t) { clearInterval(window.__t); window.__t = null; }
}

async function start() {
  const st = await api("/api/auth/status");
  if (!st.body.authenticated) { show(false); return; }
  // ⚠️ The console is admin-only. A signed-in non-admin gets 403 from every table
  // on it, so say that plainly instead of showing them an empty page they will
  // read as broken.
  const probe = await api("/api/admin/users");
  if (probe.status === 403) {
    show(false);
    say($("#loginmsg"), "That account is not an administrator. This page manages " +
                        "accounts and stops a stuck transmitter; operating happens " +
                        "in the HamDeck client.", false);
    await api("/api/auth/logout", {method: "POST"});
    return;
  }
  $("#who").textContent = st.body.username || "";
  show(true);
  refresh(); tick();
  window.__t = setInterval(tick, 1000);
}
start();
</script>
</body>
</html>
)HTMLPAGE";
