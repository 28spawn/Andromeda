#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors
"""Build and update the Cosmic Chain in Andromeda_Web_Editor.html.

Reworks the signal chain interface to match the exact device available variables,
implements the stunning Cosmos visual theme with signal pipeline,
supports all 24 presets with morphing macro controls and calibrated units,
and synchronizes with Onyx_Web_Editor.html while preserving all protocol test exports.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent.parent
ANDROMEDA_HTML = ROOT / "custom-fw" / "web" / "Andromeda_Web_Editor.html"
ONYX_HTML = ROOT / "custom-fw" / "web" / "Onyx_Web_Editor.html"

with open(ANDROMEDA_HTML, "r", encoding="utf-8") as f:
    html = f.read()

# 1. Update CSS
cosmos_css = """
/* ---- Cosmos Chain Enhanced Styles ---- */
.cosmos-pipeline {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 6px;
  padding: 10px 16px;
  margin-bottom: 18px;
  background: rgba(14, 11, 36, 0.7);
  backdrop-filter: blur(10px);
  border: 1px solid var(--line);
  border-radius: var(--r);
  overflow-x: auto;
}
.pipe-node {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  padding: 6px 14px;
  border-radius: 6px;
  background: var(--raise);
  border: 1px solid var(--line);
  font-size: 0.72rem;
  font-weight: 700;
  letter-spacing: .08em;
  color: var(--mid);
  cursor: pointer;
  transition: all .2s ease;
  user-select: none;
  white-space: nowrap;
}
.pipe-node:hover {
  border-color: var(--violet);
  color: var(--text);
  box-shadow: 0 0 10px rgba(158, 110, 255, 0.3);
}
.pipe-node.active {
  color: #fff;
  border-color: var(--p-col, var(--violet));
  box-shadow: 0 0 12px var(--p-col, var(--violet));
}
.pipe-node.in, .pipe-node.out {
  background: transparent;
  cursor: default;
}
.pipe-node.in { color: var(--ice); border-color: rgba(176, 236, 255, 0.3); }
.pipe-node.out { color: var(--solar); border-color: rgba(255, 204, 76, 0.3); }
.pipe-line {
  flex: 1;
  height: 2px;
  background: linear-gradient(90deg, var(--line), var(--line-hi));
  min-width: 10px;
}

.chain-grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(290px, 1fr));
  gap: 16px;
  margin-bottom: 24px;
}

.cosmic-card {
  background: rgba(14, 11, 36, 0.85);
  backdrop-filter: blur(12px);
  border: 1px solid var(--line);
  border-radius: var(--r);
  overflow: hidden;
  transition: border-color .2s, box-shadow .2s, transform .2s, opacity .2s;
  box-shadow: 0 6px 24px rgba(0,0,0,0.4);
  position: relative;
}
.cosmic-card:hover {
  border-color: var(--line-hi);
  transform: translateY(-2px);
}
.cosmic-card.off {
  opacity: 0.5;
  filter: grayscale(40%);
}
.card-head {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 14px;
  border-bottom: 1px solid var(--line);
  background: linear-gradient(180deg, rgba(255,255,255,0.04) 0%, transparent 100%);
}
.card-badge {
  font-size: 0.72rem;
  font-weight: 800;
  letter-spacing: .12em;
  padding: 3px 8px;
  border-radius: 4px;
  text-transform: uppercase;
  color: #fff;
  background: var(--card-col, var(--violet));
  box-shadow: 0 0 10px var(--card-col, var(--violet));
}
.card-sub {
  font-size: 0.72rem;
  color: var(--mid);
  font-weight: 500;
}
.card-btn-toggle {
  margin-left: auto;
  font-size: 0.72rem;
  font-weight: 700;
  padding: 3px 10px;
  border-radius: 4px;
  border: 1px solid rgba(255,255,255,0.25);
  background: rgba(0,0,0,0.4);
  color: #fff;
  cursor: pointer;
  transition: all .15s ease;
}
.card-btn-toggle:hover {
  background: rgba(255,255,255,0.15);
  border-color: #fff;
}
.card-btn-toggle.on {
  background: var(--ok);
  color: #040410;
  border-color: #fff;
  box-shadow: 0 0 10px var(--ok);
}

.card-body {
  padding: 14px;
  display: flex;
  flex-direction: column;
  gap: 11px;
}

