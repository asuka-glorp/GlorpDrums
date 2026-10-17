#!/usr/bin/env python3
"""
convert_to_glorpkit.py - Package DrumGizmo drumkits into GlorpKit (.glorpkit) single-file packages.

Creates a zero-copy memory-mappable .glorpkit container from a DrumGizmo kit directory
or drumkit.xml file, and installs it to ~/.glorpdrums/kits/ by default.

Features:
  - Stored uncompressed (ZIP_STORED) for zero-copy OS memory mapping in GlorpDrums
  - Verifies kit structure (drumkit.xml, instruments, samples, midimaps)
  - Automatically targets /home/$USER/.glorpdrums/kits (creates directory if missing)
  - Optional custom output path via --output / -o

Usage:
  python3 tools/convert_to_glorpkit.py /path/to/DrumGizmoKit/
  python3 tools/convert_to_glorpkit.py /path/to/drumkit.xml
  python3 tools/convert_to_glorpkit.py /path/to/DrumGizmoKit/ -o /custom/path/MyKit.glorpkit
"""

import argparse
import os
import re
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path


def sanitize_filename(name: str) -> str:
    """Sanitize kit title for filesystem use."""
    clean = re.sub(r'[^a-zA-Z0-9_\-\. ]', '_', name).strip()
    return clean if clean else "drumkit"


def find_drumkit_xml(input_path: Path) -> tuple[Path, Path]:
    """
    Given an input path (directory or XML file), returns (drumkit_xml_path, kit_root_dir).
    Raises FileNotFoundError if not found.
    """
    p = input_path.resolve()

    if p.is_file():
        if p.name.lower() in ("drumkit.xml", "kit.xml") or p.suffix.lower() == ".xml":
            return p, p.parent
        raise ValueError(f"Input file '{p}' does not appear to be a drumkit XML file.")

    if p.is_dir():
        for candidate in ("drumkit.xml", "kit.xml"):
            c_path = p / candidate
            if c_path.is_file():
                return c_path, p

        # Search top-level XML files for <drumkit> tag
        for xml_file in p.glob("*.xml"):
            try:
                tree = ET.parse(xml_file)
                root = tree.getroot()
                if root.tag.lower() in ("drumkit", "glorpkit"):
                    return xml_file, p
            except Exception:
                continue

        raise FileNotFoundError(f"No drumkit.xml or kit.xml found in directory '{p}'.")

    raise FileNotFoundError(f"Input path '{p}' does not exist.")


def inspect_drumkit(xml_path: Path, kit_dir: Path) -> dict:
    """
    Parses the drumkit XML and verifies referenced instrument XMLs and audio files.
    """
    try:
        tree = ET.parse(xml_path)
        root = tree.getroot()
    except Exception as e:
        raise ValueError(f"XML parse error in '{xml_path}': {e}")

    if root.tag.lower() not in ("drumkit", "glorpkit"):
        raise ValueError(f"Root tag in '{xml_path}' is <{root.tag}>, expected <drumkit> or <glorpkit>.")

    # Metadata
    title = root.get("name", "")
    meta = root.find("metadata")
    if meta is not None:
        title_node = meta.find("title")
        if title_node is not None and title_node.text:
            title = title_node.text.strip()
    if not title:
        title = kit_dir.name

    version = root.get("version", "1.0.0")
    samplerate = root.get("samplerate", "48000")

    # Channels
    channels = []
    chan_node = root.find("channels")
    if chan_node is not None:
        for c in chan_node.findall("channel"):
            c_name = c.get("name", "")
            if c_name:
                channels.append(c_name)

    # Instruments
    instruments = []
    missing_instruments = []
    missing_samples = []
    total_samples = 0

    inst_node = root.find("instruments")
    if inst_node is not None:
        for inst in inst_node.findall("instrument"):
            name = inst.get("name", "")
            file_rel = inst.get("file", "")
            if not file_rel:
                continue

            # Normalize path
            file_rel_norm = file_rel.replace("\\", "/")
            inst_xml_path = (kit_dir / file_rel_norm).resolve()

            if not inst_xml_path.is_file():
                missing_instruments.append(file_rel_norm)
                continue

            inst_info = {
                "name": name or inst_xml_path.stem,
                "file": file_rel_norm,
                "path": inst_xml_path,
                "sample_count": 0,
            }

            # Parse instrument XML to inspect samples
            try:
                itree = ET.parse(inst_xml_path)
                iroot = itree.getroot()
                inst_dir = inst_xml_path.parent

                samples_elem = iroot.find("samples")
                if samples_elem is not None:
                    for s in samples_elem.findall("sample"):
                        for af in s.findall("audiofile"):
                            wav_rel = af.get("file", "")
                            if not wav_rel:
                                continue
                            wav_rel_norm = wav_rel.replace("\\", "/")

                            # Resolve relative to instrument XML directory or kit dir
                            sample_path = (inst_dir / wav_rel_norm).resolve()
                            if not sample_path.is_file():
                                sample_path = (kit_dir / wav_rel_norm).resolve()

                            if not sample_path.is_file():
                                missing_samples.append(f"{inst_info['name']} -> {wav_rel_norm}")
                            else:
                                inst_info["sample_count"] += 1
                                total_samples += 1
            except Exception as e:
                print(f"Warning: could not parse instrument XML '{inst_xml_path}': {e}", file=sys.stderr)

            instruments.append(inst_info)

    return {
        "title": title,
        "version": version,
        "samplerate": samplerate,
        "channels": channels,
        "instruments": instruments,
        "missing_instruments": missing_instruments,
        "missing_samples": missing_samples,
        "total_samples": total_samples,
        "xml_path": xml_path,
        "kit_dir": kit_dir,
    }


