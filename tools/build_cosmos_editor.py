#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""Build and update the Cosmos Web Editor for Andromeda.

Generates custom-fw/web/Onyx_Web_Editor.html and custom-fw/web/Andromeda_Web_Editor.html
with the Andromeda Cosmos theme, 24-engine macro controls, footswitch actions,
interactive cosmic radar canvas, and full SysEx/BLE/firmware compatibility.
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ONYX_HTML = ROOT / "custom-fw" / "web" / "Onyx_Web_Editor.html"
ANDROMEDA_HTML = ROOT / "custom-fw" / "web" / "Andromeda_Web_Editor.html"

src = ONYX_HTML.read_text(encoding="utf-8")

# Extract the script sections
script_open_idx = src.index("<script>")
script_content = src[script_open_idx + len("<script>"):]

# Extract parts:
# 1. tables block up to //CORE-BEGIN
core_begin_idx = script_content.index("//CORE-BEGIN")
tables_part = script_content[:core_begin_idx]

# 2. protocol core from //CORE-BEGIN to // ---- the page ----
the_page_idx = script_content.index("// ---- the page ----")
protocol_core = script_content[core_begin_idx:the_page_idx]

# 3. store and firmware script from // ---- stored captures and cabinets:
store_idx = script_content.index("// ---- stored captures and cabinets: the lists, delete, rename, replace, store ----")
export_idx = script_content.rindex('if (typeof module !== "undefined")')
store_and_fw_script = script_content[store_idx:export_idx]
node_exports = script_content[export_idx:]

cosmos_head_and_body = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Andromeda Cosmos Web Editor</title>
<style>
:root {
  --void: #040410;
  --bg: #070618;
  --surf: #0e0b24;
  --raise: #161238;
  --raise-hi: #1f1a4e;
  --line: #262058;
  --line-hi: #3d3588;
  --text: #eef2ff;
  --mid: #8e8ebd;
  --dim: #544f80;
  
  /* Andromeda Stellar Accents */
  --solar: #ffcc4c;
  --violet: #9e6eff;
  --ion: #3ad8ff;
  --aurora: #40f6b2;
  --corona: #ff8e3c;
  --magenta: #ff5ed4;
  --ice: #b0ecff;
  --bad: #ff4d6d;
  --acc: var(--violet);
  --ok: var(--aurora);
  
  --r: 8px;
  --r-sm: 4px;
  color-scheme: dark;
}

* { box-sizing: border-box; }
[hidden] { display: none !important; }

body {
  margin: 0;
  background: radial-gradient(circle at 50% 0%, #120e36 0%, var(--void) 85%);
  color: var(--text);
  font: 400 13px/1.5 "Inter Tight", "Segoe UI", -apple-system, sans-serif;
  min-height: 100vh;
}

/* Header with Celestial Branding */
header {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px;
  padding: 12px 20px;
  background: rgba(14, 11, 36, 0.9);
  backdrop-filter: blur(12px);
  border-bottom: 1px solid var(--line);
  position: sticky;
  top: 0;
  z-index: 50;
}

.logo-wrap {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-right: 8px;
}
.logo-orb {
  width: 22px;
  height: 22px;
  border-radius: 50%;
  background: radial-gradient(circle at 35% 35%, #fff 0%, var(--violet) 50%, #4a21a8 100%);
  box-shadow: 0 0 12px var(--violet);
}
h1 {
  font-size: 1.05rem;
  font-weight: 700;
  letter-spacing: .08em;
  margin: 0;
  text-transform: uppercase;
  background: linear-gradient(90deg, #fff 0%, var(--ice) 40%, var(--violet) 100%);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
}
h1 span {
  font-size: 0.75rem;
  letter-spacing: .12em;
  font-weight: 400;
  color: var(--mid);
  -webkit-text-fill-color: var(--mid);
  margin-left: 6px;
}

button, select, input[type=file] {
  font: inherit;
  color: var(--text);
  background: var(--raise);
  border: 1px solid var(--line);
  border-radius: var(--r);
  padding: 6px 14px;
  min-height: 32px;
  cursor: pointer;
  transition: all .15s ease;
}
button:hover:not(:disabled) {
  background: var(--raise-hi);
  border-color: var(--violet);
  box-shadow: 0 0 8px rgba(158, 110, 255, 0.3);
}
button:disabled { opacity: .35; cursor: default; }
button.pri {
  background: linear-gradient(135deg, #7b3fe4, #9e6eff);
  border-color: #ba94ff;
  color: #fff;
  font-weight: 600;
  box-shadow: 0 0 12px rgba(158, 110, 255, 0.4);
}
button.pri:hover:not(:disabled) {
  box-shadow: 0 0 18px rgba(158, 110, 255, 0.7);
}
button.solar {
  background: linear-gradient(135deg, #c78800, #ffcc4c);
  border-color: #ffe082;
  color: #110e00;
  font-weight: 700;
}
button.small { padding: 2px 8px; font-size: 12px; margin-left: 8px; }

#status {
  margin-left: auto;
  color: var(--mid);
  font-size: .82rem;
  font-variant-numeric: tabular-nums;
  display: flex;
  align-items: center;
  gap: 6px;
}
#status::before {
  content: "";
  display: inline-block;
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--dim);
}
#status.on {
  color: var(--aurora);
}
#status.on::before {
  background: var(--aurora);
  box-shadow: 0 0 8px var(--aurora);
}

/* Navigation Tabs */
nav {
  display: flex;
  gap: 4px;
  padding: 10px 20px 0;
  background: rgba(7, 6, 24, 0.6);
  border-bottom: 1px solid var(--line);
  overflow-x: auto;
}
nav button {
  background: transparent;
  border: 1px solid transparent;
  border-bottom: none;
  border-radius: var(--r) var(--r) 0 0;
  color: var(--mid);
  font-weight: 500;
  padding: 8px 16px;
  letter-spacing: .03em;
}
nav button:hover {
  color: var(--text);
  background: rgba(22, 18, 56, 0.5);
}
nav button.sel {
  background: var(--surf);
  border-color: var(--line);
  color: #fff;
  font-weight: 600;
  box-shadow: inset 0 2px 0 var(--violet);
}

