#!/usr/bin/env python3
"""
GlorpDrums - Automated Sample Ripper: MIDI Generator
Generates a mathematically timed MIDI sequence to sample virtual instruments (like EZDrummer 3)
in any DAW (Ardour, Reaper, Bitwig, etc.).
Outputs:
  - sampling_track.mid: Standard Type 0 MIDI file to import into the DAW.
  - sampling_manifest.json: Exact timing manifest for slice_and_build_kit.py.
"""

import sys
import os
import json
import struct

# Standard EZDrummer 3 GM+ Articulations Map with Natural Acoustic Decay Spacing
# Notes matched to EZDrummer 3 default profile:
# - Tom 1: Head (48), Rimshot (82)
# - Tom 2: Head (45), Rimshot (80)
# - Floor Tom: Head (41), Rimshot (75)
# - Crash 1: Hit/Edge (49), Bow Tip (84)
# - Crash 2: Hit/Edge (57), Bow Tip (89)
# - Hi-Hat: Closed (42, CC4=127), SemiOpen (26, CC4=40), Open (46, CC4=0, no self-choke)
DEFAULT_ARTICULATIONS = [
    # (Instrument Name, Group, Chokes, MIDI Note, Piece Category, Gap, Duration)
    {"name": "Kick",          "note": 36, "group": None,    "chokes": [], "gap": 4.2,  "duration": 3.8},
    {"name": "SnareHead",     "note": 38, "group": None,    "chokes": [], "gap": 4.5,  "duration": 4.0},
    {"name": "RimClick",      "note": 37, "group": None,    "chokes": [], "gap": 3.0,  "duration": 2.5},
    {"name": "Rimshot",       "note": 40, "group": None,    "chokes": [], "gap": 4.5,  "duration": 4.0},
    {"name": "Tom1Head",      "note": 48, "group": None,    "chokes": [], "gap": 4.8,  "duration": 4.2},
    {"name": "Tom1Rim",       "note": 82, "group": None,    "chokes": [], "gap": 3.2,  "duration": 2.7},
    {"name": "Tom2Head",      "note": 45, "group": None,    "chokes": [], "gap": 5.2,  "duration": 4.6},
    {"name": "Tom2Rim",       "note": 80, "group": None,    "chokes": [], "gap": 3.2,  "duration": 2.7},
    {"name": "FTomHead",      "note": 41, "group": None,    "chokes": [], "gap": 6.2,  "duration": 5.6},
    {"name": "FTomRim",       "note": 75, "group": None,    "chokes": [], "gap": 3.2,  "duration": 2.7},
    {"name": "HihatClosed",   "note": 42, "group": "hihat", "chokes": [("HihatOpen", 40), ("HihatSemiOpen", 40)], "gap": 3.0, "duration": 2.5, "cc4": 127},
    {"name": "HihatSemiOpen", "note": 26, "group": "hihat", "chokes": [("HihatOpen", 40)], "gap": 4.8,  "duration": 4.2, "cc4": 40},
    {"name": "HihatOpen",     "note": 46, "group": None,    "chokes": [], "gap": 6.8,  "duration": 6.2, "cc4": 0},
    {"name": "Crash1Edge",    "note": 49, "group": None,    "chokes": [], "gap": 10.5, "duration": 10.0},
    {"name": "Crash1Bow",     "note": 84, "group": None,    "chokes": [], "gap": 9.0,  "duration": 8.5},
    {"name": "Crash2Edge",    "note": 57, "group": None,    "chokes": [], "gap": 10.5, "duration": 10.0},
    {"name": "Crash2Bow",     "note": 89, "group": None,    "chokes": [], "gap": 9.0,  "duration": 8.5},
    {"name": "RideBow",       "note": 51, "group": None,    "chokes": [], "gap": 9.5,  "duration": 9.0},
    {"name": "RideBell",      "note": 53, "group": None,    "chokes": [], "gap": 8.8,  "duration": 8.2},
    {"name": "RideEdge",      "note": 59, "group": None,    "chokes": [], "gap": 10.5, "duration": 10.0},
]

# Velocity Layers & Round Robins
DEFAULT_VELOCITIES = [20, 42, 64, 85, 106, 127] # 6 expressive tiers
DEFAULT_ROUND_ROBINS = 3                         # 3 round robins per tier

def var_len(value):
    """Encodes an integer into MIDI variable-length quantity bytes."""
    buf = [value & 0x7F]
    value >>= 7
    while value > 0:
        buf.append((value & 0x7F) | 0x80)
        value >>= 7
    buf.reverse()
    return bytes(buf)

