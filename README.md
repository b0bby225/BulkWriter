# Bulk Writer — Flipper Zero Facility Code Tag Reprogrammer

Bulk-program RFID cards with preset facility codes across all major protocols. Supports both 125kHz LF and 13.56MHz NFC for comprehensive access control card processing.

## ✨ Features

- **🔄 Multi-Protocol Support** — LFRFID (125kHz), NFC (13.56MHz), and auto-detection
- **📡 All RFID Protocols** — HID H10301, EM4100, NTAG, Mifare Classic, Indala, AWID, and more
- **🎯 Auto-Detection** — Automatically identifies card type and reader needed
- **📱 Reference Card Scan** — Scan a sample card to auto-detect protocol and lock reader type for faster subsequent scans
- **🔧 Unified Modulation Control** — Single field handles both protocol selection and LF modulation
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
| Paradox | 96-bit | T5577 blanks | FSK |
| Jablotron | 64-bit | T5577 blanks | ASK |

### NFC/HF (13.56MHz)
| Protocol | Format | Writable Cards | Notes |
|---|---|---|---|
| NTAG213/215/216 | User memory area | NTAG cards | 4-byte FC+CN encoding |
| Mifare Classic 1K/4K | Sector data | Mifare cards | 5-byte FC+CN encoding |
| ISO15693 | Variable | Compatible cards | Depends on manufacturer |

**Note:** Factory HID ProxII and genuine EM4100 cards are read-only. This app only works with T5577-based rewritable LF cards and writable NFC cards.

## 🚀 Installation

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
ufbt -f application_enhanced.fam
```

The compiled `.fap` will be in `dist/bulk_writer.fap`.

## 🎮 Controls

| Screen | Controls |
|---|---|
| **Setup** | Up/Down = navigate fields, Left/Right = adjust values |
| **Setup - Mod** | Right on Mod row = Scan Reference Card |
| **Processing** | Left/Back = Stop and show summary |
| **Summary** | OK/Back = New batch |

## 🔧 Streamlined Setup Screen

The enhanced setup screen has been streamlined to 3 essential fields:

1. **FC** - Facility Code (0-255)
2. **CN** - Card Number Mode (Preserve/Sequential/Fixed)
3. **Mod** - Unified Modulation/Reader Selection:
   - **Auto (LF+NFC)** - Tries both 125kHz and 13.56MHz protocols automatically
   - **ASK (125k)** - Low Frequency ASK modulation only (HID H10301, EM4100, most common)
   - **PSK (125k)** - Low Frequency PSK modulation only (Indala, AWID, less common)
The **Mod** field eliminates the need for separate reader type selection by combining both protocol and modulation into one unified option. Navigate with Left/Right arrows or use "Scan >" to auto-detect the optimal setting for your cards.

## 🔧 Modulation Options Explained

| Option | Description | Use When |
|---|---|---|
| **Auto (LF+NFC)** | Tries both 125kHz LF and 13.56MHz NFC protocols | Mixed card types, unsure of protocol, maximum compatibility |
| **ASK (125k)** | Low Frequency ASK modulation only | HID ProxCard, EM4100, most common LF cards |
| **PSK (125k)** | Low Frequency PSK modulation only | Indala, AWID, specific PSK-based cards |
| **NFC (13.56M)** | Near Field Communication 13.56MHz only | NTAG213/215/216, Mifare Classic, modern cards |

**Pro Tip:** Use reference scanning (Right arrow on Mod field) to automatically detect and lock the optimal modulation for faster subsequent processing!

## 🎯 Usage Workflow

1. Open **Bulk Writer** from Apps → RFID
2. **Configure Settings:**
   - Set your target Facility Code (0-255)
   - Choose Card Number mode (Preserve/Sequential/Fixed)
   - Select Modulation: Auto (LF+NFC)/ASK (125k)/PSK (125k)/NFC (13.56M)
3. **(Recommended)** Scan Reference Card:
   - Navigate to Mod row and press Right
   - Place a sample card to auto-detect protocol and lock modulation
4. Press OK to start bulk processing
5. Place cards on the reader one at a time
6. Listen for protocol-specific feedback:
   - **Success**: Ascending beep + green LED
   - **Failure**: Descending buzz + red LED  
7. Press Left/Back to stop and view session summary

## 🚧 What's New in v2.0

- ✅ **Full NFC Support** - 13.56MHz cards (NTAG, Mifare)
- ✅ **Unified Modulation Field** - Single field handles both protocol and modulation selection
- ✅ **Streamlined 3-Field Setup** - FC, Card Number Mode, Modulation (no redundant fields)
- ✅ **Protocol Auto-Detection** - Automatically identifies card types
- ✅ **Enhanced Reference Scan** - Detects both LF and NFC protocols
- ✅ **Expanded Protocol Support** - All major RFID protocols including Indala, AWID, Paradox
- ✅ **Improved UI** - Cleaner, more intuitive setup flow
- ✅ **Better Error Handling** - Protocol-specific error messages

## 📋 Requirements

- **For LF (125kHz)**: T5577-based rewritable cards only
- **For NFC (13.56MHz)**: Writable NTAG or Mifare cards
- **Flipper Zero**: Latest firmware with NFC support enabled

## 🏷️ License

MIT License — see [LICENSE](LICENSE) for details.

## 👨‍💻 Author

Bobby Gibbs ([@b0bby225](https://github.com/b0bby225))