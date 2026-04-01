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

### Method 1: Pre-built FAP Download
1. Download `bulk_writer.fap` from the [dist/](dist/) folder
2. Copy to your SD card: `SD Card/apps/RFID/bulk_writer.fap`
3. Refresh apps on your Flipper (Applications → Browser → Search)

### Method 2: Build from Source
```bash
# Install ufbt if you haven't already
pip install ufbt

# Clone and build
git clone https://github.com/b0bby225/BulkWriter.git
cd BulkWriter
ufbt
```

The compiled `.fap` will be in `dist/bulk_writer.fap`.

## Controls

| Screen | Controls |
|---|---|
| **Setup** | Up/Down = navigate, Left/Right = adjust, Right on Mod = Scan Reference |
| **Processing** | Left/Back = Stop |
| **Summary** | OK/Back = New batch |

## License

MIT License — see [LICENSE](LICENSE) for details.

## Author

Bobby Gibbs ([@b0bby225](https://github.com/b0bby225))