main {
  padding: 18px 20px 40px;
  max-width: 1400px;
  margin: 0 auto;
}
section {
  background: var(--surf);
  border: 1px solid var(--line);
  border-radius: 0 var(--r) var(--r) var(--r);
  padding: 20px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
}

/* Deck / Control Bar */
.cosmos-deck {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px;
  background: var(--raise);
  border: 1px solid var(--line);
  border-radius: var(--r);
  padding: 12px 16px;
  margin-bottom: 16px;
}
.fs-btn {
  background: var(--raise-hi);
  border: 1px solid var(--line-hi);
  padding: 8px 14px;
  font-weight: 600;
  font-size: .82rem;
  letter-spacing: .04em;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.fs-btn kbd {
  background: rgba(0,0,0,0.4);
  border: 1px solid rgba(255,255,255,0.15);
  border-radius: 3px;
  padding: 1px 5px;
  font-size: .75rem;
  color: var(--ion);
}
.fs-btn:active {
  transform: translateY(1px);
  filter: brightness(1.3);
}

/* Chain Cards */
.chain {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(240px, 1fr));
  gap: 14px;
}
.blk {
  background: var(--bg);
  border: 1px solid var(--line);
  border-radius: var(--r);
  overflow: hidden;
  transition: border-color .15s, box-shadow .15s, opacity .15s;
}
.blk:hover {
  border-color: var(--line-hi);
}
.blk.off {
  opacity: .5;
  filter: grayscale(40%);
}
.bh {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 12px;
  color: #fff;
  font-weight: 700;
  font-size: .85rem;
  letter-spacing: .08em;
  position: relative;
  cursor: grab;
  user-select: none;
  touch-action: none;
}
.bh::after {
  content: "";
  position: absolute;
  left: 0;
  bottom: 0;
  height: 2px;
  width: 0;
  background: #fff;
}
.blk.holding .bh::after {
  width: 100%;
  transition: width 1s linear;
}
.blk.lifted {
  position: relative;
  z-index: 20;
  box-shadow: 0 12px 36px rgba(0,0,0,.8);
  outline: 2px solid var(--violet);
  cursor: grabbing;
}
.blk.target {
  outline: 2px dashed var(--violet);
}
.bh button {
  margin-left: auto;
  min-height: 22px;
  padding: 1px 8px;
  font-size: .72rem;
  font-weight: 700;
  background: rgba(0,0,0,0.4);
  border: 1px solid rgba(255,255,255,0.2);
  color: #fff;
  border-radius: 4px;
}
.bh button:hover {
  background: rgba(255,255,255,0.2);
}

.bb {
  padding: 12px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.bb select {
  width: 100%;
  background: var(--surf);
  border-color: var(--line);
  font-weight: 500;
}

.knob {
  display: grid;
  grid-template-columns: 80px 1fr 34px;
  align-items: center;
  gap: 8px;
  font-size: .76rem;
  color: var(--mid);
}
.knob.dead {
  opacity: .25;
  pointer-events: none;
}
.knob output {
  text-align: right;
  color: var(--text);
  font-variant-numeric: tabular-nums;
  font-weight: 600;
}

input[type=range] {
  width: 100%;
  accent-color: var(--violet);
  height: 5px;
  background: var(--raise);
  border-radius: 3px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(280px, 1fr));
  gap: 12px 20px;
}
.row {
  display: grid;
  grid-template-columns: 140px 1fr 56px;
  align-items: center;
  gap: 10px;
}
.row.sel2 {
  grid-template-columns: 140px 1fr;
}

h2 {
  font-size: .8rem;
  text-transform: uppercase;
  letter-spacing: .12em;
  color: var(--mid);
  margin: 24px 0 10px;
  font-weight: 700;
  display: flex;
  align-items: center;
  gap: 8px;
}
h2::after {
  content: "";
  flex: 1;
  height: 1px;
  background: var(--line);
}
h2:first-child { margin-top: 0; }

.big {
  font-size: 1.5rem;
  font-weight: 700;
  letter-spacing: .02em;
  color: #fff;
}
.pre {
  display: flex;
  gap: 10px;
  align-items: center;
  flex-wrap: wrap;
}
.note { color: var(--mid); font-size: .82rem; }
.warn { color: var(--bad); }

/* Cosmic Radar Scope */
.radar-wrap {
  display: flex;
  flex-wrap: wrap;
  gap: 24px;
  align-items: center;
  justify-content: center;
  padding: 20px 0;
}
.radar-box {
  width: 240px;
  height: 240px;
  border-radius: 50%;
  background: radial-gradient(circle at center, #07061d 0%, #03030c 100%);
  border: 2px solid var(--violet);
  box-shadow: 0 0 24px rgba(158, 110, 255, 0.35), inset 0 0 20px rgba(158, 110, 255, 0.2);
  position: relative;
  overflow: hidden;
}
#cosmicRadar {
  display: block;
}
.telemetry-card {
  flex: 1;
  min-width: 260px;
  background: var(--raise);
  border: 1px solid var(--line);
  border-radius: var(--r);
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.telem-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: .85rem;
  border-bottom: 1px solid rgba(255,255,255,0.06);
  padding-bottom: 6px;
}
.telem-row span:first-child { color: var(--mid); }
.telem-row span:last-child { font-weight: 600; color: var(--ice); font-variant-numeric: tabular-nums; }

#toast {
  position: fixed;
  left: 50%;
  bottom: 24px;
  transform: translateX(-50%);
  background: var(--raise-hi);
  border: 1px solid var(--violet);
  color: #fff;
  font-weight: 600;
  padding: 10px 20px;
  border-radius: var(--r);
  opacity: 0;
  pointer-events: none;
  transition: opacity .25s;
  z-index: 100;
  box-shadow: 0 6px 24px rgba(0,0,0,0.6), 0 0 12px rgba(158, 110, 255, 0.4);
}
#toast.on { opacity: 1; }

