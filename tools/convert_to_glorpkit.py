#!/usr/bin/env python3
"""
convert_to_glorpkit.py - Convert DrumGizmo drumkits to GlorpKit format.

Reads a DrumGizmo drumkit (drumkit.xml + instrument XMLs) and produces a
.glorpkit ZIP archive with the directory structure expected by GlorpDrums.

Usage:
    python3 tools/convert_to_glorpkit.py <input_kit.xml> <output.glorpkit> [--name NAME]
"""

import argparse
import os
import sys
import xml.etree.ElementTree as ET
import zipfile
import re
import json


# ── DrumGizmo → GlorpKit mic mapping ─────────────────────────────────────────

def map_channel_to_mic(channel_name):
    """Map a DrumGizmo channel name to a GlorpKit mic name."""
    ch = channel_name.lower()

    # Direct/close mics
    if ch in ("direct", "direct1", "direct2", "direct3", "close", "kdruminside",
              "snaretop", "snarebottom", "kickclose", "closemic"):
        return "close"

    # Overhead left/right
    if ch in ("ohleft", "ohright", "ohl", "ohr", "overheadleft", "overheadright",
              "oh-l", "oh-r"):
        if "left" in ch or ch in ("ohl", "oh-l"):
            return "oh-left"
        if "right" in ch or ch in ("ohr", "oh-r"):
            return "oh-right"
        return "oh-left"  # default for ambiguous

    # Overhead center
    if ch in ("ohcenter", "oh-c", "ohc"):
        return "oh-center"

    # Room/ambience
    if ch in ("room", "ambleft", "ambright", "ambl", "ambr", "ambienceleft",
              "ambenceright", "amb"):
        if "left" in ch or ch in ("ambl",):
            return "amb-left"
        if "right" in ch or ch in ("ambr",):
            return "amb-right"
        return "room"

    # Generic bleed
    if ch in ("bleed", "bleed1", "bleed2"):
        return "room"

    # Fallback: use the channel name as-is
    return ch


def sanitize_name(name):
    """Convert a name to a safe directory/file name."""
    name = re.sub(r'[^a-zA-Z0-9_-]', '', name)
    return name.lower() if name else "unknown"


def title_case(name):
    """Convert a name to Title Case for display."""
    return name.replace('-', ' ').replace('_', ' ').title()


# ── DrumGizmo XML parsing ───────────────────────────────────────────────────

def parse_drumkit_xml(xml_path):
    """Parse a DrumGizmo drumkit.xml and return structured data."""
    tree = ET.parse(xml_path)
    root = tree.getroot()

    kit_dir = os.path.dirname(os.path.abspath(xml_path))

    # Kit metadata
    kit_name = root.get('name', os.path.basename(kit_dir))
    kit_desc = root.get('description', '')
    samplerate = int(root.get('samplerate', '48000'))

    # Channels
    channels = []
    channels_node = root.find('channels')
    if channels_node is not None:
        for ch in channels_node.findall('channel'):
            ch_name = ch.get('name', '')
            ch_title_node = ch.find('title')
            ch_title = ch_title_node.text if ch_title_node is not None else ch_name
            channels.append({'name': ch_name, 'title': ch_title or ch_name})

    # Instruments
    instruments = []
    instruments_node = root.find('instruments')
    if instruments_node is None:
        print(f"ERROR: No <instruments> node in {xml_path}", file=sys.stderr)
        return None

    for inst_node in instruments_node.findall('instrument'):
        inst_name = inst_node.get('name', '')
        inst_file = inst_node.get('file', '')

        if not inst_name or not inst_file:
            continue

        inst_xml_path = os.path.join(kit_dir, inst_file)
        if not os.path.exists(inst_xml_path):
            print(f"WARNING: Instrument XML not found: {inst_xml_path}", file=sys.stderr)
            continue

        inst_data = parse_instrument_xml(inst_xml_path, kit_dir)
        if inst_data is None:
            continue

        inst_data['name'] = inst_name
        instruments.append(inst_data)

    return {
        'name': kit_name,
        'description': kit_desc,
        'samplerate': samplerate,
        'channels': channels,
        'instruments': instruments,
        'kit_dir': kit_dir,
    }


def parse_instrument_xml(xml_path, kit_dir):
    """Parse a DrumGizmo instrument XML file."""
    tree = ET.parse(xml_path)
    root = tree.getroot()

    inst_dir = os.path.dirname(os.path.abspath(xml_path))

    # Display name from metadata
    display_name = root.get('name', os.path.basename(inst_dir))
    meta = root.find('metadata')
    if meta is not None:
        title_node = meta.find('title')
        if title_node is not None and title_node.text:
            display_name = title_node.text

    # Channelmaps
    channelmaps = []
    for cm in root.findall('channelmap'):
        channelmaps.append({
            'in': cm.get('in', ''),
            'out': cm.get('out', ''),
            'main': cm.get('main', 'false').lower() == 'true',
        })

    # Chokes
    chokes = []
    chokes_node = root.find('chokes')
    if chokes_node is not None:
        for ch in chokes_node.findall('choke'):
            chokes.append({
                'instrument': ch.get('instrument', ''),
                'time': float(ch.get('choketime', '100')),
            })

    # Samples
    samples = []
    samples_node = root.find('samples')
    if samples_node is None:
        return None

    for sample_node in samples_node.findall('sample'):
        sample_name = sample_node.get('name', '')
        power = float(sample_node.get('power', '1.0'))

        audio_files = []
        for af in sample_node.findall('audiofile'):
            channel = af.get('channel', '')
            file = af.get('file', '')
            file_channel = int(af.get('filechannel', '1'))

            if not channel or not file:
                continue

            # Resolve file path
            file_path = os.path.join(inst_dir, file)
            if not os.path.exists(file_path):
                print(f"WARNING: Sample file not found: {file_path}", file=sys.stderr)
                continue

            audio_files.append({
                'channel': channel,
                'file': file,
                'file_channel': file_channel,
                'abs_path': os.path.abspath(file_path),
            })

        if not audio_files:
            continue

        samples.append({
            'name': sample_name,
            'power': power,
            'audio_files': audio_files,
        })

    if not samples:
        return None

    return {
        'name': root.get('name', ''),
        'display_name': display_name,
        'channelmaps': channelmaps,
        'chokes': chokes,
        'samples': samples,
    }