def collect_files_to_package(kit_dir: Path, manifest_rel: str) -> list[tuple[Path, str]]:
    """
    Collects all files in the kit directory to include in the package,
    preserving paths relative to kit_dir. Filters out hidden files, VCS, and existing packages.
    """
    files_to_pack = []
    ignored_suffixes = {".glorpkit", ".zip", ".tar", ".gz", ".tmp", ".bak", ".swp"}
    ignored_names = {".git", ".gitignore", ".gitattributes", ".DS_Store", "Thumbs.db", "__pycache__"}

    for root, dirs, files in os.walk(kit_dir):
        # Filter directories in-place
        dirs[:] = [d for d in dirs if d not in ignored_names and not d.startswith(".")]

        for f in sorted(files):
            if f.startswith(".") or f in ignored_names:
                continue

            file_path = Path(root) / f
            suffix = file_path.suffix.lower()
            if suffix in ignored_suffixes:
                continue

            rel_arc_path = file_path.relative_to(kit_dir).as_posix()
            files_to_pack.append((file_path, rel_arc_path))

    return files_to_pack


def convert_kit(input_path: Path, output_path: Path | None, kit_name: str | None, dry_run: bool = False) -> bool:
    """Packages a DrumGizmo kit into a .glorpkit file."""
    try:
        manifest_path, kit_dir = find_drumkit_xml(input_path)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return False

    info = inspect_drumkit(manifest_path, kit_dir)

    title = kit_name if kit_name else info["title"]
    safe_name = sanitize_filename(title)

    # Determine default destination directory: /home/$USER/.glorpdrums/kits
    default_dir = Path.home() / ".glorpdrums" / "kits"

    if output_path is None:
        target_file = default_dir / f"{safe_name}.glorpkit"
    else:
        target_path = output_path.resolve()
        if target_path.is_dir():
            target_file = target_path / f"{safe_name}.glorpkit"
        else:
            if not target_path.name.lower().endswith((".glorpkit", ".zip")):
                target_file = target_path.with_suffix(".glorpkit")
            else:
                target_file = target_path

    print("=" * 64)
    print("GlorpKit Drumkit Packager")
    print("=" * 64)
    print(f"Kit Title:       {title}")
    print(f"Format Version:  {info['version']}")
    print(f"Sample Rate:     {info['samplerate']} Hz")
    print(f"Kit Directory:   {kit_dir}")
    print(f"Channels ({len(info['channels'])}):  {', '.join(info['channels']) if info['channels'] else '(default)'}")
    print(f"Instruments:     {len(info['instruments'])}")
    print(f"Samples Found:   {info['total_samples']}")
    print(f"Target Output:   {target_file}")
    print("-" * 64)

    if info["missing_instruments"]:
        print(f"WARNING: {len(info['missing_instruments'])} instrument XML files not found:", file=sys.stderr)
        for m in info["missing_instruments"][:5]:
            print(f"  - {m}", file=sys.stderr)
        if len(info["missing_instruments"]) > 5:
            print(f"  ... and {len(info['missing_instruments']) - 5} more", file=sys.stderr)

    if info["missing_samples"]:
        print(f"WARNING: {len(info['missing_samples'])} audio sample files not found:", file=sys.stderr)
        for m in info["missing_samples"][:5]:
            print(f"  - {m}", file=sys.stderr)
        if len(info["missing_samples"]) > 5:
            print(f"  ... and {len(info['missing_samples']) - 5} more", file=sys.stderr)

    manifest_rel = manifest_path.relative_to(kit_dir).as_posix()
    files_to_pack = collect_files_to_package(kit_dir, manifest_rel)

    # Ensure drumkit.xml / kit.xml is in the package
    has_manifest = any(arc_name.lower() in ("drumkit.xml", "kit.xml") for _, arc_name in files_to_pack)
    if not has_manifest:
        files_to_pack.insert(0, (manifest_path, "drumkit.xml"))

    total_bytes = sum(f.stat().st_size for f, _ in files_to_pack)
    print(f"Files to Package: {len(files_to_pack)} ({total_bytes / (1024 * 1024):.1f} MB)")

    if dry_run:
        print("\nDry-run complete. No archive created.")
        return True

    # Ensure destination directory exists (e.g. /home/$USER/.glorpdrums/kits)
    target_file.parent.mkdir(parents=True, exist_ok=True)

    print(f"\nBuilding uncompressed package: {target_file} ...")
    try:
        # Write uncompressed ZIP archive (ZIP_STORED) for zero-copy memory mapping
        with zipfile.ZipFile(target_file, "w", compression=zipfile.ZIP_STORED) as zf:
            for idx, (src_file, arc_name) in enumerate(files_to_pack, 1):
                zf.write(src_file, arc_name)
                if idx % 50 == 0 or idx == len(files_to_pack):
                    percent = (idx / len(files_to_pack)) * 100.0
                    sys.stdout.write(f"\r  Progress: {idx}/{len(files_to_pack)} files ({percent:.0f}%)")
                    sys.stdout.flush()

        print("\n\nPackage successfully built!")
        print(f"Location: {target_file}")
        print(f"Size:     {target_file.stat().st_size / (1024 * 1024):.1f} MB")
        print("\nGlorpDrums will automatically list this kit in its library as:")
        print(f"  [PKG] {title}")
        return True
    except Exception as e:
        print(f"\nERROR creating package: {e}", file=sys.stderr)
        if target_file.exists():
            target_file.unlink(missing_ok=True)
        return False


