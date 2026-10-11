#!/usr/bin/env python3
"""
GlorpDrums Advanced Drum Kit Generator
Generates a realistic physical-modeling synthesized drum kit with:
- Multi-velocity layers (4 tiers: Soft, Med, Hard, Accent)
- Round-robin samples (3 RR per tier = 12 samples per zone)
- Rich acoustic modal synthesis and realistic frequency envelopes
- Multi-zone instruments:
    * Snare: SnareHead, RimClick, Rimshot
    * Kick: Kick
    * Tom 1: Tom1Head, Tom1Rim
    * Tom 2: Tom2Head, Tom2Rim
    * Floor Tom: FTomHead, FTomRim
    * Hihat: HihatClosed, HihatSemiOpen, HihatOpen
    * Crash 1: Crash1Edge, Crash1Bow, Crash1Choke
    * Crash 2: Crash2Edge, Crash2Bow, Crash2Choke
    * Ride: RideBow, RideBell, RideEdge, RideChoke
- Full DrumGizmo drumkit.xml, instrument XMLs with <channelmap> & <chokes>, and midimap.xml.
"""

import os
import wave
import math
import struct
import random
import sys

SAMPLE_RATE = 44100

def write_wav_24bit(path, samples_stereo):
    """Writes 24-bit stereo WAV for high dynamic range and audio fidelity."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, 'w') as wf:
        wf.setnchannels(2)
        wf.setsampwidth(3) # 24-bit
        wf.setframerate(SAMPLE_RATE)
        raw = bytearray()
        max_int24 = 8388607
        min_int24 = -8388608
        for l, r in samples_stereo:
            # Soft clip / clamp
            sl = math.tanh(l)
            sr = math.tanh(r)
            il = max(min_int24, min(max_int24, int(sl * max_int24)))
            ir = max(min_int24, min(max_int24, int(sr * max_int24)))
            raw.extend(il.to_bytes(3, byteorder='little', signed=True))
            raw.extend(ir.to_bytes(3, byteorder='little', signed=True))
        wf.writeframes(raw)

# ---------------------------------------------------------------------------
# Physical Modeling DSP Synthesizers
# ---------------------------------------------------------------------------

def synth_kick(vel=1.0, seed=0):
    """Acoustic kick drum with beater slap, membrane pitch drop, and shell resonance."""
    rng = random.Random(seed)
    duration = 0.45 + (1.0 - vel) * 0.1
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    f_start = (140.0 + rng.uniform(-4, 4)) * (0.85 + 0.25 * vel)
    f_end = (48.0 + rng.uniform(-1.5, 1.5)) * (0.92 + 0.12 * vel)
    decay_rate = 10.0 - 2.5 * vel

    phase = rng.uniform(0, 6.28)
    sub_phase = rng.uniform(0, 6.28)
    
    # Filter state for beater click
    click_state = 0.0

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        # Fast exponential pitch drop
        pitch_env = math.exp(-t * 32.0)
        cur_f = f_end + (f_start - f_end) * pitch_env
        phase += 2.0 * math.pi * cur_f / SAMPLE_RATE
        sub_phase += 2.0 * math.pi * (cur_f * 0.5) / SAMPLE_RATE

        body = math.sin(phase) * 0.75 + math.sin(sub_phase) * 0.25
        # Slight 2nd harmonic warmth
        body += math.sin(phase * 2.0) * 0.15 * math.exp(-t * 22.0)

        # Beater click (high freq transient)
        click_raw = (rng.random() * 2.0 - 1.0) * math.exp(-t * 180.0) * (0.2 + 0.6 * vel)
        # 3.2 kHz bandpass emulation
        click_state += (click_raw - click_state) * 0.45
        click = (click_raw - click_state) * 1.5

        sig = (body * env + click) * (0.4 + 0.6 * vel)
        out.append((sig * 0.98, sig * 0.98))
    return out

def synth_snare_head(vel=1.0, seed=0):
    """Snare drum head: batter/resonant membranes + snare wire rattle."""
    rng = random.Random(seed)
    duration = 0.35 + 0.1 * vel
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    base_f = (185.0 + rng.uniform(-3, 3)) * (0.95 + 0.08 * vel)
    phase1 = rng.uniform(0, 6.28)
    phase2 = rng.uniform(0, 6.28)
    phase3 = rng.uniform(0, 6.28)

    # Snare wire filter states (bandpass ~3kHz - 8kHz)
    lp1 = 0.0
    hp1 = 0.0

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        # Membrane tone decay
        tone_env = math.exp(-t * (18.0 - 4.0 * vel))
        # Snare wire rattle decay
        wire_env = math.exp(-t * (13.0 - 3.0 * vel))

        # Membrane modes: base, 1.6x, 2.3x
        f_pitch = base_f * (1.0 + 0.35 * math.exp(-t * 45.0))
        phase1 += 2.0 * math.pi * f_pitch / SAMPLE_RATE
        phase2 += 2.0 * math.pi * (f_pitch * 1.59) / SAMPLE_RATE
        phase3 += 2.0 * math.pi * (f_pitch * 2.28) / SAMPLE_RATE

        tone = (math.sin(phase1) * 0.6 + math.sin(phase2) * 0.3 + math.sin(phase3) * 0.15) * tone_env

        # Stick transient
        stick = (rng.random() * 2.0 - 1.0) * math.exp(-t * 220.0) * (0.3 + 0.5 * vel)

        # Wire noise
        noise_raw = (rng.random() * 2.0 - 1.0)
        lp1 += (noise_raw - lp1) * 0.55
        hp1 += (lp1 - hp1) * 0.15
        wires = (lp1 - hp1) * wire_env * (0.4 + 0.65 * vel)

        sig = (tone * 0.55 + stick * 0.25 + wires * 0.65) * (0.35 + 0.65 * vel)
        # Slight stereo width from overheads
        out.append((sig * 1.02, sig * 0.98))
    return out

def synth_snare_rimclick(vel=1.0, seed=0):
    """Crosstick / RimClick: stick resting on head clicking metal rim. Sharp wooden transient."""
    rng = random.Random(seed)
    duration = 0.12
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    click_f1 = (920.0 + rng.uniform(-15, 15)) * (0.96 + 0.08 * vel)
    click_f2 = (1450.0 + rng.uniform(-25, 25))
    click_f3 = (280.0 + rng.uniform(-5, 5)) # slight shell knock
    p1 = rng.uniform(0, 6.28)
    p2 = rng.uniform(0, 6.28)
    p3 = rng.uniform(0, 6.28)

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * (42.0 - 8.0 * vel))
        p1 += 2.0 * math.pi * click_f1 / SAMPLE_RATE
        p2 += 2.0 * math.pi * click_f2 / SAMPLE_RATE
        p3 += 2.0 * math.pi * click_f3 / SAMPLE_RATE

        tone = math.sin(p1) * 0.55 + math.sin(p2) * 0.35 + math.sin(p3) * 0.25
        tap = (rng.random() * 2.0 - 1.0) * math.exp(-t * 280.0) * 0.4
        sig = (tone * env + tap) * (0.3 + 0.7 * vel) * 0.85
        out.append((sig * 1.01, sig * 0.99))
    return out

def synth_snare_rimshot(vel=1.0, seed=0):
    """Rimshot: hitting rim and center simultaneously with maximum crack and wire buzz."""
    rng = random.Random(seed)
    duration = 0.38 + 0.08 * vel
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    base_f = (215.0 + rng.uniform(-3, 3))
    rim_f = (1280.0 + rng.uniform(-20, 20))
    rim_f2 = (1850.0 + rng.uniform(-30, 30))
    p1 = rng.uniform(0, 6.28)
    p_rim = rng.uniform(0, 6.28)
    p_rim2 = rng.uniform(0, 6.28)
    lp1 = 0.0
    hp1 = 0.0

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        tone_env = math.exp(-t * 15.0)
        rim_env = math.exp(-t * 26.0)
        wire_env = math.exp(-t * 11.0)

        p1 += 2.0 * math.pi * (base_f * (1.0 + 0.5 * math.exp(-t * 60.0))) / SAMPLE_RATE
        p_rim += 2.0 * math.pi * rim_f / SAMPLE_RATE
        p_rim2 += 2.0 * math.pi * rim_f2 / SAMPLE_RATE

        tone = math.sin(p1) * tone_env * 0.45
        metallic_crack = (math.sin(p_rim) * 0.6 + math.sin(p_rim2) * 0.4) * rim_env * 0.55
        impact = (rng.random() * 2.0 - 1.0) * math.exp(-t * 300.0) * (0.4 + 0.6 * vel)

        noise_raw = (rng.random() * 2.0 - 1.0)
        lp1 += (noise_raw - lp1) * 0.65
        hp1 += (lp1 - hp1) * 0.12
        wires = (lp1 - hp1) * wire_env * (0.5 + 0.6 * vel)

        sig = (tone + metallic_crack + impact * 0.35 + wires * 0.75) * (0.45 + 0.65 * vel)
        out.append((sig * 1.04, sig * 0.96))
    return out

def synth_tom_head(freq_start, freq_end, vel=1.0, seed=0, pan=0.0):
    """Tom drum head: deep resonant cylindrical shell vibration."""
    rng = random.Random(seed)
    duration = 0.55 + 0.15 * vel
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    f_s = (freq_start + rng.uniform(-3, 3)) * (0.96 + 0.10 * vel)
    f_e = (freq_end + rng.uniform(-1.5, 1.5))
    decay = (11.0 - 3.0 * vel)

    p1 = rng.uniform(0, 6.28)
    p2 = rng.uniform(0, 6.28)

    gain_l = math.cos((pan + 1.0) * math.pi / 4.0)
    gain_r = math.sin((pan + 1.0) * math.pi / 4.0)

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay)
        cur_f = f_e + (f_s - f_e) * math.exp(-t * 28.0)
        p1 += 2.0 * math.pi * cur_f / SAMPLE_RATE
        p2 += 2.0 * math.pi * (cur_f * 1.54) / SAMPLE_RATE

        membrane = math.sin(p1) * 0.75 + math.sin(p2) * 0.25 * math.exp(-t * 22.0)
        stick = (rng.random() * 2.0 - 1.0) * math.exp(-t * 120.0) * (0.2 + 0.4 * vel)

        sig = (membrane * env + stick) * (0.35 + 0.65 * vel)
        out.append((sig * gain_l * 1.414, sig * gain_r * 1.414))
    return out

def synth_tom_rim(rim_f, vel=1.0, seed=0, pan=0.0):
    """Tom drum rim: metal counterhoop strike."""
    rng = random.Random(seed)
    duration = 0.16
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    f1 = (rim_f + rng.uniform(-10, 10))
    f2 = f1 * 1.85
    p1 = rng.uniform(0, 6.28)
    p2 = rng.uniform(0, 6.28)

    gain_l = math.cos((pan + 1.0) * math.pi / 4.0)
    gain_r = math.sin((pan + 1.0) * math.pi / 4.0)

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * 28.0)
        p1 += 2.0 * math.pi * f1 / SAMPLE_RATE
        p2 += 2.0 * math.pi * f2 / SAMPLE_RATE

        ring = math.sin(p1) * 0.65 + math.sin(p2) * 0.35
        stick = (rng.random() * 2.0 - 1.0) * math.exp(-t * 220.0) * 0.35
        sig = (ring * env + stick) * (0.35 + 0.65 * vel) * 0.75
        out.append((sig * gain_l * 1.414, sig * gain_r * 1.414))
    return out

def synth_hihat(decay_ms, vel=1.0, seed=0, is_open=False):
    """Hihat: clustered inharmonic metal modes + high frequency air."""
    rng = random.Random(seed)
    duration = decay_ms / 1000.0
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    # 6 metallic modal frequencies
    modes = [3180.0, 4740.0, 6220.0, 7860.0, 9600.0, 11800.0]
    phases = [rng.uniform(0, 6.28) for _ in modes]
    decay_rate = 1000.0 / (decay_ms * 0.4)

    lp = 0.0
    hp = 0.0

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        if is_open and t > 0.05:
            # Slower tail for open
            env = 0.8 * math.exp(-t * (decay_rate * 0.4))

        metal = 0.0
        for k, f in enumerate(modes):
            phases[k] += 2.0 * math.pi * f / SAMPLE_RATE
            metal += math.sin(phases[k]) * 0.16

        # Filtered high frequency noise
        noise_raw = (rng.random() * 2.0 - 1.0)
        lp += (noise_raw - lp) * 0.75
        hp += (lp - hp) * 0.35
        sizzle = (lp - hp) * 0.55

        sig = (metal + sizzle) * env * (0.35 + 0.65 * vel)
        # Overhead perspective: hihat slightly left (pan -0.35)
        out.append((sig * 1.15, sig * 0.85))
    return out

def synth_cymbal_crash(pitch_scale=1.0, vel=1.0, seed=0, pan=-0.2, choke_ms=0):
    """Crash cymbal: dense inharmonic modal clusters with long shimmering wash."""
    rng = random.Random(seed)
    duration = (choke_ms / 1000.0) if choke_ms > 0 else (1.8 + 0.6 * vel)
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    # Modal clusters
    base_modes = [4150.0, 5280.0, 6420.0, 7750.0, 9320.0, 11400.0, 13800.0]
    modes = [m * pitch_scale * rng.uniform(0.98, 1.02) for m in base_modes]
    phases = [rng.uniform(0, 6.28) for _ in modes]

    decay_rate = (1000.0 / (choke_ms * 0.5)) if choke_ms > 0 else (3.8 - 0.8 * vel)
    gain_l = math.cos((pan + 1.0) * math.pi / 4.0)
    gain_r = math.sin((pan + 1.0) * math.pi / 4.0)

    lp = 0.0

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        if choke_ms > 0 and t > (choke_ms / 1000.0 * 0.6):
            env *= math.exp(-(t - choke_ms / 1000.0 * 0.6) * 80.0)

        metal = 0.0
        for k, f in enumerate(modes):
            phases[k] += 2.0 * math.pi * f / SAMPLE_RATE
            metal += math.sin(phases[k]) * 0.14

        noise_raw = (rng.random() * 2.0 - 1.0)
        lp += (noise_raw - lp) * 0.70
        wash = lp * 0.35

        # Explosive impact at t=0
        impact = (rng.random() * 2.0 - 1.0) * math.exp(-t * 90.0) * 0.35

        sig = (metal + wash + impact) * env * (0.35 + 0.65 * vel)
        out.append((sig * gain_l * 1.414, sig * gain_r * 1.414))
    return out

def synth_cymbal_bow(pitch_scale=1.0, vel=1.0, seed=0, pan=-0.2):
    """Crash/Ride Bow: cleaner stick ping transient on cymbal body with controlled wash."""
    rng = random.Random(seed)
    duration = 1.4 + 0.4 * vel
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    base_modes = [3600.0, 4800.0, 6200.0, 8100.0, 10500.0]
    modes = [m * pitch_scale * rng.uniform(0.98, 1.02) for m in base_modes]
    phases = [rng.uniform(0, 6.28) for _ in modes]

    decay_rate = 4.5 - 0.6 * vel
    gain_l = math.cos((pan + 1.0) * math.pi / 4.0)
    gain_r = math.sin((pan + 1.0) * math.pi / 4.0)

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        metal = 0.0
        for k, f in enumerate(modes):
            phases[k] += 2.0 * math.pi * f / SAMPLE_RATE
            metal += math.sin(phases[k]) * 0.18

        # Stick tip ping
        ping = (rng.random() * 2.0 - 1.0) * math.exp(-t * 150.0) * 0.45
        sig = (metal * 0.7 + ping * 0.3) * env * (0.35 + 0.65 * vel)
        out.append((sig * gain_l * 1.414, sig * gain_r * 1.414))
    return out

def synth_ride_bell(vel=1.0, seed=0, pan=0.35):
    """Ride Bell: clear, cutting, singing metallic harmonic ring."""
    rng = random.Random(seed)
    duration = 2.4 + 0.6 * vel
    n_samples = int(SAMPLE_RATE * duration)
    out = []

    # Harmonic bell modes
    bell_modes = [585.0, 1170.0, 2340.0, 3950.0, 5800.0]
    phases = [rng.uniform(0, 6.28) for _ in bell_modes]
    decay_rate = 2.8 - 0.5 * vel

    gain_l = math.cos((pan + 1.0) * math.pi / 4.0)
    gain_r = math.sin((pan + 1.0) * math.pi / 4.0)

    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        bell = 0.0
        weights = [0.45, 0.30, 0.20, 0.12, 0.08]
        for k, f in enumerate(bell_modes):
            phases[k] += 2.0 * math.pi * f / SAMPLE_RATE
            bell += math.sin(phases[k]) * weights[k]

        strike = (rng.random() * 2.0 - 1.0) * math.exp(-t * 200.0) * 0.35
        sig = (bell + strike * 0.25) * env * (0.35 + 0.65 * vel)
        out.append((sig * gain_l * 1.414, sig * gain_r * 1.414))
    return out

# ---------------------------------------------------------------------------
# Kit Generation
# ---------------------------------------------------------------------------

VELOCITIES = [
    ("v1_soft", 0.25),
    ("v2_med", 0.50),
    ("v3_hard", 0.75),
    ("v4_accent", 1.00)
]
ROUND_ROBINS = 3

def generate_zone_samples(inst_name, synth_func, base_dir, **kwargs):
    """Generates 4 velocity tiers x 3 round robins (12 WAVs) for a given zone."""
    inst_dir = os.path.join(base_dir, inst_name)
    samples_dir = os.path.join(inst_dir, "samples")
    os.makedirs(samples_dir, exist_ok=True)

    samples_info = []
    seed_counter = 1000

    for vel_name, vel_val in VELOCITIES:
        for rr in range(1, ROUND_ROBINS + 1):
            seed_counter += 1
            hit_name = f"{vel_name}_rr{rr}"
            wav_path = os.path.join(samples_dir, f"{hit_name}.wav")
            audio = synth_func(vel=vel_val, seed=seed_counter, **kwargs)
            write_wav_24bit(wav_path, audio)
            samples_info.append((hit_name, vel_val, f"samples/{hit_name}.wav"))

    # Write instrument XML
    samples_xml = ""
    for hit_name, power, rel_wav in samples_info:
        samples_xml += f"""    <sample name="{hit_name}" power="{power:.2f}">
      <audiofile channel="OHL" file="{rel_wav}" filechannel="1"/>
      <audiofile channel="OHR" file="{rel_wav}" filechannel="2"/>
    </sample>\n"""

    inst_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<instrument version="2.0">
  <metadata>
    <title>{inst_name}</title>
  </metadata>
  <samples>
{samples_xml}  </samples>
</instrument>
"""
    with open(os.path.join(inst_dir, f"{inst_name}.xml"), "w") as f:
        f.write(inst_xml)

