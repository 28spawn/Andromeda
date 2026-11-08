# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Andromeda contributors

import json
import numpy as np
import ctypes
import os

class AtkinsonWaveNet:
    def __init__(self, weights, channels=3):
        self.channels = channels
        self.num_layers = 23
        self.kernel_sizes = [6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 15, 15, 6, 6, 6, 6, 6, 6, 6]
        self.dilations = [1, 3, 7, 17, 41, 101, 239, 1, 3, 7, 17, 41, 101, 239, 1, 13, 1, 3, 7, 17, 41, 101, 239]
        self.head_kernel_size = 16
        self.leaky_slope = 0.01

        # Weight unpacking
        it = iter(weights)
        
        # Rechannel: 1 -> Channels
        self.rechannel_w = np.array([next(it) for _ in range(channels)], dtype=np.float32)

        self.layers = []
        for li in range(self.num_layers):
            K = self.kernel_sizes[li]
            D = self.dilations[li]
            
            # Conv1D: Channels -> Bottleneck, K taps
            # Read order: for i in Bottleneck: for j in Channels: for k in K
            # We store as conv_w[k, i, j] (tap k, out_ch i, in_ch j)
            conv_w = np.zeros((K, channels, channels), dtype=np.float32)
            for i in range(channels):
                for j in range(channels):
                    for k in range(K):
                        conv_w[k, i, j] = next(it)
            
            conv_b = np.array([next(it) for _ in range(channels)], dtype=np.float32)
            
            # Mixin: 1 -> Bottleneck
            mixin_w = np.array([next(it) for _ in range(channels)], dtype=np.float32)
            
            # Layer1x1: Bottleneck -> Channels
            # Read order: for i in Channels: for j in Bottleneck
            l1x1_w = np.zeros((channels, channels), dtype=np.float32)
            for i in range(channels):
                for j in range(channels):
                    l1x1_w[i, j] = next(it)
            
            l1x1_b = np.array([next(it) for _ in range(channels)], dtype=np.float32)
            
            self.layers.append({
                'K': K,
                'D': D,
                'conv_w': conv_w,
                'conv_b': conv_b,
                'mixin_w': mixin_w,
                'l1x1_w': l1x1_w,
                'l1x1_b': l1x1_b,
                'history': []
            })
            
        # Head: Bottleneck -> 1, K=16
        # Read order: for j in Bottleneck: for k in 16
        # Store as head_w[k, j] (tap k, in_ch j)
        head_w = np.zeros((self.head_kernel_size, channels), dtype=np.float32)
        for j in range(channels):
            for k in range(self.head_kernel_size):
                head_w[k, j] = next(it)
        head_b = next(it)
        head_scale = next(it)
        
        self.head_w = head_w
        self.head_b = head_b
        self.head_scale = head_scale
        self.head_history = []
        
    def reset(self):
        for L in self.layers:
            L['history'] = []
        self.head_history = []

    def process(self, input_samples):
        out = np.zeros_like(input_samples)
        for n, x in enumerate(input_samples):
            # 1. Rechannel
            # lin shape: (channels,)
            lin = self.rechannel_w * x
            
            head_sum = np.zeros(self.channels, dtype=np.float32)
            
            # 2. Layers
            for L in self.layers:
                K = L['K']
                D = L['D']
                
                # Push current lin into history
                L['history'].append(lin.copy())
                # max lookback is (K - 1) * D
                hist = L['history']
                
                # Conv: tap 0 is (K-1)*D back in time, tap K-1 is 0 back in time (most recent)
                z = L['conv_b'].copy()
                for k in range(K):
                    taps_back = K - 1 - k
                    offset = taps_back * D
                    idx = len(hist) - 1 - offset
                    if idx >= 0:
                        src = hist[idx]
                    else:
                        src = np.zeros(self.channels, dtype=np.float32)
                    z += L['conv_w'][k] @ src
                
                # Conditioning mixin
                z += L['mixin_w'] * x
                
                # LeakyReLU
                act = np.where(z >= 0.0, z, z * self.leaky_slope)
                
                # Head sum
                head_sum += act
                
                # 1x1 residual
                lin = lin + L['l1x1_b'] + L['l1x1_w'] @ act
                
            # 3. Head
            self.head_history.append(head_sum.copy())
            y = self.head_b
            for k in range(self.head_kernel_size):
                taps_back = self.head_kernel_size - 1 - k
                idx = len(self.head_history) - 1 - taps_back
                if idx >= 0:
                    src = self.head_history[idx]
                else:
                    src = np.zeros(self.channels, dtype=np.float32)
                y += np.dot(self.head_w[k], src)
                
            out[n] = y * self.head_scale
            
        return out

print("AtkinsonWaveNet class defined successfully.")

