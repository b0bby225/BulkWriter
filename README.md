# Bulk Writer — Flipper Zero Facility Code Tag Reprogrammer

Bulk-program 125kHz RFID cards with preset facility codes. Designed for hands-free batch processing of access control cards.

## ✨ Features

- **📱 Reference Card Scan** — Scan a sample card to auto-detect protocol and modulation (ASK/PSK), locking the reader for faster subsequent scans
- **⚙️ Manual Modulation Select** — Choose ASK, PSK, or Auto
- **🔧 Configurable Facility Code** — 0–255
- **3️⃣ Three Card Number Modes** — Preserve original, Sequential from base, or Fixed value
- **🔊 Audible + Visual Feedback** — Distinct ascending beep (success) / descending buzz (failure), LED colors per state
- **⏭️ Auto-Resume** — No button press needed between cards
- **💾 Persistent Settings** — Config saved to SD card via FlipperFormat

## 🔧 Supported Protocols (125kHz LF)

| Protocol | Format | Writable Cards | Modulation |
|---|---|---|---|
| **HID H10301** | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks | ASK |
| **EM4100** | 40-bit (8-bit FC + 32-bit ID) | T5577 blanks | ASK |
| **Indala26** | 26-bit (8-bit FC + 16-bit CN, scattered bits) | T5577 blanks | PSK |
| **AWID** | 26-bit (8-bit FC + 16-bit CN, Wiegand parity) | T5577 blanks | PSK |
| **IoProxXSF** | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks | PSK |
| **Pyramid** | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks | PSK |
| **Paradox** | 26-bit (8-bit FC + 16-bit CN, CRC checksum) | T5577 blanks | PSK |

**Note:** Factory HID ProxII and genuine EM4100 cards are read-only. This app only works with T5577-based rewritable LF cards.

## 🚀 Installation

### Quick Download (Recommended)
**Download**: [bulk_writer.fap](dist/bulk_writer.fap)

### Installation Steps
1. Download the `.fap` file
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

## 🎮 Controls

| Screen | Controls |
|---|---|
| **Setup** | Up/Down = navigate, Left/Right = adjust, Right on Mod = Scan Reference |
| **Processing** | Left/Back = Stop |
| **Summary** | OK/Back = New batch |

## 🎯 Usage Workflow

1. Open **Bulk Writer** from Apps → RFID
2. **Configure Settings:**
   - Set your target Facility Code (0-255)
   - Choose Card Number mode (Preserve/Sequential/Fixed)
   - Select Modulation (Auto/ASK/PSK)
3. **(Recommended)** Scan Reference Card:
   - Navigate to Mod row and press Right
   - Place a sample card to auto-detect protocol and lock modulation
4. Press OK to start bulk processing
5. Place cards on the reader one at a time
6. Listen for feedback:
   - **Success**: Ascending beep + green LED
   - **Failure**: Descending buzz + red LED  
7. Press Left/Back to stop and view session summary

## 🔧 Modulation Options

- **Auto** — Tries both ASK and PSK (slowest, maximum compatibility)
- **ASK** — HID H10301, EM4100 cards only (fastest for common cards)
- **PSK** — Indala, AWID, IoProx, Pyramid, Paradox cards (fastest for PSK cards)

**Pro Tip:** Use reference scanning to auto-detect and lock the optimal modulation for your card batch!

## 📋 Requirements

- **Cards**: T5577-based rewritable LF cards only
- **Flipper Zero**: Latest firmware

## 🏷️ License

MIT License — see [LICENSE](LICENSE) for details.

## 👨‍💻 Author

Bobby Gibbs ([@b0bby225](https://github.com/b0bby225))

---