def main():
    parser = argparse.ArgumentParser(
        description="Convert DrumGizmo kits into single-file zero-copy .glorpkit packages.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert kit and install directly to ~/.glorpdrums/kits/
  python3 tools/convert_to_glorpkit.py /path/to/DrumGizmoKit/

  # Convert specific drumkit.xml file
  python3 tools/convert_to_glorpkit.py /path/to/drumkit.xml

  # Output to a custom path or directory
  python3 tools/convert_to_glorpkit.py /path/to/DrumGizmoKit/ -o /custom/path/MyKit.glorpkit

  # Override kit display name
  python3 tools/convert_to_glorpkit.py /path/to/DrumGizmoKit/ --name "Studio Custom Kit"
        """
    )
    parser.add_argument("input", help="Path to drumkit.xml or DrumGizmo kit directory")
    parser.add_argument("-o", "--output", help="Optional output path or directory (defaults to ~/.glorpdrums/kits/)")
    parser.add_argument("--name", help="Override kit display title and package filename")
    parser.add_argument("--dry-run", action="store_true", help="Validate kit and inspect contents without building package")

    args = parser.parse_args()

    input_path = Path(args.input)
    output_path = Path(args.output) if args.output else None

    success = convert_kit(input_path, output_path, args.name, args.dry_run)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
