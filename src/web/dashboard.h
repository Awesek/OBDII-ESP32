/**
 * @file dashboard.h
 * @brief Vestavenya jednostrankova diagnosticka aplikace (dashboard) ulozena v
 * PROGMEM
 *
 * Tento soubor obsahuje kompletni HTML/CSS/JS webovou stranku zakodovanou
 * jako PROGMEM retezec pro ESP32. Stranka se servuje pres AsyncWebServer
 * a komunikuje s ESP32 pres WebSocket (JSON protokol).
 *
 * Obsahuje nasledujici funkcni celky:
 *   - SVG ukazatele (gauge) pro otacky motoru (RPM) a rychlost vozidla
 *   - Zalozka "Dash" zobrazujici vsechny podporovane PID hodnoty v bublinach
 *     vcetne sparkline minigrafu historie poslednich 60 vzorku
 *   - DTC panel pro cteni a zobrazeni diagnostickych poruchovych kodu
 *   - Ovladani datoveho proudu (start/stop, vyber PID, nastaveni intervalu)
 *   - Cteni VIN cisla, nazvu ridici jednotky, kalibracniho ID a stavu monitoru
 *   - Komunikacni log s fixni vyskou a vlastnim posuvnikem (max 300 radku)
 *   - Zalozka "Settings" pro parametry vozidla ukladane do localStorage
 * prohlizece
 *   - Zalozka "Stats" s vypoctem okamzite a prumerne spotreby paliva,
 *     odhadem zarazeneho prevodoveho stupne a zrychleni
 *   - Zalozka "Map" s GPS logovanim polohy pomoci Geolocation API,
 *     vizualizaci trasy na canvasu a exportem do GPX formatu
 *
 * Veskeré texty uzivatelskeho rozhrani (tlacitka, popisky, HTML) jsou v
 * anglictine. Komentare v kodu jsou v cestine.
 *
 * @author Ales Pouzar
 */

#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <pgmspace.h>

static const char dashboard_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>OBD-II ISO 15031-5</title>
<style>
/* ---- OEM Minimalist / Mobile-First CSS pro OBD2 Dashboard ---- */
:root {
  --bg: #0b0c10;
  --bg2: #16181d;
  --bg3: #1f2229;
  --fg: #f0f0f0;
  --fg2: #8a8d93;
  --accent: #26a69a;
  --ok: #2e7d32;
  --err: #d32f2f;
  --warn: #f57c00;
  --nav-h: 64px;
}

* { box-sizing: border-box; margin: 0; padding: 0; }

body {
  font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
  background: var(--bg); 
  color: var(--fg);
  padding: 0 12px calc(var(--nav-h) + 16px) 12px;
  max-width: 600px;
  margin: 0 auto;
  -webkit-user-select: none; user-select: none;
  -webkit-tap-highlight-color: transparent;
}

.hdr {
  display: flex; align-items: center; gap: 10px;
  padding: 18px 18px 16px; margin: 0 -12px 12px -12px;
  position: sticky; top: 0; z-index: 100;
  background: rgba(11, 12, 16, 0.95);
  backdrop-filter: blur(10px); -webkit-backdrop-filter: blur(10px);
  border-bottom: 1px solid rgba(255,255,255,0.05);
}
.hdr h1 { 
  font-size: 1.1em; font-weight: 600; color: var(--fg); flex: 1; letter-spacing: 0.5px; margin: 0;
}
.dot {
  width: 8px; height: 8px; border-radius: 50%;
  background: #444; transition: background 0.3s, box-shadow 0.3s;
}
.dot.on { background: var(--ok); box-shadow: 0 0 8px rgba(46,125,50,0.6); }
.dot.err { background: var(--err); box-shadow: 0 0 8px rgba(211,47,47,0.6); }
.hdr .info { font-size: 0.75em; color: var(--fg2); font-weight: 500; }
.stream-mode {
  font-size: 0.62em; color: var(--fg2); border: 1px solid rgba(255,255,255,0.08);
  border-radius: 8px; padding: 2px 6px; margin-top: 3px; align-self: flex-end;
}
.stream-mode.dash { color: var(--accent); border-color: rgba(38,166,154,0.32); }
.stream-mode.inspector { color: var(--warn); border-color: rgba(245,124,0,0.36); }

.tabs {
  position: fixed; bottom: 0; left: 0; right: 0;
  height: var(--nav-h); background: rgba(22, 24, 29, 0.95);
  backdrop-filter: blur(10px); -webkit-backdrop-filter: blur(10px);
  display: flex; justify-content: space-around; align-items: center;
  border-top: 1px solid rgba(255,255,255,0.05); z-index: 1000;
  padding-bottom: env(safe-area-inset-bottom);
}
.tab {
  flex: 1; text-align: center; font-size: 0.70rem; font-weight: 600;
  padding: 8px 0; color: rgba(138, 141, 147, 0.6); cursor: pointer;
  text-transform: uppercase; letter-spacing: 0.5px;
  transition: all 0.2s;
  height: 100%; display: flex; align-items: center; justify-content: center;
}
.tab.active { color: var(--accent); font-weight: 700; transform: translateY(-2px); }
.tab-content { display: none; padding-top: 4px; }
.tab-content.active { display: block; animation: fadeIn 0.3s ease; }
@keyframes fadeIn { from { opacity: 0; transform: translateY(4px); } to { opacity: 1; transform: translateY(0); } }

.gauges { display: flex; gap: 12px; margin-bottom: 20px; }
.gauge-wrap {
  background: var(--bg2); border-radius: 20px; padding: 20px 10px 12px;
  flex: 1; text-align: center; position: relative;
}
.gauge-wrap svg { width: 100%; height: auto; max-width: 200px; display: block; margin: 0 auto; filter: drop-shadow(0 4px 6px rgba(0,0,0,0.3)); }
.gauge-val {
  font-size: 2.2em; font-weight: 300; color: var(--fg);
  margin: -10px 0 2px; line-height: 1;
}
.gauge-lbl { font-size: 0.75em; color: var(--fg2); font-weight: 500; text-transform: uppercase; letter-spacing: 1px; }

.gauge-arc-bg { fill: none; stroke: var(--bg3); stroke-width: 8; stroke-linecap: round; }
.gauge-arc { fill: none; stroke-width: 8; stroke-linecap: round; transition: stroke-dashoffset 0.4s cubic-bezier(0.4, 0, 0.2, 1), stroke 0.3s; }

.btns { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 16px; }
.btn {
  background: var(--bg3); color: var(--fg); border: none;
  padding: 12px 18px; border-radius: 12px; cursor: pointer;
  font-family: inherit; font-size: 0.85em; font-weight: 600;
  transition: background 0.2s, transform 0.1s;
  flex: 1 1 calc(50% - 8px); min-width: 120px;
}
.btn:hover { background: #2a2e37; }
.btn:active { transform: scale(0.96); background: #2a2e37; }
.btn.on { background: rgba(38, 166, 154, 0.15); color: var(--accent); }
.btn.rec { background: rgba(211, 47, 47, 0.15); color: var(--err); }
.btn:disabled { opacity: 0.3; cursor: not-allowed; }

.rec-indicator { 
  display: inline-block; width: 8px; height: 8px; border-radius: 50%;
  background: var(--err); margin-right: 6px; box-shadow: 0 0 6px var(--err);
  animation: blink 1s infinite;
}
@keyframes blink { 0%,100%{opacity:1} 50%{opacity:0} }

.panel {
  background: var(--bg2); border-radius: 20px; padding: 18px;
  margin-bottom: 12px;
}
.panel-title { 
  font-size: 0.75em; font-weight: 600; color: var(--fg2);
  text-transform: uppercase; letter-spacing: 1px; margin-bottom: 14px;
}

.dash-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.pid-bubble {
  background: var(--bg2); border-radius: 16px; padding: 14px 12px 10px;
  text-align: center; border: 1px solid rgba(255,255,255,0.02);
  transition: background 0.3s; display: flex; flex-direction: column; justify-content: center;
}
.pid-bubble.fresh { background: rgba(38, 166, 154, 0.1); }
.pid-bubble .pb-val { font-size: 1.4em; font-weight: 600; color: var(--fg); font-variant-numeric: tabular-nums; }
.pid-bubble .pb-unit { font-size: 0.6em; color: var(--fg2); font-weight: normal; margin-left: 2px; }
.pid-bubble .pb-name { font-size: 0.7em; color: var(--fg2); margin-top: 4px; font-weight: 500; }
.pid-bubble canvas { display: block; width: 100%; height: 36px; margin-top: 8px; opacity: 0.8; }
.pid-detail {
  margin-top: 8px; padding-top: 8px; border-top: 1px solid rgba(255,255,255,0.06);
  text-align: left; font-family: ui-monospace, SFMono-Regular, Consolas, "Liberation Mono", Menlo, monospace;
  font-size: 0.64em; color: var(--fg2); line-height: 1.45;
}
.pid-detail summary { cursor: pointer; color: var(--accent); font-family: inherit; }
.pid-detail.err summary { color: var(--err); }
.pid-detail div { white-space: pre-wrap; }

.dtc-list { color: var(--warn); font-size: 0.9em; line-height: 1.5; }
.dtc-list.empty { color: var(--ok); }
#infoContent { font-size: 0.85em; line-height: 1.5; color: var(--fg); }
#infoContent strong { color: var(--fg2); font-weight: 500; }
.diag-select {
  width: 100%; background: var(--bg3); color: var(--fg); border: none;
  padding: 10px 12px; border-radius: 8px; font-family: inherit; font-size: 0.9em;
  margin-bottom: 10px;
}
.diag-select:focus { outline: 1px solid var(--accent); }
.diag-mode-summary { font-size: 0.76em; color: var(--fg2); line-height: 1.45; margin-bottom: 12px; }
.diag-action-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; }
.diag-action-grid .btn { min-width: 0; }

.stream-cfg { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; margin-bottom: 12px; }
.stream-cfg label { font-size: 0.8em; color: var(--fg2); }
.stream-cfg input {
  background: var(--bg3); border: none; color: var(--fg);
  padding: 8px 12px; border-radius: 8px; font-family: inherit; font-size: 0.9em; width: 80px; text-align: center;
}
.stream-cfg input:focus { outline: 1px solid var(--accent); }
.inspector-interval {
  display: flex; align-items: center; gap: 5px; font-size: 0.7em; color: var(--fg2);
}
.inspector-interval input {
  width: 64px; background: var(--bg3); color: var(--fg); border: none;
  padding: 6px 8px; border-radius: 8px; font: inherit; text-align: center;
}
.pid-checks { display: flex; flex-wrap: wrap; gap: 6px; margin-top: 5px; }
.pid-chk {
  font-size: 0.75em; padding: 8px 12px; background: var(--bg3); color: var(--fg2);
  border-radius: 20px; cursor: pointer; transition: 0.2s; font-weight: 500; border: 1px solid transparent;
}
.pid-chk.sel { background: rgba(38, 166, 154, 0.15); color: var(--accent); border-color: rgba(38, 166, 154, 0.3); }

#logBox {
  background: var(--bg2); border-radius: 16px; padding: 12px; height: 60vh; overflow-y: auto;
  font-family: ui-monospace, SFMono-Regular, Consolas, "Liberation Mono", Menlo, monospace;
  font-size: 0.75em; line-height: 1.6; border: 1px solid rgba(255,255,255,0.02);
}
#logBox::-webkit-scrollbar { width: 4px; }
#logBox::-webkit-scrollbar-thumb { background: var(--bg3); border-radius: 4px; }
.m-in { color: var(--ok); } .m-out { color: var(--warn); } .m-sys { color: var(--fg2); }

.settings-grid { display: flex; flex-direction: column; gap: 10px; }
.set-group { background: var(--bg3); border-radius: 16px; padding: 14px; border: 1px solid rgba(255,255,255,0.02); }
.set-group label { display: block; font-size: 0.75em; font-weight: 500; color: var(--fg2); margin-bottom: 6px; }
.set-group input, .set-group select {
  width: 100%; background: var(--bg); border: none; color: var(--fg);
  padding: 10px 12px; border-radius: 8px; font-family: inherit; font-size: 0.9em;
}
.set-group input:focus, .set-group select:focus { outline: 1px solid var(--accent); }
.set-group .set-hint { font-size: 0.65em; color: rgba(255,255,255,0.3); margin-top: 6px; }

.stats-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 12px; }
.stat-card { background: var(--bg3); border-radius: 16px; padding: 14px; text-align: center; }
.stat-val { font-size: 1.5em; font-weight: 600; color: var(--fg); font-variant-numeric: tabular-nums; }
.stat-unit { font-size: 0.6em; color: var(--fg2); margin-left: 2px; font-weight: normal; }
.stat-lbl { font-size: 0.7em; color: var(--fg2); margin-top: 4px; font-weight: 500; }
.stat-card.wide { grid-column: 1 / -1; }

#mapCanvas { width: 100%; height: 350px; background: var(--bg3); border-radius: 16px; display: block; border: 1px solid rgba(255,255,255,0.02); }
.gps-info { display: flex; flex-direction: column; gap: 6px; margin-top: 12px; font-size: 0.85em; }
.gps-info span { color: var(--fg2); display: flex; justify-content: space-between; padding: 6px 12px; background: var(--bg3); border-radius: 8px; }
.gps-info strong { color: var(--fg); font-weight: 600; }

  .gauges { flex-direction: column; gap: 12px; }
  .btn { flex: 1 1 100%; }
}