def generate_advanced_kit(output_dir):
    print(f"Generating advanced GlorpDemoKit at: {output_dir}")
    os.makedirs(output_dir, exist_ok=True)

    # 1. Snare zones
    generate_zone_samples("SnareHead", synth_snare_head, output_dir)
    generate_zone_samples("RimClick", synth_snare_rimclick, output_dir)
    generate_zone_samples("Rimshot", synth_snare_rimshot, output_dir)

    # 2. Kick zone
    generate_zone_samples("Kick", synth_kick, output_dir)

    # 3. Toms zones
    generate_zone_samples("Tom1Head", synth_tom_head, output_dir, freq_start=170.0, freq_end=135.0, pan=-0.25)
    generate_zone_samples("Tom1Rim", synth_tom_rim, output_dir, rim_f=850.0, pan=-0.25)

    generate_zone_samples("Tom2Head", synth_tom_head, output_dir, freq_start=125.0, freq_end=98.0, pan=0.15)
    generate_zone_samples("Tom2Rim", synth_tom_rim, output_dir, rim_f=680.0, pan=0.15)

    generate_zone_samples("FTomHead", synth_tom_head, output_dir, freq_start=88.0, freq_end=65.0, pan=0.45)
    generate_zone_samples("FTomRim", synth_tom_rim, output_dir, rim_f=540.0, pan=0.45)

    # 4. Hihat zones
    generate_zone_samples("HihatClosed", synth_hihat, output_dir, decay_ms=65, is_open=False)
    generate_zone_samples("HihatSemiOpen", synth_hihat, output_dir, decay_ms=320, is_open=False)
    generate_zone_samples("HihatOpen", synth_hihat, output_dir, decay_ms=1300, is_open=True)

    # 5. Crash 1 zones (16" bright)
    generate_zone_samples("Crash1Edge", synth_cymbal_crash, output_dir, pitch_scale=1.05, pan=-0.4, choke_ms=0)
    generate_zone_samples("Crash1Bow", synth_cymbal_bow, output_dir, pitch_scale=1.05, pan=-0.4)
    generate_zone_samples("Crash1Choke", synth_cymbal_crash, output_dir, pitch_scale=1.05, pan=-0.4, choke_ms=55)

    # 6. Crash 2 zones (18" dark)
    generate_zone_samples("Crash2Edge", synth_cymbal_crash, output_dir, pitch_scale=0.92, pan=0.35, choke_ms=0)
    generate_zone_samples("Crash2Bow", synth_cymbal_bow, output_dir, pitch_scale=0.92, pan=0.35)
    generate_zone_samples("Crash2Choke", synth_cymbal_crash, output_dir, pitch_scale=0.92, pan=0.35, choke_ms=60)

    # 7. Ride zones (21" heavy)
    generate_zone_samples("RideBow", synth_cymbal_bow, output_dir, pitch_scale=0.85, pan=0.30)
    generate_zone_samples("RideBell", synth_ride_bell, output_dir, pan=0.30)
    generate_zone_samples("RideEdge", synth_cymbal_crash, output_dir, pitch_scale=0.85, pan=0.30, choke_ms=0)
    generate_zone_samples("RideChoke", synth_cymbal_crash, output_dir, pitch_scale=0.85, pan=0.30, choke_ms=55)

    # -----------------------------------------------------------------------
    # Drumkit XML definition with channel mappings and choke configurations
    # -----------------------------------------------------------------------
    instruments_config = [
        # (name, file_rel, group, chokes_list)
        ("SnareHead", "SnareHead/SnareHead.xml", None, []),
        ("RimClick", "RimClick/RimClick.xml", None, []),
        ("Rimshot", "Rimshot/Rimshot.xml", None, []),
        ("Kick", "Kick/Kick.xml", None, []),
        ("Tom1Head", "Tom1Head/Tom1Head.xml", None, []),
        ("Tom1Rim", "Tom1Rim/Tom1Rim.xml", None, []),
        ("Tom2Head", "Tom2Head/Tom2Head.xml", None, []),
        ("Tom2Rim", "Tom2Rim/Tom2Rim.xml", None, []),
        ("FTomHead", "FTomHead/FTomHead.xml", None, []),
        ("FTomRim", "FTomRim/FTomRim.xml", None, []),
        ("HihatClosed", "HihatClosed/HihatClosed.xml", "hihat", [("HihatOpen", 40), ("HihatSemiOpen", 40)]),
        ("HihatSemiOpen", "HihatSemiOpen/HihatSemiOpen.xml", "hihat", [("HihatOpen", 40)]),
        ("HihatOpen", "HihatOpen/HihatOpen.xml", "hihat", []),
        ("Crash1Edge", "Crash1Edge/Crash1Edge.xml", None, []),
        ("Crash1Bow", "Crash1Bow/Crash1Bow.xml", None, []),
        ("Crash1Choke", "Crash1Choke/Crash1Choke.xml", None, [("Crash1Edge", 40), ("Crash1Bow", 40)]),
        ("Crash2Edge", "Crash2Edge/Crash2Edge.xml", None, []),
        ("Crash2Bow", "Crash2Bow/Crash2Bow.xml", None, []),
        ("Crash2Choke", "Crash2Choke/Crash2Choke.xml", None, [("Crash2Edge", 40), ("Crash2Bow", 40)]),
        ("RideBow", "RideBow/RideBow.xml", None, []),
        ("RideBell", "RideBell/RideBell.xml", None, []),
        ("RideEdge", "RideEdge/RideEdge.xml", None, []),
        ("RideChoke", "RideChoke/RideChoke.xml", None, [("RideBow", 40), ("RideBell", 40), ("RideEdge", 40)])
    ]

    inst_xml_nodes = ""
    for name, file_rel, group, chokes in instruments_config:
        group_attr = f' group="{group}"' if group else ""
        chokes_xml = ""
        if chokes:
            chokes_inner = "\n".join([f'        <choke instrument="{target}" choketime="{ctime}"/>' for target, ctime in chokes])
            chokes_xml = f"      <chokes>\n{chokes_inner}\n      </chokes>\n"

        inst_xml_nodes += f"""    <instrument name="{name}"{group_attr} file="{file_rel}">
{chokes_xml}      <channelmap in="OHL" out="OHL" main="true"/>
      <channelmap in="OHR" out="OHR" main="true"/>
    </instrument>\n"""

    drumkit_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<drumkit samplerate="44100" version="2.1.0">
  <metadata>
    <version>2.0.0</version>
    <title>Glorp Demo Kit</title>
    <description>Realistic physical-modeling multi-zone drum kit for GlorpDrums</description>
    <author>GlorpDrums</author>
    <license>CC0</license>
  </metadata>
  <channels>
    <channel name="OHL"><title>Overhead Left</title></channel>
    <channel name="OHR"><title>Overhead Right</title></channel>
  </channels>
  <instruments>
{inst_xml_nodes}  </instruments>
</drumkit>
"""
    with open(os.path.join(output_dir, "drumkit.xml"), "w") as f:
        f.write(drumkit_xml)

    # -----------------------------------------------------------------------
    # midimap.xml mapping standard General MIDI notes + extra zone notes
    # -----------------------------------------------------------------------
    midimap_xml = """<?xml version="1.0" encoding="UTF-8"?>