def generate_midi_and_manifest(output_dir,
                               articulations=DEFAULT_ARTICULATIONS,
                               velocities=DEFAULT_VELOCITIES,
                               round_robins=DEFAULT_ROUND_ROBINS,
                               bpm=120,
                               scale_factor=1.0,
                               cymbal_gap_override=None):
    os.makedirs(output_dir, exist_ok=True)
    midi_path = os.path.join(output_dir, "sampling_track.mid")
    manifest_path = os.path.join(output_dir, "sampling_manifest.json")

    ticks_per_beat = 480
    sec_per_beat = 60.0 / bpm
    ticks_per_sec = ticks_per_beat / sec_per_beat

    # Build sequence of events
    manifest_events = []
    current_time_sec = 2.0 # 2.0s initial lead-in before the first hit

    for art in articulations:
        is_cymbal = art["name"].startswith(("Crash", "Ride"))
        if is_cymbal and cymbal_gap_override is not None:
            base_gap = cymbal_gap_override
            base_dur = max(1.0, cymbal_gap_override - 0.5)
        else:
            base_gap = art.get("gap", 4.5) * scale_factor
            base_dur = art.get("duration", base_gap - 0.5) * scale_factor

        clean_dur = min(base_dur, max(0.5, base_gap - 0.4))

        for v_idx, vel in enumerate(velocities):
            tier_name = f"v{v_idx + 1}_{vel}"
            for rr in range(1, round_robins + 1):
                event_info = {
                    "instrument": art["name"],
                    "note": art["note"],
                    "velocity": vel,
                    "normalized_power": round(vel / 127.0, 3),
                    "tier_name": tier_name,
                    "round_robin": rr,
                    "sample_name": f"{tier_name}_rr{rr}",
                    "start_time_sec": round(current_time_sec, 4),
                    "duration_sec": round(clean_dur, 4),
                    "gap_sec": round(base_gap, 4),
                    "group": art["group"],
                    "chokes": art["chokes"],
                    "cc4": art.get("cc4")
                }
                manifest_events.append(event_info)
                current_time_sec += base_gap

    # Construct Standard MIDI File (SMF Type 0)
    # Track events:
    # 1. Set Tempo: 120 BPM -> 500,000 microseconds per quarter note
    tempo_us = int(60000000.0 / bpm)
    track_bytes = bytearray()
    track_bytes.extend(var_len(0)) # delta 0
    track_bytes.extend(b'\xFF\x51\x03') # Meta Set Tempo
    track_bytes.extend(tempo_us.to_bytes(3, byteorder='big'))

    # Time signature 4/4
    track_bytes.extend(var_len(0))
    track_bytes.extend(b'\xFF\x58\x04\x04\x02\x18\x08')

    last_tick = 0
    note_duration_ticks = int(0.2 * ticks_per_sec) # 200ms note gate

    midi_channel = 10 # Standard General MIDI drum channel (Channel 10)
    status_on = 0x90 | ((midi_channel - 1) & 0x0F)   # 0x99 for Channel 10
    status_off = 0x80 | ((midi_channel - 1) & 0x0F)  # 0x89 for Channel 10
    status_cc = 0xB0 | ((midi_channel - 1) & 0x0F)   # 0xB9 for Channel 10

    for ev in manifest_events:
        start_tick = int(ev["start_time_sec"] * ticks_per_sec)
        note = ev["note"]
        vel = ev["velocity"]
        cc4 = ev.get("cc4")

        delta_on = max(0, start_tick - last_tick)

        # Emit CC4 (Hi-Hat Foot Pedal Controller) if defined
        if cc4 is not None:
            track_bytes.extend(var_len(delta_on))
            track_bytes.extend(bytes([status_cc, 4, int(cc4)]))
            delta_on = 0 # note follows immediately

        # Note On (Channel 10)
        track_bytes.extend(var_len(delta_on))
        track_bytes.extend(bytes([status_on, note, vel]))

        # Note Off (Channel 10)
        delta_off = note_duration_ticks
        track_bytes.extend(var_len(delta_off))
        track_bytes.extend(bytes([status_off, note, 0]))

        last_tick = start_tick + note_duration_ticks

    # End of Track
    track_bytes.extend(var_len(ticks_per_beat))
    track_bytes.extend(b'\xFF\x2F\x00')

    # MIDI File Header
    header = bytearray(b'MThd')
    header.extend((6).to_bytes(4, byteorder='big')) # header length
    header.extend((0).to_bytes(2, byteorder='big')) # format 0 (single track)
    header.extend((1).to_bytes(2, byteorder='big')) # 1 track
    header.extend(ticks_per_beat.to_bytes(2, byteorder='big'))

    track_header = bytearray(b'MTrk')
    track_header.extend(len(track_bytes).to_bytes(4, byteorder='big'))

    with open(midi_path, 'wb') as f:
        f.write(header)
        f.write(track_header)
        f.write(track_bytes)

    total_duration_sec = current_time_sec + 2.0
    manifest = {
        "kit_name": "EZD_Captured_Kit",
        "bpm": bpm,
        "total_hits": len(manifest_events),
        "total_duration_sec": round(total_duration_sec, 2),
        "total_duration_min": round(total_duration_sec / 60.0, 2),
        "articulations": [a["name"] for a in articulations],
        "events": manifest_events
    }

    with open(manifest_path, 'w') as f:
        json.dump(manifest, f, indent=2)

    print(f"=== MIDI Sampling Track Generated ===")
    print(f"  Output MIDI:       {midi_path}")
    print(f"  Manifest:          {manifest_path}")
    print(f"  Total Hits:        {len(manifest_events)}")
    print(f"  Total Runtime:     {manifest['total_duration_min']} minutes")
    print(f"  Articulations:     {len(articulations)}")
    print(f"  Cymbal Spacing:    8.8s - 10.5s (Bleed-free tails)")
    print(f"  Drum Spacing:      3.0s - 6.2s")
    print(f"  Velocities:        {len(velocities)} tiers ({velocities})")
    print(f"  Round Robins:      {round_robins} per tier")
    return midi_path, manifest_path

if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description="GlorpDrums Automated Sample Ripper MIDI Generator")
    parser.add_argument("output", nargs="?", default="./tools/sample_ripper/output",
                        help="Output directory for sampling_track.mid and sampling_manifest.json")
    parser.add_argument("--scale", type=float, default=1.0,
                        help="Global spacing scale factor (default: 1.0)")
    parser.add_argument("--cymbal-gap", type=float, default=None,
                        help="Override cymbal gap in seconds (e.g., 10.5)")
    parser.add_argument("--bpm", type=int, default=120,
                        help="BPM tempo (default: 120)")
    args = parser.parse_args()

    generate_midi_and_manifest(
        output_dir=args.output,
        bpm=args.bpm,
        scale_factor=args.scale,
        cymbal_gap_override=args.cymbal_gap
    )
