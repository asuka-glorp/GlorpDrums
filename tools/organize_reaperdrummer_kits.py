#!/usr/bin/env python3
"""
Organizes ReaperDrummer kits (multichannel WAV samples) into standard DrumGizmo XML format:
Generates:
  - instruments/<Instrument>.xml for each drum piece
  - drumkit.xml with channel maps and choke routing
  - midimap.xml with MIDI note mappings
"""

import os
import re
import wave
import sys

def get_wav_channels(path):
    with wave.open(path, 'r') as w:
        return w.getnchannels()

def get_channel_map(num_channels):
    """Maps multichannel recording tracks to DrumGizmo channel names."""
    if num_channels == 2:
        return [("OHL", 1), ("OHR", 2)]
    elif num_channels == 3:
        return [("DIRECT", 1), ("OHL", 2), ("OHR", 3)]
    elif num_channels == 4:
        return [("DIRECT", 1), ("DIRECT2", 2), ("OHL", 3), ("OHR", 4)]
    elif num_channels == 5:
        return [("DIRECT", 1), ("DIRECT2", 2), ("BLEED", 3), ("OHL", 4), ("OHR", 5)]
    elif num_channels == 6:
        return [("DIRECT", 1), ("DIRECT2", 2), ("OHL", 3), ("OHR", 4), ("AMBL", 5), ("AMBR", 6)]
    elif num_channels >= 7:
        return [("DIRECT", 1), ("DIRECT2", 2), ("DIRECT3", 3), ("OHL", 4), ("OHR", 5), ("AMBL", 6), ("AMBR", 7)]
    return [("OHL", 1)]

def parse_samples(samples_dir):
    # Standalone letter (A, B, C, D) after space: e.g. "Snare A 01.wav"
    pattern_std = re.compile(r'^(.*?)\s+([A-Da-d])\s+(\d+)\.wav$', re.IGNORECASE)
    # Letter directly attached to digit at end of word: e.g. "Ride 20D 01.wav"
    pattern_attached = re.compile(r'^(.*?\d+)([A-Da-d])\s+(\d+)\.wav$', re.IGNORECASE)
    # Simple number at end: e.g. "Ride bell 01.wav" or "Hihat Spl 08.wav"
    pattern_num = re.compile(r'^(.*?)\s+(\d+)\.wav$', re.IGNORECASE)

    groups = {} # raw_prefix -> list of (rr, vel, filename, num_channels)

    files = sorted(os.listdir(samples_dir))
    for f in files:
        if not f.endswith('.wav'):
            continue
        
        m = pattern_std.match(f)
        if m:
            prefix = m.group(1).strip()
            rr = m.group(2).upper()
            vel = int(m.group(3))
        else:
            m = pattern_attached.match(f)
            if m:
                prefix = m.group(1).strip()
                rr = m.group(2).upper()
                vel = int(m.group(3))
            else:
                m = pattern_num.match(f)
                if m:
                    prefix = m.group(1).strip()
                    rr = 'A'
                    vel = int(m.group(2))
                else:
                    print(f"Skipping unknown pattern: {f}")
                    continue

        full_path = os.path.join(samples_dir, f)
        num_ch = get_wav_channels(full_path)

        if prefix not in groups:
            groups[prefix] = []
        groups[prefix].append((rr, vel, f, num_ch))

    return groups