/* Terminal Styles */
.term-row { border-top: 1px solid rgba(255,255,255,0.03); }
.term-row:hover { background: rgba(255,255,255,0.02); }
.term-id { color: #ffa726; font-size: 0.9em; }
.term-payload { color: #64b5f6; font-weight: 500; }
.term-interp { color: #81c784; font-size: 0.9em; }
.term-neg { color: #e57373; font-style: italic; }
.modal-backdrop {
  position: fixed; inset: 0; z-index: 2000; display: none; align-items: center; justify-content: center;
  background: rgba(0,0,0,0.62); padding: 18px;
}
.modal-card {
  width: min(420px, 100%); background: var(--bg2); border: 1px solid rgba(255,255,255,0.08);
  border-radius: 18px; padding: 18px; box-shadow: 0 16px 40px rgba(0,0,0,0.45);
}
.modal-title { font-size: 0.95em; font-weight: 700; margin-bottom: 8px; }
.modal-body { font-size: 0.82em; color: var(--fg2); line-height: 1.45; margin-bottom: 14px; }
.modal-actions { display: flex; gap: 8px; }
.modal-actions .btn { flex: 1 1 0; min-width: 0; }
.modal-actions .danger { color: var(--warn); background: rgba(245,124,0,0.12); }
</style>
</head>
<body>

<!-- Hlavicka stranky s nazvem, stavovou teckou a informacemi o pripojeni -->
<div class="hdr">
  <h1 id="hdrTitle">EOBD &mdash; ISO 15031-5 / SAE J1979</h1>
  <div class="dot" id="dot"></div>
  <div style="display: flex; flex-direction: column; align-items: flex-end; margin-left: auto;">
    <span class="info" id="connTxt" style="font-weight: 600;">Disconnected</span>
    <div style="display: flex; gap: 8px; font-size: 0.65em; color: var(--fg2); font-weight: normal; margin-top: 1px;">
      <span id="heapTxt"></span>
      <span id="uptimeWeb">W: 0:00</span>
      <span id="uptimeESP">E: 0:00</span>
    </div>
    <span class="stream-mode" id="streamModeTxt">Idle</span>
  </div>
</div>

<div class="modal-backdrop" id="inspectorModal">
  <div class="modal-card">
    <div class="modal-title">Switch to PID Inspector?</div>
    <div class="modal-body">
      PID Inspector starts a separate diagnostic stream. Current DASH live data,
      recording, trip tracking and GPS tracking will be stopped.
    </div>
    <div class="modal-actions">
      <button class="btn danger" onclick="confirmInspectorSwitch()">Continue to PID Inspector</button>
      <button class="btn" onclick="closeInspectorModal()">Keep DASH Stream</button>
    </div>
  </div>
</div>

<!-- Spodni navigacni lista (Mobile-First) -->
<div class="tabs">
  <div class="tab active" onclick="switchTab('main')">Home</div>
  <div class="tab" onclick="switchTab('dash')">Dash</div>
  <div class="tab" onclick="switchTab('stats')">Trip</div>
  <div class="tab" onclick="switchTab('map')">Map</div>
  <div class="tab" onclick="switchTab('diag')">Diag</div>
  <div class="tab" onclick="switchTab('pid')">PID</div>
  <div class="tab" onclick="switchTab('settings')">Cfg</div>
  <div class="tab" onclick="switchTab('log')">Log</div>
</div>

<!-- ============================================================ -->
<!-- ZALOZKA: Stream (ukazatele otacek/rychlosti + ovladaci prvky) -->
<!-- ============================================================ -->
<div class="tab-content active" id="tab_main">

  <!-- SVG ukazatele: otacky motoru (RPM) a rychlost vozidla (km/h) -->
  <div class="gauges">
    <div class="gauge-wrap">
      <svg viewBox="0 0 200 115">
        <path d="M 20 105 A 80 80 0 0 1 180 105" class="gauge-arc-bg"/>
        <path d="M 20 105 A 80 80 0 0 1 180 105" class="gauge-arc" id="arc_rpm"
          stroke="#00e5ff" stroke-dasharray="251" stroke-dashoffset="251"/>
      </svg>
      <div class="gauge-val" id="val_rpm">&mdash;</div>
      <div class="gauge-lbl">RPM</div>
    </div>
    <div class="gauge-wrap">
      <svg viewBox="0 0 200 115">
        <path d="M 20 105 A 80 80 0 0 1 180 105" class="gauge-arc-bg"/>
        <path d="M 20 105 A 80 80 0 0 1 180 105" class="gauge-arc" id="arc_spd"
          stroke="#00e5ff" stroke-dasharray="251" stroke-dashoffset="251"/>
      </svg>
      <div class="gauge-val" id="val_spd">&mdash;</div>
      <div class="gauge-lbl">km/h</div>
    </div>
  </div>

  <!-- Vehicle Info pruh — staticka data o vozidle z init.vehicle_info.
       Zobrazuje OBD standard, typ paliva, max ranges (CONFIG PIDy).
       Skryty dokud neprijde init odpoved. -->
  <div id="vehicleInfoBar" class="panel" style="display:none; padding: 8px 14px; margin-bottom: 14px; font-size: 0.78em;">
    <div style="color: var(--fg2); font-weight: 600; text-transform: uppercase; letter-spacing: 1px; margin-bottom: 6px;">Vehicle Info</div>
    <div id="vehicleInfoContent" style="display:flex; flex-wrap:wrap; gap: 14px;"></div>
  </div>

  <!-- Ovladaci tlacitka: inicializace OBD, spusteni/zastaveni streamu -->
  <div class="btns">
    <button class="btn" id="btnInit" onclick="cmd({cmd:'init'})">Init OBD</button>
    <button class="btn" id="btnStream" onclick="toggleStream()" disabled>Start Stream</button>
    <button class="btn" onclick="cmd({cmd:'ping'})">Ping</button>
  </div>

  <!-- ROZKLIKAVACI SEKCE PRO EXPORT A LOGOVANI -->
  <details class="panel" id="exportSetup" style="margin-top: 15px;">
    <summary style="font-size: 0.75em; font-weight: 600; color: var(--fg2); text-transform: uppercase; letter-spacing: 1px; cursor: pointer;">
      Export &amp; Logging Setup
    </summary>
    <div style="margin-top: 15px;">
      <div class="stream-cfg">
        <label>Interval (ms):</label>
        <input type="number" id="streamInt" value="200" min="50" max="5000" step="50" style="width:70px">
      </div>
      
      <div class="pid-checks" id="pidChecks" style="margin-bottom: 15px;">
        <span style="color:#666; font-size: 13px;">Press Init to see available PIDs...</span>
      </div>

      <div class="btns">
        <button class="btn" id="btnRec" onclick="toggleRec()" disabled>Record</button>
        <button class="btn" id="btnExport" onclick="exportCSV()" disabled>Export CSV</button>
      </div>
    </div>
  </details>
</div>

<!-- ============================================================ -->
<!-- ZALOZKA: Dash (vsechny podporovane PID jako interaktivni bubliny) -->
<!-- ============================================================ -->
<div class="tab-content" id="tab_dash">
  <div class="panel">
    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:14px">
      <div class="panel-title" style="margin:0">Mode 01 - Live Data</div>
      <div style="display:flex; gap:8px">
        <button onclick="resetDash()" class="btn" style="padding:6px 12px; font-size:0.7em; min-width:auto">Reset</button>
        <button onclick="showAllSupportedPids()" class="btn" style="padding:6px 12px; font-size:0.7em; min-width:auto">Show All</button>
      </div>
    </div>
    <div class="dash-grid" id="dashGrid">
      <div style="color:var(--fg2);font-size:0.82em;padding:20px;text-align:center;width:100%">
        Press <strong>Init OBD</strong> and <strong>Start Stream</strong> to see live data.
      </div>
    </div>
  </div>
</div>

<!-- ============================================================ -->
<!-- ZALOZKA: Stats (vypocet spotreby paliva + statistiky jizdy)   -->
<!-- ============================================================ -->
<div class="tab-content" id="tab_stats">
  <div class="panel">
    <div class="panel-title">Trip Statistics</div>
    <div class="btns" style="margin-bottom:6px">
      <button class="btn" id="btnTrip" onclick="toggleTrip()">Start Trip</button>
      <button class="btn" onclick="resetTrip()">Reset Trip</button>
    </div>
    <div class="stats-grid">
      <div class="stat-card">
        <div class="stat-val" id="st_fc_100">&mdash;<span class="stat-unit">L/100km</span></div>
        <div class="stat-lbl">Inst. Consumption</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_fc_avg">&mdash;<span class="stat-unit">L/100km</span></div>
        <div class="stat-lbl">Avg. Consumption</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_fuel_used">&mdash;<span class="stat-unit">L</span></div>
        <div class="stat-lbl">Fuel Used (Trip)</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_dist">&mdash;<span class="stat-unit">km</span></div>
        <div class="stat-lbl">Distance (Trip)</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_cost">&mdash;<span class="stat-unit"></span></div>
        <div class="stat-lbl">Trip Cost</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_time">&mdash;</div>
        <div class="stat-lbl">Trip Timer</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_avg_spd">&mdash;<span class="stat-unit">km/h</span></div>
        <div class="stat-lbl">Avg. Speed</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_gear">&mdash;</div>
        <div class="stat-lbl">Est. Gear</div>
      </div>
      <div class="stat-card">
        <div class="stat-val" id="st_accel">&mdash;<span class="stat-unit">m/s&sup2;</span></div>
        <div class="stat-lbl">Acceleration</div>
      </div>
    </div>
  </div>
</div>

<!-- ============================================================ -->
<!-- ZALOZKA: Map (GPS zaznamenavani trasy + vizualizace na canvasu) -->
<!-- ============================================================ -->
<div class="tab-content" id="tab_map">
  <div class="panel">
    <div class="panel-title">GPS Track <span style="font-size:0.6em;opacity:0.5;margin-left:6px">Kalman + OBD + Accel fusion</span></div>
    <div class="btns" style="margin-bottom:6px">
      <button class="btn" id="btnGps" onclick="toggleGps()">Start GPS</button>
      <button class="btn" onclick="exportGpx()" id="btnGpxExport" disabled>Export GPX</button>
      <button class="btn" onclick="exportInteractiveMap()" id="btnMapExport" disabled>Export Map</button>
      <button class="btn" onclick="clearTrack()">Clear</button>
    </div>
    <div style="position:relative">
      <canvas id="mapCanvas"></canvas>
      <div id="mapPopup" style="display:none;position:absolute;background:rgba(22,33,62,0.95);border:1px solid #0df;border-radius:8px;padding:8px 10px;font-size:0.75em;color:#e0e0e0;pointer-events:none;z-index:10;min-width:140px;line-height:1.5"></div>
    </div>
    <div class="gps-info" id="gpsInfo">
      <span>Status: <strong id="gpsStatus">Inactive</strong></span>
      <span>Points: <strong id="gpsPoints">0</strong></span>
      <span>Acc: <strong id="gpsAcc">&mdash;</strong></span>
      <span>Lat: <strong id="gpsLat">&mdash;</strong></span>
      <span>Lon: <strong id="gpsLon">&mdash;</strong></span>
      <span>GPS Speed: <strong id="gpsSpd">&mdash;</strong></span>
      <span>Distance: <strong id="gpsDist">&mdash;</strong></span>
    </div>
  </div>
</div>

<!-- ============================================================ -->
<!-- ZALOZKA: Diagnostika (DTC kody, VIN, stav monitoru emisi)     -->
<!-- ============================================================ -->
<div class="tab-content" id="tab_diag">
  <div class="panel" id="diagModePanel">
    <div class="panel-title">Diagnostic Services</div>
    <select id="diagModeSelect" class="diag-select" onchange="selectDiagMode(this.value)">
      <option value="mode01">Mode 01 - Current Data</option>
      <option value="mode02">Mode 02 - Freeze Frame</option>
      <option value="mode03">Mode 03 - Stored DTC</option>
      <option value="mode04">Mode 04 - Clear DTC</option>
      <option value="mode06">Mode 06 - On-board Monitors</option>
      <option value="mode07">Mode 07 - Pending DTC</option>
      <option value="mode09">Mode 09 - Vehicle Info</option>
      <option value="mode0a">Mode 0A - Permanent DTC</option>
      <option value="transport">Transport / CAN</option>
    </select>
    <div id="diagModeSummary" class="diag-mode-summary"></div>
    <div id="diagModeActions" class="diag-action-grid"></div>
  </div>

  <!-- ZAKOMENTOVANO: Diagnostic Terminal Button -->
  <!-- 
  <button class="btn" onclick="toggleTerminal()" style="width:100%; border: 1px solid var(--warn); color: var(--warn); background:rgba(245, 124, 0, 0.05); margin-bottom:12px">Open Diagnostic Terminal</button>
  -->

  <div class="diag-results">
    <!-- Panel diagnostickych poruchovych kodu (DTC) -->
    <div class="panel" id="dtcPanel" style="display:none; margin-bottom:12px">
      <div class="panel-title">Diagnostic Trouble Codes (DTC)</div>
      <div id="dtcContent" class="dtc-list empty">No DTCs</div>
    </div>

    <!-- Informacni panel (VIN, stav monitoru, nazev ECU apod.) -->
    <div class="panel" id="infoPanel" style="display:none">
      <div class="panel-title">Vehicle Information</div>
      <div id="infoContent"></div>
    </div>
  </div>

  <!-- ZAKOMENTOVANO: TERMINAL WINDOW (Toggleable) -->
  <!-- 
  <div class="panel" id="termPanel" style="display:none; border: 1px solid var(--warn); margin-top:10px">
    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:12px">
      <div class="panel-title" style="margin:0; color: #ffa726;">Diagnostic Terminal <span style="font-size:0.6em; opacity:0.6; vertical-align:middle; margin-left:5px;">[UNDER TESTING]</span></div>
      <div style="display:flex; gap:6px">
        <button onclick="clearTerminal()" class="btn" style="padding:4px 10px; font-size:0.65em; min-width:auto; background:rgba(255,255,255,0.05)">Clear</button>
        <button onclick="toggleTerminal()" class="btn" style="padding:4px 10px; font-size:0.65em; min-width:auto; background:rgba(211, 47, 47, 0.1)">Close</button>
      </div>
    </div>
    
    <div style="display:flex; gap:8px; margin-bottom:12px">
      <div style="flex:1">
        <label style="font-size:0.6em; color:var(--fg2); display:block; margin-bottom:4px">Service (HEX)</label>
        <input type="text" id="termSvc" value="01" maxlength="2" style="width:100%; background:var(--bg2); border:1px solid var(--bg3); color:var(--fg); padding:8px; border-radius:8px; text-align:center; font-family:monospace">
      </div>
      <div style="flex:1">
        <label style="font-size:0.6em; color:var(--fg2); display:block; margin-bottom:4px">PID (HEX)</label>
        <input type="text" id="termPid" value="0C" maxlength="2" style="width:100%; background:var(--bg2); border:1px solid var(--bg3); color:var(--fg); padding:8px; border-radius:8px; text-align:center; font-family:monospace">
      </div>
      <div style="flex:0; align-self:flex-end">
        <button class="btn" id="btnTermQuery" onclick="sendManualQuery()" style="padding:10px 20px; background:var(--accent)" disabled>Query</button>
      </div>
    </div>

    <div style="background:#000; border-radius:12px; overflow:hidden; font-family:'Cascadia Code', 'Consolas', monospace; font-size:0.75em; border:1px solid rgba(255,255,255,0.05)">
      <div style="max-height:300px; overflow-y:auto">
        <table style="width:100%; border-collapse:collapse; text-align:left" id="termTable">
          <thead style="background:rgba(255,255,255,0.05); color:var(--fg2); position:sticky; top:0">
            <tr>
              <th style="padding:8px 12px; font-weight:normal">Time</th>
              <th style="padding:8px 12px; font-weight:normal">ID</th>
              <th style="padding:8px 12px; font-weight:normal">Data (HEX)</th>
              <th style="padding:8px 12px; font-weight:normal">Interpretation</th>
            </tr>
          </thead>
          <tbody id="termBody"></tbody>
        </table>
      </div>
      <div id="termEmpty" style="padding:30px; text-align:center; color:rgba(255,255,255,0.2)">
        Ready for manual PID queries...
      </div>
    </div>
  </div>
  -->
</div>

<!-- ============================================================ -->
<!-- ZALOZKA: PID Inspector (oddeleny diagnosticky stream PIDu)    -->
<!-- ============================================================ -->
<div class="tab-content" id="tab_pid">
  <div class="panel" id="pidsSelectPanel" style="margin-bottom:12px">
    <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:10px;">
      <div class="panel-title" style="margin:0">PIDs Select</div>
      <div style="display:flex; gap:6px;">
        <label class="inspector-interval">ms
          <input type="number" id="inspectorInt" value="500" min="100" max="5000" step="50">
        </label>
        <span id="pidsSelectCount" style="font-size:0.78em; color:var(--fg2); align-self:center;">0/4</span>
        <button class="btn" id="btnPidsSelectActivate" onclick="togglePidsSelect()" disabled style="padding:5px 10px; font-size:0.7em; min-width:auto">Activate</button>
      </div>
    </div>
    <div style="font-size:0.72em; color:var(--fg2); margin-bottom:10px;">
      Vyberte az 4 PIDy pro souvisle cteni. Aktivace spusti samostatny PID
      Inspector stream; pri odchodu z PID tabu nebo Stop se zastavi.
    </div>
    <div id="pidsSelectList" style="max-height:300px; overflow-y:auto; padding-right:6px;">
      <div style="color:var(--fg2); font-size:0.78em; text-align:center; padding:20px;">Run Init OBD first.</div>
    </div>
    <div id="pidsSelectCards" style="display:none; margin-top:14px; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap:10px;"></div>
  </div>
</div>

<!-- ============================================================ -->
<!-- ZALOZKA: Nastaveni (parametry vozidla pro vypocty spotreby)   -->
<!-- ============================================================ -->
<div class="tab-content" id="tab_settings">
  <div class="panel">
    <div class="panel-title">Vehicle Parameters</div>
    <p style="font-size:0.72em;color:var(--fg2);margin-bottom:10px">
      These values are used for fuel consumption and statistics calculations.
      Saved automatically to browser storage.
    </p>
    <div class="settings-grid">
      <div class="set-group">
        <label>Fuel Type</label>
        <select id="set_fuel_type" onchange="saveSetting(this)">
          <option value="diesel">Diesel</option>
          <option value="hybrid_gasoline">Hybrid Gasoline</option>
          <option value="hybrid_diesel">Hybrid Diesel</option>
          <option value="gasoline">Gasoline</option>
          <option value="lpg">LPG</option>
          <option value="cng">CNG</option>
        </select>
      </div>
      <div class="set-group">
        <label>Engine Displacement (L)</label>
        <input type="number" id="set_displacement" value="2.0" min="0.5" max="8.0" step="0.1" onchange="saveSetting(this)">
        <div class="set-hint">e.g. 2.0 for 2000cc</div>
      </div>
      <div class="set-group">
        <label>Fuel Density (g/L)</label>
        <input type="number" id="set_fuel_density" value="832" min="500" max="1000" step="1" onchange="saveSetting(this)">
        <div class="set-hint">Diesel ~832, Gasoline ~745, LPG ~550</div>
      </div>
      <div class="set-group">
        <label>Stoichiometric AFR</label>
        <input type="number" id="set_afr" value="14.6" min="6" max="20" step="0.1" onchange="saveSetting(this)">
        <div class="set-hint">Diesel 14.6, Gasoline 14.7, LPG 15.7</div>
      </div>
      <div class="set-group">
        <label>Volumetric Efficiency (%)</label>
        <input type="number" id="set_ve" value="80" min="10" max="120" step="1" onchange="saveSetting(this)">
        <div class="set-hint">Normally aspirated ~80%, Turbo ~95%+</div>
      </div>
      <div class="set-group">
        <label>Fuel Price per Liter</label>
        <input type="number" id="set_fuel_price" value="1.50" min="0" max="10" step="0.01" onchange="saveSetting(this)">
        <div class="set-hint">In your local currency</div>
      </div>
      <div class="set-group">
        <label>Currency Symbol</label>
        <input type="text" id="set_currency" value="CZK" maxlength="5" onchange="saveSetting(this)">
      </div>
    </div>
  </div>

</div>

<!-- ============================================================ -->
<!-- ZALOZKA: Log (komunikacni log s fixni vyskou a posuvnikem)    -->
<!-- ============================================================ -->
<div class="tab-content" id="tab_log">
  <div class="panel">
    <div class="panel-title">System Log</div>
    <div id="logBox"></div>
    <div style="margin-top:12px; display:grid; grid-template-columns:1fr 1fr; gap:8px">
      <button class="btn" onclick="cmd({cmd:'ping'})">Manual Ping</button>
      <button class="btn" onclick="cmd({cmd:'transport_init'})">Transport</button>
      <button class="btn" onclick="cmd({cmd:'pid00_probe'})">PID 00 Probe</button>
      <button class="btn" onclick="document.getElementById('logBox').innerHTML=''">Clear Log</button>
    </div>
  </div>
</div>

<script>
/* ================================================================ */
/*  Globalni stav aplikace                                          */
/*  ws         — instance WebSocket pripojeni k ESP32               */
/*  streaming  — priznak, zda probiha kontinualni cteni PID         */
/*  obdReady   — priznak, zda probehla uspesna inicializace OBD     */
/*  supportedPids — pole PID cisel, ktere ECU vozidla podporuje     */
/*  streamPids — aktualne vybrane PID pro streamovani (vychozi:     */
/*               RPM=12, rychlost=13, teplota chladici kap.=5)     */
/*  pidNames   — cache pro nazvy PID (pouziva se pri zobrazeni)     */
/* ================================================================ */
var ws = null, streaming = false, obdReady = false, transportReady = false;
var streamMode = 'idle';      /* idle | dash | inspector */
var inspectorPending = false; /* true mezi Activate a start_stream ACK */
var diagMode = 'mode01';
var supportedPids = [], streamPids = [12, 13, 5];
var pidNames = {};
var webStart = Date.now(), pingSent = 0;

/* ================================================================ */
/*  Pomocne funkce pro formatovani PID identifikatoru               */
/*                                                                  */
/*  Server posila PIDy jako cisla (uint8_t), ale kvuli ladeni a    */
/*  zpetnemu kompatibilite akceptujeme i hex stringy ("0x0C").     */
/*  Vsechna mista, ktera potrebuji formatovat PID, MUSI projit     */
/*  pres tyto helpery, jinak hrozi bug typu "0x0X01" (toString(16) */
/*  na stringu vrati puvodni string a toUpperCase() z 'x' udela    */
/*  'X' → vznikne nesmysl).                                         */
/* ================================================================ */

/* Normalizace PID na cislo. parseInt umi prefix "0x" automaticky.
   12        → 12
   "0x0C"    → 12
   "0X0C"    → 12 (pristrasovany pripad — kontaminovany hex string) */
function pidToInt(p) {
  if (typeof p === 'number') return p & 0xFF;
  return parseInt(p) & 0xFF;
}

/* Format "0xNN" pro UI. Vzdy 2 znaky, uppercase, s prefixem.
   12 → "0x0C", "0x0C" → "0x0C", 0x34 → "0x34" */
function pidToHex(p) {
  return '0x' + pidToInt(p).toString(16).toUpperCase().padStart(2, '0');
}

/* Plny popisek PID. Kdyz je PID v PID_INFO, prilozi popisek;
   jinak vrati jen hex.
   12 → "PID 0x0C - Engine RPM"
   0x34 → "PID 0x34" (neni v PID_INFO) */
function pidLabel(p) {
  var n = pidToInt(p);
  var hex = pidToHex(n);
  var info = PID_INFO[n];
  return info ? ('PID ' + hex + ' - ' + info.n) : ('PID ' + hex);
}

function setStreamMode(mode) {
  streamMode = mode || 'idle';
  var el = document.getElementById('streamModeTxt');
  if (el) {
    el.className = 'stream-mode ' + (streamMode === 'dash' ? 'dash' : (streamMode === 'inspector' ? 'inspector' : ''));
    el.textContent = streamMode === 'dash' ? 'DASH Stream' :
                     streamMode === 'inspector' ? 'PID Inspector' : 'Idle';
  }
  updateStreamBtn();
  updatePidsSelectCount();
}

function diagSummary(diag) {
  if (!diag) return '';
  var parts = [];
  if (diag.raw !== undefined) parts.push('raw=' + diag.raw);
  if (diag.raw_len !== undefined) parts.push('len=' + diag.raw_len);
  if (diag.tx_id) parts.push('tx=' + diag.tx_id);
  if (diag.rx_id) parts.push('rx=' + diag.rx_id);
  if (diag.obd_status) parts.push('obd=' + diag.obd_status);
  if (diag.isotp_status) parts.push('isotp=' + diag.isotp_status);
  return parts.join(' ');
}

function formatDuration(sec) {
  sec = Math.max(0, Math.floor(sec || 0));
  var h = Math.floor(sec / 3600);
  var m = Math.floor((sec % 3600) / 60);
  var s = sec % 60;
  return h + ':' + (m<10?'0':'') + m + ':' + (s<10?'0':'') + s;
}

function updateEspTelemetry(r) {
  if (!r) return;
  if (r.free_heap !== undefined) {
    document.getElementById('heapTxt').textContent =
      'Heap: ' + (Number(r.free_heap)/1024).toFixed(0) + 'KB';
  }

  var uptimeSec = null;
  if (r.uptime_s !== undefined) uptimeSec = Number(r.uptime_s);
  else if (r.uptime_ms !== undefined) uptimeSec = Number(r.uptime_ms) / 1000;

  if (uptimeSec !== null && !isNaN(uptimeSec)) {
    document.getElementById('uptimeESP').textContent = 'E: ' + formatDuration(uptimeSec);
  }
}

function formatInitDiag(d) {
  if (!d) return '';
  var parts = [];
  if (d.twai_state !== undefined) parts.push('twai_state=' + d.twai_state);
  if (d.tec !== undefined || d.rec !== undefined) {
    var tec = (d.tec !== undefined) ? d.tec : '?';
    var rec = (d.rec !== undefined) ? d.rec : '?';
    parts.push('TEC=' + tec + ' REC=' + rec);
  }
  if (d.alerts) parts.push('alerts=' + d.alerts);
  if (d.attempts !== undefined) parts.push('attempts=' + d.attempts);
  if (d.last_tx_id) parts.push('tx=' + d.last_tx_id);
  if (d.last_rx_id) parts.push('rx=' + d.last_rx_id);
  if (d.last_isotp_status) parts.push('isotp=' + d.last_isotp_status);
  if (d.last_obd_status) parts.push('obd=' + d.last_obd_status);
  if (d.used_physical_fallback) parts.push('physical_fallback=1');
  if (d.reinit_performed) parts.push('reinit=1');
  return parts.join(' ');
}

function formatActiveEcu(ecu) {
  if (!ecu) return '';
  if (!ecu.bound) return 'active_ecu=unbound';
  return 'active_ecu=' + (ecu.tx_id || '?') + '->' + (ecu.rx_id || '?');
}

function formatProbeResponses(list) {
  if (!list || list.length === 0) return 'no ECU response';
  return list.map(function(r) {
    var mask = r.pid00_mask ? (' mask=' + r.pid00_mask) : '';
    return (r.rx_id || '?') + ': ' + (r.payload || '') + mask;
  }).join(' | ');
}

/* Rozsirena tabulka informaci o PIDech v rezimu 01 (Service $01).
   Zahrnuje nazev (n), jednotku (u) a volitelne mapovani stavu (vals). */
var PID_INFO = {
  1:{n:'Monitor status since DTCs cleared',u:''},
  2:{n:'Freeze frame DTC',u:''},
  3:{n:'Fuel system status',u:''},
  4:{n:'Calculated engine load',u:'%'},
  5:{n:'Engine coolant temperature',u:'\u00B0C'},
  6:{n:'Short term fuel trim—Bank 1',u:'%'},
  7:{n:'Long term fuel trim—Bank 1',u:'%'},
  8:{n:'Short term fuel trim—Bank 2',u:'%'},
  9:{n:'Long term fuel trim—Bank 2',u:'%'},
  10:{n:'Fuel pressure',u:'kPa'},
  11:{n:'Intake manifold absolute pressure',u:'kPa'},
  12:{n:'Engine RPM',u:'rpm'},
  13:{n:'Vehicle speed',u:'km/h'},
  14:{n:'Timing advance',u:'\u00B0'},
  15:{n:'Intake air temperature',u:'\u00B0C'},
  16:{n:'MAF air flow rate',u:'g/s'},
  17:{n:'Throttle position',u:'%'},
  18:{n:'Commanded secondary air status',u:''},
  19:{n:'Oxygen sensors present (2 banks)',u:''},
  28:{n:'OBD standards this vehicle conforms to',u:'',vals:{
    1:'OBD-II (CARB)', 2:'OBD (EPA)', 3:'OBD and OBD-II', 4:'OBD-I', 
    5:'Not intended to meet any OBD', 6:'EOBD (Europe)', 7:'EOBD and OBD-II', 
    8:'EOBD and OBD', 9:'EOBD, OBD and OBD-II', 10:'JOBD (Japan)', 
    11:'JOBD and OBD-II', 12:'JOBD and EOBD', 13:'JOBD, EOBD and OBD-II'
  }},
  31:{n:'Run time since engine start',u:'s'},
  33:{n:'Distance traveled with MIL on',u:'km'},
  35:{n:'Fuel Rail Pressure (vacuum)',u:'kPa'},
  47:{n:'Fuel Level Input',u:'%'},
  49:{n:'Distance traveled since codes cleared',u:'km'},
  51:{n:'Absolute Barometric Pressure',u:'kPa'},
  66:{n:'Control module voltage',u:'V'},
  67:{n:'Absolute load value',u:'%'},
  68:{n:'Commanded equivalence ratio',u:''},
  69:{n:'Relative throttle position',u:'%'},
  70:{n:'Ambient air temperature',u:'\u00B0C'},
  71:{n:'Absolute throttle position B',u:'%'},
  76:{n:'Commanded EGR',u:'%'},
  77:{n:'EGR Error',u:'%'},
  81:{n:'Fuel Type',u:'',vals:{
    1:'Gasoline', 2:'Methanol', 3:'Ethanol', 4:'Diesel', 5:'LPG', 6:'CNG', 
    7:'Propane', 8:'Electric', 9:'Bifuel (Gasoline)', 10:'Bifuel (Methanol)',
    11:'Bifuel (Ethanol)', 12:'Bifuel (LPG)', 13:'Bifuel (CNG)', 
    14:'Bifuel (Propane)', 15:'Bifuel (Electricity)', 16:'Bifuel (Electric/Combustion)',
    17:'Hybrid (Gasoline)', 18:'Hybrid (Methanol)', 19:'Hybrid (Ethanol)', 
    20:'Hybrid (Diesel)', 21:'Hybrid (Electric)', 22:'Hybrid (Electric/Combustion)',
    23:'Hybrid (Regenerative)', 24:'Dual Fuel (Diesel)'
  }},
  92:{n:'Engine oil temperature',u:'\u00B0C'},
  94:{n:'Fuel injection timing',u:'\u00B0'},
  95:{n:'Engine fuel rate',u:'L/h'},

  /* ---- Konvencni O2 senzory ($14-$1B): primary=napeti, secondary=STFT % ---- */
  20: {n:'O2 Sensor B1S1',u:'V', multi:['Voltage','STFT %']},
  21: {n:'O2 Sensor B1S2',u:'V', multi:['Voltage','STFT %']},
  22: {n:'O2 Sensor B1S3',u:'V', multi:['Voltage','STFT %']},
  23: {n:'O2 Sensor B1S4',u:'V', multi:['Voltage','STFT %']},
  24: {n:'O2 Sensor B2S1',u:'V', multi:['Voltage','STFT %']},
  25: {n:'O2 Sensor B2S2',u:'V', multi:['Voltage','STFT %']},
  26: {n:'O2 Sensor B2S3',u:'V', multi:['Voltage','STFT %']},
  27: {n:'O2 Sensor B2S4',u:'V', multi:['Voltage','STFT %']},

  /* ---- Sirokopasmove O2 ($24-$2B): primary=lambda, secondary=napeti V ---- */
  36: {n:'O2 Sensor B1S1 (wide)',u:'λ', multi:['lambda','V']},
  37: {n:'O2 Sensor B1S2 (wide)',u:'λ', multi:['lambda','V']},
  38: {n:'O2 Sensor B1S3 (wide)',u:'λ', multi:['lambda','V']},
  39: {n:'O2 Sensor B1S4 (wide)',u:'λ', multi:['lambda','V']},
  40: {n:'O2 Sensor B2S1 (wide)',u:'λ', multi:['lambda','V']},
  41: {n:'O2 Sensor B2S2 (wide)',u:'λ', multi:['lambda','V']},
  42: {n:'O2 Sensor B2S3 (wide)',u:'λ', multi:['lambda','V']},
  43: {n:'O2 Sensor B2S4 (wide)',u:'λ', multi:['lambda','V']},

  /* ---- Sirokopasmove O2 ($34-$3B): primary=lambda, secondary=proud mA ---- */
  52: {n:'O2 Sensor B1S1 (wide+I)',u:'λ', multi:['lambda','mA']},
  53: {n:'O2 Sensor B1S2 (wide+I)',u:'λ', multi:['lambda','mA']},
  54: {n:'O2 Sensor B1S3 (wide+I)',u:'λ', multi:['lambda','mA']},
  55: {n:'O2 Sensor B1S4 (wide+I)',u:'λ', multi:['lambda','mA']},
  56: {n:'O2 Sensor B2S1 (wide+I)',u:'λ', multi:['lambda','mA']},
  57: {n:'O2 Sensor B2S2 (wide+I)',u:'λ', multi:['lambda','mA']},
  58: {n:'O2 Sensor B2S3 (wide+I)',u:'λ', multi:['lambda','mA']},
  59: {n:'O2 Sensor B2S4 (wide+I)',u:'λ', multi:['lambda','mA']},

  /* ---- Teplota katalyzatoru ($3C-$3F) ---- */
  60: {n:'Catalyst temp B1S1',u:'°C'},
  61: {n:'Catalyst temp B2S1',u:'°C'},
  62: {n:'Catalyst temp B1S2',u:'°C'},
  63: {n:'Catalyst temp B2S2',u:'°C'},

  /* ---- Pridane skalary ($41-$5E) ---- */
  65: {n:'Monitor status this drive cycle',u:''},
  72: {n:'Absolute throttle position C',u:'%'},
  73: {n:'Accelerator pedal position D',u:'%'},
  74: {n:'Accelerator pedal position E',u:'%'},
  75: {n:'Accelerator pedal position F',u:'%'},
  79: {n:'Max EQ ratio / O2V / O2I / MAP',u:''},
  80: {n:'Max MAF air flow rate',u:'g/s'},
  82: {n:'Ethanol fuel percentage',u:'%'},
  83: {n:'Absolute evap vapor pressure',u:'kPa'},
  84: {n:'Evap vapor pressure (signed)',u:'Pa'},
  85: {n:'Short term sec O2 trim B1/B3',u:'%', multi:['B1','B3']},
  86: {n:'Long term sec O2 trim B1/B3',u:'%', multi:['B1','B3']},
  87: {n:'Short term sec O2 trim B2/B4',u:'%', multi:['B2','B4']},
  88: {n:'Long term sec O2 trim B2/B4',u:'%', multi:['B2','B4']},
  89: {n:'Fuel rail absolute pressure',u:'kPa'},
  90: {n:'Relative accelerator pedal position',u:'%'},
  91: {n:'Hybrid battery pack remaining life',u:'%'},

  /* ---- Wikipedia rozsireni: $61-$63 (kroutici moment) ---- */
  97:  {n:'Driver demand engine torque',u:'%'},
  98:  {n:'Actual engine torque',u:'%'},
  99:  {n:'Engine reference torque',u:'N·m'},

  /* ---- $66-$70: Diesel/turbo (RAW format na backendu — raw bajty) ---- */
  102: {n:'MAF sensor (dual)',u:'g/s'},
  103: {n:'Engine coolant temp (dual)',u:'°C'},
  104: {n:'Intake air temp sensor (dual)',u:'°C'},
  105: {n:'EGR (Actual / Commanded / Error)',u:''},
  106: {n:'Diesel intake air flow control',u:''},
  107: {n:'EGR temperature',u:'°C'},
  108: {n:'Throttle actuator control',u:''},
  109: {n:'Fuel pressure control system',u:''},
  110: {n:'Injection pressure control system',u:''},
  111: {n:'Turbocharger compressor inlet pressure',u:'kPa'},
  112: {n:'Boost pressure control',u:''},
  113: {n:'Variable Geometry turbo',u:''},
  114: {n:'Wastegate control',u:''},
  115: {n:'Exhaust pressure',u:'Pa'},
  116: {n:'Turbocharger RPM',u:'rpm'},
  117: {n:'Turbocharger temperature',u:'°C'},
  118: {n:'Turbocharger temperature (alt)',u:'°C'},
  119: {n:'Charge air cooler temperature',u:'°C'},

  /* ---- $78-$79: EGT 4-sensor (multi-value) ---- */
  120: {n:'EGT Bank 1',u:'°C', multi:['S1','S2','S3','S4']},
  121: {n:'EGT Bank 2',u:'°C', multi:['S1','S2','S3','S4']},

  /* ---- $7A-$7F ---- */
  122: {n:'DPF differential pressure',u:'Pa'},
  123: {n:'Diesel particulate filter status',u:''},
  124: {n:'DPF temperature',u:'°C'},
  125: {n:'NOx NTE control area status',u:''},
  126: {n:'PM NTE control area status',u:''},
  127: {n:'Engine run time (total)',u:'s'},

  /* ---- $81-$94: AECD, NOx, SCR ---- */
  129: {n:'AECD run time #1-#5',u:'s'},
  130: {n:'AECD run time #6-#10',u:'s'},
  131: {n:'NOx sensor concentration',u:'ppm', multi:['S1','S2','S3','S4']},
  132: {n:'Manifold surface temperature',u:'°C'},
  133: {n:'NOx reagent system',u:''},
  134: {n:'Particulate matter sensor',u:''},
  135: {n:'Intake manifold absolute pressure (extended)',u:'kPa'},
  136: {n:'SCR induction system',u:''},
  137: {n:'AECD run time #11-#15',u:'s'},
  138: {n:'AECD run time #16-#20',u:'s'},
  139: {n:'Diesel aftertreatment',u:''},
  140: {n:'O2 sensor (wide range)',u:''},
  141: {n:'Throttle position G',u:'%'},
  142: {n:'Engine friction torque',u:'%'},
  143: {n:'PM sensor Bank 1 & 2',u:''},
  144: {n:'WWH-OBD vehicle system info',u:'h'},
  145: {n:'WWH-OBD vehicle system info (alt)',u:'h'},
  146: {n:'Fuel system control',u:''},
  147: {n:'WWH-OBD counter support',u:'h'},
  148: {n:'NOx warning/inducement system',u:''},

  /* ---- $98-$9F ---- */
  152: {n:'EGT sensor (alt)',u:'°C', multi:['S1','S2','S3','S4']},
  153: {n:'EGT sensor (alt 2)',u:'°C', multi:['S1','S2','S3','S4']},
  154: {n:'Hybrid/EV battery voltage',u:'V'},
  155: {n:'Diesel exhaust fluid sensor',u:'%'},
  156: {n:'O2 sensor data (extended)',u:''},
  157: {n:'Engine fuel rate (4-byte)',u:'g/s'},
  158: {n:'Engine exhaust flow rate',u:'kg/h'},
  159: {n:'Fuel system percentage use',u:'%'},

  /* ---- $A1-$A9 ---- */
  161: {n:'NOx sensor corrected',u:'ppm', multi:['S1','S2','S3','S4']},
  162: {n:'Cylinder fuel rate',u:'mg/stroke'},
  163: {n:'Evap vapor pressure (wide)',u:'Pa'},
  164: {n:'Transmission actual gear',u:''},
  165: {n:'Diesel exhaust fluid dosing',u:'%'},
  166: {n:'Odometer',u:'km'},
  167: {n:'NOx sensor (sensors 3-4)',u:'ppm'},
  168: {n:'NOx corrected (sensors 3-4)',u:'ppm'},
  169: {n:'ABS disable switch state',u:''},

  /* ---- $C3-$C4 ---- */
  195: {n:'Fuel level input A/B',u:'%'},
  196: {n:'Particulate control diagnostic',u:''}
};

/* Kategorizace PIDu pro UI segmentaci. Naplni se z init odpovedi
   (telemetry_pids, status_pids, config_pids); server je autoritativni zdroj. */
var pidCategories = {
  telemetry: new Set(),
  status:    new Set(),
  config:    new Set()
};

/* Vehicle Info hodnoty z init.vehicle_info — staticka data o vozidle
   (OBD standard, typ paliva, max ranges). Zobrazeni v pruhu na HOME. */
var vehicleInfo = {};

/* DASH essential — kuratorsky seznam telemetry PIDu vhodnych pro zivou jizdu.
   Tyto PIDy se nastavi jako vychozi vyber pro stream po init. Bubliny v
   zalozce DASH se zobrazi pouze pro PIDy z teto mnoziny po pruniku se
   supportedPids. */
var DASH_ESSENTIAL_PIDS = [
  0x04, /* Calculated engine load */
  0x05, /* Engine coolant temp */
  0x06, /* STFT B1 */
  0x07, /* LTFT B1 */
  0x0B, /* MAP */
  0x0C, /* RPM */
  0x0D, /* Speed */
  0x0E, /* Timing advance */
  0x0F, /* IAT */
  0x10, /* MAF */
  0x11, /* Throttle position */
  0x1F, /* Run time since engine start */
  0x33, /* Absolute baro */
  0x42, /* Control module voltage */
  0x5F, /* Engine fuel rate, preferred when supported */
  0x5C  /* Engine oil temp */
];

/* Inspector ("PIDs select" v DIAG) — samostatny diagnosticky stream
   uzivatelem vybranych PIDu. Nekrmi DASH/Trip/GPS/recording logiku. */
var inspectorPids = [];      /* uint8 PIDy vybrane v "PIDs select" */
var inspectorActive = false; /* true = bezi PID Inspector stream */
var INSPECTOR_MAX = 4;       /* maximalni pocet soucasne aktivnich PIDu */

/* Buffer historie hodnot pro sparkline minigrafy.
   Pro kazdy PID se uchovava poslednich HIST_LEN (60) vzorku.
   Hodnoty se pridavaji pri kazdem prijetom stream paketu
   a nejstarsi se zahazuji, kdyz buffer prekroci limit. */
var HIST_LEN = 60;
var pidHistory = {};  /* pid -> [hodnota, hodnota, ...] */

/* Prida novou hodnotu do kruhoveho bufferu historie daneho PID.
   Pokud buffer prekroci HIST_LEN, nejstarsi vzorek se odebere. */
function pushHistory(pid, val) {
  if (!pidHistory[pid]) pidHistory[pid] = [];
  var h = pidHistory[pid];
  h.push(val);
  if (h.length > HIST_LEN) h.shift();
}

/* Vykresli sparkline minigraf na zadany canvas prvek.
   Vstupem je pole ciselnych hodnot. Funkce najde minimum a maximum
   pro normalizaci, pak vykresli vyplneny gradient pod carou
   a samotnou caru s azurovou barvou. Sparkline umoznuje uzivately
   videt trend hodnoty PID v case primo v bubline. */
function drawSparkline(canvas, data) {
  if (!canvas || data.length < 2) return;
  var ctx = canvas.getContext('2d');
  var w = canvas.width, h = canvas.height;
  ctx.clearRect(0, 0, w, h);

  var min = data[0], max = data[0];
  for (var i = 1; i < data.length; i++) {
    if (data[i] < min) min = data[i];
    if (data[i] > max) max = data[i];
  }
  var range = max - min || 1;
  var pad = 2;

  /* Vytvoreni linearniho gradientu pod carou grafu —
     pruhledna azurova nahoze, uplne pruhledna dole */
  var grad = ctx.createLinearGradient(0, 0, 0, h);
  grad.addColorStop(0, 'rgba(38,166,154,0.18)');
  grad.addColorStop(1, 'rgba(38,166,154,0)');

  ctx.beginPath();
  for (var i = 0; i < data.length; i++) {
    var x = (i / (data.length - 1)) * w;
    var y = h - pad - ((data[i] - min) / range) * (h - pad * 2);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  /* Uzavreni oblasti pro vyplneni gradientem */
  ctx.lineTo(w, h); ctx.lineTo(0, h); ctx.closePath();
  ctx.fillStyle = grad; ctx.fill();

  /* Vykresleni samotne cary sparkline grafu */
  ctx.beginPath();
  for (var i = 0; i < data.length; i++) {
    var x = (i / (data.length - 1)) * w;
    var y = h - pad - ((data[i] - min) / range) * (h - pad * 2);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.strokeStyle = '#26a69a';
  ctx.lineWidth = 1.5;
  ctx.stroke();
}

/* ================================================================ */
/*  Nahravani dat — zaznamenavani stream vzorku do pameti            */
/*  a jejich nasledny export jako CSV soubor                        */
/*                                                                  */
/*  recData je pole objektu {ts: casove razitko, d: {pid: hodnota}} */
/*  MAX_REC_ROWS omezuje pocet vzorku na ~18000 (cca 1 hodina       */
/*  pri frekvenci 5 Hz, zabira priblizne 1 MB v pameti prohlizece). */
/*  Po dosazeni limitu se nahravani automaticky zastavi.             */
/* ================================================================ */
var recording = false;
var recData = [];     /* [{ts:milisekundy, d:{pid:hodnota,...}}, ...] */
var recStart = 0;
var MAX_REC_ROWS = 18000; /* ~1h pri 5Hz, ~1MB v pameti */

function finishRecording(reason) {
  if (!recording) return;
  recording = false;
  var btn = document.getElementById('btnRec');
  if (btn) {
    btn.textContent = 'Record';
    btn.classList.remove('rec');
  }
  document.getElementById('btnExport').disabled = (recData.length === 0);
  syslog((reason || 'Recording stopped') + ': ' + recData.length + ' samples');
}

/* Prepinac nahravani — spusti nebo zastavi zaznamenavani vzorku.
   Pred spustenim je nutne mit aktivni stream. Pri zastaveni
   se odemkne tlacitko pro export CSV. */
function toggleRec() {
  if (!streaming || streamMode !== 'dash') { syslog('Start DASH stream first!'); return; }
  recording = !recording;
  var btn = document.getElementById('btnRec');
  if (recording) {
    recData = []; recStart = Date.now();
    btn.innerHTML = '<span class="rec-indicator"></span>Stop Rec';
    btn.classList.add('rec');
    document.getElementById('btnExport').disabled = true;
    syslog('Recording started');
  } else {
    finishRecording('Recording stopped');
  }
}

/* Ulozi jeden vzorek do nahravaci pameti.
   Vola se pri kazdem prijatem stream paketu.
   Pokud pocet vzorku dosahne MAX_REC_ROWS, nahravani se automaticky zastavi. */
function recordSample(d, ts) {
  if (!recording) return;
  if (recData.length >= MAX_REC_ROWS) {
    recording = false;
    document.getElementById('btnRec').textContent = 'Record';
    document.getElementById('btnRec').classList.remove('rec');
    document.getElementById('btnExport').disabled = false;
    syslog('Recording auto-stopped: max ' + MAX_REC_ROWS + ' samples');
    return;
  }
  recData.push({ts: ts || Date.now(), d: Object.assign({}, d)});
}

/* Export nahranich dat do CSV souboru.
   Postup:
   1. Sebere vsechny unikatni PID klice ze vsech vzorku
   2. Vytvori hlavicku s casovymi sloupci a pojmenovanymi PID sloupci
   3. Sestavi radky s hodnotami (prazdne kde PID nema hodnotu)
   4. Vytvori Blob s MIME typem text/csv a spusti stahovani
      pomoci docasneho <a> elementu s vygenerovanym nazvem souboru */
function exportCSV() {
  if (recData.length === 0) { syslog('No recorded data'); return; }

  /* Sber vsech unikatnich PID klicu napric vsemi vzorky */
  var keys = {};
  recData.forEach(function(r) { for (var k in r.d) keys[k] = true; });
  var pidKeys = Object.keys(keys).sort(function(a,b){ return parseInt(a)-parseInt(b); });

  /* Hlavicka CSV: casove razitko v ms, cas v sekundach + sloupce PID s nazvy */
  var header = 'time_ms,time_s';
  pidKeys.forEach(function(pid) {
    var pidNum = pidToInt(pid);
    var info = PID_INFO[pidNum];
    var name = info ? info.n.replace(/,/g, ' ') : ('PID_' + pidToHex(pidNum));
    header += ',' + name + ' (' + pidToHex(pidNum) + ')';
  });

  /* Sestaveni datovych radku — cas relativni k prvnimu vzorku */
  var lines = [header];
  recData.forEach(function(r) {
    var elapsed = r.ts - recData[0].ts;
    var row = elapsed + ',' + (elapsed/1000).toFixed(2);
    pidKeys.forEach(function(pid) {
      row += ',' + (r.d[pid] !== undefined ? r.d[pid] : '');
    });
    lines.push(row);
  });

  /* Vytvoreni Blob objektu a spusteni stahovani souboru */
  var csv = lines.join('\n');
  var blob = new Blob([csv], {type: 'text/csv'});
  var url = URL.createObjectURL(blob);
  var a = document.createElement('a');
  var now = new Date();
  a.href = url;
  a.download = 'obd2_log_' + now.toISOString().slice(0,19).replace(/[:-]/g,'') + '.csv';
  a.click();
  URL.revokeObjectURL(url);
  syslog('Exported ' + recData.length + ' rows as CSV');
}

/* ================================================================ */
/*  Nastaveni — parametry vozidla ukladane do localStorage          */
/*                                                                  */
/*  Pristup k persistenci:                                          */
/*  - Vsechna nastaveni se ukladaji jako jeden JSON objekt pod      */
/*    klicem 'obd2_settings' v localStorage prohlizece              */
/*  - Pri nacteni stranky se hodnoty obnovi z localStorage          */
/*    a aplikuji do vstupnich poli formulare                        */
/*  - Kazda zmena pole okamzite serializuje cely objekt zpet        */
/*  - Presets automaticky vyplni hustotu a AFR pri zmene typu paliva*/
/* ================================================================ */
var SETTINGS_KEY = 'obd2_settings';
var settings = {
  fuel_type: 'diesel', displacement: 2.0, fuel_density: 832,
  afr: 14.6, ve: 80, fuel_price: 1.50, currency: 'CZK'
};

/* Nacteni ulozemich nastaveni z localStorage.
   Pokud existuji, aplikuji se na vychozi objekt settings.
   Hodnoty se pote propisou do odpovidajicich vstupnich poli na strance. */
function loadSettings() {
  try {
    var saved = localStorage.getItem(SETTINGS_KEY);
    if (saved) {
      var parsed = JSON.parse(saved);
      for (var k in parsed) {
        if (settings.hasOwnProperty(k)) settings[k] = parsed[k];
      }
    }
  } catch(e) {}
  /* Aplikovani hodnot do vstupnich poli formulare */
  for (var k in settings) {
    var el = document.getElementById('set_' + k);
    if (el) el.value = settings[k];
  }
}

/* Ulozi zmenenou hodnotu jednoho nastaveni.
   Cisla se parsuje jako float, ostatni jako retezec.
   Cely objekt nastaveni se serializuje do localStorage. */
function saveSetting(el) {
  var key = el.id.replace('set_', '');
  if (el.type === 'number') settings[key] = parseFloat(el.value) || 0;
  else settings[key] = el.value;
  try { localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings)); } catch(e) {}
}

/* Prednastavene hodnoty hustoty paliva a stechiometrickeho pomeru
   vzduch/palivo (AFR) pro ruzne typy paliv */
var FUEL_PRESETS = {
  diesel:   {density: 832, afr: 14.6},
  hybrid_diesel: {density: 832, afr: 14.6},
  hybrid_gasoline: {density: 745, afr: 14.7},
  gasoline: {density: 745, afr: 14.7},
  lpg:      {density: 550, afr: 15.7},
  cng:      {density: 720, afr: 17.2}
};

/* Automaticke predvyplneni hustoty paliva a AFR pri zmene typu paliva.
   Listener se napoji pri nacteni stranky pomoci IIFE (okamzite volane funkce). */
(function() {
  var sel = document.getElementById('set_fuel_type');
  if (sel) sel.addEventListener('change', function() {
    var preset = FUEL_PRESETS[this.value];
    if (preset) {
      document.getElementById('set_fuel_density').value = preset.density;
      document.getElementById('set_afr').value = preset.afr;
      settings.fuel_density = preset.density;
      settings.afr = preset.afr;
      saveSetting(this);
    }
  });
})();

/* ================================================================ */
/*  Statistiky — vypocet spotreby paliva a statistik jizdy          */
/*                                                                  */
/*  Hlavni vzorec spotreby paliva (zalozeny na MAF senzoru):        */
/*    prutok_paliva [L/h] = (MAF [g/s] * 3.6) / (AFR * hustota)   */
/*  kde MAF je hmotnostni prutok nasavaneho vzduchu,                */
/*  AFR je stechiometricky pomer vzduch/palivo a hustota paliva     */
/*  se prepocitava z g/L na kg/L delenim 1000.                     */
/*                                                                  */
/*  Zarazeny prevodovy stupen se odhaduje z pomeru otacky/rychlost: */
/*    rpk = RPM / rychlost [km/h]                                  */
/*  Vyssi pomer = nizsi prevodovy stupen. Prahove hodnoty:          */
/*    >120: 1. stupen, >70: 2., >45: 3., >33: 4., >25: 5., jinak 6.*/
/*                                                                  */
/*  Zrychleni se pocita jako zmena rychlosti v m/s za cas:          */
/*    a = (v2 - v1) / 3.6 / dt   [m/s^2]                          */
/* ================================================================ */
var tripActive = false;
var tripData = {
  startTime: 0, totalFuel: 0, totalDist: 0,
  lastSpeed: 0, lastTime: 0, speedSum: 0, speedCount: 0,
  lastSpeedForAccel: -1, lastAccelTime: 0
};

/* Prepinac sledovani jizdy — spusti nebo zastavi kumulaci
   statistickych dat (spotreba, vzdalenost, prumerna rychlost) */
function toggleTrip() {
  if (!tripActive && streamMode === 'inspector') {
    syslog('Stop PID Inspector before starting Trip');
    return;
  }
  tripActive = !tripActive;
  var btn = document.getElementById('btnTrip');
  if (tripActive) {
    tripData.startTime = Date.now();
    tripData.lastTime = Date.now();
    btn.textContent = 'Stop Trip';
    btn.classList.add('on');
    syslog('Trip tracking started');
  } else {
    btn.textContent = 'Start Trip';
    btn.classList.remove('on');
    syslog('Trip tracking stopped');
  }
}

/* Resetuje vsechna data jizdy do vychoziho stavu
   a vymaze zobrazene hodnoty na kartach statistik */
function resetTrip() {
  tripActive = false;
  tripData = {startTime:0, totalFuel:0, totalDist:0,
    lastSpeed:0, lastTime:0, speedSum:0, speedCount:0,
    lastSpeedForAccel:-1, lastAccelTime:0};
  document.getElementById('btnTrip').textContent = 'Start Trip';
  document.getElementById('btnTrip').classList.remove('on');
  var ids = ['st_fc_inst','st_fc_100','st_fc_avg','st_fuel_used',
             'st_dist','st_cost','st_time','st_avg_spd','st_gear','st_accel'];
  ids.forEach(function(id) {
    var el = document.getElementById(id);
    var unit = el.querySelector('.stat-unit');
    el.innerHTML = '&mdash;' + (unit ? unit.outerHTML : '');
  });
  syslog('Trip data reset');
}

function scalarNumber(v) {
  if (v === undefined || v === null || Array.isArray(v)) return null;
  var n = Number(v);
  return isNaN(n) ? null : n;
}

function isCombustionEngineStopped(rpm) {
  return rpm !== null && rpm < 50;
}

/* Hlavni funkce aktualizace statistik — vola se pri kazdem stream paketu.
   Provadi vypocet okamzite spotreby, spotreby na 100 km, kumulaci
   celkoveho paliva a vzdalenosti, odhad prevodoveho stupne a zrychleni. */
function updateStats(d) {
  var now = Date.now();
  var dt = (now - tripData.lastTime) / 1000; /* sekundy od posledni aktualizace */
  if (dt <= 0 || dt > 5) { tripData.lastTime = now; return; }
  tripData.lastTime = now;

  var maf = scalarNumber(d['16']);        /* MAF prutok vzduchu g/s — PID 0x10 */
  var speed = scalarNumber(d['13']);      /* Rychlost vozidla km/h — PID 0x0D */
  var rpm = scalarNumber(d['12']);        /* Otacky motoru RPM — PID 0x0C */
  var load = scalarNumber(d['4']);        /* Zatizeni motoru % — PID 0x04 */
  var fuelRate = scalarNumber(d['95']);   /* Engine fuel rate L/h — PID 0x5F */
  var fuelMass = scalarNumber(d['157']);  /* Engine fuel rate g/s — PID 0x9D */
  var engineStopped = isCombustionEngineStopped(rpm);

  /* ---- Okamzity prutok paliva (L/h) ---- */
  /* Pro hybrid/EV rezim je klicove nezakladat spotrebu na MAF, pokud ECU
     hlasi RPM ~0. V tu chvili je spalovaci motor vypnuty a palivovy prutok
     ma byt 0 L/h i kdyz nektery odhadovy nebo zpozdeny signal jeste zustal.
     Pokud je dostupny primy PID 0x5F, pouzije se pred MAF odhadem.

     MAF fallback pouziva hmotnostni prutok vzduchu:
     prutok_paliva [L/h] = (MAF [g/s] * 3600) / (lambda * AFR * hustota [g/L])
     Lambda je predpokladana 1.0 (stechiometricka smes). U dieselu
     motor bezi typicky chude (lambda 1.3-2.0), ale MAF uz reflektuje
     skutecny prutok vzduchu, takze lambda=1 dava stechiometrickou
     hmotnost paliva. ECU ridi vstrikovani tak, aby odpovidal MAF. */
  var fuelFlow = null;
  if (engineStopped) {
    fuelFlow = 0;
  } else if (fuelRate !== null && fuelRate >= 0) {
    fuelFlow = fuelRate;
  } else if (fuelMass !== null && fuelMass >= 0 && settings.fuel_density > 0) {
    fuelFlow = (fuelMass * 3600) / settings.fuel_density;
  } else if (maf !== null && maf > 0) {
    fuelFlow = (maf * 3600) / (settings.afr * settings.fuel_density);
  } else {
    var map = scalarNumber(d['11']); // kPa
    var iat = scalarNumber(d['15']); // °C
    if (map !== null && iat !== null && rpm !== null && rpm > 0) {
        /* Metoda Speed-Density (vypocet airflow z tlaku, teploty a otacek).
           Vyuziva stavovou rovnici idealniho plynu a objemovou ucinnost (VE).
           Idealni pro vozy bez MAF senzoru. */
        var kelvin = iat + 273.15;
        var displacement_m3 = (settings.displacement || 2.0) / 1000;
        var ve_decimal = (settings.ve || 80) / 100;
        /* Airflow [g/s] = (MAP * 1000 * V * RPM * VE * MolarMass) / (120 * R * T) */
        var maf_calc = (map * 1000 * displacement_m3 * rpm * ve_decimal * 28.97) / (120 * 8.314 * kelvin);
        fuelFlow = (maf_calc * 3600) / (settings.afr * settings.fuel_density);
    } else if (load !== null && rpm !== null && rpm > 0) {
        /* Nejmene presna zaloha: odhad ze zatizeni motoru (Load). */
        var airFlow = (load / 100) * (settings.displacement / 1000) * (rpm / 120) * 1.225;
        fuelFlow = (airFlow * 3.6) / (settings.afr * (settings.fuel_density / 1000));
    }
  }

  /* ---- Zobrazeni okamziteho prutoku paliva ---- */
  if (fuelFlow !== null) {
    setStatVal('st_fc_inst', fuelFlow.toFixed(2), 'L/h');
  }

  /* ---- Spotreba v L/100km ---- */
  /* Prepocet z L/h na L/100km: (prutok * 100) / rychlost.
     Pri rychlosti pod 3 km/h se nezobrazuje (deleni nulou, nesmyslna hodnota). */
  if (fuelFlow !== null && speed !== null && speed > 3) {
    var lPer100 = (fuelFlow * 100) / speed;
    if (lPer100 > 99) lPer100 = 99;
    setStatVal('st_fc_100', lPer100.toFixed(1), 'L/100km');
  } else if (speed !== null && speed <= 3 && fuelFlow !== null) {
    setStatVal('st_fc_100', '---', 'L/100km');
  }

  /* ---- Kumulace dat jizdy (trip) ---- */
  if (tripActive) {
    /* Spotrebovane palivo — integrace prutoku v case (L/h -> L/s * dt) */
    if (fuelFlow !== null) {
      tripData.totalFuel += (fuelFlow / 3600) * dt;
    }

    /* Ujeta vzdalenost — integrace rychlosti v case (km/h -> m/s * dt = metry) */
    if (speed !== null) {
      tripData.totalDist += (speed / 3.6) * dt;
      tripData.speedSum += speed;
      tripData.speedCount++;
    }

    /* Zobrazeni hodnot jizdy na kartach statistik */
    var distKm = tripData.totalDist / 1000;
    setStatVal('st_fuel_used', tripData.totalFuel.toFixed(3), 'L');
    setStatVal('st_dist', distKm.toFixed(2), 'km');

    /* Prumerna spotreba — celkove palivo / vzdalenost * 100 */
    if (distKm > 0.05) {
      var avgCons = (tripData.totalFuel / distKm) * 100;
      setStatVal('st_fc_avg', avgCons.toFixed(1), 'L/100km');
    }

    /* Naklady na palivo — celkove spotrebovane litry * cena za litr */
    var cost = tripData.totalFuel * settings.fuel_price;
    setStatVal('st_cost', cost.toFixed(2), ' ' + settings.currency);

    /* Cas jizdy — prepocet sekund na format HH:MM:SS */
    var elapsed = (now - tripData.startTime) / 1000;
    var mm = Math.floor(elapsed / 60);
    var ss = Math.floor(elapsed % 60);
    var hh = Math.floor(mm / 60);
    mm = mm % 60;
    setStatVal('st_time', (hh ? hh + 'h ' : '') + mm + 'm ' + ss + 's', '');

    /* Prumerna rychlost — soucet vsech rychlosti / pocet mereni */
    if (tripData.speedCount > 0) {
      setStatVal('st_avg_spd', (tripData.speedSum / tripData.speedCount).toFixed(1), 'km/h');
    }
  }

  /* ---- Odhad zarazeneho prevodoveho stupne ---- */
  /* Algorimus pouziva pomer otacky/rychlost (rpk = RPM / km/h).
     Vyssi hodnota rpk znamena nizsi prevodovy stupen.
     Prahove hodnoty jsou priblizne a mohou se lisit podle vozidla.
     Odhad funguje pouze pri otackach > 500 a rychlosti > 5 km/h. */
  if (rpm !== null && speed !== null && rpm > 500 && speed > 5) {
    var rpk = rpm / speed;
    var gear;
    if      (rpk > 120) gear = 1;
    else if (rpk > 70)  gear = 2;
    else if (rpk > 45)  gear = 3;
    else if (rpk > 33)  gear = 4;
    else if (rpk > 25)  gear = 5;
    else                 gear = 6;
    setStatVal('st_gear', gear.toString(), '');
  }

  /* ---- Vypocet zrychleni ---- */
  /* Zrychleni se pocita jako zmena rychlosti (prevedena z km/h na m/s)
     delena casovym intervalem: a = ((v2 - v1) / 3.6) / dt [m/s^2].
     Interval musi byt mezi 0.1 a 3 sekundami pro vylouceni
     chybnych hodnot pri prestce nebo dlouhem vpadku dat. */
  if (speed !== null) {
    if (tripData.lastSpeedForAccel >= 0) {
      var aDt = (now - tripData.lastAccelTime) / 1000;
      if (aDt > 0.1 && aDt < 3) {
        var accel = ((speed - tripData.lastSpeedForAccel) / 3.6) / aDt;
        setStatVal('st_accel', accel.toFixed(2), 'm/s\u00B2');
      }
    }
    tripData.lastSpeedForAccel = speed;
    tripData.lastAccelTime = now;
  }
}

/* Pomocna funkce pro nastaveni hodnoty a jednotky na statisticke karte */
function setStatVal(id, val, unit) {
  var el = document.getElementById(id);
  if (!el) return;
  el.innerHTML = val + (unit ? '<span class="stat-unit">' + unit + '</span>' : '');
}

/* ================================================================ */
/*  Mapa — GPS + OBD + Akcelerometr fuze a vizualizace              */
/*                                                                  */
/*  Multi-senzorovy pipeline:                                       */
/*    1. Accuracy gate (>100m = zahodit)                            */
/*    2. Adaptivni Kalman filtr (Q skalovano dle rychlosti)         */
/*    3. Dead Reckoning validace (kontrola fyzicke proveditelnosti) */
/*    4. Anti-jitter (<1.5m = zahodit)                              */
/*    5. OBD snapshot pripojeny ke kazdemu bodu                     */
/*  Vizualizace: canvas + klikatelne body s OBD popup.              */
/*  Export: GPX 1.1 + standalone HTML s Leaflet mapou.              */
/* ================================================================ */
var gpsActive = false;
var gpsWatchId = null;
var gpsTrack = [];        /* [{lat,lon,ts,speed,alt,acc,moving,obd},...] */
var gpsTotalDist = 0;
var lastStreamData = {};  /* Posledni OBD stream data pro GPS snapshot */
var gpsPointCount = 0;    /* Celkovy pocet prijatych GPS bodu (vcetne filtrovanych) */

/* Kalman filtr 1D — instance pro lat a lon.
   Accuracy z GPS se pouziva jako measurement noise (R).
   Procesni sum (Q) se nastavuje dynamicky podle rychlosti vozidla.
   Vyssi Q = filtr vice sleduje nove mereni (pouzit pri jizde).
   Nizsi Q = filtr vice verí predchozimu odhadu (pouzit pri stani). */
function KalmanFilter() {
  this.Q = 0.00001; this.R = 1; this.P = 1; this.X = null; this.K = 0;
}
KalmanFilter.prototype.filter = function(m, acc, dynamicQ) {
  if (this.X === null) { this.X = m; this.P = acc * acc; return this.X; }
  this.R = acc * acc;
  var q = (dynamicQ !== undefined) ? dynamicQ : this.Q;
  this.P += q;
  this.K = this.P / (this.P + this.R);
  this.X += this.K * (m - this.X);
  this.P *= (1 - this.K);
  return this.X;
};
KalmanFilter.prototype.reset = function() { this.X = null; this.P = 1; };
var kalmanLat = new KalmanFilter();
var kalmanLon = new KalmanFilter();

/* ================================================================ */
/*  Akcelerometr — orientation-agnostic detekce pohybu              */
/*  Magnitude accelerationIncludingGravity je invariantni vuci      */
/*  otoceni telefonu (~9.81 v klidu). Variance za ~1s rozlisuje     */
/*  stoji (var<0.3) vs. jede (var>0.3, vibrace motoru/silnice).     */
/* ================================================================ */
var motionBuffer = [];
var MOTION_WINDOW = 50;
var isVehicleMoving = false;
var motionVariance = 0;
var motionAvailable = false;

function onDeviceMotion(event) {
  var a = event.accelerationIncludingGravity;
  if (!a || a.x === null) return;
  motionAvailable = true;
  var mag = Math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
  motionBuffer.push(mag);
  if (motionBuffer.length > MOTION_WINDOW) motionBuffer.shift();
  if (motionBuffer.length >= MOTION_WINDOW) {
    var sum = 0, i;
    for (i = 0; i < motionBuffer.length; i++) sum += motionBuffer[i];
    var mean = sum / motionBuffer.length;
    var vSum = 0;
    for (i = 0; i < motionBuffer.length; i++) {
      var diff = motionBuffer[i] - mean; vSum += diff * diff;
    }
    motionVariance = vSum / motionBuffer.length;
    isVehicleMoving = motionVariance > 0.3;
  }
}

function startMotionDetector() {
  if (typeof DeviceMotionEvent === 'undefined') return;
  if (typeof DeviceMotionEvent.requestPermission === 'function') {
    DeviceMotionEvent.requestPermission().then(function(r) {
      if (r === 'granted') window.addEventListener('devicemotion', onDeviceMotion);
    }).catch(function() {});
  } else {
    window.addEventListener('devicemotion', onDeviceMotion);
  }
}

function toggleGps() {
  if (!navigator.geolocation) { syslog('Geolocation not supported'); return; }
  if (!gpsActive && streamMode === 'inspector') {
    syslog('Stop PID Inspector before starting GPS');
    return;
  }
  gpsActive = !gpsActive;
  var btn = document.getElementById('btnGps');
  if (gpsActive) {
    btn.textContent = 'Stop GPS'; btn.classList.add('on');
    document.getElementById('gpsStatus').textContent = 'Acquiring...';
    kalmanLat.reset(); kalmanLon.reset();
    gpsPointCount = 0;
    gpsWatchId = navigator.geolocation.watchPosition(
      onGpsPosition, onGpsError,
      {enableHighAccuracy: true, maximumAge: 0, timeout: 10000}
    );
    startMotionDetector();
    syslog('GPS started (Adaptive Kalman + Dead Reckoning + OBD fusion)');
  } else {
    btn.textContent = 'Start GPS'; btn.classList.remove('on');
    if (gpsWatchId !== null) { navigator.geolocation.clearWatch(gpsWatchId); gpsWatchId = null; }
    window.removeEventListener('devicemotion', onDeviceMotion);
    motionBuffer = []; motionAvailable = false;
    document.getElementById('gpsStatus').textContent = 'Stopped';
    syslog('GPS stopped (' + gpsTrack.length + ' pts, ' + gpsPointCount + ' raw)');
  }
}

/* GPS callback — adaptivni Kalman filtr se senzorovou fuzi.
   Pouziva rychlost vozidla (OBD PID 0x0D nebo GPS speed) pro dynamicke
   skalovani procesniho sumu (Q) Kalmanova filtru a pro Dead Reckoning
   validaci fyzicke proveditelnosti skoku. */
function onGpsPosition(pos) {
  var rawLat = pos.coords.latitude;
  var rawLon = pos.coords.longitude;
  var acc = pos.coords.accuracy || 20;
  var ts = pos.timestamp || Date.now();
  var gpsSpd = pos.coords.speed;  /* GPS rychlost v m/s, muze byt null */
  var obdSpd = (lastStreamData['13'] !== undefined) ? parseFloat(lastStreamData['13']) : null;
  gpsPointCount++;

  /* 1. Brana presnosti — body s acc > 100m jsou nepouzitelne */
  if (acc > 100) {
    document.getElementById('gpsStatus').textContent =
      'Signal too weak (' + Math.round(acc) + 'm)';
    return;
  }

  /* 2. Urceni nejlepsi dostupne rychlosti pro filtrovani.
     Priorita: OBD rychlost > GPS rychlost > 0 (predpoklad stani).
     OBD rychlost je presnejsi (primy odecet z kola/prevodovky),
     GPS rychlost je Doppler-based a muze byt nepresna pri stani. */
  var speedMs = 0; /* rychlost v m/s */
  if (obdSpd !== null) {
    speedMs = obdSpd / 3.6;
  } else if (gpsSpd !== null && gpsSpd >= 0) {
    speedMs = gpsSpd;
  }

  /* 3. Vypocet dynamickeho procesniho sumu (Q).
     Q urcuje, jak rychle filtr reaguje na nove mereni:
     - Nizke Q (stani): filtr ignoruje GPS skoky, poloha se hybe minimalne.
     - Vysoke Q (jizda): filtr sleduje GPS presne, vcetne zatacek.
     Skalovani je kvadraticke (speed^2) protoze v zatackach
     se poloha meni rychleji nez pri jizde primo. */
  var qBase;
  if (speedMs < 0.5) {
    /* Auto stoji — zamknout polohu. Velky vliv ma i accuracy:
       cim horsi signal, tim mene filtru verime novym datum. */
    qBase = 0.0000001;
  } else if (speedMs < 5) {
    /* Pomala jizda (do ~18 km/h) — jemne sledovani */
    qBase = 0.00001 * speedMs;
  } else {
    /* Normalni/rychla jizda — Q roste s rychlosti aby filtr
       stíhal zatacky. Koeficient 0.00005 je kalibrovany tak,
       aby pri 50 km/h (14 m/s) bylo Q ~ 0.0007 a Kalman gain
       pri accuracy 30m (R=900) byl K ~ 0.0007/900.0007 ~ 0.0008,
       coz odpovida posunu ~7m smerem k mereni za jeden krok. */
    qBase = 0.00005 * speedMs;
  }

  /* 4. Aplikace Kalmanova filtru s dynamickym Q.
     Prvnich 5 bodu prijmeme s minimalnim filtrovanim (warm-up faze),
     protoze filtr potrebuje nekolik mereni aby se ustálil. */
  var fLat, fLon;
  if (gpsPointCount <= 5) {
    /* Warm-up: pouzij vysoke Q aby filtr rychle konvergoval k realne poloze */
    fLat = kalmanLat.filter(rawLat, acc, 0.01);
    fLon = kalmanLon.filter(rawLon, acc, 0.01);
  } else {
    fLat = kalmanLat.filter(rawLat, acc, qBase);
    fLon = kalmanLon.filter(rawLon, acc, qBase);
  }

  /* 5. Dead Reckoning — kontrola fyzicke proveditelnosti skoku.
     Porovname vzdalenost skoku s maximalnim moznym presunem
     na zaklade rychlosti a casu. Tim odfiltrujeme GPS outliers
     ktere by jinak prosly Kalmanovym filtrem. */
  if (gpsTrack.length > 0) {
    var prev = gpsTrack[gpsTrack.length - 1];
    var dt = (ts - prev.ts) / 1000;
    var dist = haversine(prev.lat, prev.lon, fLat, fLon);

    if (dt > 0 && dt < 30) {
      /* Maximalni mozna vzdalenost = rychlost * cas + bezpecnostni rezerva.
         Rezerva pokryva: zrychleni/brzdeni, nepresnost OBD, GPS sumeni.
         Pri stani (speedMs < 1): povolime max 5m (GPS jitter).
         Pri jizde: povolime 1.5x rychlost * cas + 15m. */
      var maxDist;
      if (speedMs < 1) {
        maxDist = 5 + acc * 0.3;
      } else {
        maxDist = speedMs * dt * 1.5 + 15 + acc * 0.2;
      }
      if (dist > maxDist) {
        document.getElementById('gpsStatus').textContent =
          'Outlier (' + Math.round(dist) + 'm>' + Math.round(maxDist) + 'm)';
        return;
      }
    }

    /* Anti-jitter — neukladat body pokud jsme se temer nepohli */
    if (dist < 1.5) return;
    gpsTotalDist += dist;
  }

  var pt = {
    lat: fLat, lon: fLon, ts: ts,
    speed: gpsSpd, alt: pos.coords.altitude,
    acc: Math.round(acc), moving: isVehicleMoving, obd: {}
  };
  for (var k in lastStreamData) pt.obd[k] = lastStreamData[k];

  gpsTrack.push(pt);

  /* Vizualizace stavu GPS — slovni hodnoceni duveryhodnosti signalu
     a barevna indikace (zelena/oranzova) */
  var statusEl = document.getElementById('gpsStatus');
  var confidence = acc < 15 ? 'Excellent' : (acc < 30 ? 'Good' : 'Poor');
  var color = acc < 30 ? 'var(--ok)' : 'var(--warn)';
  var icon = motionAvailable ? (isVehicleMoving ? ' \uD83D\uDE97' : ' \uD83C\uDD7F\uFE0F') : '';
  var spdTxt = obdSpd !== null ? (' | OBD:' + Math.round(obdSpd)) : '';
  statusEl.innerHTML = '<span style="color:' + color + '">' + confidence +
    ' (' + Math.round(acc) + 'm)</span>' + icon + spdTxt;

  document.getElementById('gpsPoints').textContent = gpsTrack.length;
  document.getElementById('gpsLat').textContent = pt.lat.toFixed(6);
  document.getElementById('gpsLon').textContent = pt.lon.toFixed(6);
  document.getElementById('gpsDist').textContent =
    gpsTotalDist > 1000 ? (gpsTotalDist/1000).toFixed(2)+' km' : Math.round(gpsTotalDist)+' m';

  if (pt.speed !== null && pt.speed !== undefined)
    document.getElementById('gpsSpd').textContent = (pt.speed*3.6).toFixed(1)+' km/h';

  document.getElementById('gpsAcc').textContent = pt.acc + ' m';

  document.getElementById('btnGpxExport').disabled = (gpsTrack.length < 2);
  document.getElementById('btnMapExport').disabled = (gpsTrack.length < 2);
  drawMap();
}

function onGpsError(err) {
  document.getElementById('gpsStatus').textContent = 'Error: ' + err.message;
  syslog('GPS error: ' + err.message);
}

/* Haversinuv vzorec — vypocet vzdalenosti dvou GPS bodu v metrech.
   Vzorec bere v uvahu zakriveni Zeme pomoci polomerem R = 6371000 m.
   Vstup: zemepisna sirka a delka obou bodu ve stupnich.
   Vystup: vzdalenost v metrech. */
function haversine(lat1, lon1, lat2, lon2) {
  var R = 6371000;
  var dLat = (lat2 - lat1) * Math.PI / 180;
  var dLon = (lon2 - lon1) * Math.PI / 180;
  var a = Math.sin(dLat/2) * Math.sin(dLat/2) +
          Math.cos(lat1 * Math.PI/180) * Math.cos(lat2 * Math.PI/180) *
          Math.sin(dLon/2) * Math.sin(dLon/2);
  return R * 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
}

/* Vykresleni GPS trasy na canvas — ekvirektangularni projekce s barvami rychlosti.
   Postup:
   1. Zjisti hranice (min/max lat/lon) vsech bodu trasy
   2. Prida odsazeni (padding) kolem hranic
   3. Koriguje pomer stran podle cosinu stredni zemepisne sirky
      (na vysokych sirkach je stupen delky kratsi nez stupen sirky)
   4. Vypocte meritko tak, aby se trasa vesla do canvasu
   5. Vykresli segmenty s barevnym prechodem podle rychlosti:
      zelena (pomala) -> azurova (stredni) -> cervena (rychla)
   6. Nakresli zeleny bod na zacatek a azurovy bod na konec trasy
   7. Zobrazi meritkovou listu a legendu barev rychlosti */
function drawMap() {
  var canvas = document.getElementById('mapCanvas');
  if (!canvas || gpsTrack.length < 2) return;
  var ctx = canvas.getContext('2d');

  /* Nastaveni rozliseni canvasu podle velikosti zobrazeni a DPI displeje */
  var rect = canvas.getBoundingClientRect();
  canvas.width = rect.width * (window.devicePixelRatio || 1);
  canvas.height = rect.height * (window.devicePixelRatio || 1);
  ctx.scale(window.devicePixelRatio || 1, window.devicePixelRatio || 1);
  var w = rect.width, h = rect.height;

  ctx.clearRect(0, 0, w, h);

  /* Zjisteni geografickych hranic trasy (bounding box) */
  var minLat = gpsTrack[0].lat, maxLat = gpsTrack[0].lat;
  var minLon = gpsTrack[0].lon, maxLon = gpsTrack[0].lon;
  for (var i = 1; i < gpsTrack.length; i++) {
    if (gpsTrack[i].lat < minLat) minLat = gpsTrack[i].lat;
    if (gpsTrack[i].lat > maxLat) maxLat = gpsTrack[i].lat;
    if (gpsTrack[i].lon < minLon) minLon = gpsTrack[i].lon;
    if (gpsTrack[i].lon > maxLon) maxLon = gpsTrack[i].lon;
  }

  /* Pridani odsazeni (15%) kolem hranic pro lepsi vizualni vysledek */
  var latRange = maxLat - minLat || 0.001;
  var lonRange = maxLon - minLon || 0.001;
  var pad = 0.15;
  minLat -= latRange * pad; maxLat += latRange * pad;
  minLon -= lonRange * pad; maxLon += lonRange * pad;
  latRange = maxLat - minLat;
  lonRange = maxLon - minLon;

  /* Korekce pomeru stran zavisle na zemepisne sirce —
     na vyssich sirkach je stupen delky kratsi (cos efekt) */
  var midLat = (minLat + maxLat) / 2;
  var cosLat = Math.cos(midLat * Math.PI / 180);
  var scaledLonRange = lonRange * cosLat;

  /* Vypocet meritka tak, aby se trasa vesla do canvasu se zachovanim pomeru stran */
  var scaleX = w / scaledLonRange;
  var scaleY = h / latRange;
  var scale = Math.min(scaleX, scaleY);
  var offX = (w - scaledLonRange * scale) / 2;
  var offY = (h - latRange * scale) / 2;

  function toX(lon) { return offX + (lon - minLon) * cosLat * scale; }
  function toY(lat) { return h - offY - (lat - minLat) * scale; }
  mapToX = toX; mapToY = toY; /* Ulozit pro click handler */

  /* Zjisteni maximalni rychlosti pro normalizaci barevneho mapovani */
  var maxSpd = 1;
  for (var i = 0; i < gpsTrack.length; i++) {
    var s = gpsTrack[i].speed;
    if (s !== null && s !== undefined && s > maxSpd) maxSpd = s;
  }

  /* Vykresleni segmentu trasy s barvou podle rychlosti */
  ctx.lineWidth = 3;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  for (var i = 1; i < gpsTrack.length; i++) {
    var spd = gpsTrack[i].speed || 0;
    var ratio = Math.min(1, spd / maxSpd);
    /* Barevny prechod: zelena (pomala) -> azurova (stredni) -> cervena (rychla) */
    var r, g, b;
    if (ratio < 0.5) {
      r = 0; g = Math.round(230 * (1 - ratio * 2) + 229 * ratio * 2);
      b = Math.round(229 * ratio * 2);
    } else {
      var t = (ratio - 0.5) * 2;
      r = Math.round(244 * t); g = Math.round(229 * (1 - t));
      b = Math.round(255 * (1 - t));
    }
    ctx.strokeStyle = 'rgb(' + r + ',' + g + ',' + b + ')';
    ctx.beginPath();
    ctx.moveTo(toX(gpsTrack[i-1].lon), toY(gpsTrack[i-1].lat));
    ctx.lineTo(toX(gpsTrack[i].lon), toY(gpsTrack[i].lat));
    ctx.stroke();
  }

  /* Znacka zacatku trasy (zeleny kruh) */
  ctx.beginPath();
  ctx.arc(toX(gpsTrack[0].lon), toY(gpsTrack[0].lat), 5, 0, Math.PI * 2);
  ctx.fillStyle = '#00e676';
  ctx.fill();

  /* Znacka konce trasy (azurovy kruh) */
  var last = gpsTrack[gpsTrack.length - 1];
  ctx.beginPath();
  ctx.arc(toX(last.lon), toY(last.lat), 5, 0, Math.PI * 2);
  ctx.fillStyle = '#26a69a';
  ctx.fill();

  /* Meritkova lista v levem dolnim rohu — zobrazuje vzdalenost v m nebo km */
  var scaleBarM = calcScaleBar(latRange, h, scale);
  if (scaleBarM) {
    var barPx = scaleBarM.meters / (latRange / h * 111320);
    var barX = 10, barY = h - 15;
    ctx.strokeStyle = '#666';
    ctx.lineWidth = 2;
    ctx.beginPath();
    ctx.moveTo(barX, barY); ctx.lineTo(barX + barPx, barY);
    ctx.moveTo(barX, barY - 4); ctx.lineTo(barX, barY + 4);
    ctx.moveTo(barX + barPx, barY - 4); ctx.lineTo(barX + barPx, barY + 4);
    ctx.stroke();
    ctx.fillStyle = '#888';
    ctx.font = '11px sans-serif';
    ctx.fillText(scaleBarM.label, barX + barPx / 2 - 10, barY - 6);
  }

  /* Legenda barev rychlosti v pravem hornim rohu */
  ctx.font = '10px sans-serif';
  ctx.fillStyle = '#00e676'; ctx.fillText('slow', w - 80, 15);
  ctx.fillStyle = '#26a69a'; ctx.fillText('mid', w - 52, 15);
  ctx.fillStyle = '#f44336'; ctx.fillText('fast', w - 26, 15);
}

/* Vypocet vhodne delky meritkove listy.
   Z rozsahu zemepisne sirky a vysky canvasu urcime metry na pixel,
   pak vybereme nejblizsi "peknou" hodnotu (10, 20, 50, 100, ... 10000 m)
   tak, aby lista merila priblizne 80 pixelu. */
function calcScaleBar(latRange, canvasH, scale) {
  var metersPerPx = (latRange * 111320) / canvasH;
  var targetPx = 80;
  var targetM = metersPerPx * targetPx;
  var nice = [10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000];
  for (var i = 0; i < nice.length; i++) {
    if (nice[i] >= targetM * 0.5) {
      var label = nice[i] >= 1000 ? (nice[i]/1000) + ' km' : nice[i] + ' m';
      return {meters: nice[i], label: label};
    }
  }
  return null;
}

function clearTrack() {
  gpsTrack = []; gpsTotalDist = 0;
  kalmanLat.reset(); kalmanLon.reset();
  document.getElementById('gpsPoints').textContent = '0';
  document.getElementById('gpsDist').textContent = '\u2014';
  document.getElementById('gpsAcc').textContent = '\u2014';
  document.getElementById('btnGpxExport').disabled = true;
  document.getElementById('btnMapExport').disabled = true;
  var canvas = document.getElementById('mapCanvas');
  if (canvas) { canvas.getContext('2d').clearRect(0, 0, canvas.width, canvas.height); }
  hidePointPopup();
  syslog('GPS track cleared');
}

/* GPX export s OBD daty v extensions */
function exportGpx() {
  if (gpsTrack.length < 2) { syslog('No GPS data'); return; }
  var gpx = '<?xml version="1.0" encoding="UTF-8"?>\n';
  gpx += '<gpx version="1.1" creator="OBD2-ESP32-Dashboard"\n';
  gpx += '  xmlns="http://www.topografix.com/GPX/1/1"\n';
  gpx += '  xmlns:obd="http://obd2-esp32/gpx/ext/1">\n';
  gpx += '  <trk><name>OBD2 Drive '+new Date().toISOString().slice(0,10)+'</name>\n';
  gpx += '    <trkseg>\n';
  gpsTrack.forEach(function(pt) {
    gpx += '      <trkpt lat="'+pt.lat.toFixed(7)+'" lon="'+pt.lon.toFixed(7)+'">';
    if (pt.alt != null) gpx += '<ele>'+pt.alt.toFixed(1)+'</ele>';
    gpx += '<time>'+new Date(pt.ts).toISOString()+'</time>';
    if (pt.speed != null) gpx += '<speed>'+pt.speed.toFixed(2)+'</speed>';
    if (pt.obd && Object.keys(pt.obd).length > 0) {
      gpx += '<extensions>';
      if (pt.obd['12'] !== undefined) gpx += '<obd:rpm>'+pt.obd['12']+'</obd:rpm>';
      if (pt.obd['13'] !== undefined) gpx += '<obd:speed>'+pt.obd['13']+'</obd:speed>';
      if (pt.obd['5'] !== undefined) gpx += '<obd:coolant>'+pt.obd['5']+'</obd:coolant>';
      if (pt.obd['4'] !== undefined) gpx += '<obd:load>'+pt.obd['4']+'</obd:load>';
      if (pt.obd['17'] !== undefined) gpx += '<obd:throttle>'+pt.obd['17']+'</obd:throttle>';
      if (pt.acc != null) gpx += '<obd:accuracy>'+pt.acc+'</obd:accuracy>';
      gpx += '</extensions>';
    }
    gpx += '</trkpt>\n';
  });
  gpx += '    </trkseg>\n  </trk>\n</gpx>';
  dlFile(gpx, 'application/gpx+xml',
    'obd2_track_'+new Date().toISOString().slice(0,19).replace(/[:-]/g,'')+'.gpx');
  syslog('Exported '+gpsTrack.length+' GPS points as GPX (with OBD)');
}

/* Pomocna funkce pro stazeni souboru */
function dlFile(content, mime, name) {
  var blob = new Blob([content], {type: mime});
  var a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = name; a.click();
  URL.revokeObjectURL(a.href);
}

/* ================================================================ */
/*  Canvas click handler — klikatelne body s OBD popup              */
/* ================================================================ */
var mapToX, mapToY; /* Ulozene projekcni funkce z drawMap */

function initMapClick() {
  var c = document.getElementById('mapCanvas');
  if (!c) return;
  c.addEventListener('click', function(e) {
    if (gpsTrack.length < 2) return;
    var rect = c.getBoundingClientRect();
    var cx = e.clientX - rect.left, cy = e.clientY - rect.top;
    var minD = Infinity, best = -1;
    for (var i = 0; i < gpsTrack.length; i++) {
      var px = mapToX(gpsTrack[i].lon), py = mapToY(gpsTrack[i].lat);
      var dd = (px-cx)*(px-cx) + (py-cy)*(py-cy);
      if (dd < minD) { minD = dd; best = i; }
    }
    if (best >= 0 && minD < 400) showPointPopup(gpsTrack[best], e.clientX, e.clientY);
    else hidePointPopup();
  });
}

function showPointPopup(pt, x, y) {
  var pop = document.getElementById('mapPopup');
  if (!pop) return;
  var h = '<b>\u23F1 '+new Date(pt.ts).toLocaleTimeString()+'</b><br>';
  h += '\uD83D\uDEF0 GPS: '+((pt.speed||0)*3.6).toFixed(1)+' km/h<br>';
  if (pt.acc != null) h += '\uD83D\uDCE1 Acc: '+pt.acc+' m<br>';
  if (pt.moving !== undefined) h += '\uD83D\uDCF1 '+(pt.moving?'Moving':'Stationary')+'<br>';
  if (pt.obd) {
    if (pt.obd['12'] !== undefined) h += '\uD83D\uDD27 RPM: '+pt.obd['12']+'<br>';
    if (pt.obd['13'] !== undefined) h += '\uD83D\uDE97 OBD Spd: '+pt.obd['13']+' km/h<br>';
    if (pt.obd['5'] !== undefined) h += '\uD83C\uDF21 Cool: '+pt.obd['5']+'\u00B0C<br>';
    if (pt.obd['4'] !== undefined) h += '\u26A1 Load: '+pt.obd['4']+'%<br>';
    if (pt.obd['17'] !== undefined) h += '\uD83E\uDDB6 Thr: '+pt.obd['17']+'%<br>';
  }
  pop.innerHTML = h; pop.style.display = 'block';
  var rect = document.getElementById('mapCanvas').getBoundingClientRect();
  pop.style.left = Math.min(x - rect.left, rect.width - 160) + 'px';
  pop.style.top = Math.max(0, y - rect.top - pop.offsetHeight - 5) + 'px';
}

function hidePointPopup() {
  var p = document.getElementById('mapPopup');
  if (p) p.style.display = 'none';
}

/* ================================================================ */
/*  Standalone HTML export — Leaflet mapa s OBD daty                */
/*  Generuje self-contained HTML soubor s embeddovanymi GPS+OBD     */
/*  daty. Leaflet CSS+JS se nacitaji z CDN pri otevreni souboru.    */
/* ================================================================ */
function exportInteractiveMap() {
  if (gpsTrack.length < 2) { syslog('Min 2 GPS points'); return; }
  var D = gpsTrack.map(function(p) {
    var o = {la:+(p.lat.toFixed(6)),lo:+(p.lon.toFixed(6)),t:p.ts,
             s:p.speed?+(p.speed.toFixed(2)):null, a:p.acc};
    if (p.obd) {
      var ob = {};
      if (p.obd['4'] !== undefined) ob.ld = p.obd['4'];
      if (p.obd['5'] !== undefined) ob.cl = p.obd['5'];
      if (p.obd['12'] !== undefined) ob.rp = p.obd['12'];
      if (p.obd['13'] !== undefined) ob.sp = p.obd['13'];
      if (p.obd['17'] !== undefined) ob.th = p.obd['17'];
      if (Object.keys(ob).length) o.ob = ob;
    }
    return o;
  });
  var dist = (gpsTotalDist/1000).toFixed(2);
  var dt = new Date().toLocaleString();
  var h = '<!DOCTYPE html><html><head><meta charset="utf-8">';
  h += '<meta name="viewport" content="width=device-width,initial-scale=1">';
  h += '<title>OBD2 Trip - '+dt+'</title>';
  h += '<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css"/>';
  h += '<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"><\/script>';
  h += '<style>body{margin:0;font-family:sans-serif;background:#1a1a2e;color:#e0e0e0}';
  h += '#hd{padding:10px 15px;background:#16213e}#hd h2{margin:0;font-size:16px;color:#0df}';
  h += '#hd p{margin:4px 0 0;font-size:13px;color:#999}#map{width:100%;height:calc(100vh - 70px)}';
  h += '.lp b{color:#0df}.lp{font-size:13px;line-height:1.6}';
  h += '.lg{background:rgba(26,26,46,.9);padding:8px 12px;border-radius:6px;color:#e0e0e0;font-size:12px}';
  h += '.lg i{width:14px;height:14px;display:inline-block;margin-right:6px;border-radius:50%;vertical-align:middle}</style>';
  h += '</head><body><div id="hd"><h2>\uD83D\uDE97 OBD2 Trip \u2014 '+dt+'</h2>';
  h += '<p>\uD83D\uDCCD '+D.length+' pts | \uD83D\uDCCF '+dist+' km</p></div>';
  h += '<div id="map"></div><script>';
  h += 'var D='+JSON.stringify(D)+';';
  /* Outlier removal */
  h += 'function hav(a,b,c,d){var R=6371e3,p=Math.PI/180,dl=(c-a)*p,dp=(d-b)*p,';
  h += 'x=Math.sin(dp/2),y=Math.sin(dl/2),z=x*x+Math.cos(a*p)*Math.cos(c*p)*y*y;';
  h += 'return R*2*Math.atan2(Math.sqrt(z),Math.sqrt(1-z))}';
  h += 'function cln(t){if(t.length<3)return t;var c=[t[0]];';
  h += 'for(var i=1;i<t.length-1;i++){var p=c[c.length-1],q=t[i],n=t[i+1];';
  h += 'var d1=hav(p.la,p.lo,q.la,q.lo),d2=hav(q.la,q.lo,n.la,n.lo),d3=hav(p.la,p.lo,n.la,n.lo);';
  h += 'if((d1+d2)>d3*3&&d1>100)continue;c.push(q)}c.push(t[t.length-1]);return c}';
  /* Speed color */
  h += 'function sc(s){var k=s?s*3.6:0;return k<20?"#4CAF50":k<50?"#8BC34A":k<80?"#FFC107":k<110?"#FF9800":"#F44336"}';
  /* Init map */
  h += 'var T=cln(D),map=L.map("map");';
  h += 'L.tileLayer("https://tile.openstreetmap.org/{z}/{x}/{y}.png",{attribution:"\\u00a9 OpenStreetMap",maxZoom:19}).addTo(map);';
  h += 'var la=T.map(function(p){return p.la}),lo=T.map(function(p){return p.lo});';
  h += 'map.fitBounds([[Math.min.apply(null,la),Math.min.apply(null,lo)],[Math.max.apply(null,la),Math.max.apply(null,lo)]],{padding:[30,30]});';
  /* Polyline segments */
  h += 'for(var i=1;i<T.length;i++)L.polyline([[T[i-1].la,T[i-1].lo],[T[i].la,T[i].lo]],{color:sc(T[i].s),weight:4,opacity:.85}).addTo(map);';
  /* Start/End */
  h += 'if(T.length>0){L.marker([T[0].la,T[0].lo]).addTo(map).bindPopup("<b style=color:#4CAF50>\\u25B6 START</b><br>"+new Date(T[0].t).toLocaleTimeString());';
  h += 'L.marker([T[T.length-1].la,T[T.length-1].lo]).addTo(map).bindPopup("<b style=color:#F44336>\\u23F9 END</b><br>"+new Date(T[T.length-1].t).toLocaleTimeString())}';
  /* Clickable points */
  h += 'var st=Math.max(1,Math.floor(T.length/100));';
  h += 'for(var i=0;i<T.length;i+=st){var p=T[i],spd=p.s?(p.s*3.6).toFixed(1):"?";';
  h += 'var h2="<div class=lp><b>\\u23F1 "+new Date(p.t).toLocaleTimeString()+"</b><br>";';
  h += 'h2+="\\uD83D\\uDEF0 GPS: "+spd+" km/h<br>";';
  h += 'if(p.a)h2+="\\uD83D\\uDCE1 Acc: "+p.a+" m<br>";';
  h += 'if(p.ob){h2+="<hr style=border-color:#333;margin:4px\\x200>";';
  h += 'if(p.ob.rp)h2+="\\uD83D\\uDD27 RPM: "+p.ob.rp+"<br>";';
  h += 'if(p.ob.sp)h2+="\\uD83D\\uDE97 OBD: "+p.ob.sp+" km/h<br>";';
  h += 'if(p.ob.cl)h2+="\\uD83C\\uDF21 Cool: "+p.ob.cl+"\\u00B0C<br>";';
  h += 'if(p.ob.ld)h2+="\\u26A1 Load: "+p.ob.ld+"%<br>";';
  h += 'if(p.ob.th)h2+="\\uD83E\\uDDB6 Thr: "+p.ob.th+"%<br>"}';
  h += 'h2+="</div>";';
  h += 'L.circleMarker([p.la,p.lo],{radius:6,color:sc(p.s),fillColor:sc(p.s),fillOpacity:.9,weight:2}).addTo(map).bindPopup(h2)}';
  /* Legend */
  h += 'var lg=L.control({position:"bottomright"});lg.onAdd=function(){var d=L.DomUtil.create("div","lg");';
  h += 'd.innerHTML="<b>Speed</b><br><i style=background:#4CAF50></i>0-20<br><i style=background:#8BC34A></i>20-50<br>';
  h += '<i style=background:#FFC107></i>50-80<br><i style=background:#FF9800></i>80-110<br><i style=background:#F44336></i>110+ km/h";';
  h += 'return d};lg.addTo(map);';
  h += '<\/script></body></html>';
  dlFile(h, 'text/html', 'obd2_trip_'+new Date().toISOString().slice(0,16).replace(/:/g,'-')+'.html');
  syslog('Exported interactive map ('+gpsTrack.length+' pts, Leaflet+OBD)');
}

/* ================================================================ */
/*  System zalozek (tabu)                                           */
/*  Prepinani mezi sedmi zalozkami: Stream, Dash, Stats, Map,       */
/*  Diagnostics, Settings a Log. Aktivni zalozka se zvyrazni        */
/*  a jeji obsah se zobrazi, ostatni se skryji.                     */
/*  Pri prepnuti na zalozku Map se s kratkym zpozdenim prekresli    */
/*  canvas (kvuli spravnemu vypoctu rozmeru po zmene zobrazeni).    */
/* ================================================================ */
var TAB_IDS = ['main','dash','stats','map','diag','pid','settings','log'];

/* Prepne aktivni zalozku — nastavi CSS tridu .active na zvolenou zalozku
   i jeji obsah a skryje ostatni */
function switchTab(name) {
  document.querySelectorAll('.tab').forEach(function(t,i) {
    t.classList.toggle('active', TAB_IDS[i] === name);
  });
  document.querySelectorAll('.tab-content').forEach(function(c) {
    c.classList.toggle('active', c.id === 'tab_' + name);
  });
  /* Prekresleni mapy pri prepnuti na zalozku Map (kvuli layoutu) */
  if (name === 'map' && gpsTrack.length >= 2) {
    setTimeout(drawMap, 50);
  }
  /* Auto-stop "PIDs Select" pri prepnuti mimo PID zalozku. Inspector je
     samostatny diagnosticky stream, proto se bez obnovy DASH rezimu zastavi. */
  if (name !== 'pid' && (streamMode === 'inspector' || inspectorPending)) {
    stopInspectorStream();
  }
}

/* ================================================================ */
/*  WebSocket pripojeni k ESP32                                     */
/*  Pripojuje se na ws://<hostname>/ws (adresa ESP32 AP).           */
/*  Logika opetovneho pripojeni:                                    */
/*  - Pri otevreni se nastavi zeleny indikator a stav "Connected"   */
/*  - Prichozi zpravy se parsuje jako JSON a predaji handleru       */
/*  - Pri uzavreni spojeni se automaticky pokusi o znovupripojeni   */
/*    po 2 sekundach (setTimeout) a nastavi stav "Reconnecting"     */
/*  - Pri chybe se nastavi cerveny indikator                        */
/*  Funkce cmd() odesila JSON prikazy na server, pokud je spojeni   */
/*  otevrene (readyState === 1 = OPEN).                             */
/* ================================================================ */
function connect() {
  var url = 'ws://' + location.hostname + '/ws';
  syslog('Connecting to ' + url + '...');
  ws = new WebSocket(url);

  ws.onopen = function() {
    document.getElementById('dot').className = 'dot on';
    document.getElementById('connTxt').textContent = 'Connected';
    syslog('WebSocket OPEN');
  };
  ws.onmessage = function(e) {
    if (e.data.indexOf('"hb":true') === -1) logMsg('in', e.data);
    try { handleResponse(JSON.parse(e.data)); } catch(err) {}
  };
  ws.onclose = function() {
    document.getElementById('dot').className = 'dot';
    document.getElementById('connTxt').textContent = 'Reconnecting...';
    syslog('WebSocket CLOSED');
    closeInspectorModal();
    streaming = false; inspectorActive = false; inspectorPending = false; setStreamMode('idle');
    setTimeout(connect, 2000);
  };
  ws.onerror = function() {
    document.getElementById('dot').className = 'dot err';
    syslog('WebSocket ERROR');
  };
}

/* Odesle JSON prikaz na ESP32 pres WebSocket.
   Pred odeslanim overi, ze je spojeni aktivni. */
function cmd(obj) {
  if (!ws || ws.readyState !== 1) { syslog('Not connected!'); return; }
  if (obj.cmd === 'ping') pingSent = Date.now();
  var json = JSON.stringify(obj);
  if (!obj.hb) logMsg('out', json);
  ws.send(json);
}

/* ================================================================ */
/*  Zpracovani odpovedi ze serveru (dispatcher)                     */
/*  Hlavni funkce handleResponse rozdeluje odpovedi podle pole      */
/*  r.cmd a vola prislusne funkce pro aktualizaci UI:               */
/*  - 'init': inicializace OBD probehla, odemknuti tlacitek        */
/*  - 'stream': aktualizace ukazatelu, bublin, statistik, nahravani*/
/*  - 'start/stop_stream': zmena stavu streamovani                 */
/*  - 'get_pid/get_pids': zobrazeni vysledku cteni PID             */
/*  - 'get_supported_pids': naplneni seznamu podporovanych PID     */
/*  - 'get_dtc/get_pending_dtc': zobrazeni diagnostickych kodu     */
/*  - 'get_vin/get_ecu_name/get_cal_id': info o vozidle            */
/*  - 'get_freeze_frame': zmrazeny snimek dat pri zavade            */
/*  - 'get_monitor_status': stav emisnich monitoru a MIL           */
/*  Pole r.free_heap se zobrazuje v hlavicce jako volna pamet ESP32.*/
/* ================================================================ */
function handleResponse(r) {
  updateEspTelemetry(r);
  if (r.transport_ready !== undefined) transportReady = !!r.transport_ready;

  switch (r.cmd) {
  case 'transport_init':
    if (r.status === 'ok') {
      syslog('Transport OK: ' + r.baudrate + ' bps TX=GPIO' + r.tx_pin + ' RX=GPIO' + r.rx_pin + ' | ' + formatActiveEcu(r.active_ecu));
    } else {
      var trDiag = formatInitDiag(r.diag);
      syslog('Transport error: ' + (r.error || '') + (trDiag ? ' | ' + trDiag : ''));
    }
    break;

  case 'pid00_probe':
    if (r.status === 'ok') {
      syslog('PID 00 Probe OK (' + (r.response_count || 0) + '): ' + formatProbeResponses(r.responses));
      if (r.diag) syslog('PID 00 diag: ' + formatInitDiag(r.diag));
    } else {
      var prDiag = formatInitDiag(r.diag);
      syslog('PID 00 Probe error: ' + (r.error || '') + ' ' + (r.message || '') + (prDiag ? ' | ' + prDiag : ''));
    }
    break;

  case 'init':
    if (r.status === 'ok') {
      obdReady = true;
      var newPids = r.supported_pids || r.pids || [];

      if (newPids.length > 0) {
        /* Defenzivni normalizace na cisla \u2014 server posila uint8_t,
           historicke verze posilaly hex stringy ("0x0C"). */
        supportedPids = newPids.map(pidToInt);
        var hexList = supportedPids.map(pidToHex).join(', ');
        syslog('Supported PIDs (Service 01): ' + hexList);

        /* Naplnime kategorie z init odpovedi (server je autoritativni zdroj).
           Frontend pouziva pidCategories pro DASH filtering, "PIDs select"
           grouping a Vehicle Info zobrazeni. */
        pidCategories.telemetry = new Set((r.telemetry_pids || []).map(pidToInt));
        pidCategories.status    = new Set((r.status_pids    || []).map(pidToInt));
        pidCategories.config    = new Set((r.config_pids    || []).map(pidToInt));

        /* Vehicle Info \u2014 staticka data o vozidle z init odpovedi.
           Klice jsou string decimalni PIDy ("28" pro $1C), hodnoty mohou
           byt cisla nebo hex stringy (pro ENUM/BIT_ENCODED). */
        vehicleInfo = r.vehicle_info || {};
        renderVehicleInfo();

        /* Vychozi vyber pro stream = DASH essential \u2229 supportedPids.
           Pokud uzivatel uz neco vybral, neprepiseme; jinak nastavime defaulty. */
        if (streamPids.length <= 3) {
          var supSet = new Set(supportedPids);
          streamPids = DASH_ESSENTIAL_PIDS.filter(function(p) { return supSet.has(p); });
          syslog('Default stream PIDs: ' + streamPids.map(pidToHex).join(', '));
        }
      }

      enableButtons(true);
      buildPidChecks();
      buildPidsSelect();   /* PIDs select panel v PID tabu */

      /* Auto-otevreni "Export & Logging Setup" panelu, aby uzivatel hned
         videl seznam dostupnych PIDu. */
      var setupPanel = document.getElementById('exportSetup');
      if (setupPanel) setupPanel.open = true;

      var activeTxt = formatActiveEcu(r.active_ecu);
      syslog('OBD init OK \u2014 ' + (r.pid_count||supportedPids.length) + ' PIDs found' + (activeTxt ? ' | ' + activeTxt : ''));
    } else {
      var initDiag = formatInitDiag(r.diag);
      syslog('Init error: ' + (r.error||'') + ' ' + (r.message||'') + (initDiag ? ' | ' + initDiag : ''));
    }
    break;

  case 'stream':
    if ((r.mode || streamMode) === 'inspector') {
      updateInspectorCards(r.d || {}, r.diag || {});
    } else {
      lastStreamData = r.d || {};   /* Ulozeni poslednich OBD dat pro GPS snapshot */
      updateGauges(lastStreamData);
      updateDashBubbles(lastStreamData);
      updateStats(lastStreamData);
      recordSample(lastStreamData, r.ts);
    }
    break;

  case 'start_stream':
    if (r.status === 'ok') {
      var startedMode = r.mode || 'dash';
      streaming = true;
      inspectorPending = false;
      setStreamMode(startedMode);
      document.getElementById('btnRec').disabled = (startedMode !== 'dash');
      var hexPids = (r.pids||[]).map(pidToHex).join(',');
      if (startedMode === 'inspector') {
        inspectorActive = true;
        var cardsDiv = document.getElementById('pidsSelectCards');
        if (cardsDiv) {
          cardsDiv.style.display = 'grid';
          buildInspectorCards();
        }
        buildPidsSelect();
        updatePidsSelectCount();
        syslog('PID Inspector started: ' + r.pid_count + ' PIDs [' + hexPids + '], ' + r.interval_ms + 'ms');
      } else {
        inspectorActive = false;
        var dashCards = document.getElementById('pidsSelectCards');
        if (dashCards) { dashCards.style.display = 'none'; dashCards.innerHTML = ''; }
        syslog('DASH stream started (Service 01): ' + r.pid_count + ' PIDs [' + hexPids + '], ' + r.interval_ms + 'ms');
      }
    } else {
      inspectorPending = false;
      updateStreamBtn();
      buildPidsSelect();
      updatePidsSelectCount();
      syslog('Stream error: ' + (r.error||''));
    }
    break;

  case 'stop_stream':
    finishRecording('Recording stopped');
    streaming = false;
    inspectorPending = false;
    setStreamMode('idle');
    document.getElementById('btnRec').disabled = true;
    /* Auto-cleanup PIDs Select — bez streamu nedavalo smysl drzet inspector
       PIDy v aktivnim stavu. Karty zmizi, checkboxy se odemknou. */
    if (inspectorActive) {
      inspectorActive = false;
      var cardsDiv = document.getElementById('pidsSelectCards');
      if (cardsDiv) { cardsDiv.style.display = 'none'; cardsDiv.innerHTML = ''; }
    }
    buildPidsSelect();
    updatePidsSelectCount();
    syslog('Stream stopped');
    break;

  case 'manual_query':
    if (r.status === 'ok') {
      if (typeof appendTerminalRow === 'function') appendTerminalRow(r);
      else syslog('Manual query OK: rx=' + (r.rx_id || '?') + ' payload=' + (r.payload || ''));
      if (r.transport_only && r.diag) syslog('Manual PID00 diag: ' + formatInitDiag(r.diag));
    } else {
      var termDiag = formatInitDiag(r.diag);
      syslog('Terminal Error: ' + (r.message || r.error) + (termDiag ? ' | ' + termDiag : ''));
    }
    break;

  case 'get_pid':
    if (r.status === 'ok') showPidResult(r);
    else {
      var oneDiag = diagSummary(r.diag);
      syslog(pidLabel(r.pid) + ' error: ' + r.error + (oneDiag ? ' | ' + oneDiag : ''));
    }
    break;

  case 'get_pids':
    if (r.results) r.results.forEach(function(p) {
      if (p.status==='ok') showPidResult(p);
      else syslog(pidLabel(p.pid) + ' error: ' + p.error + (diagSummary(p.diag) ? ' | ' + diagSummary(p.diag) : ''));
    });
    break;

  case 'get_supported_pids':
    if (r.status === 'ok') {
      /* Defenzivni normalizace na cisla — viz komentar u 'init' handleru */
      supportedPids = (r.pids || []).map(pidToInt);
      if (supportedPids.length > 0) obdReady = true;
      buildPidChecks();
      buildPidsSelect();   /* Aktualizace PIDs Select panelu v PID tabu */
      showSupportedPids(supportedPids);
      enableButtons(obdReady);
      var hexList = supportedPids.map(pidToHex).join(', ');
      syslog('Supported PIDs: ' + hexList);
    }
    break;

  case 'get_dtc':
  case 'get_pending_dtc':
  case 'get_permanent_dtc':
    showDtc(r);
    break;

  case 'clear_dtc':
    if (r.status === 'ok') {
      showInfo('Clear DTC (Service 04)', r.message || 'DTC smazany');
      syslog('Clear DTC (Service 04): OK');
      /* Skryjeme stary DTC panel, aby uzivatel videl, ze je vse smazano */
      var dp = document.getElementById('dtcPanel');
      if (dp) dp.style.display = 'none';
    } else {
      showInfo('Clear DTC', 'Chyba: ' + fmtErr(r));
      syslog('Clear DTC selhalo: ' + (r.error || '?'));
    }
    break;

  case 'get_mode06_monitor':
    if (r.status === 'ok') showMode06Monitor(r);
    else showInfo('Mode 06 OBDMID ' + pidToHex(r.mid || 0), 'Error: ' + fmtErr(r));
    break;

  case 'get_vin':
    if (r.status === 'ok') {
      if (r.vins && r.vins.length > 1) {
        var vs = r.vins.map(function(v){ return '<strong>['+v.id+']</strong> '+v.vin; }).join('<br>');
        showInfo('VIN (Service 09)', vs);
      } else {
        showInfo('VIN (Service 09)', r.vin);
      }
    } else { showInfo('VIN (Service 09)', 'Error: ' + fmtErr(r)); }
    break;

  case 'get_ecu_name':
    if (r.status === 'ok') {
      if (r.ecu_names && r.ecu_names.length > 1) {
        var ns = r.ecu_names.map(function(n){ return '<strong>['+n.id+']</strong> '+n.name; }).join('<br>');
        showInfo('ECU Names (Service 09)', ns);
      } else {
        showInfo('ECU Name (Service 09)', r.ecu_name);
      }
    } else { showInfo('ECU Name (Service 09)', 'Error: ' + fmtErr(r)); }
    break;

  case 'get_cal_id':
    if (r.status === 'ok') {
      if (r.ecu_cal_ids && r.ecu_cal_ids.length > 1) {
        var ci = r.ecu_cal_ids.map(function(i){ return '<strong>['+i.id+']</strong> '+ (i.cal_ids||[]).join(', '); }).join('<br>');
        showInfo('Calibration IDs (Service 09)', ci);
      } else {
        showInfo('Calibration ID (Service 09)', (r.cal_ids||[]).join(', ') + ' (' + r.count + ' item(s))');
      }
    } else { showInfo('Calibration ID (Service 09)', 'Error: ' + fmtErr(r)); }
    break;

  case 'get_supported_infotypes':
    if (r.status === 'ok') showSupportedInfoTypes(r);
    else showInfo('Supported InfoTypes (Service 09)', 'Error: ' + fmtErr(r));
    break;

  case 'get_mode09_info':
    if (r.status === 'ok') showMode09RawInfo(r);
    else showInfo('Mode 09 InfoType ' + pidToHex(r.infotype || 0), 'Error: ' + fmtErr(r));
    break;

  case 'get_cvn':
    if (r.status === 'ok') showCvn(r);
    else showInfo('CVN (Service 09)', 'Error: ' + fmtErr(r));
    break;

  case 'get_ipt':
    if (r.status === 'ok') showIpt(r);
    else showInfo('IPT (Service 09)', 'Error: ' + fmtErr(r));
    break;

  case 'get_monitor_status_all':
    if (r.status === 'ok') {
      if (r.ecus && r.ecus.length > 0) {
        var html = '<h4 style="margin:0 0 10px 0;color:var(--accent)">Readiness (Service 01)</h4>';
        r.ecus.forEach(function(ecu) {
          html += '<div style="background:var(--bg3);padding:10px;border-radius:12px;margin-bottom:10px;border:1px solid rgba(255,255,255,0.02)">';
          html += '<div style="display:flex;justify-content:space-between;border-bottom:1px solid #444;padding-bottom:5px;margin-bottom:8px">' +
                  '<span><strong>ECU ID:</strong> ' + ecu.id + '</span>' +
                  '<span><strong>MIL:</strong> ' + (ecu.mil ? '<span style="color:#f44336">ON</span>' : '<span style="color:#00e676">OFF</span>') + '</span>' +
                  '<span><strong>DTCs:</strong> ' + ecu.dtc_count + '</span></div>';
          html += '<div style="display:grid;grid-template-columns:1fr 1fr;gap:4px;font-size:0.85em">';
          for (var name in ecu.monitors) {
            var m = ecu.monitors[name];
            if (!m.sup) continue;
            var label = m.name || name.replace(/_/g, ' ');
            if (!m.name) label = label.charAt(0).toUpperCase() + label.slice(1);
            var color = m.rdy ? '#00e676' : '#ff9800';
            html += '<div style="display:flex;justify-content:space-between;padding:2px 4px;background:rgba(0,0,0,0.2)">' +
                    '<span style="color:var(--fg2)">' + label + '</span>' +
                    '<span style="color:' + color + ';font-weight:bold">' + (m.rdy ? 'OK' : 'INC') + '</span></div>';
          }
          html += '</div></div>';
        });
        document.getElementById('infoPanel').style.display = 'block';
        document.getElementById('infoContent').innerHTML = html;
      } else { syslog('No ECU responded to monitor query'); }
    } else { syslog('Multi-monitor error: ' + (r.error||'')); }
    break;

  case 'discover_ecus':
    if (r.status === 'ok') {
      if (r.ecus && r.ecus.length > 0) {
        var html = '<h4 style="margin:0 0 10px 0;color:var(--accent)">Network Discovery (CAN)</h4>';
        html += '<table style="width:100%;text-align:left;border-collapse:collapse">';
        html += '<tr style="color:var(--fg2);border-bottom:1px solid #444"><th style="padding:5px">ID</th><th style="padding:5px">ECU Name / Type</th></tr>';
        r.ecus.forEach(function(ecu) {
          html += '<tr style="border-bottom:1px solid #222"><td style="padding:5px;font-family:monospace;color:var(--accent)">' + ecu.id + '</td>' +
                  '<td style="padding:5px">' + (ecu.name || 'Unknown Unit') + '</td></tr>';
        });
        html += '</table>';
        showInfo('Network Scanner', html);
      } else { showInfo('Network Scanner', 'No units detected on CAN bus.'); }
    } else { syslog('Discovery error: ' + (r.error||'')); }
    break;

  case 'get_freeze_frame':
    if (r.status === 'ok') {
      var ff = pidLabel(r.pid) + ': ';
      ff += r.value !== undefined ? r.value.toFixed(2) : ('raw=' + (r.raw||'?'));
      if (r.unit) ff += ' ' + r.unit;
      showInfo('Freeze Frame', ff);
    } else { showInfo('Freeze Frame', 'Error: ' + fmtErr(r)); }
    break;

  case 'get_monitor_status':
    if (r.status === 'ok') showMonitorStatus(r);
    else syslog('Monitor status error: ' + (r.error||''));
    break;

  case 'ping':
    if (r.hb) return; // Heartbeat aktualizuje telemetrii, ale nezapisuje se do logu.
    syslog('Ping OK - ' + (Date.now() - pingSent) + 'ms');
    break;
  }
}

/* Formatovani chybove zpravy s volitelnym detailem NRC (Negative Response Code).
   Obsahuje cislo NRC v hexadecimalnim formatu a jeho nazev, pokud jsou k dispozici. */
function fmtErr(r) {
  var s = r.error || 'unknown';
  if (r.nrc_name) s += ' (NRC 0x' + (r.nrc_code||0).toString(16).toUpperCase() + ': ' + r.nrc_name + ')';
  if (r.message) s += ' \u2014 ' + r.message;
  return s;
}

/* ================================================================ */
/*  SVG ukazatele (gauges) — RPM a rychlost                        */
/*  Kazdy ukazatel pouziva SVG polokruhovy oblouk (path element).  */
/*  Animace se provadi zmenou atributu stroke-dashoffset:           */
/*    plny oblouk ma delku 251 (stroke-dasharray), offset 251 =    */
/*    prazdny, offset 0 = plne vyplneny. Hodnota se linearne       */
/*    interpoluje mezi min a max.                                   */
/*  Barva oblouku se meni pri vysokych hodnotach:                   */
/*    azurova (normalni) -> oranzova (>80% varovani) -> cervena     */
/*    (prekroceni varovne hranice warnHigh).                        */
/* ================================================================ */
function setGauge(arcId, valId, value, min, max, warnHigh) {
  var pct = Math.max(0, Math.min(1, (value - min) / (max - min)));
  var arc = document.getElementById(arcId);
  arc.style.strokeDashoffset = 251 * (1 - pct);
  if (warnHigh && value > warnHigh) arc.style.stroke = '#f44336';
  else if (warnHigh && value > warnHigh * 0.8) arc.style.stroke = '#ff9800';
  else arc.style.stroke = '#26a69a';
  document.getElementById(valId).textContent = Math.round(value);
}

/* Aktualizace obou ukazatelu z prijatych stream dat.
   PID 12 = otacky motoru (rozsah 0-7000, varovani na 6000).
   PID 13 = rychlost vozidla (rozsah 0-240, bez varovani). */
function updateGauges(d) {
  if (d['12'] !== undefined) setGauge('arc_rpm','val_rpm', d['12'], 0, 7000, 6000);
  if (d['13'] !== undefined) setGauge('arc_spd','val_spd', d['13'], 0, 240, null);
}

/* ================================================================ */
/*  Zalozka Dash — dynamicke PID bubliny                            */
/*  Pro kazdy PID prijaty ve stream datech se vytvori bublina       */
/*  (div.pid-bubble) obsahujici:                                    */
/*    - Aktualni ciselnou hodnotu s jednotkou                       */
/*    - Nazev PID z tabulky PID_INFO                                */
/*    - Canvas se sparkline minigrafem posledních 60 vzorku         */
/*  Bubliny se vytvareji dynamicky pri prvnim prijeti daneho PID    */
/*  a nasledne se jen aktualizuji. Puvodni zastupny text            */
/*  ("Press Init OBD...") se odstrani pri prvnich datech.           */
/* ================================================================ */
/* Dekoduje bitovou masku stavu monitoru (PID 0x01) na citelny text */
function decodeMonitorStatusVal(hex) {
  if (!hex || typeof hex !== 'string' || !hex.startsWith('0x')) return hex;
  var val = parseInt(hex, 16);
  if (isNaN(val)) return hex;
  var mil = (val >>> 31) & 1;
  var dtc = (val >>> 24) & 0x7F;
  return (mil ? 'MIL: ON' : 'MIL: OFF') + ', ' + dtc + ' DTCs';
}

/* Renderovaci helper pro hodnotu PIDu — vraci HTML retezec.

   Format streamovanych hodnot:
   1) number              → skalarni telemetry (RPM, teplota, tlak...)
   2) string "0x..."      → bit-encoded / enum / config
   3) Array of numbers    → multi-value (O2 senzory, EGT 4-sensor, NOx)
   4) Array of strings    → raw bajty pro PIDy bez decoderu (RAW format)

   Funkce vraci:
   - { html: ..., isNum: true/false, primaryNum: <number nebo null pro sparkline> }
*/
function renderPidValue(pidNum, valRaw, info) {
  /* --- Multi-value pole --- */
  if (Array.isArray(valRaw)) {
    if (valRaw.length === 0) return { html: '?', isNum: false, primaryNum: null };

    /* Pole stringu = raw bajty (PID bez decoderu) */
    if (typeof valRaw[0] === 'string') {
      var bytes = valRaw.join(' ');
      return {
        html: '<span style="font-family:monospace;font-size:0.65em;color:var(--fg2)">' + bytes + '</span>',
        isNum: false,
        primaryNum: null
      };
    }

    /* Pole cisel = multi-sensor (O2, EGT, NOx). info.multi[] obsahuje labely. */
    var labels = (info && info.multi) || [];
    var primary = (typeof valRaw[0] === 'number') ? valRaw[0] : null;
    var unit = (info && info.u) || '';

    /* Prvni hodnota velka, ostatni male radky pod ni */
    var lines = [];
    var firstLabel = labels[0] || '';
    var firstUnit = (labels.length > 0 && labels[0]) ? '' : unit; /* kdyz jsou labely, jednotka je v label */
    if (primary !== null) {
      lines.push('<span style="font-size:1em;font-weight:600">' +
                 primary.toFixed(2) + '</span> <span class="pb-unit">' +
                 (firstLabel || firstUnit) + '</span>');
    } else {
      lines.push('—');
    }

    /* Sekundarni a dalsi hodnoty mensim fontem */
    for (var i = 1; i < valRaw.length; i++) {
      var v = valRaw[i];
      var lbl = labels[i] || ('S' + (i+1));
      if (v === null || (typeof v === 'number' && isNaN(v))) {
        lines.push('<span style="font-size:0.62em;color:var(--fg2)">' + lbl + ': —</span>');
      } else {
        lines.push('<span style="font-size:0.62em;color:var(--fg2)">' + lbl + ': ' +
                   (typeof v === 'number' ? v.toFixed(2) : v) + '</span>');
      }
    }
    return {
      html: lines.join('<br>'),
      isNum: primary !== null,
      primaryNum: primary
    };
  }

  /* --- Skalarni cislo --- */
  if (typeof valRaw === 'number') {
    var u = (info && info.u) || '';
    return {
      html: valRaw.toFixed(1) + ' <span class="pb-unit">' + u + '</span>',
      isNum: true,
      primaryNum: valRaw
    };
  }

  /* --- Bit-encoded / enum / hex string --- */
  var valStr;
  if (pidNum === 0x01) {
    valStr = decodeMonitorStatusVal(valRaw);
  } else if (info && info.vals) {
    var numericVal = parseInt(valRaw, 16);
    valStr = info.vals[numericVal] || valRaw;
  } else {
    valStr = valRaw;
  }
  return { html: valStr, isNum: false, primaryNum: null };
}

function updateDashBubbles(d) {
  var grid = document.getElementById('dashGrid');
  /* Odebrani zastupneho textu pri prvnich prijatrch datech */
  if (grid.dataset.init !== '1') { grid.innerHTML = ''; grid.dataset.init = '1'; }

  /* Mnozina PIDu, ktere se na DASH zobrazi — DASH essential ∩ supported.
     Inspector PIDy se NEzobrazuji v DASH (maji vlastni karty v DIAG panelu). */
  var dashSet = new Set(DASH_ESSENTIAL_PIDS);

  for (var pidKey in d) {
    var pidNum = pidToInt(pidKey);

    /* Filtrace: na DASH ukazujeme jen essential PIDy (RPM, speed, ECT, ...).
       Inspector PIDy se zpracovavaji v updateInspectorCards(). */
    if (!dashSet.has(pidNum)) continue;

    var valRaw = d[pidKey];
    var info = PID_INFO[pidNum];
    var rendered = renderPidValue(pidNum, valRaw, info);

    if (rendered.isNum && rendered.primaryNum !== null) {
      pushHistory(pidKey, rendered.primaryNum);
    }

    var hexId = pidToHex(pidNum);
    var displayName = info ? ('PID ' + hexId + ' - ' + info.n) : ('PID ' + hexId);
    var id = 'pb_' + pidKey;
    var el = document.getElementById(id);

    if (!el) {
      el = document.createElement('div');
      el.className = 'pid-bubble'; el.id = id;
      el.innerHTML = '<div class="pb-val">' + rendered.html + '</div>' +
                     '<div class="pb-name">' + displayName + '</div>' +
                     '<canvas id="sp_' + pidKey + '" width="180" height="32" style="' + (rendered.isNum?'':'display:none') + '"></canvas>';
      grid.appendChild(el);
    } else {
      el.querySelector('.pb-val').innerHTML = rendered.html;
      var canv = el.querySelector('canvas');
      if (canv) canv.style.display = rendered.isNum ? 'block' : 'none';
    }

    if (rendered.isNum) {
      var canvas = document.getElementById('sp_' + pidKey);
      if (pidHistory[pidKey]) drawSparkline(canvas, pidHistory[pidKey]);
    }
  }

  /* Inspector stream se zpracovava oddelene v handleResponse('stream'). */
}

/* Vykresleni Vehicle Info pruhu na HOME zalozce.
   Vstup: globalni 'vehicleInfo' object naplneny v handleResponse('init')
   z r.vehicle_info. Klice jsou string decimalni PIDy ("28", "81", "79", "80").
   Hodnoty jsou cisla (skalary) nebo hex stringy ("0x06" pro ENUM/BIT_ENCODED).

   Pro kazdy PID z vehicleInfo:
     - lookup PID_INFO[pidNum] pro nazev a vals[] (pokud je vyctovy)
     - kdyz je info.vals[v], zobrazi se citelny popis ("EOBD (Europe)")
     - jinak zobrazi raw cislo nebo hex */
function renderVehicleInfo() {
  var bar = document.getElementById('vehicleInfoBar');
  var content = document.getElementById('vehicleInfoContent');
  if (!bar || !content) return;

  var keys = Object.keys(vehicleInfo);
  if (keys.length === 0) {
    bar.style.display = 'none';
    return;
  }

  var html = '';
  keys.forEach(function(key) {
    var pidNum = pidToInt(key);
    var info = PID_INFO[pidNum];
    var raw = vehicleInfo[key];
    var label = info ? info.n : pidLabel(pidNum);
    var valStr;

    if (typeof raw === 'string' && raw.startsWith('0x')) {
      /* Hex string z ENUM/BIT_ENCODED */
      var num = parseInt(raw, 16);
      if (info && info.vals && info.vals[num] !== undefined) {
        valStr = info.vals[num];
      } else {
        valStr = raw;
      }
    } else if (typeof raw === 'number') {
      valStr = (Number.isInteger(raw)) ? raw.toString() : raw.toFixed(2);
      if (info && info.u) valStr += ' ' + info.u;
    } else {
      valStr = String(raw);
    }

    html += '<div style="display:flex; flex-direction:column;">' +
            '<span style="color:var(--fg2); font-size:0.82em;">' + label + '</span>' +
            '<span style="font-weight:600;">' + valStr + '</span>' +
            '</div>';
  });
  content.innerHTML = html;
  bar.style.display = 'block';
}

/* Smaže kompletně celý Dashboard a historii */
function resetDash() {
    var grid = document.getElementById('dashGrid');
    grid.innerHTML = '<div style="color:var(--fg2);font-size:0.82em;padding:20px;text-align:center;width:100%">Press <strong>Init OBD</strong> and <strong>Start Stream</strong> to see live data.</div>';
    grid.dataset.init = '0';
    pidHistory = {};
    syslog('Dashboard reseted');
}

/* Vynutí zobrazení všech podporovaných PIDů jako prázdných bublin */
function showAllSupportedPids() {
    if (supportedPids.length === 0) {
        syslog('No supported PIDs found. Run Init first.');
        return;
    }
    var fakeData = {};
    supportedPids.forEach(p => {
        if (p % 32 !== 0) fakeData[p] = "---";
    });
    updateDashBubbles(fakeData);
    syslog('Showing all ' + supportedPids.length + ' supported PIDs');
}

/* Zobrazeni vysledku cteni jednoho PID v systemovem logu */
function showPidResult(r) {
  var pidNum = pidToInt(r.pid);
  var info = PID_INFO[pidNum];
  var v = '';
  if (r.value !== undefined) {
    v = r.value.toFixed(1);
  } else if (r.value_raw) {
    if (pidNum === 0x01) {
      v = decodeMonitorStatusVal(r.value_raw);
    } else if (info && info.vals) {
      v = info.vals[parseInt(r.value_raw, 16)] || r.value_raw;
    } else {
      v = r.value_raw;
    }
  } else {
    v = '?';
  }
  /* Pokud server poslal r.name (uz ve formatu "PID 0xNN - Name"), pouzijeme
     ho primo. Jinak fallback na lokalni pidLabel. */
  var displayName = (r.name && r.name.startsWith('PID')) ? r.name : pidLabel(pidNum);
  var dsum = diagSummary(r.diag);
  syslog(displayName + ': ' + v + ' ' + (r.unit||'') + (dsum ? ' | ' + dsum : ''));
}

/* ================================================================ */
/*  Diagnostika — zobrazeni DTC kodu, informaci o vozidle           */
/*  a stavu emisnich monitoru                                       */
/* ================================================================ */

/* Zobrazeni diagnostickych poruchovych kodu (DTC). */
var DIAG_MODES = {
  mode01: {
    summary: 'Current powertrain data and readiness status from Service 01.',
    actions: [
      {id:'btnSup', label:'Supported PIDs', payload:{cmd:'get_supported_pids'}},
      {id:'btnMon', label:'Monitor Status', payload:{cmd:'get_monitor_status'}},
      {id:'btnMonAll', label:'Monitors Multi', payload:{cmd:'get_monitor_status_all'}}
    ]
  },
  mode02: {
    summary: 'Freeze frame data captured when an emissions DTC was stored.',
    actions: [
      {id:'btnFreezePid', label:'Read Freeze PID', run:function(){
        var val = prompt('Freeze frame PID in HEX:', '0C');
        if (val === null) return;
        var pid = parseInt(val, 16);
        if (isNaN(pid) || pid < 0 || pid > 255) { syslog('Invalid freeze PID'); return; }
        cmd({cmd:'get_freeze_frame', pid: pid});
      }}
    ]
  },
  mode03: {
    summary: 'Confirmed/stored emissions DTCs.',
    actions: [
      {id:'btnDtc', label:'Read Stored DTC', payload:{cmd:'get_dtc'}}
    ]
  },
  mode04: {
    summary: 'Destructive clear/reset command. Requires token confirmation.',
    actions: [
      {id:'btnClrDtc', label:'Clear DTC', danger:true, run:clearDtc}
    ]
  },
  mode06: {
    summary: 'On-board monitor test results. Raw OBDMID/TID data is shown without J1979-DA scaling claims.',
    actions: [
      {id:'btnMode06Supported', label:'Supported MIDs', payload:{cmd:'get_mode06_monitor', mid:0}},
      {id:'btnMode06Raw', label:'Read MID', run:function(){
        var val = prompt('Mode 06 OBDMID in HEX:', '01');
        if (val === null) return;
        var mid = parseInt(val, 16);
        if (isNaN(mid) || mid < 0 || mid > 255) { syslog('Invalid OBDMID'); return; }
        cmd({cmd:'get_mode06_monitor', mid:mid});
      }}
    ]
  },
  mode07: {
    summary: 'Pending DTCs detected in the current or last driving cycle.',
    actions: [
      {id:'btnPdtc', label:'Read Pending DTC', payload:{cmd:'get_pending_dtc'}}
    ]
  },
  mode09: {
    summary: 'Vehicle information InfoTypes: supported list, VIN, calibration IDs, CVN, IPT and ECU names.',
    actions: [
      {id:'btnInfoTypes', label:'Supported InfoTypes', payload:{cmd:'get_supported_infotypes'}},
      {id:'btnVin', label:'Read VIN', payload:{cmd:'get_vin'}},
      {id:'btnEcu', label:'ECU Name', payload:{cmd:'get_ecu_name'}},
      {id:'btnCal', label:'Calibration ID', payload:{cmd:'get_cal_id'}},
      {id:'btnMode09Raw', label:'Raw InfoType', run:function(){
        var val = prompt('Mode 09 InfoType in HEX:', '06');
        if (val === null) return;
        var infotype = parseInt(val, 16);
        if (isNaN(infotype) || infotype < 0 || infotype > 255) { syslog('Invalid InfoType'); return; }
        cmd({cmd:'get_mode09_info', infotype:infotype});
      }},
      {id:'btnCvn', label:'CVN', payload:{cmd:'get_cvn'}},
      {id:'btnIptSpark', label:'IPT Spark', payload:{cmd:'get_ipt', infotype:8}},
      {id:'btnIptCompression', label:'IPT Compression', payload:{cmd:'get_ipt', infotype:11}},
      {id:'btnDisc', label:'Scan Network', payload:{cmd:'discover_ecus'}}
    ]
  },
  mode0a: {
    summary: 'Permanent emissions DTCs. These should not disappear after a simple Mode 04 clear.',
    actions: [
      {id:'btnPermDtc', label:'Read Permanent DTC', payload:{cmd:'get_permanent_dtc'}}
    ]
  },
  transport: {
    summary: 'Low-level transport checks. These are useful before full OBD init.',
    actions: [
      {id:'btnTransport', label:'Transport Init', requiresObd:false, payload:{cmd:'transport_init'}},
      {id:'btnPid00Probe', label:'PID 00 Probe', requiresObd:false, payload:{cmd:'pid00_probe'}},
      {id:'btnDiagPing', label:'Ping', requiresObd:false, payload:{cmd:'ping'}}
    ]
  }
};

function clearDiagResults() {
  var dp = document.getElementById('dtcPanel');
  var dc = document.getElementById('dtcContent');
  var ip = document.getElementById('infoPanel');
  var ic = document.getElementById('infoContent');
  if (dp) dp.style.display = 'none';
  if (dc) { dc.className = 'dtc-list empty'; dc.textContent = 'No DTCs'; }
  if (ip) ip.style.display = 'none';
  if (ic) ic.innerHTML = '';
}

function renderDiagMode(mode, keepResults) {
  diagMode = DIAG_MODES[mode] ? mode : 'mode01';
  var select = document.getElementById('diagModeSelect');
  if (select) select.value = diagMode;
  if (!keepResults) clearDiagResults();

  var cfg = DIAG_MODES[diagMode];
  var summary = document.getElementById('diagModeSummary');
  if (summary) summary.textContent = cfg.summary;

  var actions = document.getElementById('diagModeActions');
  if (!actions) return;
  actions.innerHTML = '';
  cfg.actions.forEach(function(action) {
    var btn = document.createElement('button');
    btn.className = 'btn';
    btn.id = action.id;
    btn.textContent = action.label;
    if (action.danger) btn.style.background = '#c62828';
    btn.disabled = (action.requiresObd !== false) && !obdReady;
    btn.onclick = function() {
      if (action.run) action.run();
      else cmd(action.payload);
    };
    actions.appendChild(btn);
  });
}

function selectDiagMode(mode) {
  renderDiagMode(mode, false);
}

function showDtc(r) {
  var p = document.getElementById('dtcPanel');
  var c = document.getElementById('dtcContent');
  var svc = (r.cmd === 'get_pending_dtc' ? '07' : (r.cmd === 'get_permanent_dtc' ? '0A' : '03'));
  p.style.display = 'block';
  p.querySelector('.panel-title').textContent = 'DTCs (Service ' + svc + ')';
  if (r.status !== 'ok') {
    c.className = 'dtc-list'; c.textContent = 'Error: ' + (r.error||'');
    return;
  }
  if (!r.count || r.count === 0) {
    c.className = 'dtc-list empty';
    c.textContent = (r.cmd==='get_pending_dtc' ? 'No pending DTCs'
                   : (r.cmd==='get_permanent_dtc' ? 'No permanent DTCs' : 'No DTCs')) + ' \u2014 all clear';
    return;
  }
  c.className = 'dtc-list';
  c.innerHTML = '<strong>' + r.count + ' fault(s):</strong> ' + (r.dtcs||[]).join(', ');
}

/* Smazani DTC — destruktivni operace, vyzaduje dvojitou ochranu:
   1) confirm() — uzivatel potvrdi, ze opravdu chce smazat
   2) prompt() — uzivatel zada autentizacni token (WS_AUTH_TOKEN ze secrets.h)
   Token se posila serveru, ktery ho overi pred vykonanim obd2_clear_dtc().
   Pri chybe serveru (AUTH_INVALID) zobrazi sysylog upozorneni. */
function clearDtc() {
  if (!confirm('Opravdu smazat vsechny DTC a resetovat readiness monitory?\n\n' +
               'POZOR: tato operace je nevratna a vozidlo neprojde STK, dokud se ' +
               'monitory znovu nedokonci jezdnim cyklem.')) {
    return;
  }
  var token = prompt('Zadejte autentizacni token:');
  if (token === null || token === '') {
    syslog('Clear DTC zruseno — zadny token');
    return;
  }
  cmd({cmd: 'clear_dtc', token: token});
}

/* Zobrazeni obecne informace o vozidle v informacnim panelu */
function showInfo(title, text) {
  document.getElementById('infoPanel').style.display = 'block';
  document.getElementById('infoContent').innerHTML = '<strong>' + title + ':</strong> ' + text;
}

/* Zobrazeni stavu emisnich monitoru v tabulkovem formatu.
   Zahrnuje stav MIL kontrolky (zapnuta/vypnuta), pocet ulozenych
   DTC a tabulku vsech monitoru s informaci, zda jsou podporovany
   a zda jsou ve stavu Ready (pripraven) nebo Not Ready. */
function showMonitorStatus(r) {
  var html = '<div style="display:flex;gap:16px;align-items:center;margin-bottom:8px">' +
    '<div><strong>MIL (Service 01):</strong> ' +
    (r.mil ? '<span style="color:#f44336;font-weight:700">ON</span>'
           : '<span style="color:#00e676;font-weight:700">OFF</span>') + '</div>' +
    '<div><strong>DTCs:</strong> ' + r.dtc_count + '</div></div>';

  if (r.ignition || r.raw) {
    html += '<div style="font-size:0.78em;color:var(--fg2);margin-bottom:8px">';
    if (r.ignition) html += '<strong>Ignition:</strong> ' + r.ignition + ' ';
    if (r.raw) html += '<strong>PID 01 raw:</strong> ' + r.raw;
    html += '</div>';
  }

  if (r.monitors) {
    html += '<table style="width:100%;font-size:0.82em;border-collapse:collapse">';
    html += '<tr style="color:var(--fg2);text-align:left"><th style="padding:3px 6px">Monitor</th>' +
            '<th style="padding:3px 6px">Supported</th><th style="padding:3px 6px">Status</th></tr>';
    for (var name in r.monitors) {
      var m = r.monitors[name];
      var label = m.name || name.replace(/_/g, ' ');
      if (!m.name) label = label.charAt(0).toUpperCase() + label.slice(1);
      var sup = m.sup ? '<span style="color:#00e676">Yes</span>' : '<span style="color:#555">No</span>';
      var rdy = !m.sup ? '<span style="color:#555">\u2014</span>'
                : (m.rdy ? '<span style="color:#00e676">Ready</span>'
                         : '<span style="color:#ff9800">Not Ready</span>');
      html += '<tr style="border-top:1px solid #222"><td style="padding:3px 6px">' + label + '</td>' +
              '<td style="padding:3px 6px">' + sup + '</td>' +
              '<td style="padding:3px 6px">' + rdy + '</td></tr>';
    }
    html += '</table>';
  }
  document.getElementById('infoPanel').style.display = 'block';
  document.getElementById('infoContent').innerHTML = html;
}

/* Zobrazeni seznamu podporovanych PID jako barevne stitky s hexadecimalnim
   cislem a nazvem. PID ktere jsou nasobky 32 se preskakovuji,
   protoze slouzi pouze jako bitmaskove indikatory podpory dalsich PID. */
function showSupportedPids(pids) {
  var html = '<strong>Supported PIDs (Service 01):</strong><div style="display:flex;flex-wrap:wrap;gap:4px;margin-top:6px">';
  pids.forEach(function(p) {
    var pidNum = pidToInt(p);
    var hexId = pidToHex(pidNum);
    var info = PID_INFO[pidNum];
    var name = info ? info.n : '';
    var shortName = name.length > 20 ? name.substring(0, 18) + '..' : name;
    html += '<span style="background:var(--bg3);padding:3px 8px;border-radius:4px;font-size:0.78em" title="' + (name || hexId) + '">' +
            '<span style="color:var(--accent)">' + hexId + '</span>' +
            (shortName ? ' ' + shortName : '') + '</span>';
  });
  html += '</div>';
  document.getElementById('infoPanel').style.display = 'block';
  document.getElementById('infoContent').innerHTML = html;
}

function showSupportedInfoTypes(r) {
  var html = '<strong>Supported InfoTypes (Service 09):</strong> ';
  html += (r.infotypes || []).map(pidToHex).join(', ') || 'none';
  if (r.ecus && r.ecus.length) {
    html += '<div style="margin-top:8px">';
    r.ecus.forEach(function(ecu) {
      html += '<div style="padding:8px 0;border-top:1px solid #222">' +
              '<strong>[' + ecu.id + ']</strong> ' +
              ((ecu.infotypes || []).map(pidToHex).join(', ') || 'none') +
              '<br><span style="color:var(--fg2);font-family:monospace">raw=' +
              (ecu.raw || '') + '</span></div>';
    });
    html += '</div>';
  }
  showInfo('Mode 09 InfoType 00', html);
}

function showCvn(r) {
  var html = '';
  if (r.ecu_cvns && r.ecu_cvns.length) {
    r.ecu_cvns.forEach(function(ecu) {
      html += '<div style="padding:8px 0;border-top:1px solid #222">' +
              '<strong>[' + ecu.id + ']</strong> ' +
              ((ecu.cvns || []).join(', ') || 'No CVN values') + '</div>';
    });
  } else {
    html = (r.cvns || []).join(', ') || 'No CVN values';
  }
  showInfo('CVN (Service 09 InfoType 06)', html);
}

function showMode09RawInfo(r) {
  var html = '';
  (r.ecus || []).forEach(function(ecu) {
    html += '<div style="padding:8px 0;border-top:1px solid #222">' +
            '<div><strong>[' + ecu.id + ']</strong> NODI=' + (ecu.nodi || 0) +
            ' raw_len=' + (ecu.raw_len || 0) +
            (ecu.truncated ? ' truncated' : '') + '</div>' +
            '<div style="color:var(--fg2);font-family:monospace;font-size:0.82em;word-break:break-word">raw=' +
            (ecu.raw || '') + '</div></div>';
  });
  showInfo('Mode 09 InfoType ' + pidToHex(r.infotype || 0), html || 'No data');
}

function showMode06Monitor(r) {
  var html = '<div><strong>RX:</strong> ' + (r.rx_id || '?') +
             ' <strong>raw_len:</strong> ' + (r.raw_len || 0) + '</div>';
  html += '<div style="color:var(--fg2);font-family:monospace;font-size:0.82em;word-break:break-word">raw=' +
          (r.raw || '') + '</div>';
  if (r.supported_mids && r.supported_mids.length) {
    html += '<div style="margin-top:8px"><strong>Supported MIDs:</strong> ' +
            r.supported_mids.map(pidToHex).join(', ') + '</div>';
  }
  if (r.tests && r.tests.length) {
    html += '<table style="width:100%;font-size:0.82em;border-collapse:collapse;margin-top:8px">';
    html += '<tr style="color:var(--fg2);text-align:left"><th>TID</th><th>Value</th><th>Min</th><th>Max</th></tr>';
    r.tests.forEach(function(t) {
      html += '<tr style="border-top:1px solid #222"><td style="padding:3px 4px;font-family:monospace">' +
              pidToHex(t.tid || 0) + '</td><td style="padding:3px 4px;font-family:monospace">' +
              t.value + '</td><td style="padding:3px 4px;font-family:monospace">' +
              t.min + '</td><td style="padding:3px 4px;font-family:monospace">' +
              t.max + '</td></tr>';
    });
    html += '</table>';
  }
  showInfo('Mode 06 OBDMID ' + pidToHex(r.mid || 0), html);
}

function showIpt(r) {
  var title = 'IPT ' + ((r.kind || '') === 'compression' ? 'Compression' : 'Spark') +
              ' (Service 09 InfoType ' + pidToHex(r.infotype || 8) + ')';
  var html = '';
  (r.ecus || []).forEach(function(ecu) {
    html += '<div style="padding:8px 0;border-top:1px solid #222">';
    html += '<div><strong>[' + ecu.id + ']</strong> NODI=' + (ecu.nodi || 0) +
            ' raw_len=' + (ecu.raw_len || 0) + '</div>';
    html += '<div style="color:var(--fg2);font-family:monospace;font-size:0.82em;word-break:break-word">raw=' +
            (ecu.raw || '') + '</div>';
    if (ecu.counters && ecu.counters.length) {
      html += '<table style="width:100%;font-size:0.82em;border-collapse:collapse;margin-top:6px">';
      html += '<tr style="color:var(--fg2);text-align:left"><th>Name</th><th>Value</th></tr>';
      ecu.counters.forEach(function(c) {
        html += '<tr style="border-top:1px solid #222"><td style="padding:3px 4px">' +
                (c.name || ('#' + c.idx)) + '</td><td style="padding:3px 4px;font-family:monospace">' +
                c.value + '</td></tr>';
      });
      html += '</table>';
    }
    html += '</div>';
  });
  showInfo(title, html || 'No IPT data');
}

/* ================================================================ */
/*  Ovladani datoveho proudu (stream)                               */
/*  Umoznuje spustit a zastavit kontinualni cteni vybranych PID     */
/*  se zadanym intervalem. PID se vybiraji pomoci toglovatelnych    */
/*  stitku v konfiguracnim panelu.                                  */
/* ================================================================ */

/* Prepinac streamu — odesle prikaz start_stream nebo stop_stream.
   Pri startu sebere vybrane PID z checkboxu a interval z inputu. */
function toggleStream() {
  if (inspectorPending) {
    syslog('PID Inspector start is pending');
    return;
  }
  if (streaming) {
    if (streamMode === 'inspector') {
      syslog('Stop PID Inspector from PID tab first');
      return;
    }
    cmd({cmd: 'stop_stream'});
  } else {
    var checks = document.querySelectorAll('.pid-chk.sel');
    var pids = [];
    checks.forEach(function(el) { pids.push(parseInt(el.dataset.pid)); });
    if (pids.length === 0) { syslog('Select at least one PID!'); return; }
    var interval = parseInt(document.getElementById('streamInt').value) || 200;
    cmd({cmd: 'start_stream', mode: 'dash', pids: pids, interval_ms: interval});
  }
}

/* Aktualizace textu a stylu tlacitka streamu podle aktualniho stavu */
function updateStreamBtn() {
  var btn = document.getElementById('btnStream');
  if (!btn) return;
  btn.disabled = !obdReady || streamMode === 'inspector' || inspectorPending;
  if (inspectorPending) {
    btn.textContent = 'Inspector Starting'; btn.classList.add('on');
  } else if (streamMode === 'inspector') {
    btn.textContent = 'Inspector Active'; btn.classList.add('on');
  } else if (streaming) {
    btn.textContent = 'Stop Stream'; btn.classList.add('on');
  } else {
    btn.textContent = 'Start Stream'; btn.classList.remove('on');
  }
}

/* Odemknuti/zamknuti diagnostickych a streamovacich tlacitek.
   Vola se po uspesne inicializaci OBD (init OK). */
function enableButtons(en) {
  ['btnStream','btnPidsSelectActivate'].forEach(function(id) {
    var el = document.getElementById(id);
    if (el) el.disabled = !en;
  });
  document.getElementById('exportSetup').style.display = en ? 'block' : 'none';
  if (en) document.getElementById('btnInit').textContent = 'Re-Init';
  renderDiagMode(diagMode, true);
}

/* ================================================================ */
/*  Diagnosticky Terminal (Manualni dotazy) - ZAKOMENTOVANO        */
/* ================================================================ */

/* 
function toggleTerminal() {
  var p = document.getElementById('termPanel');
  if (!p) return;
  p.style.display = (p.style.display === 'none') ? 'block' : 'none';
  if (p.style.display === 'block') {
    p.scrollIntoView({behavior: 'smooth'});
  }
}

function sendManualQuery() {
  var svc = document.getElementById('termSvc').value;
  var pid = document.getElementById('termPid').value;
  var svcNum = parseInt(svc, 16);
  var pidNum = parseInt(pid, 16);
  if (isNaN(svcNum) || isNaN(pidNum)) { syslog('Invalid HEX!'); return; }
  cmd({cmd: 'manual_query', service: svcNum, pid: pidNum});
}

function appendTerminalRow(r) {
  var body = document.getElementById('termBody');
  if (!body) return;
  var empty = document.getElementById('termEmpty');
  if (empty) empty.style.display = 'none';
  var ts = new Date().toLocaleTimeString('en-GB',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
  var row = document.createElement('tr');
  row.className = 'term-row';
  var interp = '';
  if (r.is_neg) interp = '<span class="term-neg">Negative Response (NRC 0x' + r.nrc.toString(16).toUpperCase() + ')</span>';
  else if (r.interp_name) interp = '<span class="term-interp">' + r.interp_name + ': ' + r.interp_val.toFixed(2) + ' ' + (r.interp_unit || '') + '</span>';
  row.innerHTML = '<td style="padding:8px 12px; color:var(--fg2)">' + ts + '</td>' +
                  '<td style="padding:8px 12px" class="term-id">' + (r.rx_id || '0x???') + '</td>' +
                  '<td style="padding:8px 12px" class="term-payload">' + (r.payload || '') + '</td>' +
                  '<td style="padding:8px 12px">' + interp + '</td>';
  body.insertBefore(row, body.firstChild);
  if (body.childElementCount > 50) body.removeChild(body.lastChild);
}

function clearTerminal() {
  var body = document.getElementById('termBody');
  if (body) body.innerHTML = '';
  var empty = document.getElementById('termEmpty');
  if (empty) empty.style.display = 'block';
}
*/

/* Sestaveni toglovatelnych PID stitku v konfiguracnim panelu streamu.
   Pro kazdy podporovany PID (krome bitmaskovych) se vytvori element,
   ktery lze kliknutim vybrat nebo zrusit pro zarazeni do streamu.

   Vsechny PID se uchovavaji jako CISLO (po pidToInt v handleResponse).
   Stitek zobrazuje "0xNN - shortName" nebo jen "0xNN" kdyz neni v PID_INFO. */
function buildPidChecks() {
  var wrap = document.getElementById('pidChecks');
  wrap.innerHTML = '';
  supportedPids.forEach(function(pid) {
    if (pid % 32 === 0) return; /* preskocit bitmaskove PID */
    var hexId = pidToHex(pid);
    var info = PID_INFO[pid];
    var label = info ? (hexId + ' - ' + (info.n.length > 15 ? info.n.substring(0,12)+'..' : info.n))
                     : hexId;
    var el = document.createElement('span');
    el.className = 'pid-chk' + (streamPids.indexOf(pid) >= 0 ? ' sel' : '');
    el.dataset.pid = pid;
    el.textContent = label;
    el.title = info ? info.n : hexId;
    el.onclick = function() {
      this.classList.toggle('sel');
      var idx = streamPids.indexOf(pid);
      if (idx >= 0) streamPids.splice(idx, 1);
      else streamPids.push(pid);
    };
    wrap.appendChild(el);
  });
}

/* ================================================================ */
/*  PIDs Select panel (PID zalozka)                                 */
/*                                                                  */
/*  Power-user nastroj pro live cteni libovolnych podporovanych     */
/*  PIDu z descriptoru. Maximum INSPECTOR_MAX (4) soucasne aktivnich.*/
/*                                                                  */
/*  Pracovni postup:                                                 */
/*   1. Po init se naplni seznam vsech supportedPids, seskupenych   */
/*      podle bitmask range ($01-$20, $21-$40, $41-$60, atd.).      */
/*   2. Uzivatel zaskrtne az 4 PIDy.                                */
/*   3. Klikem na "Activate" se spusti samostatny Inspector stream. */
/*      Pokud bezi DASH/Trip/GPS/recording, uzivatel prepnuti       */
/*      potvrdi v modalu. Karty zobrazuji live hodnoty a diag.      */
/*   4. Po prepnuti tabu nebo kliku "Stop" se Inspector stream      */
/*      zastavi; DASH stream se automaticky neobnovuje.             */
/* ================================================================ */

/* Sestavi seznam vybiratelnych PIDu seskupenych podle bitmask range.
   Range = (pid - 1) / 32: $01-$20 (0), $21-$40 (1), $41-$60 (2), atd. */
function buildPidsSelect() {
  var wrap = document.getElementById('pidsSelectList');
  if (!wrap) return;

  if (supportedPids.length === 0) {
    wrap.innerHTML = '<div style="color:var(--fg2); font-size:0.78em; text-align:center; padding:20px;">Run Init OBD first.</div>';
    return;
  }

  /* Seskupeni podle range $01-$20, $21-$40, atd. */
  var groups = {};
  supportedPids.forEach(function(pid) {
    if (pid % 32 === 0) return; /* preskocit bitmaskove PID ($00, $20, $40...) */
    var rangeIdx = Math.floor((pid - 1) / 32);
    if (!groups[rangeIdx]) groups[rangeIdx] = [];
    groups[rangeIdx].push(pid);
  });

  var rangeNames = {
    0: '$01-$20',  1: '$21-$40',  2: '$41-$60',  3: '$61-$80',
    4: '$81-$A0',  5: '$A1-$C0',  6: '$C1-$E0',  7: '$E1-$FF'
  };

  var html = '';
  Object.keys(groups).sort(function(a,b){return a-b;}).forEach(function(rIdx) {
    var pidsInGroup = groups[rIdx];
    html += '<details open style="margin-bottom:8px;">' +
            '<summary style="cursor:pointer; font-size:0.78em; font-weight:600; color:var(--accent); padding:4px 0;">' +
            'Range ' + rangeNames[rIdx] + ' (' + pidsInGroup.length + ')' +
            '</summary>' +
            '<div style="padding-left:8px; padding-top:4px;">';
    pidsInGroup.forEach(function(pid) {
      var info = PID_INFO[pid];
      var hexId = pidToHex(pid);
      var name = info ? info.n : '(no decoder, raw bytes)';
      var checked = inspectorPids.indexOf(pid) >= 0 ? 'checked' : '';
      var disabled = (inspectorActive || inspectorPending) ? 'disabled' : ''; /* behem aktivace zamezit zmenam */
      html += '<label style="display:flex; align-items:center; gap:6px; font-size:0.74em; padding:3px 0; cursor:pointer;">' +
              '<input type="checkbox" data-pid="' + pid + '" ' + checked + ' ' + disabled +
              ' onchange="onPidsSelectToggle(this)">' +
              '<span style="color:var(--accent); font-family:monospace;">' + hexId + '</span>' +
              '<span style="color:var(--fg);">' + name + '</span>' +
              '</label>';
    });
    html += '</div></details>';
  });
  wrap.innerHTML = html;
  updatePidsSelectCount();
}

/* Toggle handler — kdyz uzivatel zaskrtne/odskrtne checkbox */
function onPidsSelectToggle(checkbox) {
  var pid = parseInt(checkbox.dataset.pid);
  if (checkbox.checked) {
    if (inspectorPids.length >= INSPECTOR_MAX) {
      checkbox.checked = false;
      syslog('PIDs Select: max ' + INSPECTOR_MAX + ' PIDs');
      return;
    }
    if (inspectorPids.indexOf(pid) === -1) inspectorPids.push(pid);
  } else {
    var idx = inspectorPids.indexOf(pid);
    if (idx >= 0) inspectorPids.splice(idx, 1);
  }
  updatePidsSelectCount();
}

/* Aktualizace pocitadla "X/4" a stavu Activate tlacitka. */
function updatePidsSelectCount() {
  var cnt = document.getElementById('pidsSelectCount');
  var btn = document.getElementById('btnPidsSelectActivate');
  if (cnt) cnt.textContent = inspectorPids.length + '/' + INSPECTOR_MAX;
  if (btn) {
    btn.disabled = !obdReady || inspectorPids.length === 0 || inspectorPending;
    btn.textContent = inspectorPending ? 'Starting...' : (inspectorActive ? 'Stop' : 'Activate');
    btn.style.background = inspectorActive ? 'rgba(244, 67, 54, 0.2)' : '';
  }
}

function closeInspectorModal() {
  var modal = document.getElementById('inspectorModal');
  if (modal) modal.style.display = 'none';
}

function showInspectorModal() {
  var modal = document.getElementById('inspectorModal');
  if (modal) modal.style.display = 'flex';
}

function stopLiveContextsForInspector() {
  finishRecording('Recording stopped by PID Inspector');
  if (tripActive) toggleTrip();
  if (gpsActive) toggleGps();
}

function startInspectorStream() {
  if (inspectorPids.length === 0) {
    syslog('PIDs Select: vyberte alespon jeden PID');
    return;
  }
  inspectorPending = true;
  updateStreamBtn();
  buildPidsSelect();
  updatePidsSelectCount();
  var interval = parseInt(document.getElementById('inspectorInt').value) || 500;
  if (interval < 100) interval = 100;
  cmd({
    cmd: 'start_stream',
    mode: 'inspector',
    pids: inspectorPids.slice(),
    diag_pids: inspectorPids.slice(),
    interval_ms: interval
  });
  syslog('PID Inspector starting: ' + inspectorPids.map(pidToHex).join(', '));
}

function confirmInspectorSwitch() {
  closeInspectorModal();
  stopLiveContextsForInspector();
  startInspectorStream();
}

function stopInspectorStream() {
  if (streamMode === 'inspector' || inspectorActive || inspectorPending) {
    inspectorPending = false;
    updateStreamBtn();
    updatePidsSelectCount();
    cmd({cmd: 'stop_stream'});
  }
}

/* Toggle Activate/Stop pro exkluzivni Inspector rezim.
   Activate spusti samostatny stream vybranych PIDu. Pokud bezi DASH stream,
   recording, Trip nebo GPS, nejdrive ukaze modal s potvrzenim. Stop ukonci
   Inspector stream a neobnovuje predchozi DASH stream. */
function togglePidsSelect() {
  if (inspectorPending) return;
  if (inspectorActive) {
    stopInspectorStream();
    return;
  }
  if (streaming || recording || tripActive || gpsActive) {
    showInspectorModal();
    return;
  }
  startInspectorStream();
}

/* Vytvori prazdne karty pro vybrane inspector PIDy. */
function buildInspectorCards() {
  var cardsDiv = document.getElementById('pidsSelectCards');
  if (!cardsDiv) return;
  var html = '';
  inspectorPids.forEach(function(pid) {
    var info = PID_INFO[pid];
    var hexId = pidToHex(pid);
    var name = info ? info.n : '(raw bytes)';
    html += '<div class="pid-bubble" id="ic_' + pid + '" style="padding:10px;">' +
            '<div class="pb-val" id="icv_' + pid + '">—</div>' +
            '<details class="pid-detail" id="icd_' + pid + '">' +
            '<summary>Diagnostics</summary><div id="icdiag_' + pid + '">Waiting for data...</div></details>' +
            '<div class="pb-name" style="font-size:0.7em;">PID ' + hexId + ' - ' + name + '</div>' +
            '</div>';
  });
  cardsDiv.innerHTML = html;
}

/* Aktualizace hodnot v inspector kartach z prijatych stream dat. */
function updateInspectorCards(d, diag) {
  inspectorPids.forEach(function(pid) {
    var key = String(pid);
    var elv = document.getElementById('icv_' + pid);
    if (!elv) return;
    var detailEl = document.getElementById('icd_' + pid);
    var diagEl = document.getElementById('icdiag_' + pid);
    var info = PID_INFO[pid];
    var hasValue = key in d;
    var diagObj = diag ? diag[key] : null;
    if (!hasValue && !diagObj) return;
    var rendered = hasValue
      ? renderPidValue(pid, d[key], info)
      : {html: 'Error', isNum: false, primaryNum: null};
    elv.innerHTML = rendered.html;

    if (diagObj && detailEl && diagEl) {
      var errClass = diagObj.obd_status && diagObj.obd_status !== 'OK' ? ' err' : '';
      detailEl.className = 'pid-detail' + errClass;
      diagEl.textContent = diagSummary(diagObj).replace(/ /g, '\n');
    }
  });
}

/* ================================================================ */
/*  Log (komunikacni log s fixni vyskou)                            */
/*  Zobrazuje vsechny zpravy (prichozi, odchozi, systemove)        */
/*  s casovym razitkem. Stream data se filtruje (prilis caste).     */
/*  Buffer je omezen na 300 radku — nejstarsi se automaticky mazi.  */
/* ================================================================ */

/* Zapise systemovou zpravu do logu */
function syslog(t) { logMsg('sys', t); }

/* Zapise zpravu do komunikacniho logu s casovym razitkem a typem.
   Typy: 'in' = prichozi (zelena), 'out' = odchozi (oranzova),
   'sys' = systemova (seda). Zpravy obsahujici "stream" se v
   prichozim smeru preskakovuji kvuli nadmernemu poctu. */
function logMsg(type, t) {
  var box = document.getElementById('logBox');
  /* Vynechani stream dat a heartbeatů v logu (v obou smerech) */
  if (t.indexOf('"stream"') > -1 || t.indexOf('"hb":true') > -1) return;
  
  /* Omezeni na 300 radku — nejstarsi radek se odstrani */
  if (box.childElementCount > 300) box.removeChild(box.firstChild);
  var d = document.createElement('div');
  d.className = 'm-' + type;
  var ts = new Date().toLocaleTimeString('en-GB',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
  d.textContent = '[' + ts + '] ' + (type==='in'?'<< ':type==='out'?'>> ':'') + t;
  box.appendChild(d);
  box.scrollTop = box.scrollHeight;
}

/* ================================================================ */
/*  Spusteni aplikace                                               */
/*  Pri nacteni stranky se nejprve obnovi nastaveni z localStorage  */
/*  a pote se navaze WebSocket pripojeni k ESP32 serveru.           */
/* ================================================================ */
loadSettings();
renderDiagMode(diagMode, true);
connect();
initMapClick();

/* Periodicka aktualizace UI a uptime (kazdou vterinu pro hladky chod) */
setInterval(function() {
  var sec = Math.floor((Date.now() - webStart) / 1000);
  document.getElementById('uptimeWeb').textContent = 'W: ' + formatDuration(sec);
}, 1000);

/* Periodicky heartbeat (ping) kazde 3 sekundy pro udrzeni spojeni a telemetrii */
setInterval(function() {
  cmd({cmd:'ping', hb:true});
}, 3000);
</script>
</body>
</html>
)rawliteral";

#endif /* DASHBOARD_H */
