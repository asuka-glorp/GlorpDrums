#!/usr/bin/env python3
"""
convert_to_glorpkit.py - Convert DrumGizmo drumkits into native GlorpKit (.glorpkit) format.

Produces a structured GlorpKit ZIP archive with:
  ├── kit.xml                          # Kit metadata + mic definitions + graphics
  ├── instruments/
  │   ├── <piece>/
  │   │   ├── instrument.xml           # <glorp-instrument>: zones, output-bus, chokes
  │   │   ├── <zone>/
  │   │   │   ├── zone.xml             # <glorp-zone>: samples, mics, power, round-robin
  │   │   │   └── samples/*.wav        # Uncompressed 24-bit audio slices
  └── presets/
      └── factory-mixer.xml            # Default per-mic, per-zone levels

Defaults to installing packages directly into /home/$USER/.glorpdrums/kits/
Stored uncompressed (ZIP_STORED) for zero-copy OS memory mapping in GlorpDrums.
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


# ── Canonical Mic Mapping ─────────────────────────────────────────────────────

def map_channel_to_mic(channel_name: str) -> str:
    """Map a DrumGizmo channel name to a canonical GlorpKit mic name."""
    ch = channel_name.lower().replace("-", "").replace("_", "")

    # Direct / Close mics
    if any(k in ch for k in ("kdruminside", "kdrumoutside", "kickin", "kickout",
                             "snaretop", "snarebot", "snaredir", "direct", "close", "closemic")):
        return "close"

    # Overheads
    if any(k in ch for k in ("ohleft", "ohl", "overheadleft")):
        return "oh-left"
    if any(k in ch for k in ("ohright", "ohr", "overheadright")):
        return "oh-right"
    if any(k in ch for k in ("ohcenter", "ohc", "overheadc", "overhead", "oh")):
        return "oh-center"

    # Ambience / Room
    if any(k in ch for k in ("ambleft", "ambl", "ambienceleft")):
        return "amb-left"
    if any(k in ch for k in ("ambright", "ambr", "ambienceright")):
        return "amb-right"
    if any(k in ch for k in ("room", "amb", "bleed")):
        return "room"

    # Default fallback
    return "close"


# ── Logical Piece & Zone Grouping ─────────────────────────────────────────────

PIECE_RULES = [
    # (Regex pattern, Piece Name, Piece ID, Graphic, Bus, ChokeGroup)
    (r'^(?:kick|kdrum|bassdrum|bdrum)', 'Kick', 'kick', 'kick', 0, 'none'),
    (r'^(?:snare|rimclick|rimshot|crossstick|sidestick)', 'Snare', 'snare', 'snare', 1, 'none'),
    (r'^(?:hihat|hh)', 'Hihat', 'hihat', 'hihat', 2, 'hihat'),
    (r'^(?:tom\s*2(?![\d])|tom2(?![\d])|rack\s*2|rack2|midtom|tom\s*13|tom13)', 'Tom 2', 'tom2', 'tom2', 3, 'none'),
    (r'^(?:tom\s*3(?![\d])|tom3(?![\d])|rack\s*3|rack3|lowtom|tom\s*14|tom14)', 'Tom 3', 'tom3', 'ftom', 3, 'none'),
    (r'^(?:tom\s*16|tom16|tom\s*18|tom18|ftom|floor|lowfloortom)', 'Floor Tom', 'ftom', 'ftom', 3, 'none'),
    (r'^(?:tom\s*1(?![\d])|tom1(?![\d])|rack\s*1|rack1|hitom|tom\s*10|tom10|tom\s*12|tom12)', 'Tom 1', 'tom1', 'tom1', 3, 'none'),
    (r'^(?:ride\s*2|ride2|avedis)', 'Ride 2', 'ride2', 'ride', 5, 'none'),
    (r'^(?:ride)', 'Ride', 'ride', 'ride', 5, 'none'),
    (r'^(?:crash\s*1|crash1|crashl|crash_l|sabian\s*16)', 'Crash 1', 'crash1', 'crash1', 4, 'cymbals'),
    (r'^(?:crash\s*2|crash2|crashr|crash_r|istanbul|xist|efx)', 'Crash 2', 'crash2', 'crash2', 4, 'cymbals'),
    (r'^(?:crash\s*3|crash3)', 'Crash 3', 'crash3', 'crash2', 4, 'cymbals'),
    (r'^(?:crash)', 'Crash 1', 'crash1', 'crash1', 4, 'cymbals'),
    (r'^(?:cowbell)', 'Cowbell', 'cowbell', 'cowbell', 6, 'none'),
]

ZONE_RULES = [
    # Graduated Hi-hat zones (must come before generic open/closed/edge)
    (r'open\s*1\s*bow|op\s*1\s*bow', 'open1-bow'),
    (r'open\s*1\s*(?:edge|edg)|op\s*1\s*(?:edge|edg)', 'open1-edge'),
    (r'open\s*2\s*bow|op\s*2\s*bow', 'open2-bow'),
    (r'open\s*2\s*(?:edge|edg)|op\s*2\s*(?:edge|edg)', 'open2-edge'),
    (r'open\s*3\s*bow|op\s*3\s*bow', 'open3-bow'),
    (r'open\s*3\s*(?:edge|edg)|op\s*3\s*(?:edge|edg)', 'open3-edge'),
    (r'open\s*4\s*bow|op\s*4\s*bow', 'open4-bow'),
    (r'open\s*4\s*(?:edge|edg)|op\s*4\s*(?:edge|edg)', 'open4-edge'),
    (r'open\s*(?:edge|edg)|op\s*(?:edge|edg)', 'open-edge'),
    (r'closed\s*(?:edge|edg)|clo\s*(?:edge|edg)', 'closed-edge'),
    (r'full\s*op|open', 'open'),
    (r'semi\s*open|semi', 'semiopen'),
    (r'closed|clo\b|close\b', 'closed'),
    (r'splash|footsplash|spl\b', 'splash'),
    (r'slammed|slam', 'slammed'),
    (r'pedal|foot|ped\b', 'pedal'),

    # Snare & Tom specific zones
    (r'rimshot|shot', 'rimshot'),
    (r'rimclick|sidestick|crossstick|cross|click', 'rimclick'),
    (r'muted|muff', 'muted'),
    (r'fat', 'fat'),
    (r'rim', 'rim'),

    # Cymbal specific zones
    (r'bell', 'bell'),
    (r'edge|edg', 'edge'),
    (r'bow', 'bow'),
    (r'choke', 'choke'),

    # Percussion specific zones
    (r'hi\b|high', 'hi'),

    # Generic head
    (r'head|main|center|ctr', 'head'),
]

DEFAULT_NOTES = {
    ('kick', 'head'): 36,
    ('snare', 'head'): 38,
    ('snare', 'rimclick'): 37,
    ('snare', 'rimshot'): 40,
    ('snare', 'edge'): 39,
    ('snare', 'rim'): 37,
    ('snare', 'muted'): 33,
    ('snare', 'fat'): 29,
    ('hihat', 'closed'): 42,
    ('hihat', 'closed-edge'): 22,
    ('hihat', 'open1-bow'): 23,
    ('hihat', 'open1-edge'): 24,
    ('hihat', 'open2-bow'): 31,
    ('hihat', 'open2-edge'): 32,
    ('hihat', 'open3-bow'): 34,
    ('hihat', 'open3-edge'): 27,
    ('hihat', 'open4-bow'): 30,
    ('hihat', 'open4-edge'): 25,
    ('hihat', 'open'): 46,
    ('hihat', 'open-edge'): 26,
    ('hihat', 'pedal'): 44,
    ('hihat', 'splash'): 63,
    ('hihat', 'slammed'): 61,
    ('hihat', 'semiopen'): 80,
    ('tom1', 'head'): 48,
    ('tom1', 'rim'): 50,
    ('tom2', 'head'): 47,
    ('tom2', 'rim'): 60,
    ('tom3', 'head'): 45,
    ('tom3', 'rim'): 62,
    ('ftom', 'head'): 43,
    ('ftom', 'rim'): 58,
    ('ftom2', 'head'): 41,
    ('ftom2', 'rim'): 65,
    ('ride', 'bow'): 51,
    ('ride', 'bell'): 53,
    ('ride', 'edge'): 59,
    ('ride', 'choke'): 60,
    ('ride2', 'bow'): 28,
    ('ride2', 'bell'): 29,
    ('ride2', 'edge'): 60,
    ('crash1', 'edge'): 55,
    ('crash1', 'bow'): 49,
    ('crash1', 'choke'): 54,
    ('crash2', 'edge'): 52,
    ('crash2', 'bow'): 57,
    ('crash2', 'choke'): 58,
    ('crash3', 'bow'): 56,
    ('crash3', 'edge'): 54,
    ('cowbell', 'head'): 31,
    ('cowbell', 'muted'): 32,
    ('cowbell', 'hi'): 33,
}


def classify_instrument(inst_name: str) -> tuple[str, str, str, str, int, str]:
    """
    Returns (piece_name, piece_id, zone_name, graphic, output_bus, choke_group).
    """
    name_clean = inst_name.lower().replace("-", "").replace("_", "")

    matched_piece = None
    for pattern, p_name, p_id, p_graphic, p_bus, p_choke in PIECE_RULES:
        if re.search(pattern, name_clean):
            matched_piece = (p_name, p_id, p_graphic, p_bus, p_choke)
            break

    if matched_piece is None:
        p_name = inst_name.title()
        p_id = re.sub(r'[^a-z0-9]', '', inst_name.lower()) or "inst"
        matched_piece = (p_name, p_id, p_id, 0, 'none')

    p_name, p_id, p_graphic, p_bus, p_choke = matched_piece

    # Zone classification
    z_name = "head"
    for pattern, z in ZONE_RULES:
        if re.search(pattern, name_clean):
            z_name = z
            break

    # Graphic refinement
    z_graphic = p_graphic
    if p_id == 'snare' and z_name in ('rim', 'rimshot', 'rimclick'):
        z_graphic = 'snare-rim'
    elif p_id == 'ride' and z_name == 'bell':
        z_graphic = 'ride-bell'

    return p_name, p_id, z_name, z_graphic, p_bus, p_choke


def escape_xml(s: str) -> str:
    return (str(s).replace('&', '&amp;')
                  .replace('<', '&lt;')
                  .replace('>', '&gt;')
                  .replace('"', '&quot;')
                  .replace("'", '&apos;'))


# ── DrumGizmo Parser ─────────────────────────────────────────────────────────

def parse_midimap(kit_dir: Path) -> dict[str, int]:
    """Reads midimap.xml if present, returning {instrument_name.lower(): midi_note}."""
    note_map = {}
    for candidate in ("midimap.xml", "Midimap.xml"):
        p = kit_dir / candidate
        if p.is_file():
            try:
                tree = ET.parse(p)
                for m in tree.getroot().findall("map"):
                    note_attr = m.get("note")
                    instr_attr = m.get("instr")
                    if note_attr and instr_attr:
                        note_map[instr_attr.strip().lower()] = int(note_attr)
            except Exception as e:
                print(f"Warning parsing {p}: {e}", file=sys.stderr)
            break
    return note_map


def parse_drumkit(xml_path: Path) -> dict:
    kit_dir = xml_path.parent
    tree = ET.parse(xml_path)
    root = tree.getroot()

    title = root.get("name", "")
    meta = root.find("metadata")
    author = "Unknown"
    license_str = "Custom"
    description = ""
    if meta is not None:
        if meta.find("title") is not None and meta.find("title").text:
            title = meta.find("title").text.strip()
        if meta.find("author") is not None and meta.find("author").text:
            author = meta.find("author").text.strip()
        if meta.find("license") is not None and meta.find("license").text:
            license_str = meta.find("license").text.strip()
        if meta.find("description") is not None and meta.find("description").text:
            description = meta.find("description").text.strip()

    if not title:
        title = kit_dir.name

    samplerate = int(root.get("samplerate", "48000"))

    # Midimap
    note_map = parse_midimap(kit_dir)

    # Instruments
    raw_instruments = []
    inst_node = root.find("instruments")
    if inst_node is not None:
        for i_elem in inst_node.findall("instrument"):
            name = i_elem.get("name", "")
            file_rel = i_elem.get("file", "")
            if not name or not file_rel:
                continue

            inst_xml_path = (kit_dir / file_rel.replace("\\", "/")).resolve()
            if not inst_xml_path.is_file():
                continue

            # Parse instrument samples
            itree = ET.parse(inst_xml_path)
            iroot = itree.getroot()
            inst_dir = inst_xml_path.parent

            samples = []
            samples_elem = iroot.find("samples")
            if samples_elem is not None:
                for s_elem in samples_elem.findall("sample"):
                    s_name = s_elem.get("name", "")
                    s_power = float(s_elem.get("power", "1.0"))
                    audio_files = []
                    for af in s_elem.findall("audiofile"):
                        ch = af.get("channel", "")
                        wav_rel = af.get("file", "")
                        f_ch = int(af.get("filechannel", "1"))
                        if not ch or not wav_rel:
                            continue
                        wav_path = (inst_dir / wav_rel.replace("\\", "/")).resolve()
                        if not wav_path.is_file():
                            wav_path = (kit_dir / wav_rel.replace("\\", "/")).resolve()

                        if wav_path.is_file():
                            audio_files.append({
                                'channel': ch,
                                'file': wav_rel,
                                'abs_path': wav_path,
                                'filechannel': f_ch,
                            })
                    if audio_files:
                        samples.append({
                            'name': s_name,
                            'power': s_power,
                            'audio_files': audio_files,
                        })

            # Chokes
            chokes = []
            chokes_elem = i_elem.find("chokes")
            if chokes_elem is not None:
                for c in chokes_elem.findall("choke"):
                    t_inst = c.get("instrument", "")
                    t_time = float(c.get("choketime", "40"))
                    if t_inst:
                        chokes.append({'instrument': t_inst, 'time': t_time})

            raw_instruments.append({
                'name': name,
                'xml_path': inst_xml_path,
                'samples': samples,
                'chokes': chokes,
            })

    return {
        'title': title,
        'author': author,
        'license': license_str,
        'description': description,
        'samplerate': samplerate,
        'raw_instruments': raw_instruments,
        'note_map': note_map,
        'kit_dir': kit_dir,
    }


# ── Transform to GlorpKit Hierarchy ──────────────────────────────────────────

def build_glorpkit_tree(kit_data: dict) -> dict:
    """
    Transforms flat DrumGizmo instruments into the hierarchical GlorpKit model.
    """
    pieces = {}  # piece_id -> piece_dict
    mics_discovered = set()

    note_map = kit_data['note_map']

    for raw in kit_data['raw_instruments']:
        raw_name = raw['name']
        p_name, p_id, z_name, z_graphic, p_bus, p_choke = classify_instrument(raw_name)

        if p_id not in pieces:
            pieces[p_id] = {
                'name': p_name,
                'id': p_id,
                'graphic': z_graphic,
                'output_bus': p_bus,
                'choke_group': p_choke,
                'zones': {},
            }

        piece = pieces[p_id]

        # Determine note
        note = note_map.get(raw_name.lower())
        if note is None:
            note = DEFAULT_NOTES.get((p_id, z_name), -1)

        # Process zone samples
        zone_samples = []
        for s in raw['samples']:
            power = s['power']
            hit_name = s['name']
            for af in s['audio_files']:
                mic = map_channel_to_mic(af['channel'])
                mics_discovered.add(mic)
                zone_samples.append({
                    'name': hit_name,
                    'file': af['abs_path'],
                    'filename': af['abs_path'].name,
                    'mic': mic,
                    'channel': af['filechannel'],
                    'power': power,
                })

        # Ensure unique zone name within piece so articulations never overwrite each other
        zone_key = z_name
        count = 2
        while zone_key in piece['zones']:
            zone_key = f"{z_name}_{count}"
            count += 1

        piece['zones'][zone_key] = {
            'name': zone_key,
            'graphic': z_graphic,
            'note': note,
            'samples': zone_samples,
            'chokes': raw['chokes'],
            'round_robin': len(raw['samples']),
        }

    # Ensure canonical mics list
    mic_titles = {
        'close': 'Close Mic',
        'room': 'Room / Ambience',
        'oh-left': 'Overhead Left',
        'oh-right': 'Overhead Right',
        'oh-center': 'Overhead Center',
        'amb-left': 'Ambience Left',
        'amb-right': 'Ambience Right',
    }

    mics = []
    for m in ('close', 'oh-left', 'oh-right', 'oh-center', 'room', 'amb-left', 'amb-right'):
        if m in mics_discovered:
            mics.append({
                'name': m,
                'title': mic_titles.get(m, m.title()),
                'default': '1.0' if m == 'close' else ('0.8' if 'oh' in m else '0.6'),
            })

    if not mics:
        mics.append({'name': 'close', 'title': 'Direct / Master', 'default': '1.0'})

    return {
        'title': kit_data['title'],
        'author': kit_data['author'],
        'license': kit_data['license'],
        'description': kit_data['description'],
        'samplerate': kit_data['samplerate'],
        'mics': mics,
        'pieces': list(pieces.values()),
    }


# ── XML Generators ───────────────────────────────────────────────────────────

def generate_kit_xml(tree: dict) -> str:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>']
    lines.append(f'<glorpkit version="1.0" name="{escape_xml(tree["title"])}" '
                 f'author="{escape_xml(tree["author"])}" '
                 f'license="{escape_xml(tree["license"])}" '
                 f'samplerate="{tree["samplerate"]}">' )

    if tree['description']:
        lines.append(f'  <description>{escape_xml(tree["description"])}</description>')

    lines.append('  <mics>')
    for m in tree['mics']:
        lines.append(f'    <mic name="{escape_xml(m["name"])}" title="{escape_xml(m["title"])}" default="{m["default"]}"/>')
    lines.append('  </mics>')

    lines.append('  <graphics>')
    seen_graphics = set()
    for p in tree['pieces']:
        g = p['graphic']
        if g and g not in seen_graphics:
            seen_graphics.add(g)
            lines.append(f'    <graphic name="{escape_xml(g)}" src="assets/{escape_xml(g)}.png"/>')
    lines.append('  </graphics>')

    lines.append('</glorpkit>')
    return '\n'.join(lines)


def generate_instrument_xml(piece: dict) -> str:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>']
    lines.append(f'<glorp-instrument name="{escape_xml(piece["name"])}" '
                 f'graphic="{escape_xml(piece["graphic"])}" '
                 f'output-bus="{piece["output_bus"]}" '
                 f'choke-group="{escape_xml(piece["choke_group"])}">')

    for z in piece['zones'].values():
        note_str = f' note="{z["note"]}"' if z["note"] >= 0 else ''
        lines.append(f'  <zone name="{escape_xml(z["name"])}" graphic="{escape_xml(z["graphic"])}"{note_str}/>')

    lines.append('</glorp-instrument>')
    return '\n'.join(lines)


def generate_zone_xml(zone: dict, piece: dict) -> str:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>']
    rr = zone['round_robin'] if zone['round_robin'] > 0 else 1
    lines.append(f'<glorp-zone name="{escape_xml(zone["name"])}" power="1.0" round-robin="{rr}">')

    for s in zone['samples']:
        file_name = s['filename']
        name_attr = f' name="{escape_xml(s["name"])}"' if s.get("name") else ''
        ch_attr = f' channel="{s["channel"]}"' if s.get("channel", 1) > 1 else ''
        lines.append(f'  <sample{name_attr} file="samples/{escape_xml(file_name)}" mic="{escape_xml(s["mic"])}" '
                     f'power="{s["power"]:.4f}"{ch_attr}/>')

    for c in zone['chokes']:
        # If choke targets another instrument
        lines.append(f'  <choke target="{escape_xml(c["instrument"])}" time="{c["time"]:.0f}"/>')

    lines.append('</glorp-zone>')
    return '\n'.join(lines)


def generate_factory_mixer_xml(tree: dict) -> str:
    lines = ['<?xml version="1.0" encoding="UTF-8"?>']
    lines.append('<glorp-mixer>')

    for m in tree['mics']:
        lines.append(f'  <mic name="{escape_xml(m["name"])}" level="{m["default"]}"/>')

    for p in tree['pieces']:
        for z in p['zones'].values():
            lines.append(f'  <instrument name="{escape_xml(p["name"])}" zone="{escape_xml(z["name"])}" '
                         f'level="1.0" pan="0.0"/>')

    lines.append('</glorp-mixer>')
    return '\n'.join(lines)


# ── Packaging ────────────────────────────────────────────────────────────────

def sanitize_filename(name: str) -> str:
    clean = re.sub(r'[^a-zA-Z0-9_\-\. ]', '_', name).strip()
    return clean if clean else "drumkit"


def convert_kit(input_path: Path, output_path: Path | None, kit_name: str | None, dry_run: bool = False) -> bool:
    input_p = input_path.resolve()
    if input_p.is_file():
        xml_path = input_p
    elif input_p.is_dir():
        for candidate in ("drumkit.xml", "kit.xml"):
            if (input_p / candidate).is_file():
                xml_path = input_p / candidate
                break
        else:
            print(f"Error: No drumkit.xml or kit.xml found in '{input_p}'.", file=sys.stderr)
            return False
    else:
        print(f"Error: Path '{input_p}' does not exist.", file=sys.stderr)
        return False

    print("=" * 64)
    print("GlorpKit Native Converter (DrumGizmo → GlorpKit)")
    print("=" * 64)
    print(f"Reading: {xml_path}")

    kit_data = parse_drumkit(xml_path)
    if kit_name:
        kit_data['title'] = kit_name

    tree = build_glorpkit_tree(kit_data)

    total_samples = sum(len(z['samples']) for p in tree['pieces'] for z in p['zones'].values())
    total_zones = sum(len(p['zones']) for p in tree['pieces'])

    print(f"Kit Title:       {tree['title']}")
    print(f"Author:          {tree['author']}")
    print(f"Sample Rate:     {tree['samplerate']} Hz")
    print(f"Canonical Mics:  {', '.join(m['name'] for m in tree['mics'])}")
    print(f"Pieces ({len(tree['pieces'])}):    {', '.join(p['name'] for p in tree['pieces'])}")
    print(f"Total Zones:     {total_zones}")
    print(f"Total Samples:   {total_samples}")

    default_dir = Path.home() / ".glorpdrums" / "kits"
    safe_name = sanitize_filename(tree['title'])

    if output_path is None:
        target_file = default_dir / f"{safe_name}.glorpkit"
    else:
        target_p = output_path.resolve()
        if target_p.is_dir():
            target_file = target_p / f"{safe_name}.glorpkit"
        else:
            target_file = target_p if target_p.suffix.lower() == ".glorpkit" else target_p.with_suffix(".glorpkit")

    print(f"Target Output:   {target_file}")
    print("-" * 64)

    if dry_run:
        print("Dry run complete. No archive written.")
        return True

    target_file.parent.mkdir(parents=True, exist_ok=True)

    print(f"Building native GlorpKit container: {target_file} ...")

    # Generate XML strings
    kit_xml_content = generate_kit_xml(tree)
    mixer_xml_content = generate_factory_mixer_xml(tree)

    # Collect sample files to write
    files_to_write = []
    # (src_file, arc_name)

    for p in tree['pieces']:
        p_id = p['id']
        for z in p['zones'].values():
            z_name = z['name']
            zone_arc_dir = f"instruments/{p_id}/{z_name}"

            seen_wavs = set()
            for s in z['samples']:
                src_wav = s['file']
                wav_name = s['filename']
                if wav_name not in seen_wavs:
                    seen_wavs.add(wav_name)
                    arc_path = f"{zone_arc_dir}/samples/{wav_name}"
                    files_to_write.append((src_wav, arc_path))

    total_bytes = sum(f.stat().st_size for f, _ in files_to_write)
    print(f"Packaging {len(files_to_write)} audio files ({total_bytes / (1024 * 1024):.1f} MB)...")

    # Write uncompressed ZIP (ZIP_STORED) for zero-copy memory mapping
    with zipfile.ZipFile(target_file, "w", compression=zipfile.ZIP_STORED) as zf:
        # kit.xml
        zf.writestr("kit.xml", kit_xml_content)

        # factory-mixer.xml
        zf.writestr("presets/factory-mixer.xml", mixer_xml_content)

        # Instruments & zones
        for p in tree['pieces']:
            p_id = p['id']
            inst_xml = generate_instrument_xml(p)
            zf.writestr(f"instruments/{p_id}/instrument.xml", inst_xml)

            for z in p['zones'].values():
                z_name = z['name']
                zone_xml = generate_zone_xml(z, p)
                zf.writestr(f"instruments/{p_id}/{z_name}/zone.xml", zone_xml)

        # Write audio files
        for idx, (src_file, arc_name) in enumerate(files_to_write, 1):
            zf.write(src_file, arc_name)
            if idx % 50 == 0 or idx == len(files_to_write):
                pct = (idx / len(files_to_write)) * 100.0
                sys.stdout.write(f"\r  Progress: {idx}/{len(files_to_write)} samples ({pct:.0f}%)")
                sys.stdout.flush()

    print("\n\nPackage successfully built in native GlorpKit format!")
    print(f"Location: {target_file}")
    print(f"Size:     {target_file.stat().st_size / (1024 * 1024):.1f} MB")
    print(f"\nGlorpDrums will load this as native [PKG] {tree['title']}")
    return True


def main():
    parser = argparse.ArgumentParser(description="Convert DrumGizmo drumkits into native GlorpKit format.")
    parser.add_argument("input", help="Path to drumkit.xml or DrumGizmo kit directory")
    parser.add_argument("-o", "--output", help="Optional output path or directory (defaults to ~/.glorpdrums/kits/)")
    parser.add_argument("--name", help="Override kit display title")
    parser.add_argument("--dry-run", action="store_true", help="Validate and inspect without creating archive")

    args = parser.parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output) if args.output else None

    success = convert_kit(input_path, output_path, args.name, args.dry_run)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