def build_kit(kit_dir, kit_title, prefix_map, open_hat_names, closed_hat_names):
    print(f"\n=======================================================")
    print(f"Building DrumGizmo Kit: {kit_title}")
    print(f"Path: {kit_dir}")
    print(f"=======================================================")

    samples_dir = os.path.join(kit_dir, "samples")
    inst_dir = os.path.join(kit_dir, "instruments")
    os.makedirs(inst_dir, exist_ok=True)

    raw_groups = parse_samples(samples_dir)
    print(f"Found {len(raw_groups)} raw sample groups.")

    all_channels_set = set(["DIRECT", "DIRECT2", "DIRECT3", "BLEED", "OHL", "OHR", "AMBL", "AMBR"])
    drumkit_instruments = [] # (inst_name, group, chokes_list, file_rel, channel_map)
    midimap_entries = []     # (note, inst_name)

    for raw_prefix, files_info in raw_groups.items():
        if raw_prefix not in prefix_map:
            print(f"Warning: No mapping for raw prefix '{raw_prefix}' (skipping)")
            continue

        meta = prefix_map[raw_prefix]
        inst_name = meta["name"]
        note = meta["note"]
        group = meta.get("group")
        
        # Max velocity in this group to normalize power
        max_vel = max(x[1] for x in files_info)
        min_vel = min(x[1] for x in files_info)
        vel_span = max(1, max_vel - min_vel)

        num_channels = files_info[0][3]
        ch_map = get_channel_map(num_channels)

        # Chokes configuration
        chokes_list = []
        if inst_name in closed_hat_names:
            for op in open_hat_names:
                chokes_list.append((op, 40))

        # Write instrument XML
        samples_xml = ""
        # Sort by velocity then round-robin
        files_info.sort(key=lambda x: (x[1], x[0]))

        for rr, vel, fname, chs in files_info:
            sample_id = f"{fname.replace('.wav', '').replace(' ', '_')}"
            # Power in range 0.05 .. 1.00
            power = 0.05 + 0.95 * ((vel - min_vel) / float(vel_span))
            
            af_tags = ""
            for ch_name, ch_idx in ch_map:
                af_tags += f'      <audiofile channel="{ch_name}" file="../samples/{fname}" filechannel="{ch_idx}"/>\n'

            samples_xml += f"""    <sample name="{sample_id}" power="{power:.3f}">
{af_tags}    </sample>\n"""

        inst_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<instrument version="2.0">
  <metadata>
    <title>{inst_name}</title>
  </metadata>
  <samples>
{samples_xml}  </samples>
</instrument>
"""
        inst_file_path = os.path.join(inst_dir, f"{inst_name}.xml")
        with open(inst_file_path, 'w') as f:
            f.write(inst_xml)

        rel_inst_file = f"instruments/{inst_name}.xml"
        drumkit_instruments.append((inst_name, group, chokes_list, rel_inst_file, ch_map))
        midimap_entries.append((note, inst_name))

    # Write drumkit.xml
    channels_xml = ""
    # Defined in logical order
    ordered_chans = [
        ("DIRECT", "Direct 1"),
        ("DIRECT2", "Direct 2"),
        ("DIRECT3", "Direct 3"),
        ("BLEED", "Bleed"),
        ("OHL", "Overhead Left"),
        ("OHR", "Overhead Right"),
        ("AMBL", "Ambience Left"),
        ("AMBR", "Ambience Right"),
    ]
    for ch_name, ch_title in ordered_chans:
        channels_xml += f'    <channel name="{ch_name}"><title>{ch_title}</title></channel>\n'

    inst_nodes = ""
    for inst_name, group, chokes, file_rel, ch_map in drumkit_instruments:
        group_attr = f' group="{group}"' if group else ""
        chokes_xml = ""
        if chokes:
            chokes_inner = "\n".join([f'        <choke instrument="{t}" choketime="{ct}"/>' for t, ct in chokes])
            chokes_xml = f"      <chokes>\n{chokes_inner}\n      </chokes>\n"

        ch_map_xml = ""
        for ch_name, _ in ch_map:
            main_attr = ' main="true"' if ch_name in ("OHL", "OHR") else ""
            ch_map_xml += f'      <channelmap in="{ch_name}" out="{ch_name}"{main_attr}/>\n'

        inst_nodes += f"""    <instrument name="{inst_name}"{group_attr} file="{file_rel}">
{chokes_xml}{ch_map_xml}    </instrument>\n"""

    drumkit_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<drumkit samplerate="44100" version="2.1.0">
  <metadata>
    <version>1.0.0</version>
    <title>{kit_title}</title>
    <description>Converted from ReaperDrummer for GlorpDrums</description>
    <author>ReaperDrummer / GlorpDrums</author>
  </metadata>
  <channels>
{channels_xml}  </channels>
  <instruments>
{inst_nodes}  </instruments>
</drumkit>
"""
    with open(os.path.join(kit_dir, "drumkit.xml"), 'w') as f:
        f.write(drumkit_xml)

    # Write midimap.xml
    midimap_nodes = ""
    # Sort by note
    midimap_entries.sort(key=lambda x: x[0])
    for note, inst_name in midimap_entries:
        midimap_nodes += f'  <map note="{note}" instr="{inst_name}"/>\n'

    midimap_xml = f"""<?xml version="1.0" encoding="UTF-8"?>
<midimap>
{midimap_nodes}</midimap>
"""
    with open(os.path.join(kit_dir, "midimap.xml"), 'w') as f:
        f.write(midimap_xml)

    print(f"[OK] drumkit.xml created with {len(drumkit_instruments)} instruments.")
    print(f"[OK] midimap.xml created with {len(midimap_entries)} mappings.")

# ---------------------------------------------------------------------------
# Kit Mapping Definitions
# ---------------------------------------------------------------------------