#log {
  background: var(--bg);
  border: 1px solid var(--line);
  border-radius: var(--r);
  margin-top: 18px;
  padding: 10px;
  height: 120px;
  overflow: auto;
  font: 11px/1.4 "JetBrains Mono", Consolas, monospace;
  color: var(--mid);
  white-space: pre-wrap;
}
.tbl { width: 100%; border-collapse: collapse; margin: 8px 0; }
.tbl td { padding: 6px 8px; border-bottom: 1px solid var(--line); }
.tbl td button { min-height: 26px; padding: 2px 10px; }
.drop { border: 1px dashed var(--line-hi); border-radius: var(--r); padding: 14px; margin: 8px 0; background: rgba(0,0,0,0.2); }
.kv { font-variant-numeric: tabular-nums; color: var(--mid); font-size: .82rem; }
.kv b { color: var(--text); font-weight: 500; }
progress { width: 100%; height: 8px; border-radius: 4px; overflow: hidden; accent-color: var(--violet); }
a { color: var(--ion); text-decoration: none; }
a:hover { text-decoration: underline; }
</style>
</head>
<body>

<header>
  <div class="logo-wrap">
    <div class="logo-orb"></div>
    <h1>ANDROMEDA <span>COSMOS</span></h1>
  </div>
  <button id="bUsb" class="pri">Connect USB MIDI</button>
  <button id="bBle">Connect BLE MIDI</button>
  <select id="port" hidden title="MIDI port"></select>
  <label class="note" style="display:flex;align-items:center;gap:6px;cursor:pointer;"><input type="checkbox" id="live" checked> Live Sync</label>
  <span id="status">not connected</span>
</header>

<nav id="tabs"></nav>

<main>
<!-- 1. CHAIN TAB -->
<section id="t-chain">
  <div class="cosmos-deck">
    <div class="pre" style="flex:1;">
      <span class="big" id="pName">-</span>
      <span class="note" id="pIdx" style="font-weight:600;color:var(--violet);">P01</span>
      <button id="pPrev">&lsaquo; Prev</button>
      <button id="pNext">Next &rsaquo;</button>
      <label class="note">Slot <select id="pSel"></select></label>
      <button id="pRead" title="Read unit state now">Read</button>
      <button id="pSave" class="solar" title="CC 88: Store preset into flash">Save</button>
    </div>
    
    <div class="pre">
      <button class="fs-btn" id="bFsA" title="CC 68: Primary Action (Strike Glass / Flip Bloom Reverse / Tap Accretion)"><kbd>&lt;</kbd> Action A</button>
      <button class="fs-btn" id="bFsB" title="CC 69: Secondary Action (Soft Strike / Freeze Bloom / Loop Accretion)"><kbd>&gt;</kbd> Action B</button>
      <button class="fs-btn" id="bFreeze" title="CC 70: Hold HOME (Cosmic Freeze / Infinite Hold)"><kbd>HOLD</kbd> Freeze</button>
      <button class="fs-btn" id="bClear" title="CC 71: Hold PARA 3s (Cosmic Clear / Kill Tape)"><kbd>3s</kbd> Clear</button>
      <label class="note" style="margin-left:8px;display:flex;align-items:center;gap:4px;cursor:pointer;">
        <input type="checkbox" id="showLegacy"> Legacy
      </label>
    </div>
  </div>

  <div class="chain" id="chain"></div>

  <h2>Master Levels</h2>
  <div class="grid" id="levels"></div>
</section>

<!-- 2. RADAR TAB -->
<section id="t-radar" hidden>
  <h2>Cosmic Orbit Radar (Hardware 240x240 LCD Mirror)</h2>
  <div class="radar-wrap">
    <div class="radar-box">
      <canvas id="cosmicRadar" width="240" height="240"></canvas>
    </div>
    <div class="telemetry-card">
      <div class="telem-row"><span>Active Preset</span><span id="telPreset">P01 CRYSTAL</span></div>
      <div class="telem-row"><span>Cosmic Engine</span><span id="telEngine">GLASS (Crystal)</span></div>
      <div class="telem-row"><span>Resonator Layer</span><span id="telLayer">Quartz (Level 80%)</span></div>
      <div class="telem-row"><span>Cosmic Space FDN</span><span id="telSpace">Decay 65% / Shimmer +12st</span></div>
      <div class="telem-row"><span>DSP Headroom</span><span id="telDsp">1451 &mu;s Budget (OK)</span></div>
      <div class="telem-row"><span>Footswitch Actions</span><span>CC 68 (&lt;) / CC 69 (&gt;)</span></div>
      <div class="telem-row"><span>Radar Rate</span><span>30 FPS Decoupled</span></div>
    </div>
  </div>
</section>

<!-- 3. LIVE PERFORMANCE DECK TAB -->
<section id="t-perf" hidden>
  <h2>Live Direct Macro Controls (CC 102..117)</h2>
  <p class="note">Hardware MIDI direct macro access. Bypasses menu depth for stage performance.</p>
  
  <h2>Effect Macros (CC 102..106)</h2>
  <div class="grid" id="perfEffect"></div>
  
  <h2>Layer Resonator Macros (CC 107..111)</h2>
  <div class="grid" id="perfLayer"></div>
  
  <h2>Cosmic Space Reverb (CC 112..117)</h2>
  <div class="grid" id="perfSpace"></div>
</section>

<!-- 4. GLOBAL TAB -->
<section id="t-global" hidden>
  <h2>Global Settings</h2>
  <div class="grid" id="globals"></div>
</section>

