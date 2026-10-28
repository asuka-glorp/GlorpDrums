# Security Policy

## Supported Versions

We release security updates for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| 0.2.x   | :white_check_mark: |
| < 0.2   | :x:                |

## Reporting a Vulnerability

We take security vulnerabilities seriously. If you discover a security vulnerability in GlorpDrums, please report it responsibly.

### How to Report

**Please do NOT report security vulnerabilities via public GitHub issues.**

Instead, email us at **security@glorpdrums.org** with:

1. A description of the vulnerability
2. Steps to reproduce
3. Potential impact
4. Any suggested fixes (if you have them)

### What to Expect

- We will acknowledge receipt within 48 hours
- We will provide a preliminary assessment within 7 days
- We will work with you to understand and resolve the issue
- We will coordinate disclosure timing with you

## Security Considerations for GlorpDrums

### Audio Plugin Security

As an audio plugin (VST3/LV2), GlorpDrums runs within a host application (DAW). Security considerations include:

- **No network access**: GlorpDrums does not make network connections
- **No file execution**: Only reads audio/sample files and configuration
- **Memory safety**: Written in modern C++20 with RAII, bounds checking
- **Sandboxing**: Relies on host DAW sandboxing for plugin isolation

### File Format Security

The `.glorpkit` format is a ZIP64 archive. Security measures:

- ZIP parsing uses custom implementation with bounds checking
- No executable code in kit files
- Sample data is raw PCM audio only
- XML manifests parsed with custom lightweight parser (no external libxml2)

### User Data

- Settings stored in `~/.config/GlorpDrums/settings.xml`
- Song database in `~/.config/GlorpDrums/song_library.xml`
- Kit editor settings in `~/.config/GlorpDrums/kit_editor_settings.xml`
- No personal data collected or transmitted

### Build Security

- Dependencies: JUCE 8.0.6 (fetched via CMake FetchContent with pinned tag)
- No runtime dependencies on system libraries beyond standard audio stack
- Reproducible builds supported via pinned JUCE version

## Disclosure Policy

When a security vulnerability is confirmed:

1. We will develop a fix
2. We will release a patch version
3. We will publish a security advisory on GitHub
4. We will credit the reporter (unless they request anonymity)

## Contact

For security concerns: **security@glorpdrums.org**

For general questions: Open a [GitHub Discussion](https://github.com/glorpdrums/GlorpDrums/discussions)