# Bulk Writer — Flipper Zero 125kHz Tag Reprogrammer

Bulk-program T5577 125kHz cards with a preset facility code. Designed for hands-free batch processing of access control cards.

## Features

- **Reference Card Scan** — Scan a sample card to auto-detect protocol and modulation (ASK/PSK), locking the reader for faster subsequent scans
- **Manual Modulation Select** — Choose ASK, PSK, or Auto
- **Configurable Facility Code** — 0–255
- **Three Card Number Modes** — Preserve original, Sequential from base, or Fixed value
- **Audible + Visual Feedback** — Distinct ascending beep (success) / descending buzz (failure), LED colors per state
- **Auto-Resume** — No button press needed between cards
- **Persistent Settings** — Config saved to SD card via FlipperFormat

## Supported Protocols

| Protocol | Format | Writable Cards |
|---|---|---|
| HID H10301 | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks |
| EM4100 | 40-bit (8-bit FC + 32-bit ID) | T5577 blanks |

**Note:** Factory HID ProxII and genuine EM4100 cards are read-only. This app only works with T5577-based rewritable cards.
## Installation

### Option 1: Flipper App Store (Recommended)
Install directly from the Flipper mobile app or [Flipper Lab](https://lab.flipper.net/apps) — search for "Bulk Writer" in the RFID category.

### Option 2: Pre-built FAP Download
1. Download `bulk_writer.fap` from the [dist/](dist/) folder or [Releases](https://github.com/b0bby225/BulkWriter/releases)
2. Copy to your Flipper Zero SD card: `SD Card/apps/RFID/bulk_writer.fap`
3. Launch from **Apps → RFID → Bulk Writer**

### Option 3: Build from Source
```bash
# Install ufbt if you haven't already
pip install ufbt

# Clone and build
git clone https://github.com/b0bby225/BulkWriter.git
cd BulkWriter
ufbt

# The compiled .fap will be in dist/bulk_writer.fap
```

## Usage

1. **Setup** — Configure facility code (0-255), card number mode, and optionally scan a reference card for faster reads
2. **Start** — Press OK to begin bulk processing  
3. **Scan** — Place T5577 cards on the reader one at a time
4. **Listen** — Ascending beep = success, descending buzz = failure
5. **Stop** — Press Back to view session summary
## Controls

| Screen | Controls |
|---|---|
| **Setup** | Up/Down = navigate, Left/Right = adjust, Right on Mod = Scan Reference |
| **Processing** | Left/Back = Stop |
| **Summary** | OK/Back = New batch |

## Requirements

- T5577-based writable cards (factory HID ProxII and EM4100 cards are read-only)
- Flipper Zero with RFID capability
- Target 7+ firmware (built with ufbt)

## License

MIT License — see [LICENSE](LICENSE) for details.

## Author

Bobby Gibbs ([@b0bby225](https://github.com/b0bby225))

---

### Technical Details

**Architecture:** ViewPort-based GUI with persistent FlipperFormat settings. State machine: Config → RefScan → Ready → Reading → Writing → Success/Error (auto-resume).

**Modulation Detection:** ASK (most 125kHz cards), PSK (Indala/AWID), or Auto-detect. Reference scan locks modulation for faster bulk reads.

**Protocol Support:** HID H10301 26-bit with parity recalculation, EM4100 with FC byte substitution.