<midimap>
  <!-- Kick -->
  <map note="36" instr="Kick"/>
  <map note="35" instr="Kick"/>

  <!-- Snare -->
  <map note="38" instr="SnareHead"/>
  <map note="37" instr="RimClick"/>
  <map note="40" instr="Rimshot"/>

  <!-- Toms -->
  <map note="48" instr="Tom1Head"/>
  <map note="50" instr="Tom1Rim"/>
  <map note="45" instr="Tom2Head"/>
  <map note="47" instr="Tom2Rim"/>
  <map note="41" instr="FTomHead"/>
  <map note="43" instr="FTomRim"/>

  <!-- Hi-hat -->
  <map note="42" instr="HihatClosed"/>
  <map note="44" instr="HihatSemiOpen"/>
  <map note="46" instr="HihatOpen"/>

  <!-- Crash 1 -->
  <map note="49" instr="Crash1Edge"/>
  <map note="55" instr="Crash1Bow"/>
  <map note="54" instr="Crash1Choke"/>

  <!-- Crash 2 -->
  <map note="57" instr="Crash2Edge"/>
  <map note="52" instr="Crash2Bow"/>
  <map note="58" instr="Crash2Choke"/>

  <!-- Ride -->
  <map note="51" instr="RideBow"/>
  <map note="53" instr="RideBell"/>
  <map note="59" instr="RideEdge"/>
  <map note="60" instr="RideChoke"/>
</midimap>
"""
    with open(os.path.join(output_dir, "midimap.xml"), "w") as f:
        f.write(midimap_xml)

    print("Kit generation complete!")

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "/home/servandoc/Documents/DEV/GlorpDrums/test/kits/GlorpDemoKit"
    generate_advanced_kit(target)