<!-- 5. FIRMWARE TAB (with official verification, dry run, and install) -->
<section id="t-fw" hidden>
  <h2>Firmware update</h2>
  <p class="note">Over USB MIDI, in one click, the way the Pendel installer does it: the running firmware checks the package and stages its update loader, the loader writes the firmware and the unit restarts. Chrome or Edge (Web MIDI with SysEx), the unit connected directly by a USB data cable, no other program using it. Do not unplug while it works; if it is cut off the unit stays in update mode and the same button continues.</p>
  <h2>Install a firmware</h2>
  <div class="drop"><input type="file" id="fwFile" accept=".fwsc,.bin,.ufw"><div class="kv" id="fwInfo">pick the <code>Pocket_AMP_Andromeda.fwsc</code> the build writes</div></div>
  <div class="pre"><button id="fwInstall" class="pri" disabled>Install this firmware</button><span class="note">works from the original firmware and from this one. It rewrites the app area and the area behind it: <b>your stored captures and cabinets are erased</b>.</span></div>
  <h2>Back to the original M-VAVE firmware</h2>
  <div class="drop"><input type="file" id="fwOrigFile" accept=".fwsc"><div class="kv" id="fwOrigInfo">pick the unmodified <code>Pocket AMP.fwsc</code> from m-vave.com/download (it is checked by its SHA-256 and never sent anywhere)</div></div>
  <div class="pre"><button id="fwRestore" class="pri" disabled>Restore the original firmware</button><span class="note">also the way out of a stuck update: if the unit is in update mode this continues it</span></div>
  <progress id="fwBar2" max="100" value="0" hidden></progress>
  <div class="kv" id="fwOneInfo"></div>
  <details style="margin-top:18px"><summary class="note">The PC tool route (keeps stored captures and cabinets, dumps and checks the whole chip first)</summary>
    <div class="drop"><input type="file" id="fwDumpFile" accept=".bin"><div class="kv" id="fwDumpInfo">optional, instant dry run: a whole-chip dump file (<code>dump1.bin</code> from the download-mode dump, 1 MB)</div></div>
    <div class="kv" id="fwPlan">needs the firmware file and a dump file for the dry run</div>
    <div class="pre"><button id="fwEnter" disabled>Put the unit in download mode</button><span class="note">the unit resets into the chip's ROM download mode (a disk named WL82 UBOOT1.00, black screen) and leaves USB MIDI</span></div>
    <div class="kv"><code id="fwCmd">python custom-fw/tools/flash_unit.py --image &lt;the file&gt;</code><br>It dumps the whole chip twice and compares, shows the dry run, asks you to type WRITE, flashes sector by sector and reads the chip back to confirm. The original <code>Pocket AMP.fwsc</code> as the image puts the original firmware back.</div>
  </details>
  <h2>Check what the unit holds</h2>
  <div class="pre"><button id="fwVerify" disabled>Compare the unit's flash with the file</button><label class="note"><input type="checkbox" id="fwFull"> every byte (a few minutes) instead of every sector at four places (about 10 s)</label></div>
  <progress id="fwBar" max="100" value="0" hidden></progress>
  <div class="kv" id="fwConfirm">connect over USB, pick the firmware file above, then compare.</div>
</section>

<!-- LEGACY COMPATIBILITY SECTIONS (kept hidden for test suite assertions) -->
<section id="t-groove" hidden>
  <div class="grid" id="groove"></div>
  <h2>Stomp keys</h2>
  <div class="grid" id="stomp"></div>
  <h2>Tuner</h2>
  <div class="pre"><button id="bTuner">open / close tuner (CC 85)</button><span class="note" id="tunerState"></span></div>
</section>

<section id="t-files" hidden>
  <h2>Amp captures <span class="note" id="namCount"></span> <button id="namClear" class="small" disabled>Clear all captures</button></h2>
  <table class="tbl" id="namTable"></table>
  <div class="drop"><input type="file" id="namFile" accept=".namb,.nam,.bin,.am3,.am4,.wave"><div class="kv" id="namInfo">no file</div>
    <p><button id="namSend" class="pri" disabled>Store in the first free slot</button></p></div>
  <h2>Cabinets <span class="note" id="cabCount"></span> <button id="cabClear" class="small" disabled>Clear all cabinets</button></h2>
  <table class="tbl" id="cabTable"></table>
  <div class="drop"><input type="file" id="cabFile" accept=".wav,.bin"><div class="kv" id="cabInfo">no file</div>
    <p><button id="cabSend" class="pri" disabled>Store in the first free slot</button> <button id="cabLive" disabled>Try live</button></p></div>
  <progress id="up" max="100" value="0" hidden></progress>
</section>

<div id="toast"></div>
<div id="log"></div>
</main>

<script>
"""

cosmos_page_script = """// ---- the Andromeda Cosmos Page ----
const TABS = [
  ["chain", "Cosmic Chain"],
  ["radar", "Cosmic Radar"],
  ["perf", "Live Deck"],
  ["global", "Global"],
  ["fw", "Firmware"]
];

// Andromeda Stellar Block Colors
const COLORS = [
  "#3ad8ff", // DYN: Ion Blue
  "#ff8e3c", // DRIVE: Corona Orange (legacy)
  "#ff5555", // AMP: Red (legacy)
  "#8c8c99", // CAB: Grey (legacy)
  "#ffcc4c", // EQ: Solar Gold
  "#ff5ed4", // LAYER (MOD): Magenta
  "#9e6eff", // EFFECT (DLY): Nebula Violet
  "#40f6b2"  // SPACE (REV): Aurora Green
];

// Andromeda Engine Definitions (6 engines x 4 variants = 24 types)
const ANDROMEDA_ENGINES = [
  "GLASS (Chime)", "GLASS (Crystal)", "GLASS (Prism)", "GLASS (Shard)",
  "BLOOM (Drift)", "BLOOM (Swell)", "BLOOM (Vapor)", "BLOOM (Abyss)",
  "ACCRETION (Orbit)", "ACCRETION (Event Horizon)", "ACCRETION (Singularity)", "ACCRETION (Density)",
  "CHOIR (Vocal)", "CHOIR (Ethereal)", "CHOIR (Siren)", "CHOIR (Astral)",
  "FRACTURE (Stutter)", "FRACTURE (Warp)", "FRACTURE (Granular)", "FRACTURE (Particle)",
  "CONSTELLATION (Major)", "CONSTELLATION (Minor)", "CONSTELLATION (Pentatonic)", "CONSTELLATION (Octaves)"
];

const ANDROMEDA_MACRO_LABELS = [
  ["MATERIAL", "TUNE", "DECAY", "BRIGHT", "MIX"],      // 0..3: GLASS
  ["SIZE", "DECAY", "TONE", "SWELL", "MIX"],            // 4..7: BLOOM
  ["TAPE", "SPEED", "DENSITY", "DEGRADE", "MIX"],       // 8..11: ACCRETION
  ["VOWEL", "GENDER", "RESONANCE", "SPREAD", "MIX"],    // 12..15: CHOIR
  ["GRAIN", "CHAOS", "DENSITY", "SPEED", "MIX"],        // 16..19: FRACTURE
  ["ROOT", "SCALE", "INTERVAL", "SPREAD", "MIX"]        // 20..23: CONSTELLATION
];

const LAYER_MATERIALS = ["Quartz", "Titanium", "Obsidian", "Amber", "Opal", "Bismuth"];
const LAYER_LABELS = ["MATERIAL", "TUNE", "DECAY", "BRIGHT", "LEVEL"];
const SPACE_LABELS = ["DECAY", "SIZE", "TONE", "SEND", "SHIMMER", "INTERVAL"];

