#!/usr/bin/env python3
"""
tools/fetch_salamander_packin.py
Downloads a curated, lightweight (~25 MB) subset of the open-source Salamander Studio Kit
(Yamaha Custom Oak + real Zildjian A-Custom cymbals, CC-BY / CC0) directly from archive.org
and constructs the official pack-in kit for GlorpDrums (test/kits/GlorpDemoKit and ~/.config/GlorpDrums/kits/GlorpDemoKit).
"""

import os
import sys
import shutil
import tarfile
import urllib.request
import wave
import struct
import math

ARCHIVE_URL = "https://dn710709.ca.archive.org/0/items/SalamanderDrumkit/salamanderDrumkit.tar.bz2"

# Destination directory
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEMO_KIT_DIR = os.path.join(REPO_ROOT, "test", "kits", "GlorpDemoKit")
USER_KIT_DIR = os.path.expanduser("~/.config/GlorpDrums/kits/GlorpDemoKit")

# Map of target instrument -> list of patterns in OH/ to extract
# We pick 2 to 3 velocity layers, with up to 2 round-robins each, to keep total size under 25 MB!
SELECTIONS = {
    "Kick": [
        ("OH/kick_OH_P_1.wav", 0.4),
        ("OH/kick_OH_P_2.wav", 0.4),
        ("OH/kick_OH_F_1.wav", 0.75),
        ("OH/kick_OH_F_2.wav", 0.75),
        ("OH/kick_OH_FF_1.wav", 1.0),
        ("OH/kick_OH_FF_2.wav", 1.0),
    ],
    "SnareHead": [
        ("OH/snare_OH_Ghost_1.wav", 0.25),
        ("OH/snare_OH_Ghost_2.wav", 0.25),
        ("OH/snare_OH_MP_1.wav", 0.55),
        ("OH/snare_OH_MP_2.wav", 0.55),
        ("OH/snare_OH_F_1.wav", 0.8),
        ("OH/snare_OH_F_2.wav", 0.8),
        ("OH/snare_OH_FF_1.wav", 1.0),
        ("OH/snare_OH_FF_2.wav", 1.0),
    ],
    "RimClick": [
        ("OH/snareStick_OH_F_1.wav", 0.6),
        ("OH/snareStick_OH_F_2.wav", 0.85),
        ("OH/snareStick_OH_F_3.wav", 1.0),
    ],
    "Rimshot": [
        ("OH/snare2_OH_MP_1.wav", 0.5),
        ("OH/snare2_OH_MP_2.wav", 0.5),
        ("OH/snare2_OH_F_1.wav", 0.8),
        ("OH/snare2_OH_F_2.wav", 0.8),
        ("OH/snare2_OH_FF_1.wav", 1.0),
        ("OH/snare2_OH_FF_2.wav", 1.0),
    ],
    "Tom1Head": [
        ("OH/hiTom_OH_P_1.wav", 0.4),
        ("OH/hiTom_OH_P_2.wav", 0.4),
        ("OH/hiTom_OH_F_1.wav", 0.75),
        ("OH/hiTom_OH_F_2.wav", 0.75),
        ("OH/hiTom_OH_FF_1.wav", 1.0),
        ("OH/hiTom_OH_FF_2.wav", 1.0),
    ],
    "Tom1Rim": [
        ("OH/hiTom_OH_F_3.wav", 0.7),
        ("OH/hiTom_OH_FF_3.wav", 1.0),
    ],
    "Tom2Head": [
        ("OH/hiTom_OH_P_3.wav", 0.4),
        ("OH/hiTom_OH_F_4.wav", 0.75),
        ("OH/hiTom_OH_FF_4.wav", 1.0),
    ],
    "Tom2Rim": [
        ("OH/hiTom_OH_F_5.wav", 0.7),
        ("OH/hiTom_OH_FF_5.wav", 1.0),
    ],
    "FTomHead": [
        ("OH/loTom_OH_PP_1.wav", 0.35),
        ("OH/loTom_OH_PP_2.wav", 0.35),
        ("OH/loTom_OH_MP_1.wav", 0.7),
        ("OH/loTom_OH_MP_2.wav", 0.7),
        ("OH/loTom_OH_FF_1.wav", 1.0),
        ("OH/loTom_OH_FF_2.wav", 1.0),
    ],
    "FTomRim": [
        ("OH/loTom_OH_MP_3.wav", 0.7),
        ("OH/loTom_OH_FF_3.wav", 1.0),
    ],
    "HihatClosed": [
        ("OH/hihatClosed_OH_P_1.wav", 0.45),
        ("OH/hihatClosed_OH_P_2.wav", 0.45),
        ("OH/hihatClosed_OH_F_1.wav", 0.85),
        ("OH/hihatClosed_OH_F_2.wav", 0.85),
    ],
    "HihatSemiOpen": [
        ("OH/hihatSemiOpen2_OH_P_1.wav", 0.5),
        ("OH/hihatSemiOpen2_OH_F_1.wav", 0.85),
        ("OH/hihatSemiOpen4_OH_F_1.wav", 1.0),
    ],
    "HihatOpen": [
        ("OH/hihatOpen_OH_P_1.wav", 0.4),
        ("OH/hihatOpen_OH_P_2.wav", 0.4),
        ("OH/hihatOpen_OH_F_1.wav", 0.75),
        ("OH/hihatOpen_OH_F_2.wav", 0.75),
        ("OH/hihatOpen_OH_FF_1.wav", 1.0),
        ("OH/hihatOpen_OH_FF_2.wav", 1.0),
    ],
    "Crash1Edge": [
        ("OH/crash1_OH_P_1.wav", 0.5),
        ("OH/crash1_OH_P_2.wav", 0.5),
        ("OH/crash1_OH_FF_1.wav", 1.0),
        ("OH/crash1_OH_FF_2.wav", 1.0),
    ],
    "Crash1Bow": [
        ("OH/crash1_OH_P_3.wav", 0.5),
        ("OH/crash1_OH_FF_3.wav", 0.85),
    ],
    "Crash1Choke": [
        ("OH/crash1Choke_OH_F_1.wav", 0.7),
        ("OH/crash1Choke_OH_F_2.wav", 1.0),
    ],
    "Crash2Edge": [
        ("OH/crash2_OH_P_1.wav", 0.5),
        ("OH/crash2_OH_FF_1.wav", 0.85),
        ("OH/crash2_OH_FF_2.wav", 1.0),
    ],
    "Crash2Bow": [
        ("OH/crash2_OH_P_2.wav", 0.5),
        ("OH/crash2_OH_FF_3.wav", 0.9),
    ],
    "Crash2Choke": [
        ("OH/crash2Choke_OH_F_1.wav", 0.7),
        ("OH/crash2Choke_OH_F_2.wav", 1.0),
    ],
    "RideBow": [
        ("OH/ride1_OH_MP_1.wav", 0.5),
        ("OH/ride1_OH_MP_2.wav", 0.5),
        ("OH/ride1_OH_FF_1.wav", 0.9),
        ("OH/ride1_OH_FF_2.wav", 1.0),
    ],
    "RideBell": [
        ("OH/ride1Bell_OH_F_1.wav", 0.6),
        ("OH/ride1Bell_OH_F_2.wav", 0.85),
        ("OH/ride1Bell_OH_F_3.wav", 1.0),
    ],
    "RideEdge": [
        ("OH/ride2Crash_OH_MP_1.wav", 0.6),
        ("OH/ride2Crash_OH_FF_1.wav", 0.9),
        ("OH/ride2Crash_OH_FF_2.wav", 1.0),
    ],
    "RideChoke": [
        ("OH/ride2CrashChoke_OH_F_1.wav", 0.7),
        ("OH/ride2CrashChoke_OH_F_2.wav", 1.0),
    ],
}

