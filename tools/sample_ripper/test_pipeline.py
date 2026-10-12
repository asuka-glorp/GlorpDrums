#!/usr/bin/env python3
"""
Tests the automated sample ripper pipeline end-to-end:
1. Generates mini MIDI and manifest (Kick & Snare, 3 vel, 2 RR).
2. Simulates an audio render using math synthesis.
3. Runs slice_and_build_kit.py.
4. Verifies output drumkit.xml, WAV files, and midimap.xml.
"""

import os
import wave
import struct
import math
from make_sampling_midi import generate_midi_and_manifest
from slice_and_build_kit import slice_and_build

TEST_DIR = "/home/servandoc/Documents/DEV/GlorpDrums/tools/sample_ripper/test_out"
os.makedirs(TEST_DIR, exist_ok=True)

test_arts = [
    {"name": "Kick",      "note": 36, "group": None, "chokes": [], "duration": 0.8},
    {"name": "SnareHead", "note": 38, "group": None, "chokes": [], "duration": 0.8},
]
test_vels = [40, 80, 127]
test_rrs = 2
test_gap = 1.0

midi_path, manifest_path = generate_midi_and_manifest(
    TEST_DIR,
    articulations=test_arts,
    velocities=test_vels,
    round_robins=test_rrs,
    gap_seconds=test_gap
)

# Generate a synthetic rendered WAV matching the manifest timestamps
sr = 44100
total_len_sec = (len(test_arts) * len(test_vels) * test_rrs + 2) * test_gap
total_frames = int(total_len_sec * sr)
audio = [(0.0, 0.0)] * total_frames

cur_t = 1.0
for art in test_arts:
    freq = 60.0 if art["name"] == "Kick" else 200.0
    for vel in test_vels:
        amp = (vel / 127.0) * 0.8
        for rr in range(test_rrs):
            start_f = int(cur_t * sr)
            dur_f = int(0.5 * sr)
            for i in range(dur_f):
                t = i / float(sr)
                sig = math.sin(2.0 * math.pi * freq * t) * math.exp(-t * 8.0) * amp
                audio[start_f + i] = (sig, sig)
            cur_t += test_gap

render_wav = os.path.join(TEST_DIR, "simulated_render.wav")
with wave.open(render_wav, 'w') as wf:
    wf.setnchannels(2)
    wf.setsampwidth(3)
    wf.setframerate(sr)
    raw = bytearray()
    for l, r in audio:
        il = int(l * 8388607)
        ir = int(r * 8388607)
        raw.extend(il.to_bytes(3, 'little', signed=True))
        raw.extend(ir.to_bytes(3, 'little', signed=True))
    wf.writeframes(raw)

kit_out = os.path.join(TEST_DIR, "SlicedKit")
slice_and_build(render_wav, manifest_path, kit_out, "Test Sliced Kit")

# Verify
assert os.path.isfile(os.path.join(kit_out, "drumkit.xml")), "drumkit.xml missing"
assert os.path.isfile(os.path.join(kit_out, "midimap.xml")), "midimap.xml missing"
assert os.path.isfile(os.path.join(kit_out, "Kick", "Kick.xml")), "Kick.xml missing"
assert os.path.isfile(os.path.join(kit_out, "Kick", "samples", "v3_127_rr1.wav")), "Sample WAV missing"
print("\n[PIPELINE TEST PASSED] All sliced files and XML generated cleanly!")