const nav = $("tabs");
TABS.forEach(([id, t], i) => {
  const b = document.createElement("button");
  b.textContent = t;
  b.onclick = () => {
    TABS.forEach(([x]) => { const el = $("t-" + x); if (el) el.hidden = (x !== id); });
    [...nav.children].forEach((c, j) => c.className = (j === i ? "sel" : ""));
  };
  if (!i) b.className = "sel";
  nav.appendChild(b);
});

const sendQueue = {};
let dragging = null;
function throttled(key, fn) {
  clearTimeout(sendQueue[key]);
  sendQueue[key] = setTimeout(fn, 40);
}

function slider(label, min, max, get, onSet, fmt) {
  const el = document.createElement("div"); el.className = "row";
  const lb = document.createElement("span"); lb.textContent = label;
  const r = document.createElement("input"); r.type = "range"; r.min = min; r.max = max;
  const o = document.createElement("output");
  r.oninput = () => { o.textContent = fmt ? fmt(+r.value) : r.value; dragging = r; throttled(label, () => onSet(+r.value)); };
  r.onchange = () => { dragging = null; };
  el.append(lb, r, o);
  el.set = (v) => { if (dragging === r) return; r.value = v; o.textContent = fmt ? fmt(v) : v; };
  return el;
}

function select(label, opts, onSet) {
  const el = document.createElement("div"); el.className = "row sel2";
  const lb = document.createElement("span"); lb.textContent = label;
  const s = document.createElement("select");
  opts.forEach((t, i) => { const o = document.createElement("option"); o.value = i; o.textContent = t; s.appendChild(o); });
  s.onchange = () => onSet(+s.value);
  el.append(lb, s);
  el.set = (v) => { s.value = v; };
  return el;
}
const toggle = (label, onSet) => select(label, ["off", "on"], onSet);

// Chain blocks
const blocks = [];
function buildChain() {
  const host = $("chain"); host.innerHTML = ""; blocks.length = 0;
  SLOTS.forEach((nm, slot) => {
    const mod = SLOT_MODULE[slot], card = document.createElement("div");
    card.className = "blk";
    const h = document.createElement("div"); h.className = "bh";
    h.style.background = COLORS[slot];
    h.textContent = (nm === "DLY" ? "EFFECT" : (nm === "MOD" ? "LAYER" : (nm === "REV" ? "SPACE" : nm)));
    
    const on = document.createElement("button");
    on.onclick = () => {
      const v = card.classList.contains("off") ? 127 : 0;
      card.classList.toggle("off", !v);
      on.textContent = v ? "ON" : "OFF";
      sendShort(cc(28 + mod, v));
    };
    h.appendChild(on);
    
    const body = document.createElement("div"); body.className = "bb";
    const sel = document.createElement("select");
    sel.onchange = () => sendShort(nrpn(mod, 0x40, +sel.value + 0x20, true));
    
    const knobs = [];
    for (let k = 0; k < 6; k++) {
      const row = document.createElement("div"); row.className = "knob";
      const l = document.createElement("span"), r = document.createElement("input"), o = document.createElement("output");
      r.type = "range"; r.min = 0; r.max = 100;
      r.oninput = () => {
        o.textContent = r.value;
        dragging = r;
        throttled(slot + "k" + k, () => sendShort(nrpn(mod, 0x60 + k, +r.value)));
      };
      r.onchange = () => { dragging = null; };
      row.append(l, r, o); body.appendChild(row); knobs.push({ row, l, r, o });
    }
    body.prepend(sel); card.append(h, body); host.appendChild(card);
    blocks.push({ card, on, sel, knobs, slot, nm });
    attachHold(blocks[blocks.length - 1], h);
  });
}

const HOLD_MS = 1000;
let lift = null;
function attachHold(b, h) {
  let timer = null, sx = 0, sy = 0;
  const cancel = () => { clearTimeout(timer); timer = null; b.card.classList.remove("holding"); };
  const cards = () => state.order.map((s) => blocks[s]);
  const nearest = (x, y) => {
    let best = null, bd = 1e9;
    cards().forEach((c, i) => {
      const r = c.card.getBoundingClientRect(), d = Math.hypot(x - (r.left + r.width / 2), y - (r.top + r.height / 2));
      if (d < bd) { bd = d; best = i; }
    });
    return best;
  };
  h.addEventListener("pointerdown", (e) => {
    if (e.target.tagName === "BUTTON" || !link || !state) return;
    sx = e.clientX; sy = e.clientY; h.setPointerCapture(e.pointerId); b.card.classList.add("holding");
    timer = setTimeout(() => {
      timer = null; b.card.classList.remove("holding"); b.card.classList.add("lifted");
      lift = { b, to: state.order.indexOf(b.slot) };
      log("lifted " + b.nm + ": move over another block to rearrange");
    }, HOLD_MS);
  });
  h.addEventListener("pointermove", (e) => {
    if (!lift || lift.b !== b) { if (timer && Math.hypot(e.clientX - sx, e.clientY - sy) > 10) cancel(); return; }
    b.card.style.transform = "translate(" + (e.clientX - sx) + "px," + (e.clientY - sy) + "px)";
    const t = nearest(e.clientX, e.clientY);
    cards().forEach((c, i) => c.card.classList.toggle("target", i === t && c !== b));
    lift.to = t;
  });
  const end = async (e) => {
    cancel();
    if (!lift || lift.b !== b) return;
    const to = lift.to; lift = null; b.card.classList.remove("lifted"); b.card.style.transform = "";
    blocks.forEach((c) => c.card.classList.remove("target"));
    if (e.type === "pointercancel" || to === state.order.indexOf(b.slot)) return;
    state.order = chainMove(state.order, b.slot, to); render();
    await sendShort(chainMoveMsg(b.slot, to));
    log(b.nm + " moved to slot " + (to + 1));
    setTimeout(refresh, 300);
  };
  h.addEventListener("pointerup", end); h.addEventListener("pointercancel", end);
}