LUDWIG_PREFIX_MAP = {
    "BassDrum":                            {"name": "Kick",              "note": 36},
    "BlackCortex Vers2 Snare ctr":         {"name": "SnareHead",         "note": 38},
    "BlackCortex Vers2 Snare rimshot":     {"name": "Rimshot",           "note": 40},
    "Black cortex snare side stick":       {"name": "RimClick",          "note": 37},
    "BlackCortex Vers2 Snare edge":        {"name": "SnareEdge",         "note": 39},
    "BlackCortex Vers2 muffled snare":     {"name": "SnareMuted",        "note": 33},
    "Ludwig 13 muffled":                   {"name": "SnareFat",          "note": 29},
    "Black cortex Tom 12v2 ctr":           {"name": "Tom1Head",          "note": 48},
    "Black cortex Tom 12v2 edge":          {"name": "Tom1Rim",           "note": 50},
    "Black cortex Tom 13v2 ctr":           {"name": "Tom2Head",          "note": 47},
    "Black cortex Tom 13v2 edge":          {"name": "Tom2Rim",           "note": 60},
    "Black cortex Tom 14 v2 ctr":          {"name": "Tom3Head",          "note": 45},
    "Black cortex Tom 14 v2 edge":         {"name": "Tom3Rim",           "note": 62},
    "Black cortex Floor tom 1 v2":         {"name": "FTomHead",          "note": 43},
    "Black cortex Floor tom 1 edg":        {"name": "FTomRim",           "note": 58},
    "Black cortex Floor tom 18 v2 ctr":    {"name": "FTom2Head",         "note": 41},
    "Black cortex Floor tom 18 v2 edge":   {"name": "FTom2Rim",          "note": 65},
    "Black cortex HH clo bow":             {"name": "HihatClosed",       "note": 42, "group": "hihat"},
    "Black cortex HH clo edg":             {"name": "HihatClosedEdge",   "note": 22, "group": "hihat"},
    "Black cortex HH op 1 bow":            {"name": "HihatOpen1Bow",     "note": 23, "group": "hihat"},
    "Black cortex HH op 1 edg":            {"name": "HihatOpen1Edge",    "note": 24, "group": "hihat"},
    "Black cortex HH op 2 bow":            {"name": "HihatOpen2Bow",     "note": 31, "group": "hihat"},
    "Black cortex HH op 2 edg":            {"name": "HihatOpen2Edge",    "note": 32, "group": "hihat"},
    "Black cortex HH op 3 bow":            {"name": "HihatOpen3Bow",     "note": 34, "group": "hihat"},
    "Black cortex HH op 3 edg":            {"name": "HihatOpen3Edge",    "note": 27, "group": "hihat"},
    "Black cortex HH op 4 bow":            {"name": "HihatOpen4Bow",     "note": 30, "group": "hihat"},
    "Black cortex HH op 4 edg":            {"name": "HihatOpen4Edge",    "note": 25, "group": "hihat"},
    "Black cortex HH full op bow":         {"name": "HihatOpen",         "note": 46, "group": "hihat"},
    "Black cortex HH full op edg":         {"name": "HihatOpenEdge",     "note": 26, "group": "hihat"},
    "Black cortex HH ped":                 {"name": "HihatPedal",        "note": 44, "group": "hihat"},
    "Black cortex HH slammed":             {"name": "HihatSlammed",      "note": 61, "group": "hihat"},
    "Sabian 16 bow":                       {"name": "Crash1Bow",         "note": 49},
    "Sabian 16 edge":                      {"name": "Crash1Edge",        "note": 55},
    "Istanbul Xist ion 19 dark crash bow": {"name": "Crash2Bow",         "note": 57},
    "Istanbul Xist ion 19 dark crash edge":{"name": "Crash2Edge",        "note": 52},
    "Sabian bow":                          {"name": "Crash3Bow",         "note": 56},
    "Sabian edge":                         {"name": "Crash3Edge",        "note": 54},
    "Ride Istanbul agop sigature bow":     {"name": "RideBow",           "note": 51},
    "Ride Istanbul agop sigature bell":    {"name": "RideBell",          "note": 53},
    "Ride Istanbul agop sigature edge":    {"name": "RideEdge",          "note": 59},
}

LUDWIG_OPEN_HATS = [
    "HihatOpen1Bow", "HihatOpen1Edge", "HihatOpen2Bow", "HihatOpen2Edge",
    "HihatOpen3Bow", "HihatOpen3Edge", "HihatOpen4Bow", "HihatOpen4Edge",
    "HihatOpen", "HihatOpenEdge"
]
LUDWIG_CLOSED_HATS = ["HihatClosed", "HihatClosedEdge", "HihatPedal", "HihatSlammed"]