def build_instrument_xml(inst_name, samples_info, inst_dir):
    xml_lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<instrument name="{inst_name}" version="2.0">',
        '  <samples>'
    ]
    for filename, power in samples_info:
        sample_name = os.path.splitext(filename)[0]
        xml_lines.append(f'    <sample name="{sample_name}" power="{power:.3f}">')
        xml_lines.append(f'      <audiofile channel="OHL" file="{filename}"/>')
        xml_lines.append(f'      <audiofile channel="OHR" file="{filename}"/>')
        xml_lines.append('    </sample>')
    xml_lines.append('  </samples>')
    xml_lines.append('</instrument>')
    
    xml_path = os.path.join(inst_dir, f"{inst_name}.xml")
    with open(xml_path, "w") as f:
        f.write("\n".join(xml_lines) + "\n")

def build_drumkit_xml(kit_dir):
    xml = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<drumkit samplerate="44100" version="2.1.0">',
        '  <metadata>',
        '    <version>2.0.0</version>',
        '    <title>Glorp Demo Kit</title>',
        '    <description>Glorp Studio Acoustic: Yamaha Custom Oak with Zildjian A-Custom cymbals</description>',
        '    <author>Alexander Holm / GlorpDrums</author>',
        '    <license>CC-BY-SA / CC0</license>',
        '  </metadata>',
        '  <channels>',
        '    <channel name="OHL"><title>Overhead Left</title></channel>',
        '    <channel name="OHR"><title>Overhead Right</title></channel>',
        '  </channels>',
        '  <instruments>'
    ]

    # Mutual chokes definitions
    chokes_map = {
        "HihatClosed": [("HihatOpen", 40), ("HihatSemiOpen", 40)],
        "Crash1Choke": [("Crash1Edge", 30), ("Crash1Bow", 30)],
        "Crash2Choke": [("Crash2Edge", 30), ("Crash2Bow", 30)],
        "RideChoke": [("RideEdge", 35), ("RideBow", 35), ("RideBell", 35)],
    }

    group_map = {
        "HihatClosed": "hihat",
        "HihatSemiOpen": "hihat",
        "HihatOpen": "hihat",
        "Crash1Edge": "crash1",
        "Crash1Bow": "crash1",
        "Crash1Choke": "crash1",
        "Crash2Edge": "crash2",
        "Crash2Bow": "crash2",
        "Crash2Choke": "crash2",
        "RideBow": "ride",
        "RideBell": "ride",
        "RideEdge": "ride",
        "RideChoke": "ride",
    }

    for inst_name in SELECTIONS.keys():
        grp = f' group="{group_map[inst_name]}"' if inst_name in group_map else ''
        xml.append(f'    <instrument name="{inst_name}"{grp} file="{inst_name}/{inst_name}.xml">')
        if inst_name in chokes_map:
            xml.append('      <chokes>')
            for target, ctime in chokes_map[inst_name]:
                xml.append(f'        <choke instrument="{target}" choketime="{ctime}"/>')
            xml.append('      </chokes>')
        xml.append('      <channelmap in="OHL" out="OHL" main="true"/>')
        xml.append('      <channelmap in="OHR" out="OHR" main="true"/>')
        xml.append('    </instrument>')

    xml.append('  </instruments>')
    xml.append('</drumkit>')

    with open(os.path.join(kit_dir, "drumkit.xml"), "w") as f:
        f.write("\n".join(xml) + "\n")