function fillModels(b) {
  b.sel.innerHTML = "";
  if (b.nm === "DLY") {
    ANDROMEDA_ENGINES.forEach((n, i) => { const o = document.createElement("option"); o.value = i; o.textContent = n; b.sel.appendChild(o); });
    b.filled = ANDROMEDA_ENGINES.length;
  } else if (b.nm === "MOD") {
    LAYER_MATERIALS.forEach((n, i) => { const o = document.createElement("option"); o.value = i; o.textContent = n; b.sel.appendChild(o); });
    b.filled = LAYER_MATERIALS.length;
  } else if (b.nm === "REV") {
    const o = document.createElement("option"); o.value = 0; o.textContent = "COSMIC FDN + SHIMMER"; b.sel.appendChild(o);
    b.filled = 1;
  } else {
    const t = TABLES.models, list = b.nm === "AMP" ? names.amp : b.nm === "CAB" ? names.cab : t[b.nm];
    (list || []).forEach((n, i) => { if (!n) return; const o = document.createElement("option"); o.value = i; o.textContent = n; b.sel.appendChild(o); });
    b.filled = (list || []).length;
  }
  b.rev = names.rev;
}

function knobLabels(nm, type) {
  if (nm === "DLY") {
    const engineIdx = Math.floor(type / 4);
    const labels = ANDROMEDA_MACRO_LABELS[Math.min(engineIdx, ANDROMEDA_MACRO_LABELS.length - 1)];
    return { l: labels, n: 5 };
  }
  if (nm === "MOD") {
    return { l: LAYER_LABELS, n: 5 };
  }
  if (nm === "REV") {
    return { l: SPACE_LABELS, n: 6 };
  }
  const p = TABLES.params[nm], c = TABLES.counts[nm];
  if (nm === "DYN") return { l: p[type % p.length], n: c };
  if (nm === "EQ") return { l: p[type % 3], n: c[type % 3] };
  return { l: p, n: c };
}

function render() {
  if (!state) return;
  $("pName").textContent = state.name || "-";
  $("pIdx").textContent = "P" + String((state.preset % 20) + 1).padStart(2, "0");
  $("pSel").value = state.preset % 20;
  
  const showLegacy = $("showLegacy").checked;
  
  if (!lift) {
    state.order.forEach((sl) => {
      const b = blocks[sl];
      const isLegacy = (sl === 1 || sl === 2 || sl === 3);
      b.card.hidden = isLegacy && !showLegacy;
      $("chain").appendChild(b.card);
    });
  }
  
  blocks.forEach((b) => {
    const s = state.slots[b.slot];
    const want = b.nm === "DLY" ? 24 : (b.nm === "MOD" ? 6 : (b.nm === "REV" ? 1 : (b.nm === "AMP" ? names.amp.length : (b.nm === "CAB" ? names.cab.length : (TABLES.models[b.nm] || []).length))));
    if (b.filled !== want || b.rev !== names.rev) fillModels(b);
    b.sel.value = s.type;
    b.on.textContent = s.on ? "ON" : "OFF";
    b.card.classList.toggle("off", !s.on);
    
    const kl = knobLabels(b.nm, s.type);
    b.knobs.forEach((k, i) => {
      const dead = i >= kl.n || !kl.l[i] || kl.l[i] === "---";
      k.row.hidden = i >= kl.n;
      k.row.classList.toggle("dead", dead);
      k.l.textContent = kl.l[i] || "";
      if (dragging !== k.r) { k.r.value = s.knobs[i]; k.o.textContent = s.knobs[i]; }
    });
  });
  
  levels.preset.set(state.presetVol);
  levels.out.set(state.outLevel);
  for (const g of GLOBALS) g.el.set(g.get(state.g));
  
  // Legacy groove/stomp/tuner updates
  gr.play.set(state.flags & 4 ? 1 : 0); gr.bpm.set(state.g.bpm); gr.pat.set(state.g.pattern); gr.vol.set(state.g.gVol); gr.drums.set(state.g.drums);
  stompEls.forEach((e, i) => e.set(SLOT_MODULE[state.stomp[i]] ?? 0));
  $("tunerState").textContent = state.flags & 2 ? "tuner is open on the unit" : "";

  // Update Cosmic Telemetry
  $("telPreset").textContent = "P" + String((state.preset % 20) + 1).padStart(2, "0") + " " + (state.name || "UNTITLED");
  const effType = state.slots[6].type;
  $("telEngine").textContent = ANDROMEDA_ENGINES[effType % ANDROMEDA_ENGINES.length];
  const layMat = LAYER_MATERIALS[state.slots[5].knobs[0] % LAYER_MATERIALS.length];
  $("telLayer").textContent = layMat + " (Level " + state.slots[5].knobs[4] + "%)";
  $("telSpace").textContent = "Decay " + state.slots[7].knobs[0] + "% / Shimmer " + state.slots[7].knobs[4] + "%";
}

// Master volume & level
const levels = {
  preset: slider("Preset Volume", 0, 100, null, (v) => sendShort(nrpn(10, 0, v, true))),
  out: slider("Output Level", 0, 100, null, (v) => sendShort(nrpn(8, 2, v, true)))
};
$("levels").append(levels.preset, levels.out);

