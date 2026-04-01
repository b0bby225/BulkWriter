# Bulk Writer — Flipper Zero RFID Tag Reprogrammer

Bulk-program RFID cards with preset facility codes. Available in two versions:

- **v1.0 (this version)** — 125kHz LF RFID only (HID, EM4100)
- **🚀 v2.0 Enhanced** — Multi-protocol support: LF (125kHz) + NFC (13.56MHz) 

**👉 For full NFC support and all modern protocols, see [README_enhanced.md](README_enhanced.md)**

## v1.0 Features (125kHz LF Only)

- **Reference Card Scan** — Scan a sample card to auto-detect protocol and modulation (ASK/PSK), locking the reader for faster subsequent scans
- **Manual Modulation Select** — Choose ASK, PSK, or Auto (now includes NFC option for v2.0 compatibility)
- **Configurable Facility Code** — 0–255
- **Three Card Number Modes** — Preserve original, Sequential from base, or Fixed value
- **Audible + Visual Feedback** — Distinct ascending beep (success) / descending buzz (failure), LED colors per state
- **Auto-Resume** — No button press needed between cards
- **Persistent Settings** — Config saved to SD card via FlipperFormat

## v1.0 Supported Protocols (125kHz LF Only)

| Protocol | Format | Writable Cards |
|---|---|---|
| HID H10301 | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks |
| EM4100 | 40-bit (8-bit FC + 32-bit ID) | T5577 blanks |

**Note:** Factory HID ProxII and genuine EM4100 cards are read-only. This app only works with T5577-based rewritable cards.

**🔥 Want NFC support?** The enhanced version adds full 13.56MHz NFC support (NTAG, Mifare) alongside all LF protocols. See [README_enhanced.md](README_enhanced.md) for details!
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