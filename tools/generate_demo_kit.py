import os
import wave
import math
import struct
import random

SAMPLE_RATE = 44100

def write_wav(path, samples_stereo):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, 'w') as wf:
        wf.setnchannels(2)
        wf.setsampwidth(2)
        wf.setframerate(SAMPLE_RATE)
        raw = bytearray()
        for l, r in samples_stereo:
            il = max(-32767, min(32767, int(l * 32767.0)))
            ir = max(-32767, min(32767, int(r * 32767.0)))
            raw.extend(struct.pack('<hh', il, ir))
        wf.writeframes(raw)

def make_kick(freq_start=150.0, freq_end=45.0, duration=0.35, decay_rate=12.0):
    n_samples = int(SAMPLE_RATE * duration)
    out = []
    phase = 0.0
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        # exponential pitch envelope
        pitch_env = math.exp(-t * 25.0)
        cur_freq = freq_end + (freq_start - freq_end) * pitch_env
        phase += 2.0 * math.pi * cur_freq / SAMPLE_RATE
        # Add slight click at start
        click = (random.random() * 2.0 - 1.0) * math.exp(-t * 150.0) * 0.4
        sample = math.sin(phase) * env + click
        out.append((sample, sample))
    return out

def make_snare(duration=0.3, pitch=200.0, decay_rate=14.0):
    n_samples = int(SAMPLE_RATE * duration)
    out = []
    phase = 0.0
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        noise_env = math.exp(-t * 10.0)
        tone = math.sin(phase) * env * 0.5
        noise = (random.random() * 2.0 - 1.0) * noise_env * 0.6
        phase += 2.0 * math.pi * (pitch * math.exp(-t * 30.0) + pitch * 0.8) / SAMPLE_RATE
        sample = tone + noise
        out.append((sample * 0.9, sample * 0.9))
    return out

def make_hihat(duration=0.06, decay_rate=60.0):
    n_samples = int(SAMPLE_RATE * duration)
    out = []
    # Bandpass-like noise with metallic frequencies
    phases = [0.0, 0.0, 0.0, 0.0]
    freqs = [3200.0, 4800.0, 6400.0, 8900.0]
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        sample = (random.random() * 2.0 - 1.0) * 0.6
        for k in range(4):
            phases[k] += 2.0 * math.pi * freqs[k] / SAMPLE_RATE
            sample += math.sin(phases[k]) * 0.15
        s = sample * env * 0.7
        out.append((s, s))
    return out

def make_hihat_open(duration=0.45, decay_rate=8.0):
    n_samples = int(SAMPLE_RATE * duration)
    out = []
    phases = [0.0, 0.0, 0.0, 0.0]
    freqs = [3200.0, 4800.0, 6400.0, 8900.0]
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        sample = (random.random() * 2.0 - 1.0) * 0.5
        for k in range(4):
            phases[k] += 2.0 * math.pi * freqs[k] / SAMPLE_RATE
            sample += math.sin(phases[k]) * 0.15
        s = sample * env * 0.7
        out.append((s, s))
    return out

def make_tom(freq=120.0, duration=0.4, decay_rate=9.0):
    n_samples = int(SAMPLE_RATE * duration)
    out = []
    phase = 0.0
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        cur_freq = freq * (1.0 + 0.6 * math.exp(-t * 20.0))
        phase += 2.0 * math.pi * cur_freq / SAMPLE_RATE
        stick = (random.random() * 2.0 - 1.0) * math.exp(-t * 80.0) * 0.3
        s = math.sin(phase) * env * 0.8 + stick
        out.append((s, s))
    return out

def make_cymbal(duration=0.8, decay_rate=4.5):
    n_samples = int(SAMPLE_RATE * duration)
    out = []
    phases = [random.random() * 6.28 for _ in range(6)]
    freqs = [4200.0, 5600.0, 6800.0, 7900.0, 9500.0, 11200.0]
    for i in range(n_samples):
        t = i / SAMPLE_RATE
        env = math.exp(-t * decay_rate)
        noise = (random.random() * 2.0 - 1.0) * 0.4
        tones = sum(math.sin(phases[k]) * 0.15 for k in range(6))
        for k in range(6):
            phases[k] += 2.0 * math.pi * freqs[k] / SAMPLE_RATE
        s = (noise + tones) * env * 0.7
        out.append((s * 0.9, s * 1.1))
    return out