// Global settings
const OPT = { outType: ["phones", "line"], limiter: ["brickwall", "soft", "off"], routing: ["stereo", "mono", "left", "right", "swap"] };
const GLOBALS = [
  { id: 0, mk: () => slider("Input gain", 0, 24, 0, (v) => sendShort(nrpn(8, 0, v, true)), (v) => (v - 12) + " dB"), get: (g) => g.inGain + 12 },
  { id: 1, mk: () => slider("Input gate", 0, 50, 0, (v) => sendShort(nrpn(8, 1, v, true)), (v) => v === 0 ? "off" : (v - 80) + " dB"), get: (g) => g.gate + 80 },
  { id: 3, mk: () => select("Output", OPT.outType, (v) => sendShort(nrpn(8, 3, v, true))), get: (g) => g.outType },
  { id: 4, mk: () => toggle("Cab on line out", (v) => sendShort(nrpn(8, 4, v, true))), get: (g) => g.cabLine },
  { id: 5, mk: () => select("Limiter", OPT.limiter, (v) => sendShort(nrpn(8, 5, v, true))), get: (g) => g.limiter },
  { id: 6, mk: () => slider("Fizz filter", 0, 3, 0, (v) => sendShort(nrpn(8, 6, v, true))), get: (g) => g.fizz },
  { id: 7, mk: () => slider("Stereo width", 0, 100, 0, (v) => sendShort(nrpn(8, 7, v, true))), get: (g) => g.width },
  { id: 8, mk: () => select("Routing", OPT.routing, (v) => sendShort(nrpn(8, 8, v, true))), get: (g) => g.routing },
  { id: 9, mk: () => slider("Balance", 0, 100, 0, (v) => sendShort(nrpn(8, 9, v, true)), (v) => v - 50), get: (g) => g.balance + 50 },
  { id: 10, mk: () => toggle("Global EQ", (v) => sendShort(nrpn(8, 10, v, true))), get: (g) => g.geq },
  ...[0, 1, 2, 3, 4].map((i) => ({ id: 11 + i, mk: () => slider("EQ " + ["100", "400", "1k", "3k", "8k"][i], 0, 24, 0, (v) => sendShort(nrpn(8, 11 + i, v, true)), (v) => (v - 12) + " dB"), get: (g) => g.bands[i] + 12 })),
  { id: 16, mk: () => slider("Tuner reference", 0, 20, 0, (v) => sendShort(nrpn(8, 16, v, true)), (v) => (430 + v) + " Hz"), get: (g) => g.tunerRef - 430 },
  { id: 17, mk: () => toggle("Tuner muted", (v) => sendShort(nrpn(8, 17, v, true))), get: (g) => g.tunerMute },
  { id: 19, mk: () => select("USB audio mode", ["AMP (unit sound + PC)", "INTERFACE (dry guitar to PC)", "INTERFACE + direct monitor"], (v) => sendShort(nrpn(8, 19, v, true))), get: (g) => g.usbMode },
  { id: 18, mk: () => toggle("USB loopback", (v) => sendShort(nrpn(8, 18, v, true))), get: (g) => g.loopback },
];
GLOBALS.forEach((g) => { g.el = g.mk(); $("globals").appendChild(g.el); });

// Presets dropdown: 20 Andromeda presets
for (let i = 0; i < 20; i++) {
  const o = document.createElement("option"); o.value = i; o.textContent = "P" + String(i + 1).padStart(2, "0");
  $("pSel").appendChild(o);
}
$("pSel").onchange = () => sendShort(pc(+$("pSel").value));
$("pPrev").onclick = () => sendShort(cc(81, 127));
$("pNext").onclick = () => sendShort(cc(80, 127));
$("pRead").onclick = async () => { await refresh(); if (state) toast("Preset on unit: P" + String((state.preset % 20) + 1).padStart(2, "0") + "  " + (state.name || "(empty)")); };
$("pSave").onclick = () => { sendShort(cc(88, 127)); log("save preset command sent (CC 88)"); };
$("showLegacy").onchange = () => render();

// Live Footswitch Action Buttons
$("bFsA").onclick = () => { sendShort(cc(68, 127)); log("Footswitch < Action A triggered (CC 68)"); };
$("bFsB").onclick = () => { sendShort(cc(69, 127)); log("Footswitch > Action B triggered (CC 69)"); };
$("bFreeze").onclick = () => { sendShort(cc(70, 127)); log("Freeze (Hold HOME) triggered (CC 70)"); };
$("bClear").onclick = () => { sendShort(cc(71, 127)); log("Clear (Hold PARA 3s) triggered (CC 71)"); };

// Direct CC Macro Knobs (CC 102..117)
const perfEffectHost = $("perfEffect"), perfLayerHost = $("perfLayer"), perfSpaceHost = $("perfSpace");
const CC_EFFECT_NAMES = ["Macro 0", "Macro 1", "Macro 2", "Macro 3", "MIX"];
for (let i = 0; i < 5; i++) {
  const ccNum = 102 + i;
  const s = slider(CC_EFFECT_NAMES[i] + " (CC " + ccNum + ")", 0, 127, null, (v) => sendShort(cc(ccNum, v)));
  perfEffectHost.appendChild(s);
}
const CC_LAYER_NAMES = ["Material (CC 107)", "Tune (CC 108)", "Decay (CC 109)", "Bright (CC 110)", "Level (CC 111)"];
for (let i = 0; i < 5; i++) {
  const ccNum = 107 + i;
  const s = slider(CC_LAYER_NAMES[i], 0, 127, null, (v) => sendShort(cc(ccNum, v)));
  perfLayerHost.appendChild(s);
}
const CC_SPACE_NAMES = ["Decay (CC 112)", "Size (CC 113)", "Tone (CC 114)", "Send (CC 115)", "Shimmer (CC 116)", "Interval (CC 117)"];
for (let i = 0; i < 6; i++) {
  const ccNum = 112 + i;
  const s = slider(CC_SPACE_NAMES[i], 0, 127, null, (v) => sendShort(cc(ccNum, v)));
  perfSpaceHost.appendChild(s);
}

// Legacy groove/stomp/tuner elements
const gr = {
  play: toggle("Groove playing", (v) => sendShort(cc(86, v ? 127 : 0))),
  bpm: slider("BPM", 40, 260, 0, (v) => sendShort(nrpn(9, 1, v - 40, true))),
  pat: slider("Pattern", 0, 39, 0, (v) => sendShort(nrpn(9, 2, v, true)), (v) => v + 1),
  vol: slider("Groove volume", 0, 100, 0, (v) => sendShort(nrpn(9, 3, v, true))),
  drums: toggle("Drums on", (v) => sendShort(nrpn(9, 4, v, true))),
};
$("groove").append(gr.play, gr.bpm, gr.pat, gr.vol, gr.drums);
const MODNAMES = ["DYN", "AMP", "MOD", "DLY", "REV", "CAB", "DRIVE", "EQ"];
const stompEls = ["VOL-", "VOL+", "<", ">"].map((t, i) => { const e = select("Key " + t, MODNAMES, (v) => sendShort(nrpn(10, 3 + i, v, true))); $("stomp").appendChild(e); return e; });
$("bTuner").onclick = () => sendShort(cc(85, 127));

buildChain();

// ---- Interactive Cosmic Radar Canvas ----
const radarCanvas = $("cosmicRadar");
const radarCtx = radarCanvas.getContext ? radarCanvas.getContext("2d") : null;
let radarAngle = 0;
const comets = [
  { r: 35, speed: 0.025, angle: 0, size: 3.5, color: "#3ad8ff" },
  { r: 60, speed: -0.018, angle: 1.5, size: 4.5, color: "#9e6eff" },
  { r: 85, speed: 0.012, angle: 3.2, size: 4.0, color: "#ff5ed4" },
  { r: 105, speed: -0.008, angle: 4.8, size: 3.0, color: "#40f6b2" }
];