def test_models():
    # Load DLL
    dll_path = os.path.abspath("custom-fw/emu/pocket_amp_emu.dll")
    cdll = ctypes.CDLL(dll_path)
    
    # Check what functions exist in DLL
    print("DLL loaded:", dll_path)
    
    # Load Bogner
    with open('BOG UU II Gain BAL CAB.nam', 'r') as f:
        bog_json = json.load(f)
        
    # Load Fender
    with open(r'custom-fw\emu\FNDR BFSR VB Edge WRM2 CAB FREE.nam', 'r') as f:
        fndr_json = json.load(f)

    # Test signals:
    # 1. Clean guitar level: ~0.1 to 0.3 peak
    # 2. Hard strum: ~0.8 to 1.5 peak
    np.random.seed(42)
    t = np.linspace(0, 0.1, 4800, endpoint=False) # 100ms at 48kHz
    sig_clean = (0.2 * np.sin(2 * np.pi * 220 * t) + 0.1 * np.sin(2 * np.pi * 440 * t)).astype(np.float32)
    sig_heavy = (1.2 * np.sin(2 * np.pi * 110 * t) + 0.8 * np.sin(2 * np.pi * 220 * t)).astype(np.float32)
    
    for name, data in [("Bogner (High Gain)", bog_json), ("Fender (Clean)", fndr_json)]:
        print("=" * 60)
        print(f"Testing {name}:")
        for sm_idx in [0, 1]:
            sm = data['config']['submodels'][sm_idx]
            ch = sm['model']['config']['layers'][0]['channels']
            w = sm['model']['weights']
            hs = sm['model']['config']['head_scale']
            print(f"  Submodel {sm_idx}: Channels={ch}, Weights={len(w)}, HeadScale={hs}")
            
            ref_net = AtkinsonWaveNet(w, channels=ch)
            out_clean = ref_net.process(sig_clean)
            ref_net.reset()
            out_heavy = ref_net.process(sig_heavy)
            
            clean_peak = np.max(np.abs(out_clean))
            clean_rms = np.sqrt(np.mean(out_clean**2))
            heavy_peak = np.max(np.abs(out_heavy))
            heavy_rms = np.sqrt(np.mean(out_heavy**2))
            
            print(f"    [Python Atkinson WaveNet] Clean -> Peak: {clean_peak:.4f}, RMS: {clean_rms:.4f}")
            print(f"    [Python Atkinson WaveNet] Heavy -> Peak: {heavy_peak:.4f}, RMS: {heavy_rms:.4f}")
            print(f"    Dynamic Ratio (Heavy/Clean RMS): {heavy_rms / (clean_rms + 1e-9):.2f}")
            
            # Run C DLL test for BOTH channels=3 and channels=8
            c_weights = (ctypes.c_float * len(w))(*w)
            cdll.emu_load_wavenet_floats.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.c_uint32, ctypes.c_float]
            cdll.emu_load_wavenet_floats.restype = ctypes.c_int
            ret = cdll.emu_load_wavenet_floats(c_weights, len(w), hs)
            assert ret == 0, f"Load failed with code {ret}"
            
            cdll.emu_test_wavenet_direct.argtypes = [ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_float), ctypes.c_int]
            cdll.emu_test_wavenet_direct.restype = None
            
            cdll.emu_reset_wavenet_state.argtypes = []
            cdll.emu_reset_wavenet_state.restype = None
            
            # Run C DLL on clean
            cdll.emu_reset_wavenet_state()
            c_clean = np.zeros_like(sig_clean)
            in_clean_ptr = sig_clean.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
            out_clean_ptr = c_clean.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
            cdll.emu_test_wavenet_direct(in_clean_ptr, out_clean_ptr, len(sig_clean))
            
            # Reset model
            cdll.emu_reset_wavenet_state()
            
            # Run C DLL on heavy
            c_heavy = np.zeros_like(sig_heavy)
            in_heavy_ptr = sig_heavy.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
            out_heavy_ptr = c_heavy.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
            cdll.emu_test_wavenet_direct(in_heavy_ptr, out_heavy_ptr, len(sig_heavy))
            
            c_clean_peak = np.max(np.abs(c_clean))
            c_clean_rms = np.sqrt(np.mean(c_clean**2))
            c_heavy_peak = np.max(np.abs(c_heavy))
            c_heavy_rms = np.sqrt(np.mean(c_heavy**2))
            
            diff_clean = np.max(np.abs(out_clean - c_clean))
            diff_heavy = np.max(np.abs(out_heavy - c_heavy))
            
            print(f"    [C DLL pa_wavenet.c]       Clean -> Peak: {c_clean_peak:.4f}, RMS: {c_clean_rms:.4f}")
            print(f"    [C DLL pa_wavenet.c]       Heavy -> Peak: {c_heavy_peak:.4f}, RMS: {c_heavy_rms:.4f}")
            print(f"    MAX ABS DIFF (Atkinson vs C): Clean={diff_clean:.6f}, Heavy={diff_heavy:.6f}")




if __name__ == '__main__':
    test_models()