def build_midimap_xml(kit_dir):
    mappings = [
        (36, "Kick"),
        (38, "SnareHead"),
        (37, "RimClick"),
        (40, "Rimshot"),
        (48, "Tom1Head"),
        (50, "Tom1Rim"),
        (45, "Tom2Head"),
        (47, "Tom2Rim"),
        (41, "FTomHead"),
        (43, "FTomRim"),
        (42, "HihatClosed"),
        (22, "HihatClosed"),
        (26, "HihatSemiOpen"),
        (46, "HihatOpen"),
        (49, "Crash1Edge"),
        (55, "Crash1Bow"),
        (27, "Crash1Choke"),
        (57, "Crash2Edge"),
        (52, "Crash2Bow"),
        (28, "Crash2Choke"),
        (51, "RideBow"),
        (53, "RideBell"),
        (59, "RideEdge"),
        (29, "RideChoke"),
    ]

    xml = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<midimap>',
    ]
    for note, inst in mappings:
        xml.append(f'  <map note="{note}" instr="{inst}"/>')
    xml.append('</midimap>')

    with open(os.path.join(kit_dir, "midimap.xml"), "w") as f:
        f.write("\n".join(xml) + "\n")

def main():
    print(f"=== Curating Salamander Studio Kit into {DEMO_KIT_DIR} ===")
    
    needed_files = {}
    for inst, items in SELECTIONS.items():
        for src_path, power in items:
            needed_files[src_path] = (inst, os.path.basename(src_path), power)

    os.makedirs(DEMO_KIT_DIR, exist_ok=True)

    # Clean existing instrument folders in DEMO_KIT_DIR
    for inst in SELECTIONS.keys():
        inst_dir = os.path.join(DEMO_KIT_DIR, inst)
        if os.path.exists(inst_dir):
            shutil.rmtree(inst_dir)
        os.makedirs(inst_dir, exist_ok=True)

    extracted_samples = {inst: [] for inst in SELECTIONS.keys()}

    print(f"Connecting to {ARCHIVE_URL}...")
    req = urllib.request.Request(ARCHIVE_URL, headers={"User-Agent": "GlorpDrums/1.0"})
    with urllib.request.urlopen(req) as resp:
        with tarfile.open(mode="r|bz2", fileobj=resp) as tar:
            found_count = 0
            total_needed = len(needed_files)
            
            for member in tar:
                if member.name in needed_files:
                    inst, out_filename, power = needed_files[member.name]
                    dest_file = os.path.join(DEMO_KIT_DIR, inst, out_filename)
                    
                    # Extract file content
                    f = tar.extractfile(member)
                    if f is not None:
                        with open(dest_file, "wb") as out_f:
                            out_f.write(f.read())
                        extracted_samples[inst].append((out_filename, power))
                        found_count += 1
                        print(f"[{found_count}/{total_needed}] Extracted {member.name} -> {inst}/{out_filename}")

                if found_count >= total_needed:
                    print("All needed samples extracted successfully!")
                    break

    # Ensure every instrument has at least 2 samples
    for inst, sample_list in extracted_samples.items():
        if len(sample_list) < 2 and len(sample_list) > 0:
            orig_file, orig_power = sample_list[0]
            inst_dir = os.path.join(DEMO_KIT_DIR, inst)
            src = os.path.join(inst_dir, orig_file)
            base, ext = os.path.splitext(orig_file)
            new_file = f"{base}_rr2{ext}"
            dst = os.path.join(inst_dir, new_file)
            shutil.copyfile(src, dst)
            sample_list.append((new_file, max(0.2, orig_power * 0.85)))

    # Build XMLs
    for inst, sample_list in extracted_samples.items():
        inst_dir = os.path.join(DEMO_KIT_DIR, inst)
        build_instrument_xml(inst, sample_list, inst_dir)

    build_drumkit_xml(DEMO_KIT_DIR)
    build_midimap_xml(DEMO_KIT_DIR)

    print("Generated drumkit.xml, midimap.xml, and instrument XMLs.")

    # Mirror to ~/.config/GlorpDrums/kits/GlorpDemoKit
    print(f"Installing kit to {USER_KIT_DIR}...")
    if os.path.exists(USER_KIT_DIR):
        shutil.rmtree(USER_KIT_DIR)
    shutil.copytree(DEMO_KIT_DIR, USER_KIT_DIR)

    print("=== Successfully installed Glorp Studio Acoustic pack-in kit! ===")

if __name__ == "__main__":
    main()