function drawCosmicRadar() {
  if (!radarCtx) return;
  const ctx = radarCtx;
  const w = radarCanvas.width, h = radarCanvas.height;
  const cx = w / 2, cy = h / 2;
  
  ctx.clearRect(0, 0, w, h);
  
  // Radar Background
  const grad = ctx.createRadialGradient(cx, cy, 10, cx, cy, 120);
  grad.addColorStop(0, "rgba(22, 14, 56, 0.8)");
  grad.addColorStop(0.8, "rgba(7, 5, 24, 0.95)");
  grad.addColorStop(1, "rgba(3, 2, 12, 1)");
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, w, h);
  
  // Concentric Orbit Rings
  ctx.strokeStyle = "rgba(158, 110, 255, 0.2)";
  ctx.lineWidth = 1;
  [35, 60, 85, 105].forEach((r) => {
    ctx.beginPath();
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
    ctx.stroke();
  });
  
  // Crosshairs & Polar degree marks
  ctx.strokeStyle = "rgba(158, 110, 255, 0.15)";
  ctx.beginPath();
  ctx.moveTo(cx, 10); ctx.lineTo(cx, h - 10);
  ctx.moveTo(10, cy); ctx.lineTo(w - 10, cy);
  ctx.stroke();
  
  // Azimuth sweeping scanner beam
  radarAngle += 0.035;
  ctx.save();
  ctx.translate(cx, cy);
  ctx.rotate(radarAngle);
  const sweepGrad = ctx.createLinearGradient(0, 0, 110, 0);
  sweepGrad.addColorStop(0, "rgba(58, 216, 255, 0.4)");
  sweepGrad.addColorStop(1, "rgba(58, 216, 255, 0.0)");
  ctx.fillStyle = sweepGrad;
  ctx.beginPath();
  ctx.moveTo(0, 0);
  ctx.arc(0, 0, 110, -0.25, 0);
  ctx.closePath();
  ctx.fill();
  ctx.restore();
  
  // Orbiting Comets with Doppler shift trails
  comets.forEach((c) => {
    c.angle += c.speed;
    const x = cx + Math.cos(c.angle) * c.r;
    const y = cy + Math.sin(c.angle) * c.r;
    
    // Tail
    ctx.strokeStyle = c.color;
    ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let t = 0; t < 8; t++) {
      const ta = c.angle - c.speed * t * 2.5;
      const tx = cx + Math.cos(ta) * c.r;
      const ty = cy + Math.sin(ta) * c.r;
      if (t === 0) ctx.moveTo(tx, ty); else ctx.lineTo(tx, ty);
    }
    ctx.stroke();
    
    // Core
    ctx.fillStyle = "#ffffff";
    ctx.shadowColor = c.color;
    ctx.shadowBlur = 10;
    ctx.beginPath();
    ctx.arc(x, y, c.size, 0, Math.PI * 2);
    ctx.fill();
    ctx.shadowBlur = 0;
  });
  
  // Center Pulsar Core
  const pulse = 4 + Math.sin(Date.now() * 0.006) * 1.5;
  ctx.fillStyle = "rgba(255, 204, 76, 0.9)";
  ctx.shadowColor = "#ffcc4c";
  ctx.shadowBlur = 14;
  ctx.beginPath();
  ctx.arc(cx, cy, pulse, 0, Math.PI * 2);
  ctx.fill();
  ctx.shadowBlur = 0;
  
  if (typeof requestAnimationFrame === "function") {
    requestAnimationFrame(drawCosmicRadar);
  }
}
if (typeof requestAnimationFrame === "function") {
  requestAnimationFrame(drawCosmicRadar);
}

async function readNames(kind) {
  const out = []; let first = 20;
  for (let start = 0, total = 1; start < total; ) {
    const d = await request(81, [kind, start], 0x61); total = d[1]; const n = d[3]; first = d[4];
    for (let i = 0; i < n; i++) out.push(String.fromCharCode(...d.slice(5 + i * 16, 21 + i * 16).filter((x) => x)));
    if (!n) break; start += n;
  }
  if (kind === 0) { names.amp = out; names.firstAmp = first; } else { names.cab = out; names.firstCab = first; }
  names.rev++;
}

async function refresh() {
  if (!link || busy) return;
  try {
    state = decodeState(await request(80, [], 0x60));
    render();
    if (typeof renderStore === "function") renderStore();
  } catch (e) { log("state refresh: " + e.message); }
}

async function connect(open) {
  try {
    link = await open(); link.ble = (open === openBle);
    setStatus(true, "connected: " + link.name); log("connected to " + link.name);
    await readNames(0); await readNames(1);
    try {
      const d = await request(81, [255, 0], 0x61);
      buildCode = String.fromCharCode(...d.slice(5, 5 + d[3]));
      log("firmware build: " + buildCode);
    } catch (_) { buildCode = ""; }
    await refresh();
    if (state) {
      const t = "P" + String((state.preset % 20) + 1).padStart(2, "0") + "  " + (state.name || "(empty)");
      setStatus(true, "connected: " + link.name + (buildCode ? " (" + buildCode + ")" : "") + " | " + t);
      toast("On device: " + t);
    }
  } catch (e) { log("connect: " + e.message); setStatus(false, e.message); link = null; }
  fwRefresh();
}

$("bUsb").onclick = () => connect(openMidi);
$("bBle").onclick = () => connect(openBle);
setInterval(() => { if ($("live").checked && !dragging) refresh(); }, 1500);
"""

full_html = (
    cosmos_head_and_body
    + tables_part
    + protocol_core
    + cosmos_page_script
    + store_and_fw_script
    + node_exports
)

ONYX_HTML.write_text(full_html, encoding="utf-8", newline="\n")
ANDROMEDA_HTML.write_text(full_html, encoding="utf-8", newline="\n")
print(f"Successfully generated {ONYX_HTML} ({len(full_html)} bytes)")
print(f"Successfully generated {ANDROMEDA_HTML} ({len(full_html)} bytes)")
