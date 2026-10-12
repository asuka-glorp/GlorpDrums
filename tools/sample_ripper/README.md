# GlorpDrums Automated Sample Ripper

An automated extraction pipeline to sample virtual drum instruments (such as **EZDrummer 3**, **Superior Drummer**, **Addictive Drums**, or **Steven Slate Drums**) using **Ardour 8** (or any DAW like Reaper/Bitwig) and build ready-to-play **DrumGizmo XML** kits for **GlorpDrums**.

---

## Quick Start (3 Simple Steps)

### Step 1: Generate the Sampling MIDI Sequence
Run the MIDI generator:
```bash
python3 tools/sample_ripper/make_sampling_midi.py ./capture
```
This produces two files in `./capture`:
* `sampling_track.mid` — The mathematically timed MIDI trigger track.
* `sampling_manifest.json` — The exact timestamp and articulation timetable.

*By default, it captures 20 drum articulations $\times$ 6 velocity tiers (20, 42, 64, 85, 106, 127) $\times$ 3 round robins = 360 hits. Each articulation features tailored decay spacing (up to 10.5 seconds for cymbals and 6.8 seconds for open hi-hats) so tails decay into total silence without bleeding into subsequent hits.*

---

### Step 2: Render in Ardour 8 (or Reaper / Bitwig)

1. Open **Ardour 8** and create a new session.
2. Go to **Track -> Add Track, Bus or VCA...**
   * Select **MIDI Tracks**.
   * In the **Instrument** dropdown, select your **EZDrummer 3** plugin.
3. Open the **EZDrummer 3** GUI:
   * Select the drum kit, preset, or tuning you want to capture (e.g., "Vintage 70s", "Modern Metal", "Studio Tight").
   * Adjust the mixer (room mic, overhead, snare bleed) to your preferred tone.
4. Drag and drop `sampling_track.mid` onto the MIDI track, aligned to the start (**00:00:00**).
5. In Ardour, open **Session -> Export -> Export to Audio File...** (or press `Alt + E`):
   * Channels: **Master (Stereo)**
   * Format: **WAV (24-bit, 44.1 kHz or 48 kHz)**
   * Export path: Save as `render.wav` (e.g., in `~/Desktop/render.wav` or `./capture/render.wav`).
6. Click **Export**. Ardour will bounce the track offline in approximately **20–30 seconds**.

---

### Step 3: Slice and Build the GlorpDrums Kit

Run the slicer and kit builder:
```bash
python3 tools/sample_ripper/slice_and_build_kit.py \
    ./capture/render.wav \
    ./capture/sampling_manifest.json \
    ~/.config/GlorpDrums/kits/EZD_MyKit \
    "EZD My Kit"
```

### What Happens Automatically:
1. **Sample-Accurate Transient Detection:** Scans around each scheduled timestamp to find the true onset, compensating for any DAW/plugin buffer latency.
2. **Clean Fadeout & Trimming:** Applies a 1.5ms pre-roll to preserve the initial stick attack click, and a natural anti-click tail fade.
3. **24-bit Stereo Samples:** Slices and saves all 360 WAV files organized into instrument folders (`Kick`, `SnareHead`, `RimClick`, `Rimshot`, `Tom1Head`, `Tom1Rim`, `HihatClosed`, `HihatOpen`, `Crash1Edge`, `RideBell`, etc.).
4. **DrumGizmo XML Generation:**
   * Creates per-instrument XML files with `<sample power="...">` velocity curves.
   * Creates `drumkit.xml` with channel mapping and mutual choke configurations (hi-hat choking open hat, etc.).
   * Creates `midimap.xml` matching standard General MIDI mapping.

---

## Loading into GlorpDrums

Open **GlorpDrums** (Standalone or Plugin):
1. Click **FILE -> Load Kit**.
2. Select your newly extracted kit directory: `~/.config/GlorpDrums/kits/EZD_MyKit/drumkit.xml`.
3. All zones, velocity layers, round robins, and choke groupings will be active and responsive!