def generate_demo_kit(base_dir):
    os.makedirs(base_dir, exist_ok=True)

    # 1. Generate audio files
    instruments = [
        ("Kick", [("1-Kick", make_kick(160, 48, 0.35, 11)), ("2-Kick", make_kick(145, 44, 0.38, 10))]),
        ("Snare", [("1-Snare", make_snare(0.32, 210, 13)), ("2-Snare", make_snare(0.28, 195, 15))]),
        ("HihatClosed", [("1-HihatClosed", make_hihat(0.07, 50)), ("2-HihatClosed", make_hihat(0.05, 65))]),
        ("HihatOpen", [("1-HihatOpen", make_hihat_open(0.48, 7.5)), ("2-HihatOpen", make_hihat_open(0.42, 8.5))]),
        ("Tom1", [("1-Tom1", make_tom(160.0, 0.38, 9.5)), ("2-Tom1", make_tom(155.0, 0.35, 10.0))]),
        ("Tom2", [("1-Tom2", make_tom(115.0, 0.45, 8.0)), ("2-Tom2", make_tom(110.0, 0.42, 8.5))]),
        ("Crash", [("1-Crash", make_cymbal(0.85, 4.0)), ("2-Crash", make_cymbal(0.75, 4.8))]),
        ("Ride", [("1-Ride", make_cymbal(0.95, 3.5)), ("2-Ride", make_cymbal(0.85, 4.0))]),
    ]

    for inst_name, hits in instruments:
        inst_dir = os.path.join(base_dir, inst_name)
        samples_dir = os.path.join(inst_dir, "samples")
        os.makedirs(samples_dir, exist_ok=True)

        samples_xml = ""
        for hit_name, audio_data in hits:
            wav_path = os.path.join(samples_dir, f"{hit_name}.wav")
            write_wav(wav_path, audio_data)
            rel_wav = f"samples/{hit_name}.wav"
            samples_xml += f"""    <sample name="{hit_name}" power="1.0">
      <audiofile channel="OHL" file="{rel_wav}" filechannel="1"/>
      <audiofile channel="OHR" file="{rel_wav}" filechannel="2"/>
    </sample>\n"""

        inst_xml_content = f"""<?xml version="1.0" encoding="UTF-8"?>
<instrument version="2.0">
  <metadata>
    <title>{inst_name}</title>
  </metadata>
  <samples>
{samples_xml}  </samples>
</instrument>
"""
        with open(os.path.join(inst_dir, f"{inst_name}.xml"), "w") as f:
            f.write(inst_xml_content)

    # 2. Generate drumkit.xml
    inst_nodes = ""
    for inst_name, _ in instruments:
        extra_attrs = ""
        chokes_xml = ""
        if inst_name == "HihatClosed":
            extra_attrs = ' group="hihat"'
            chokes_xml = """      <chokes>
        <choke instrument="HihatOpen" choketime="80"/>
      </chokes>\n"""
        elif inst_name == "HihatOpen":
            extra_attrs = ' group="hihat"'

        inst_nodes += f"""    <instrument name="{inst_name}"{extra_attrs} file="{inst_name}/{inst_name}.xml">
{chokes_xml}      <channelmap in="OHL" out="OHL" main="true"/>
      <channelmap in="OHR" out="OHR" main="true"/>
    </instrument>\n"""

    drumkit_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<drumkit samplerate="44100" version="2.1.0">
  <metadata>
    <version>1.0.0</version>
    <title>Glorp Demo Kit</title>
    <description>Lightweight demo kit for GlorpDrums</description>
    <author>GlorpDrums</author>
    <license>CC0</license>
  </metadata>
  <channels>
    <channel name="OHL"><title>Overhead Left</title></channel>
    <channel name="OHR"><title>Overhead Right</title></channel>
  </channels>
  <instruments>
{inst_nodes}  </instruments>
</drumkit>
"""
    with open(os.path.join(base_dir, "drumkit.xml"), "w") as f:
        f.write(drumkit_xml)

    # 3. Generate midimap.xml
    midimap_xml = """<?xml version="1.0" encoding="UTF-8"?>
<midimap>
  <map note="36" instr="Kick"/>
  <map note="38" instr="Snare"/>
  <map note="42" instr="HihatClosed"/>
  <map note="46" instr="HihatOpen"/>
  <map note="48" instr="Tom1"/>
  <map note="45" instr="Tom2"/>
  <map note="49" instr="Crash"/>
  <map note="51" instr="Ride"/>
</midimap>
"""
    with open(os.path.join(base_dir, "midimap.xml"), "w") as f:
        f.write(midimap_xml)

    print(f"Generated demo kit at: {base_dir}")

if __name__ == "__main__":
    generate_demo_kit("/home/servandoc/Documents/DEV/GlorpDrums/test/kits/GlorpDemoKit")
