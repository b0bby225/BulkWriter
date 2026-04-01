# Bulk Writer Enhanced — Flipper Zero Multi-Protocol Tag Reprogrammer

Enhanced bulk-program LFRFID 125kHz and NFC 13.56MHz cards with preset facility codes. Designed for hands-free batch processing of access control cards across all major RFID protocols.

## ✨ Enhanced Features

- **🔄 Multi-Protocol Support** — LFRFID (125kHz), NFC (13.56MHz), and auto-detection
- **📡 All RFID Protocols** — HID H10301, EM4100, NTAG, Mifare Classic, Indala, AWID, and more
- **🎯 Auto-Detection** — Automatically identifies card type and reader needed
- **📱 Reference Card Scan** — Scan a sample card to auto-detect protocol and lock reader type for faster subsequent scans
- **⚙️ Flexible Reader Selection** — Choose LF (125kHz), NFC (13.56MHz), or Auto mode
- **🔧 Manual Modulation Select** — For LFRFID: Choose ASK, PSK, or Auto modulation
- **🎛️ Configurable Facility Code** — 0–255
- **3️⃣ Three Card Number Modes** — Preserve original, Sequential from base, or Fixed value
- **🔊 Enhanced Audible + Visual Feedback** — Protocol-specific beeps, LED colors per state
- **⏭️ Auto-Resume** — No button press needed between cards
- **💾 Persistent Settings** — All configuration saved to SD card via FlipperFormat

## 🔧 Supported Protocols

### LF RFID (125kHz)
| Protocol | Format | Writable Cards | Modulation |
|---|---|---|---|
| HID H10301 | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks | ASK |
| EM4100 | 40-bit (8-bit FC + 32-bit ID) | T5577 blanks | ASK |
| Indala | 64-bit | T5577 blanks | PSK |
| AWID | 50-bit | T5577 blanks | PSK |

### NFC/HF (13.56MHz)
| Protocol | Format | Writable Cards | Notes |
|---|---|---|---|
| NTAG213/215/216 | User memory area | NTAG cards | 4-byte FC+CN encoding |
| Mifare Classic 1K/4K | Sector data | Mifare cards | 5-byte FC+CN encoding |

**Note:** Factory HID ProxII and genuine EM4100 cards are read-only. This app only works with T5577-based rewritable LF cards and writable NFC cards.

## 🚀 Installation

### Method 1: Pre-built FAP Download
1. Download `bulk_writer_enhanced.fap` from the [dist/](dist/) folder
2. Copy to your SD card: `SD Card/apps/RFID/bulk_writer_enhanced.fap`
3. Refresh apps on your Flipper (Applications → Browser → Search)

### Method 2: Build from Source
```bash
# Install ufbt if you haven't already
pip install ufbt

# Clone and build
git clone https://github.com/b0bby225/BulkWriter.git
cd BulkWriter
ufbt -f application_enhanced.fam
```

The compiled `.fap` will be in `dist/bulk_writer_enhanced.fap`.

## 🎮 Enhanced Controls

| Screen | Controls |
|---|---|
| **Setup** | Up/Down = navigate, Left/Right = adjust |
| **Setup - Reader** | Select LF (125kHz), NFC (13.56MHz), or Auto |
| **Setup - Modulation** | Right on Mod row = Scan Reference Card (LF only) |
| **Processing** | Left/Back = Stop and show summary |
| **Summary** | OK/Back = New batch |

## 🔄 Reader Types Explained

- **LF (125k)** - Low Frequency 125kHz RFID only (HID, EM4100, etc.)
- **NFC (13.56M)** - Near Field Communication 13.56MHz only (NTAG, Mifare, etc.)  
- **Auto** - Tries both LF and NFC protocols automatically (slower but comprehensive)

## 🎯 Usage Workflow

1. Open **Bulk Writer Enhanced** from Apps → RFID
2. **Configure Settings:**
   - Set your target Facility Code (0-255)
   - Choose Card Number mode (Preserve/Sequential/Fixed)
   - Select Reader Type (LF/NFC/Auto)
   - For LF: Choose Modulation (Auto/ASK/PSK)
3. **(Recommended)** Scan Reference Card:
   - Navigate to Mod row and press Right
   - Place a sample card to auto-detect protocol and lock reader
4. Press OK to start bulk processing
5. Place cards on the reader one at a time
6. Listen for protocol-specific feedback:
   - **Success**: Ascending beep + green LED
   - **Failure**: Descending buzz + red LED  
7. Press Left/Back to stop and view session summary

## 🚧 What's New in v2.0

- ✅ **Full NFC Support** - 13.56MHz cards (NTAG, Mifare)
- ✅ **Reader Type Selection** - LF/NFC/Auto modes in Setup screen
- ✅ **Protocol Auto-Detection** - Automatically identifies card types
- ✅ **Enhanced Reference Scan** - Detects both LF and NFC protocols
- ✅ **Expanded Protocol Support** - All major RFID protocols
- ✅ **Improved UI** - Reader type shown in setup and processing screens
- ✅ **Better Error Handling** - Protocol-specific error messages

## 📋 Requirements

- **For LF (125kHz)**: T5577-based rewritable cards only
- **For NFC (13.56MHz)**: Writable NTAG or Mifare cards
- **Flipper Zero**: Latest firmware with NFC support enabled

## 🏷️ License

MIT License — see [LICENSE](LICENSE) for details.

## 👨‍💻 Author

Bobby Gibbs ([@b0bby225](https://github.com/b0bby225))