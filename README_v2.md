# Bulk Writer V2 — Multi-Protocol RFID Tag Reprogrammer

**🚧 DEVELOPMENT VERSION** - Enhanced multi-protocol support for future expansion

Enhanced version of BulkWriter with architecture for multi-protocol RFID support. Currently implements all LF protocols with foundation for future NFC expansion.

## ✨ V2 Enhancements

- **🏗️ Multi-Protocol Architecture** - Foundation ready for LF + NFC support  
- **🔄 Unified Modulation Field** - Single field handles protocol and modulation selection
- **⚡ Optimized Performance** - Enhanced detection and processing
- **📡 Future NFC Ready** - Architecture prepared for 13.56MHz support when APIs are available
- **🎯 Streamlined UI** - Clean 3-field setup: FC, Card Number Mode, Modulation

## 🔧 Current Protocol Support

### LF RFID (125kHz) - Fully Implemented
| Protocol | Format | Writable Cards | Modulation | Status |
|---|---|---|---|---|
| **HID H10301** | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks | ASK | ✅ Working |
| **EM4100** | 40-bit (8-bit FC + 32-bit ID) | T5577 blanks | ASK | ✅ Working |
| **Indala** | 64-bit | T5577 blanks | PSK | ✅ Working |
| **AWID** | 50-bit | T5577 blanks | PSK | ✅ Working |

### NFC/HF (13.56MHz) - Planned
| Protocol | Format | Status | Notes |
|---|---|---|---|
| **NTAG213/215/216** | User memory area | 🔜 Planned | Awaiting NFC API availability |
| **Mifare Classic 1K/4K** | Sector data | 🔜 Planned | Awaiting NFC API availability |

## 🎮 V2 Modulation Options

- **Auto (LF)** — Auto-detect LF protocols (ASK+PSK)
- **ASK (125k)** — LF ASK only (HID H10301, EM4100)  
- **PSK (125k)** — LF PSK only (Indala, AWID)
- **NFC (Future)** — 13.56MHz NFC/HF (when available)

## 🚀 Installation

### Build from Source
```bash
# Install ufbt if you haven't already
pip install ufbt

# Clone and build V2
git clone https://github.com/b0bby225/BulkWriter.git
cd BulkWriter
ufbt -f application_v2.fam
```

The compiled `.fap` will be in `dist/bulk_writer_v2.fap`.

## 🔄 Differences from V1

| Feature | V1 | V2 |
|---|---|---|
| **Protocols** | LF only | LF + NFC architecture |
| **Setup Fields** | 4 fields | 3 streamlined fields |
| **Modulation** | LF ASK/PSK/Auto | Unified protocol selection |
| **Architecture** | Single worker | Multi-worker ready |
| **Future Ready** | No | NFC expansion ready |

## 📋 Development Status

- ✅ **Core LF Support** - All 125kHz protocols working
- ✅ **Unified UI** - Streamlined 3-field configuration  
- ✅ **Architecture** - Multi-protocol foundation complete
- 🔜 **NFC Implementation** - Waiting for Flipper firmware NFC APIs
- 🔜 **Auto-Detection** - Cross-protocol card identification
- 🔜 **Reference Scanning** - Multi-protocol reference detection

## 🎯 Usage (Current)

Same workflow as V1 but with enhanced modulation selection:

1. **FC** - Set facility code (0-255)
2. **CN** - Card number mode (Preserve/Sequential/Fixed)  
3. **Mod** - Choose protocol: Auto(LF)/ASK/PSK/NFC(Future)
4. **Scan Reference** - Auto-detect optimal modulation (when implemented)
5. **Bulk Process** - Place cards and process automatically

## ⚠️ Current Limitations

- **NFC protocols disabled** until Flipper firmware APIs are available
- **Reference scanning** only works for LF protocols  
- **Auto-detection** limited to LF ASK/PSK switching

## 🏷️ License

MIT License — see [LICENSE](LICENSE) for details.

## 👨‍💻 Author

Bobby Gibbs ([@b0bby225](https://github.com/b0bby225))

---

**📈 This is the development version** - For stable LF-only operation, use the main BulkWriter app. V2 provides the foundation for future multi-protocol expansion.
