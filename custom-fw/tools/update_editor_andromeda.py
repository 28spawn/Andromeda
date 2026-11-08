# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

import sys

def main():
    path = 'custom-fw/web/Andromeda_Web_Editor.html'
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()

    # 1. Remove legacy checkbox
    old_cb = '''      <label class="note" style="margin-left:8px;display:flex;align-items:center;gap:4px;cursor:pointer;">
        <input type="checkbox" id="showLegacy"> Legacy
      </label>'''
    if old_cb in text:
        text = text.replace(old_cb, '')
        print('Checkbox removed')
    else:
        print('Checkbox already removed or not found')

    # 2. Engines
    old_eng = '''const ANDROMEDA_ENGINES = [
  "GLASS (Chime)", "GLASS (Crystal)", "GLASS (Prism)", "GLASS (Shard)",
  "BLOOM (Drift)", "BLOOM (Swell)", "BLOOM (Vapor)", "BLOOM (Abyss)",
  "ACCRETION (Orbit)", "ACCRETION (Event Horizon)", "ACCRETION (Singularity)", "ACCRETION (Density)",
  "CHOIR (Vocal)", "CHOIR (Ethereal)", "CHOIR (Siren)", "CHOIR (Astral)",
  "FRACTURE (Stutter)", "FRACTURE (Warp)", "FRACTURE (Granular)", "FRACTURE (Particle)",
  "CONSTELLATION (Major)", "CONSTELLATION (Minor)", "CONSTELLATION (Pentatonic)", "CONSTELLATION (Octaves)"
];'''
    new_eng = '''const ANDROMEDA_ENGINES = [
  "GLASS CRYSTAL",  "GLASS BELL",    "GLASS KALIMBA",  "GLASS MARIMBA",
  "BLOOM GARDEN",   "BLOOM DUSK",    "BLOOM DRIFT",    "BLOOM RAW",
  "ACCRETE SOS",    "ACCRETE TAPE",  "ACCRETE DECAY",  "ACCRETE MIRROR",
  "CHOIR ANGELS",   "CHOIR MOURN",   "CHOIR DEEP",     "CHOIR INFINITE",
  "FRACT CLOCK",    "FRACT WATER",   "FRACT HEART",    "FRACT SKIP",
  "CONST STARDUST", "CONST GRAVITY", "CONST DANCE",    "CONST ORBIT"
];'''
    if old_eng in text:
        text = text.replace(old_eng, new_eng)
        print('Engines updated')
    else:
        print('Engines already updated or not found')

    # 3. Macro labels
    old_macro = '''const ANDROMEDA_MACRO_LABELS = [
  ["MATERIAL", "TUNE", "DECAY", "BRIGHT", "MIX"],      // 0..3: GLASS
  ["SIZE", "DECAY", "TONE", "SWELL", "MIX"],            // 4..7: BLOOM
  ["TAPE", "SPEED", "DENSITY", "DEGRADE", "MIX"],       // 8..11: ACCRETION
  ["VOWEL", "GENDER", "RESONANCE", "SPREAD", "MIX"],    // 12..15: CHOIR
  ["GRAIN", "CHAOS", "DENSITY", "SPEED", "MIX"],        // 16..19: FRACTURE
  ["ROOT", "SCALE", "INTERVAL", "SPREAD", "MIX"]        // 20..23: CONSTELLATION
];'''
    new_macro = '''const ANDROMEDA_MACRO_LABELS = [
  ["MATERIAL", "TUNE", "DECAY", "BRIGHT", "MIX"],        // 0..3: GLASS
  ["SIZE", "PITCH", "BLOOM", "DRIFT", "MIX"],            // 4..7: BLOOM
  ["FDBK", "TONE", "WOBBLE", "DEGRADE", "MIX"],          // 8..11: ACCRETION
  ["VOICING", "DETUNE", "SPREAD", "ATTACK", "MIX"],      // 12..15: CHOIR
  ["GRID", "SLICE", "CHAOS", "PITCH", "MIX"],            // 16..19: FRACTURE
  ["TEMPO", "SCALE", "MOTION", "DENSITY", "MIX"]         // 20..23: CONSTELLATION
];'''
    if old_macro in text:
        text = text.replace(old_macro, new_macro)
        print('Macro labels updated')
    else:
        print('Macro labels already updated or not found')

    # 4. Layer materials
    old_mat = 'const LAYER_MATERIALS = ["Quartz", "Titanium", "Obsidian", "Amber", "Opal", "Bismuth"];'
    new_mat = 'const LAYER_MATERIALS = ["CRYSTAL", "BELL", "KALIMBA", "METAL", "WOOD", "MARIMBA"];'
    if old_mat in text:
        text = text.replace(old_mat, new_mat)
        print('Layer materials updated')
    else:
        print('Layer materials already updated or not found')

    # 5. render loop
    old_render = '''  const showLegacy = $("showLegacy").checked;
  
  if (!lift) {
    state.order.forEach((sl) => {
      const b = blocks[sl];
      const isLegacy = (sl === 1 || sl === 2 || sl === 3);
      b.card.hidden = isLegacy && !showLegacy;
      $("chain").appendChild(b.card);
    });
  }'''
    new_render = '''  if (!lift) {
    state.order.forEach((sl) => {
      const b = blocks[sl];
      const isLegacy = (sl === 1 || sl === 2 || sl === 3); // DRIVE, AMP, CAB (hardware locked off in Andromeda)
      if (isLegacy) {
        b.card.hidden = true;
      } else {
        b.card.hidden = false;
        $("chain").appendChild(b.card);
      }
    });
  }'''
    if old_render in text:
        text = text.replace(old_render, new_render)
        print('Render loop updated')
    else:
        print('Render loop already updated or not found')

    # 6. onchange showLegacy
    old_onchange = '$("showLegacy").onchange = () => render();'
    if old_onchange in text:
        text = text.replace(old_onchange, '// legacy hidden by default in Andromeda')
        print('Onchange updated')
    else:
        print('Onchange already updated or not found')

    # 7. auto-connect regex
    old_reg = '/pocket|donner|custom midi|onyx|update|ota|usb-midi|composite/i'
    new_reg = '/pocket|donner|custom midi|andromeda|onyx|update|ota|usb-midi|composite/i'
    if old_reg in text:
        text = text.replace(old_reg, new_reg)
        print('Regex updated')
    else:
        print('Regex already updated or not found')

    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)

    print('Updated Andromeda_Web_Editor.html successfully!')

if __name__ == '__main__':
    main()