.preset-select-wrap {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.preset-select-wrap label {
  font-size: 0.70rem;
  font-weight: 700;
  color: var(--mid);
  text-transform: uppercase;
  letter-spacing: .08em;
}
.preset-select-wrap select {
  width: 100%;
  background: var(--surf);
  border: 1px solid var(--line-hi);
  font-weight: 600;
  color: var(--ice);
  padding: 6px 10px;
  border-radius: 6px;
}

.var-row {
  display: grid;
  grid-template-columns: 88px 1fr 56px;
  align-items: center;
  gap: 10px;
  font-size: 0.76rem;
}
.var-row label {
  color: var(--mid);
  font-weight: 600;
  text-transform: uppercase;
  letter-spacing: .06em;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.var-row output {
  text-align: right;
  font-variant-numeric: tabular-nums;
  color: var(--ice);
  font-weight: 700;
  font-size: 0.78rem;
}
.var-row select {
  width: 100%;
  padding: 4px 8px;
  font-size: 0.75rem;
  font-weight: 600;
  background: var(--surf);
  border: 1px solid var(--line);
  color: var(--text);
  border-radius: 4px;
}
.var-row.full {
  grid-template-columns: 88px 1fr;
}
.mode-badge {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  font-size: 0.68rem;
  font-weight: 700;
  letter-spacing: .08em;
  padding: 2px 6px;
  border-radius: 3px;
  background: rgba(255,255,255,0.06);
  color: var(--solar);
  border: 1px solid rgba(255,204,76,0.3);
}
"""

if ".cosmos-pipeline" not in html:
    html = html.replace("</style>", cosmos_css + "\n</style>", 1)

# 2. Update HTML structure for <section id="t-chain">
new_chain_section = """<!-- 1. CHAIN TAB -->
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
    </div>
  </div>

  <!-- Cosmic Signal Flow Pipeline -->
  <div class="cosmos-pipeline" id="cosmicPipeline">
    <div class="pipe-node in"><span>IN</span></div>
    <div class="pipe-line"></div>
    <div class="pipe-node active" id="pnode-dyn" title="CC 20 / CC 28: Toggle Dynamics" style="--p-col:#00f0ff;"><span>DYNAMICS</span></div>
    <div class="pipe-line"></div>
    <div class="pipe-node active" id="pnode-eff" title="CC 23 / CC 31: Toggle Cosmic Effect" style="--p-col:#9e6eff;"><span>EFFECT</span></div>
    <div class="pipe-line"></div>
    <div class="pipe-node active" id="pnode-layer" title="CC 22 / CC 30: Toggle Resonator Layer" style="--p-col:#ff5ed4;"><span>LAYER</span></div>
    <div class="pipe-line"></div>
    <div class="pipe-node active" id="pnode-space" title="CC 24 / CC 32: Toggle Space Reverb" style="--p-col:#3ad8ff;"><span>SPACE</span></div>
    <div class="pipe-line"></div>
    <div class="pipe-node active" id="pnode-shim" title="CC 25 / CC 33: Toggle Shimmer Feedback" style="--p-col:#b840ff;"><span>SHIMMER</span></div>
    <div class="pipe-line"></div>
    <div class="pipe-node active" id="pnode-eq" title="CC 27 / CC 35: Toggle Master EQ" style="--p-col:#ffcc4c;"><span>EQ</span></div>
    <div class="pipe-line"></div>
    <div class="pipe-node out"><span>OUT</span></div>
  </div>

  <!-- Andromeda Cosmic Modules Grid -->
  <div class="chain-grid" id="cosmicChain"></div>

  <h2>Master Output &amp; Levels</h2>
  <div class="grid" id="levels"></div>
</section>"""

m = re.search(r'<section id="t-chain">.*?</section>', html, re.S)
if m:
    html = html[:m.start()] + new_chain_section + html[m.end():]

# 3. Update JavaScript logic
# Find script block
script_m = re.search(r'<script>(.*?)</script>', html, re.S)
assert script_m, "script block not found"
old_script = script_m.group(1)

# We want to replace buildChain() and render() with our complete Cosmic Chain architecture
# Let's inspect where buildChain starts and render ends in old_script
chain_logic = """
// ============================================================================
// ANDROMEDA COSMIC CONSTANTS & CALIBRATIONS
// ============================================================================
const ENGINE_NAMES = ["GLASS", "BLOOM", "ACCRETION", "CHOIR", "FRACTURE", "CONSTELLATION"];
const ENGINE_COLORS = ["#9e6eff", "#3ad8ff", "#ff8e3c", "#40f6b2", "#ff5ed4", "#ffcc4c"];
const ENGINE_WAVE_NAMES = ["RING", "GROW", "SPIRAL", "VOICES", "SLICE", "ARP"];
const MATERIAL_NAMES = ["CRYSTAL", "BELL", "KALIMBA", "METAL", "WOOD", "MARIMBA"];
const VOICING_NAMES = ["OCTAVE", "FIFTH", "MAJOR", "MINOR", "MASSIVE", "CLUSTER"];
const GRID_NAMES = ["1/4", "1/8", "1/16", "1/32", "1/64"];
const SLICE_MS = [5, 7, 11, 16, 24, 35, 52, 78, 115, 170, 250];
const SCALE_NAMES = ["MAJOR", "MINOR", "DORIAN", "MIXOLYD", "PENT +", "PENT -", "LYDIAN", "WHOLE"];
const SHIMMER_INTERVALS = ["+12st (Octave Up)", "+7st (Fifth Up)", "+12 & +7st (Octave + 5th)", "+19st (Two Octaves + 5th)"];

// 24 Andromeda Presets with Engine Optgroups
const ANDROMEDA_ENGINES_CATEGORIZED = [
  { group: "GLASS (Physical Resonator)", items: ["GLASS CRYSTAL", "GLASS BELL", "GLASS KALIMBA", "GLASS MARIMBA"] },
  { group: "BLOOM (Granular Cloud)", items: ["BLOOM GARDEN", "BLOOM DUSK", "BLOOM DRIFT", "BLOOM RAW"] },
  { group: "ACCRETION (Lo-Fi Sound-on-Sound Tape)", items: ["ACCRETE SOS", "ACCRETE TAPE", "ACCRETE DECAY", "ACCRETE MIRROR"] },
  { group: "CHOIR (Ethereal Formant Voices)", items: ["CHOIR ANGELS", "CHOIR MOURN", "CHOIR DEEP", "CHOIR INFINITE"] },
  { group: "FRACTURE (Glitch & Rhythmic Slicing)", items: ["FRACT CLOCK", "FRACT WATER", "FRACT HEART", "FRACT SKIP"] },
  { group: "CONSTELLATION (Generative Arpeggiator)", items: ["CONST STARDUST", "CONST GRAVITY", "CONST DANCE", "CONST ORBIT"] }
];

function fmtMacroVal(bank, i, val) {
  if (bank === 0) { // GLASS
    if (i === 0) return MATERIAL_NAMES[Math.min(5, Math.floor(val * 6 / 101))];
    if (i === 1) { const st = Math.round((val - 50) * 24 / 100); return (st >= 0 ? "+" + st : st) + " st"; }
    return val + " %";
  }
  if (bank === 1) { // BLOOM
    if (i === 0) return Math.round(60 + val * 4.4) + " ms";
    if (i === 1) { const st = Math.round(val * 36 / 100) - 12; return (st >= 0 ? "+" + st : st) + " st"; }
    if (i === 2) return (val * 0.02).toFixed(1) + " s";
    return val + " %";
  }
  if (bank === 2) { // ACCRETION
    if (i === 1) return val < 30 ? "Dark" : (val > 70 ? "Bright" : val + " %");
    return val + " %";
  }
  if (bank === 3) { // CHOIR
    if (i === 0) return VOICING_NAMES[Math.min(5, Math.floor(val * 6 / 101))];
    if (i === 1) return Math.round(val * 25 / 100) + " CT";
    if (i === 3) return Math.round(val * 8) + " ms";
    return val + " %";
  }
  if (bank === 4) { // FRACTURE
    if (i === 0) return GRID_NAMES[Math.min(4, Math.floor(val * 5 / 101))];
    if (i === 1) return SLICE_MS[Math.min(10, Math.floor(val * 11 / 101))] + " MS";
    if (i === 3) { const st = Math.round((val - 50) * 48 / 100); return (st >= 0 ? "+" + st : st) + " ST"; }
    return val + " %";
  }
  if (bank === 5) { // CONSTELLATION
    if (i === 0) return Math.round(40 + val * 2) + " BPM";
    if (i === 1) return SCALE_NAMES[Math.min(7, Math.floor(val * 8 / 101))];
    if (i === 3) return Math.max(1, Math.round(val * 16 / 100)) + " steps";
    return val + " %";
  }
  return val + " %";
}

// Build dedicated Cosmic Chain UI
const cosmicCards = {};
function buildChain() {
  const host = $("cosmicChain");
  if (!host) return;
  host.innerHTML = "";

  // 1. DYNAMICS CARD (Slot 0, mod 0)
  {
    const card = document.createElement("div"); card.className = "cosmic-card"; card.style.setProperty("--card-col", "#00f0ff");
    card.innerHTML = `
      <div class="card-head">
        <span class="card-badge" style="background:#00f0ff;box-shadow:0 0 10px #00f0ff;color:#040410;">DYN</span>
        <span class="card-sub">Input Noise Gate</span>
        <button class="card-btn-toggle" id="btnTogDyn">ON</button>
      </div>
      <div class="card-body">
        <div class="var-row"><label>Thresh</label><input type="range" id="dyn-k0" min="0" max="100"><output id="dyn-o0">-50 dB</output></div>
        <div class="var-row"><label>Release</label><input type="range" id="dyn-k1" min="0" max="100"><output id="dyn-o1">50%</output></div>
        <div class="var-row"><label>Attack</label><input type="range" id="dyn-k2" min="0" max="100"><output id="dyn-o2">10%</output></div>
        <div class="var-row"><label>Level</label><input type="range" id="dyn-k3" min="0" max="100"><output id="dyn-o3">0 dB</output></div>
      </div>
    `;
    host.appendChild(card);
    cosmicCards.dyn = { card, tog: card.querySelector("#btnTogDyn") };
    card.querySelector("#btnTogDyn").onclick = () => {
      const v = card.classList.contains("off") ? 127 : 0;
      sendShort(cc(28, v));
    };
    for (let k = 0; k < 4; k++) {
      const r = card.querySelector("#dyn-k" + k), o = card.querySelector("#dyn-o" + k);
      r.oninput = () => {
        dragging = r;
        if (k === 0) o.textContent = Math.round(-80 + r.value * 0.5) + " dB";
        else if (k === 3) { const db = Math.round((r.value - 50) * 0.24); o.textContent = (db >= 0 ? "+" + db : db) + " dB"; }
        else o.textContent = r.value + "%";
        throttled("dyn" + k, () => sendShort(nrpn(0, 0x60 + k, +r.value)));
      };
      r.onchange = () => { dragging = null; };
    }
  }

  // 2. EFFECT CARD (Slot 6, mod 3)
  {
    const card = document.createElement("div"); card.className = "cosmic-card"; card.id = "cardEffect"; card.style.setProperty("--card-col", "#9e6eff");
    card.innerHTML = `
      <div class="card-head">
        <span class="card-badge" id="effBadge">EFFECT</span>
        <span class="mode-badge" id="effWave">RING</span>
        <button class="card-btn-toggle" id="btnTogEff">ON</button>
      </div>
      <div class="card-body">
        <div class="preset-select-wrap">
          <label>Engine Preset (6 Families &times; 4 Variants)</label>
          <select id="effTypeSel"></select>
        </div>
        <div id="effMacrosHost" style="display:flex;flex-direction:column;gap:11px;"></div>
        <div class="var-row"><label>Effect Mix</label><input type="range" id="eff-mix" min="0" max="100"><output id="eff-mix-o">50%</output></div>
        <div class="var-row"><label>Space Send</label><input type="range" id="eff-send" min="0" max="100"><output id="eff-send-o">40%</output></div>
      </div>
    `;
    host.appendChild(card);
    cosmicCards.eff = { card, tog: card.querySelector("#btnTogEff"), sel: card.querySelector("#effTypeSel") };
    
    // Populate categorized presets
    const sel = card.querySelector("#effTypeSel");
    let pIdx = 0;
    ANDROMEDA_ENGINES_CATEGORIZED.forEach((grp) => {
      const og = document.createElement("optgroup"); og.label = grp.group;
      grp.items.forEach((item) => {
        const opt = document.createElement("option"); opt.value = pIdx; opt.textContent = item;
        og.appendChild(opt);
        pIdx++;
      });
      sel.appendChild(og);
    });
    sel.onchange = () => sendShort(nrpn(3, 0x40, +sel.value + 0x20, true));
    card.querySelector("#btnTogEff").onclick = () => {
      const v = card.classList.contains("off") ? 127 : 0;
      sendShort(cc(31, v));
    };

    // Knobs 4 (MIX) and 5 (SPACE SEND)
    const rMix = card.querySelector("#eff-mix"), oMix = card.querySelector("#eff-mix-o");
    rMix.oninput = () => { dragging = rMix; oMix.textContent = rMix.value + "%"; throttled("effmix", () => sendShort(nrpn(3, 0x64, +rMix.value))); };
    rMix.onchange = () => { dragging = null; };
    const rSend = card.querySelector("#eff-send"), oSend = card.querySelector("#eff-send-o");
    rSend.oninput = () => { dragging = rSend; oSend.textContent = rSend.value + "%"; throttled("effsend", () => sendShort(nrpn(3, 0x65, +rSend.value))); };
    rSend.onchange = () => { dragging = null; };
  }

  // 3. LAYER CARD (Slot 5, mod 2)
  {
    const card = document.createElement("div"); card.className = "cosmic-card"; card.style.setProperty("--card-col", "#ff5ed4");
    card.innerHTML = `
      <div class="card-head">
        <span class="card-badge" style="background:#ff5ed4;box-shadow:0 0 10px #ff5ed4;color:#040410;">LAYER</span>
        <span class="card-sub">Acoustic Resonator (GLASS 2)</span>
        <button class="card-btn-toggle" id="btnTogLayer">ON</button>
      </div>
      <div class="card-body">
        <div class="var-row full"><label>Material</label><select id="lay-mat"></select></div>
        <div class="var-row"><label>Tune</label><input type="range" id="lay-k1" min="0" max="100"><output id="lay-o1">0 st</output></div>
        <div class="var-row"><label>Decay</label><input type="range" id="lay-k2" min="0" max="100"><output id="lay-o2">50%</output></div>
        <div class="var-row"><label>Bright</label><input type="range" id="lay-k3" min="0" max="100"><output id="lay-o3">50%</output></div>
        <div class="var-row"><label>Level</label><input type="range" id="lay-k4" min="0" max="100"><output id="lay-o4">80%</output></div>
      </div>
    `;
    host.appendChild(card);
    cosmicCards.layer = { card, tog: card.querySelector("#btnTogLayer") };
    const sMat = card.querySelector("#lay-mat");
    MATERIAL_NAMES.forEach((m, idx) => { const opt = document.createElement("option"); opt.value = idx; opt.textContent = m; sMat.appendChild(opt); });
    sMat.onchange = () => sendShort(nrpn(2, 0x60, Math.round((+sMat.value + 0.5) * 100 / 6)));
    card.querySelector("#btnTogLayer").onclick = () => {
      const v = card.classList.contains("off") ? 127 : 0;
      sendShort(cc(30, v));
    };
    for (let k = 1; k <= 4; k++) {
      const r = card.querySelector("#lay-k" + k), o = card.querySelector("#lay-o" + k);
      r.oninput = () => {
        dragging = r;
        if (k === 1) { const st = Math.round((r.value - 50) * 24 / 100); o.textContent = (st >= 0 ? "+" + st : st) + " st"; }
        else o.textContent = r.value + "%";
        throttled("lay" + k, () => sendShort(nrpn(2, 0x60 + k, +r.value)));
      };
      r.onchange = () => { dragging = null; };
    }
  }

  // 4. SPACE CARD (Slot 7, mod 4)
  {
    const card = document.createElement("div"); card.className = "cosmic-card"; card.style.setProperty("--card-col", "#3ad8ff");
    card.innerHTML = `
      <div class="card-head">
        <span class="card-badge" style="background:#3ad8ff;box-shadow:0 0 10px #3ad8ff;color:#040410;">SPACE</span>
        <span class="card-sub">Cosmic 4-Line FDN Reverb</span>
        <button class="card-btn-toggle" id="btnTogSpace">ON</button>
      </div>
      <div class="card-body">
        <div class="var-row"><label>Decay</label><input type="range" id="spc-k0" min="0" max="100"><output id="spc-o0">65%</output></div>
        <div class="var-row"><label>Size</label><input type="range" id="spc-k1" min="0" max="100"><output id="spc-o1">50%</output></div>
        <div class="var-row"><label>Tone</label><input type="range" id="spc-k2" min="0" max="100"><output id="spc-o2">50%</output></div>
        <div class="var-row"><label>Send</label><input type="range" id="spc-k3" min="0" max="100"><output id="spc-o3">40%</output></div>
      </div>
    `;
    host.appendChild(card);
    cosmicCards.space = { card, tog: card.querySelector("#btnTogSpace") };
    card.querySelector("#btnTogSpace").onclick = () => {
      const v = card.classList.contains("off") ? 127 : 0;
      sendShort(cc(32, v));
    };
    for (let k = 0; k < 4; k++) {
      const r = card.querySelector("#spc-k" + k), o = card.querySelector("#spc-o" + k);
      r.oninput = () => { dragging = r; o.textContent = r.value + "%"; throttled("spc" + k, () => sendShort(nrpn(4, 0x60 + k, +r.value))); };
      r.onchange = () => { dragging = null; };
    }
  }

  // 5. SHIMMER CARD (Slot 7, mod 4, knobs 4 & 5)
  {
    const card = document.createElement("div"); card.className = "cosmic-card"; card.style.setProperty("--card-col", "#b840ff");
    card.innerHTML = `
      <div class="card-head">
        <span class="card-badge" style="background:#b840ff;box-shadow:0 0 10px #b840ff;color:#fff;">SHIMMER</span>
        <span class="card-sub">Pitch-Shifted Feedback</span>
        <button class="card-btn-toggle" id="btnTogShim">ON</button>
      </div>
      <div class="card-body">
        <div class="var-row full"><label>Interval</label><select id="shim-iv"></select></div>
        <div class="var-row"><label>Amount</label><input type="range" id="shim-k4" min="0" max="100"><output id="shim-o4">50%</output></div>
      </div>
    `;
    host.appendChild(card);
    cosmicCards.shim = { card, tog: card.querySelector("#btnTogShim") };
    const sIv = card.querySelector("#shim-iv");
    SHIMMER_INTERVALS.forEach((iv, idx) => { const opt = document.createElement("option"); opt.value = idx; opt.textContent = iv; sIv.appendChild(opt); });
    sIv.onchange = () => sendShort(nrpn(4, 0x65, Math.round((+sIv.value + 0.5) * 100 / 4)));
    card.querySelector("#btnTogShim").onclick = () => {
      const isOff = card.classList.contains("off");
      const v = isOff ? 65 : 0; // turn on (50%) or off (0)
      sendShort(nrpn(4, 0x64, v));
    };
    const rAmt = card.querySelector("#shim-k4"), oAmt = card.querySelector("#shim-o4");
    rAmt.oninput = () => { dragging = rAmt; oAmt.textContent = rAmt.value + "%"; throttled("shimk4", () => sendShort(nrpn(4, 0x64, +rAmt.value))); };
    rAmt.onchange = () => { dragging = null; };
  }

  // 6. MASTER EQ CARD (Slot 4, mod 7)
  {
    const card = document.createElement("div"); card.className = "cosmic-card"; card.style.setProperty("--card-col", "#ffcc4c");
    card.innerHTML = `
      <div class="card-head">
        <span class="card-badge" style="background:#ffcc4c;box-shadow:0 0 10px #ffcc4c;color:#040410;">EQ</span>
        <span class="card-sub">5-Band Master Tone</span>
        <button class="card-btn-toggle" id="btnTogEq">ON</button>
      </div>
      <div class="card-body">
        ${["100 Hz", "400 Hz", "1 kHz", "3 kHz", "8 kHz"].map((b, i) => `
          <div class="var-row"><label>${b}</label><input type="range" id="eq-k${i}" min="0" max="100"><output id="eq-o${i}">0 dB</output></div>
        `).join("")}
      </div>
    `;
    host.appendChild(card);
    cosmicCards.eq = { card, tog: card.querySelector("#btnTogEq") };
    card.querySelector("#btnTogEq").onclick = () => {
      const v = card.classList.contains("off") ? 127 : 0;
      sendShort(cc(35, v));
    };
    for (let i = 0; i < 5; i++) {
      const r = card.querySelector("#eq-k" + i), o = card.querySelector("#eq-o" + i);
      r.oninput = () => {
        dragging = r;
        const db = Math.round((r.value - 50) * 0.24);
        o.textContent = (db >= 0 ? "+" + db : db) + " dB";
        throttled("eq" + i, () => sendShort(nrpn(7, 0x60 + i, +r.value)));
      };
      r.onchange = () => { dragging = null; };
    }
  }

  // Pipeline node clicks
  $("pnode-dyn").onclick = () => $("btnTogDyn").click();
  $("pnode-eff").onclick = () => $("btnTogEff").click();
  $("pnode-layer").onclick = () => $("btnTogLayer").click();
  $("pnode-space").onclick = () => $("btnTogSpace").click();
  $("pnode-shim").onclick = () => $("btnTogShim").click();
  $("pnode-eq").onclick = () => $("btnTogEq").click();
}

// Render dynamic effect macro rows
let curEngineBank = -1;
function renderEffectMacros(bank, knobs) {
  const host = $("effMacrosHost");
  if (!host) return;
  const labels = ANDROMEDA_MACRO_LABELS[bank] || ["M1", "M2", "M3", "M4"];
  
  if (curEngineBank !== bank) {
    curEngineBank = bank;
    host.innerHTML = "";
    for (let k = 0; k < 4; k++) {
      const row = document.createElement("div"); row.className = "var-row"; row.id = "eff-mrow-" + k;
      const lb = document.createElement("label"); lb.textContent = labels[k];
      
      // Discrete selection checks
      const isGlassMat = (bank === 0 && k === 0);
      const isChoirVoicing = (bank === 3 && k === 0);
      const isFractGrid = (bank === 4 && k === 0);
      const isFractSlice = (bank === 4 && k === 1);
      const isConstScale = (bank === 5 && k === 1);
      
      if (isGlassMat || isChoirVoicing || isFractGrid || isFractSlice || isConstScale) {
        row.classList.add("full");
        const s = document.createElement("select"); s.id = "eff-m-sel-" + k;
        let opts = [];
        if (isGlassMat) opts = MATERIAL_NAMES;
        else if (isChoirVoicing) opts = VOICING_NAMES;
        else if (isFractGrid) opts = GRID_NAMES;
        else if (isFractSlice) opts = SLICE_MS.map(ms => ms + " MS");
        else if (isConstScale) opts = SCALE_NAMES;
        
        opts.forEach((item, idx) => { const o = document.createElement("option"); o.value = idx; o.textContent = item; s.appendChild(o); });
        s.onchange = () => {
          const val = Math.round((+s.value + 0.5) * 100 / opts.length);
          sendShort(nrpn(3, 0x60 + k, val));
        };
        row.append(lb, s);
      } else {
        const r = document.createElement("input"); r.type = "range"; r.min = 0; r.max = 100; r.id = "eff-m-rng-" + k;
        const o = document.createElement("output"); o.id = "eff-m-out-" + k;
        r.oninput = () => {
          dragging = r;
          o.textContent = fmtMacroVal(bank, k, +r.value);
          throttled("effm" + k, () => sendShort(nrpn(3, 0x60 + k, +r.value)));
        };
        r.onchange = () => { dragging = null; };
        row.append(lb, r, o);
      }
      host.appendChild(row);
    }
  }

  // Update values
  for (let k = 0; k < 4; k++) {
    const val = knobs ? knobs[k] : 50;
    const isGlassMat = (bank === 0 && k === 0);
    const isChoirVoicing = (bank === 3 && k === 0);
    const isFractGrid = (bank === 4 && k === 0);
    const isFractSlice = (bank === 4 && k === 1);
    const isConstScale = (bank === 5 && k === 1);
    
    if (isGlassMat || isChoirVoicing || isFractGrid || isFractSlice || isConstScale) {
      const s = $("eff-m-sel-" + k);
      if (s) {
        const count = s.children.length;
        const idx = Math.min(count - 1, Math.floor(val * count / 101));
        s.value = idx;
      }
    } else {
      const r = $("eff-m-rng-" + k), o = $("eff-m-out-" + k);
      if (r && dragging !== r) {
        r.value = val;
        if (o) o.textContent = fmtMacroVal(bank, k, val);
      }
    }
  }
}

function render() {
  if (!state) return;
  $("pName").textContent = state.name || "-";
  $("pIdx").textContent = "P" + String((state.preset % 20) + 1).padStart(2, "0");
  $("pSel").value = state.preset % 20;

  // 1. DYNAMICS
  const sDyn = state.slots[0];
  if (sDyn && cosmicCards.dyn) {
    cosmicCards.dyn.card.classList.toggle("off", !sDyn.on);
    cosmicCards.dyn.tog.textContent = sDyn.on ? "ON" : "OFF";
    cosmicCards.dyn.tog.classList.toggle("on", !!sDyn.on);
    $("pnode-dyn").classList.toggle("active", !!sDyn.on);
    for (let k = 0; k < 4; k++) {
      const r = $("dyn-k" + k), o = $("dyn-o" + k);
      if (r && dragging !== r) {
        r.value = sDyn.knobs[k];
        if (k === 0) o.textContent = Math.round(-80 + r.value * 0.5) + " dB";
        else if (k === 3) { const db = Math.round((r.value - 50) * 0.24); o.textContent = (db >= 0 ? "+" + db : db) + " dB"; }
        else o.textContent = r.value + "%";
      }
    }
  }

  // 2. EFFECT
  const sEff = state.slots[6];
  if (sEff && cosmicCards.eff) {
    const effType = sEff.type % 24;
    const bank = Math.floor(effType / 4);
    const color = ENGINE_COLORS[bank] || "#9e6eff";
    cosmicCards.eff.card.classList.toggle("off", !sEff.on);
    cosmicCards.eff.tog.textContent = sEff.on ? "ON" : "OFF";
    cosmicCards.eff.tog.classList.toggle("on", !!sEff.on);
    $("pnode-eff").classList.toggle("active", !!sEff.on);
    $("pnode-eff").style.setProperty("--p-col", color);
    
    // Header & badge
    const badge = $("effBadge"), wave = $("effWave");
    if (badge) { badge.style.background = color; badge.style.boxShadow = "0 0 10px " + color; }
    if (wave) { wave.textContent = ENGINE_WAVE_NAMES[bank] || "COSMIC"; }
    if (cosmicCards.eff.sel) cosmicCards.eff.sel.value = effType;

    // Macros
    renderEffectMacros(bank, sEff.knobs);

    // Mix & Space send
    const rMix = $("eff-mix"), oMix = $("eff-mix-o");
    if (rMix && dragging !== rMix) { rMix.value = sEff.knobs[4]; oMix.textContent = sEff.knobs[4] + "%"; }
    const rSend = $("eff-send"), oSend = $("eff-send-o");
    if (rSend && dragging !== rSend) { rSend.value = sEff.knobs[5]; oSend.textContent = sEff.knobs[5] + "%"; }
  }

  // 3. LAYER
  const sLay = state.slots[5];
  if (sLay && cosmicCards.layer) {
    cosmicCards.layer.card.classList.toggle("off", !sLay.on);
    cosmicCards.layer.tog.textContent = sLay.on ? "ON" : "OFF";
    cosmicCards.layer.tog.classList.toggle("on", !!sLay.on);
    $("pnode-layer").classList.toggle("active", !!sLay.on);
    
    const sMat = $("lay-mat");
    if (sMat) sMat.value = Math.min(5, Math.floor(sLay.knobs[0] * 6 / 101));
    for (let k = 1; k <= 4; k++) {
      const r = $("lay-k" + k), o = $("lay-o" + k);
      if (r && dragging !== r) {
        r.value = sLay.knobs[k];
        if (k === 1) { const st = Math.round((r.value - 50) * 24 / 100); o.textContent = (st >= 0 ? "+" + st : st) + " st"; }
        else o.textContent = r.value + "%";
      }
    }
  }

  // 4. SPACE
  const sSpc = state.slots[7];
  if (sSpc && cosmicCards.space) {
    cosmicCards.space.card.classList.toggle("off", !sSpc.on);
    cosmicCards.space.tog.textContent = sSpc.on ? "ON" : "OFF";
    cosmicCards.space.tog.classList.toggle("on", !!sSpc.on);
    $("pnode-space").classList.toggle("active", !!sSpc.on);
    for (let k = 0; k < 4; k++) {
      const r = $("spc-k" + k), o = $("spc-o" + k);
      if (r && dragging !== r) { r.value = sSpc.knobs[k]; o.textContent = sSpc.knobs[k] + "%"; }
    }
  }

  // 5. SHIMMER (in slot 7 knobs 4 & 5)
  if (sSpc && cosmicCards.shim) {
    const shimOn = sSpc.on && sSpc.knobs[4] > 0;
    cosmicCards.shim.card.classList.toggle("off", !shimOn);
    cosmicCards.shim.tog.textContent = shimOn ? "ON" : "OFF";
    cosmicCards.shim.tog.classList.toggle("on", !!shimOn);
    $("pnode-shim").classList.toggle("active", !!shimOn);
    
    const sIv = $("shim-iv");
    if (sIv) sIv.value = Math.min(3, Math.floor(sSpc.knobs[5] * 4 / 101));
    const rAmt = $("shim-k4"), oAmt = $("shim-o4");
    if (rAmt && dragging !== rAmt) { rAmt.value = sSpc.knobs[4]; oAmt.textContent = sSpc.knobs[4] + "%"; }
  }

  // 6. MASTER EQ
  const sEq = state.slots[4];
  if (sEq && cosmicCards.eq) {
    cosmicCards.eq.card.classList.toggle("off", !sEq.on);
    cosmicCards.eq.tog.textContent = sEq.on ? "ON" : "OFF";
    cosmicCards.eq.tog.classList.toggle("on", !!sEq.on);
    $("pnode-eq").classList.toggle("active", !!sEq.on);
    for (let i = 0; i < 5; i++) {
      const r = $("eq-k" + i), o = $("eq-o" + i);
      if (r && dragging !== r) {
        r.value = sEq.knobs[i];
        const db = Math.round((r.value - 50) * 0.24);
        o.textContent = (db >= 0 ? "+" + db : db) + " dB";
      }
    }
  }

  // Master Levels & Global
  levels.preset.set(state.presetVol);
  levels.out.set(state.outLevel);
  for (const g of GLOBALS) g.el.set(g.get(state.g));
  gr.play.set(state.flags & 4 ? 1 : 0); gr.bpm.set(state.g.bpm); gr.pat.set(state.g.pattern); gr.vol.set(state.g.gVol); gr.drums.set(state.g.drums);
  stompEls.forEach((e, i) => e.set(SLOT_MODULE[state.stomp[i]] ?? 0));
  $("tunerState").textContent = state.flags & 2 ? "tuner is open on the unit" : "";

  // Update Radar tab Telemetry
  $("telPreset").textContent = "P" + String((state.preset % 20) + 1).padStart(2, "0") + " " + (state.name || "UNTITLED");
  const effType = state.slots[6].type;
  $("telEngine").textContent = ANDROMEDA_ENGINES[effType % ANDROMEDA_ENGINES.length];
  const layMat = MATERIAL_NAMES[Math.min(5, Math.floor(state.slots[5].knobs[0] * 6 / 101))];
  $("telLayer").textContent = layMat + " (Level " + state.slots[5].knobs[4] + "%)";
  $("telSpace").textContent = "Decay " + state.slots[7].knobs[0] + "% / Shimmer " + state.slots[7].knobs[4] + "%";
}
"""

# Replace in script from `function buildChain()` up to the end of `render()`
p_build = old_script.find("function buildChain()")
p_render_end = old_script.find("levels.preset.set(state.presetVol);")
assert p_build >= 0 and p_render_end >= 0, "Anchor not found"
p_end_brace = old_script.find("}\n", p_render_end)
assert p_end_brace >= 0, "End brace not found"

new_script = old_script[:p_build] + chain_logic + "\n" + old_script[p_end_brace + 2:]

# Replace script in html
html = html[:script_m.start()] + "<script>" + new_script + "</script>" + html[script_m.end():]

with open(ANDROMEDA_HTML, "w", encoding="utf-8") as f:
    f.write(html)
print("Updated Andromeda_Web_Editor.html successfully.")

# Also copy to Onyx_Web_Editor.html
with open(ONYX_HTML, "w", encoding="utf-8") as f:
    f.write(html)
print("Synchronized Onyx_Web_Editor.html successfully.")