PREMIER_PREFIX_MAP = {
    "Kick Signia muf":                     {"name": "Kick",              "note": 36},
    "Snare Premier Ctr":                   {"name": "SnareHead",         "note": 38},
    "Snare Premier Rim":                   {"name": "Rimshot",           "note": 40},
    "Snare Premier Side Stick":            {"name": "RimClick",          "note": 37},
    "Snare Premier edg":                   {"name": "SnareEdge",         "note": 39},
    "Tom 12 Signia ctr":                   {"name": "Tom1Head",          "note": 48},
    "Tom 12 Signia edg":                   {"name": "Tom1Rim",           "note": 50},
    "Tom 13 Signia ctr":                   {"name": "Tom2Head",          "note": 45},
    "Tom 13 Signia edg":                   {"name": "Tom2Rim",           "note": 47},
    "Tom 16 Signia ctr":                   {"name": "FTomHead",          "note": 43},
    "Tom 16 Signia edg":                   {"name": "FTomRim",           "note": 58},
    "Zildj K Hyb Clo":                     {"name": "HihatClosed",       "note": 42, "group": "hihat"},
    "Zildj K Hyb Clo edg":                 {"name": "HihatClosedEdge",   "note": 22, "group": "hihat"},
    "Zildj K Hyb Clo Op1":                 {"name": "HihatOpen1Bow",     "note": 23, "group": "hihat"},
    "Zildj K Hyb Clo Op1 edge":            {"name": "HihatOpen1Edge",    "note": 24, "group": "hihat"},
    "Zildj K Hyb Clo Op 2":                {"name": "HihatOpen2Bow",     "note": 34, "group": "hihat"},
    "Zildj K Hyb Clo Op 2 Edge":           {"name": "HihatOpen2Edge",    "note": 27, "group": "hihat"},
    "Zildj K Hyb Clo Op 3":                {"name": "HihatOpen3Bow",     "note": 30, "group": "hihat"},
    "Zildj K Hyb Clo Op 3 Edge":           {"name": "HihatOpen3Edge",    "note": 25, "group": "hihat"},
    "Zildj K Hyb Op 3":                    {"name": "HihatOpen",         "note": 46, "group": "hihat"},
    "Zildj K Hyb ped":                     {"name": "HihatPedal",        "note": 44, "group": "hihat"},
    "Zildj K Hyb Spl":                     {"name": "HihatSplash",       "note": 63, "group": "hihat"},
    "Crash Cymbal Sabian bow":             {"name": "Crash1Bow",         "note": 49},
    "Crash Cymbal Sabian edge":            {"name": "Crash1Edge",        "note": 55},
    "Crash FX cymb Zildjian K EFX bow":    {"name": "Crash2Bow",         "note": 57},
    "Crash FX cymb Zildjian K EFX edge":   {"name": "Crash2Edge",        "note": 52},
    "Ride Bosphorus Vintage Master bow":   {"name": "RideBow",           "note": 51},
    "Ride Bosphorus Vintage Master bell":  {"name": "RideBell",          "note": 53},
    "Ride Bosphorus Vintage Master edg":   {"name": "RideEdge",          "note": 59},
    "Ride Zildjian Avedis 20":             {"name": "Ride2Bow",          "note": 28},
    "Ride Zildjian Avedis 20 Bell":        {"name": "Ride2Bell",         "note": 29},
    "Ride Zildjian Avedis 20 Crash":       {"name": "Ride2Edge",         "note": 60},
    "Cowbell LP":                          {"name": "Cowbell",           "note": 31},
    "Cowbell LP muff":                     {"name": "CowbellMuff",       "note": 32},
    "Cowbell LP Muff hi":                  {"name": "CowbellHi",         "note": 33},
}

PREMIER_OPEN_HATS = [
    "HihatOpen1Bow", "HihatOpen1Edge", "HihatOpen2Bow", "HihatOpen2Edge",
    "HihatOpen3Bow", "HihatOpen3Edge", "HihatOpen", "HihatSplash"
]
PREMIER_CLOSED_HATS = ["HihatClosed", "HihatClosedEdge", "HihatPedal"]

if __name__ == "__main__":
    ludwig_dir = "/home/servandoc/DrumGizmo/kits/ReaperDrummer Ludwig Black Cortex"
    if os.path.isdir(ludwig_dir):
        build_kit(ludwig_dir, "Ludwig Black Cortex", LUDWIG_PREFIX_MAP, LUDWIG_OPEN_HATS, LUDWIG_CLOSED_HATS)

    premier_dir = "/home/servandoc/DrumGizmo/kits/ReaperDrummer Premier Signia"
    if os.path.isdir(premier_dir):
        build_kit(premier_dir, "Premier Signia", PREMIER_PREFIX_MAP, PREMIER_OPEN_HATS, PREMIER_CLOSED_HATS)