# ── GlorpKit generation ─────────────────────────────────────────────────────

def generate_kit_xml(kit_data):
    """Generate kit.xml content."""
    lines = []
    lines.append('<?xml version="1.0" encoding="UTF-8"?>')
    lines.append(f'<glorpkit version="1.0" name="{escape_xml(kit_data["name"])}" samplerate="{kit_data["samplerate"]}">')

    # Mic definitions
    mics = {}
    for inst in kit_data['instruments']:
        for sample in inst['samples']:
            for af in sample['audio_files']:
                mic = map_channel_to_mic(af['channel'])
                if mic not in mics:
                    mics[mic] = {
                        'name': mic,
                        'title': title_case(mic),
                        'default': '1.0' if mic == 'close' else '0.5',
                    }

    lines.append('  <mics>')
    for mic in mics.values():
        lines.append(f'    <mic name="{escape_xml(mic["name"])}" title="{escape_xml(mic["title"])}" default="{mic["default"]}"/>')
    lines.append('  </mics>')

    lines.append('</glorpkit>')
    return '\n'.join(lines)


def generate_instrument_xml(inst_data, kit_data):
    """Generate instrument.xml for an instrument directory."""
    lines = []
    lines.append('<?xml version="1.0" encoding="UTF-8"?>')

    inst_name = sanitize_name(inst_data['name'])
    display_name = escape_xml(inst_data['display_name'])
    lines.append(f'<glorp-instrument name="{display_name}">')

    # Group zones by sample power brackets for round-robin
    # For now, each sample becomes a zone entry
    for i, sample in enumerate(inst_data['samples']):
        zone_name = f"zone{i}"
        power = sample['power']
        lines.append(f'  <zone name="{zone_name}" power="{power:.4f}"/>')

    lines.append('</glorp-instrument>')
    return '\n'.join(lines)


def generate_zone_xml(sample_data):
    """Generate zone.xml for a zone directory."""
    lines = []
    lines.append('<?xml version="1.0" encoding="UTF-8"?>')
    lines.append(f'<glorp-zone name="{escape_xml(sample_data["name"])}" power="{sample_data["power"]:.4f}">')

    for af in sample_data['audio_files']:
        mic = map_channel_to_mic(af['channel'])
        # Store relative path within the glorpskit archive
        file_name = os.path.basename(af['file'])
        lines.append(f'  <sample file="{escape_xml(file_name)}" mic="{mic}" channel="{af["file_channel"]}"/>')

    lines.append('</glorp-zone>')
    return '\n'.join(lines)


def escape_xml(text):
    """Escape XML special characters."""
    return text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;')


def convert_kit(input_xml, output_path, kit_name=None):
    """Convert a DrumGizmo kit to GlorpKit format."""
    print(f"Parsing {input_xml}...")
    kit_data = parse_drumkit_xml(input_xml)
    if kit_data is None:
        print("ERROR: Failed to parse kit", file=sys.stderr)
        return False

    if kit_name:
        kit_data['name'] = kit_name

    print(f"Found {len(kit_data['instruments'])} instruments")
    print(f"Found {len(kit_data['channels'])} channels")

    # Build ZIP archive (stored, not compressed — WAV files don't compress well)
    with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_STORED) as zf:
        # kit.xml
        kit_xml = generate_kit_xml(kit_data)
        zf.writestr('kit.xml', kit_xml)

        # Instruments
        for inst in kit_data['instruments']:
            inst_name = sanitize_name(inst['name'])
            inst_dir = f'instruments/{inst_name}'

            # instrument.xml
            inst_xml = generate_instrument_xml(inst, kit_data)
            zf.writestr(f'{inst_dir}/instrument.xml', inst_xml)

            # Zone directories
            for i, sample in enumerate(inst['samples']):
                zone_name = f"zone{i}"
                zone_dir = f'{inst_dir}/{zone_name}'

                # zone.xml
                zone_xml = generate_zone_xml(sample)
                zf.writestr(f'{zone_dir}/zone.xml', zone_xml)

                # Samples
                os.makedirs(f'{zone_dir}/samples', exist_ok=True)
                for af in sample['audio_files']:
                    file_name = os.path.basename(af['file'])
                    sample_arc_path = f'{zone_dir}/samples/{file_name}'
                    zf.write(af['abs_path'], sample_arc_path)

    print(f"Created {output_path}")
    return True


def main():
    parser = argparse.ArgumentParser(description='Convert DrumGizmo kit to GlorpKit format')
    parser.add_argument('input', help='Path to drumkit.xml')
    parser.add_argument('output', help='Path to output .glorpkit file')
    parser.add_argument('--name', help='Override kit name')
    args = parser.parse_args()

    success = convert_kit(args.input, args.output, args.name)
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
