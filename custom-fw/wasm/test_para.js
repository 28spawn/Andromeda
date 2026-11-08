// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Andromeda contributors

/* PARA and HOME have BOTH a click and a hold (Project Andromeda).
 * A real button cannot do both: the click is what happens when you let go
 * quickly, the hold is what happens when you keep holding.
 *
 * In Andromeda:
 *   - Quick PARA press cycles parameter focus / detail on Cosmic Radar (view 0)
 *   - PARA hold (1.2s) opens Save Dialog (view 5)
 *   - Quick HOME press toggles between Radar (view 0) and Signal Chain (view 8)
 *   - HOME hold (1s) opens Settings menu (view 17)
 */
const fs = require('fs');
const wasmPath = process.argv[2] || (fs.existsSync('custom-fw/wasm/out/andromeda.wasm') ? 'custom-fw/wasm/out/andromeda.wasm' : 'custom-fw/wasm/out/pocket_amp.wasm');
const buf = fs.readFileSync(wasmPath);
const mem = new WebAssembly.Memory({ initial: 512, maximum: 4096 });
const imp = {
  env: { memory: mem,
    __indirect_function_table: new WebAssembly.Table({ initial: 1024, element: 'anyfunc' }),
    __stack_pointer: new WebAssembly.Global({ value: 'i32', mutable: true }, 16 * 1024 * 1024),
    __memory_base: new WebAssembly.Global({ value: 'i32', mutable: false }, 0),
    __table_base:  new WebAssembly.Global({ value: 'i32', mutable: false }, 0),
    __main_argc_argv: () => 0 },
  wasi_snapshot_preview1: { args_get: () => 0, args_sizes_get: () => 0, proc_exit: () => 0,
    fd_write: () => 0, environ_get: () => 0, environ_sizes_get: () => 0,
    clock_time_get: () => 0, random_get: () => 0 },
};
const X = new WebAssembly.Instance(new WebAssembly.Module(buf), imp).exports;
X.pa_wasm_init();

const T = new Uint32Array(mem.buffer, X.pa_wasm_telemetry_ptr(), 21);
const tick = (n = 1) => { for (let i = 0; i < n; i++) X.pa_wasm_tick(16); X.pa_wasm_update_telemetry(); };
const view = () => T[0];

const PARA = 1 << 5, HOME = 1 << 0;

let fails = 0;
const check = (ok, msg) => { console.log(`  ${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) fails++; };

console.log('=== a quick PARA press stays on Radar (view 0) ===');
tick(20);
const v0 = view();
X.pa_wasm_press(PARA);
tick(3);
X.pa_wasm_release(PARA);
tick(4);
const v1 = view();
check(v0 === 0 && v1 === 0, `quick PARA click handled on Radar (view ${v0} -> ${v1})`);

console.log('\n=== a PARA hold is NOT a quick click ===');
const v2 = view();
X.pa_wasm_press(PARA);
tick(40);                       // 640 ms - under hold threshold
X.pa_wasm_release(PARA);
tick(4);
const v3 = view();
check(v3 === v2, `a 640 ms press stayed on Radar (view ${v2} -> ${v3})`);

console.log('\n=== a PARA hold fires the HOLD bit, opening Save dialog ===');
// PARA held for 1.28 s: opens Save Dialog (view 5)
X.pa_wasm_press(PARA);
tick(80);                       // 1.28 s
X.pa_wasm_release(PARA);
tick(10);
const v4 = view();
check(v4 === 5, `the hold opened the Save dialog (view ${v3} -> ${v4})`);

// Exit save dialog with quick HOME
X.pa_wasm_press(HOME); tick(3); X.pa_wasm_release(HOME); tick(10);
check(view() === 0, `HOME closed save dialog back to Radar (view ${view()})`);

console.log('\n=== HOME: quick press toggles Radar <-> Chain (view 0 <-> 17) ===');
if (view() === 0) {
  X.pa_wasm_press(HOME); tick(3); X.pa_wasm_release(HOME); tick(6);
  const s1 = view();
  check(s1 === 17, `a quick HOME press opened Signal Chain (view ${s1})`);

  // quick HOME again returns to Radar
  X.pa_wasm_press(HOME); tick(3); X.pa_wasm_release(HOME); tick(6);
  const s2 = view();
  check(s2 === 0, `and HOME again returned to Radar (view ${s2})`);

  // a long HOME hold (1.6 s) opens Settings (view 8)
  console.log('\n=== HOME hold opens Settings (view 8) ===');
  X.pa_wasm_press(HOME);
  tick(100);                     // 1.6 s - past the 1.0 s HOME hold
  X.pa_wasm_release(HOME);
  tick(10);
  const s4 = view();
  check(s4 === 8, `a 1.6 s HOME hold opened Settings (view ${s4})`);
}

console.log('\n' + (fails === 0 ? 'PARA SEMANTICS OK' : fails + ' FAILURE(S)'));
process.exit(fails === 0 ? 0 : 1);
