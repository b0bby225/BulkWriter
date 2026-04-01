# Bulk Writer V2 — Enhanced RFID Tag Reprogrammer

**✨ ENHANCED VERSION** - Advanced UI and improved user experience 

Enhanced version of BulkWriter with improved interface, statistics tracking, and streamlined workflow. Same powerful LF protocol support with a better user experience.

## ✨ V2 Enhancements

- **📊 Advanced Statistics** - Session tracking, lifetime counters, and protocol-specific stats
- **🎨 Enhanced UI** - Cleaner design with better visual feedback and progress indicators  
- **⚡ Improved Workflow** - Streamlined navigation and auto-transitions between screens
- **🎯 Smart Detection** - Better protocol detection and error handling with retry logic
- **📈 Success Tracking** - Real-time success rates and detailed session summaries
- **💾 Persistent Stats** - Lifetime statistics saved across sessions

## 🔧 Current Protocol Support

### LF RFID (125kHz) - Fully Implemented
| Protocol | Format | Writable Cards | Modulation | Status |
|---|---|---|---|---|
| **HID H10301** | 26-bit (8-bit FC + 16-bit CN) | T5577 blanks | ASK | ✅ Enhanced |
| **EM4100** | 40-bit (8-bit FC + 32-bit ID) | T5577 blanks | ASK | ✅ Enhanced |
| **Indala** | 64-bit | T5577 blanks | PSK | ✅ Enhanced |
| **AWID** | 50-bit | T5577 blanks | PSK | ✅ Enhanced |

## 🎮 V2 Enhanced Features

- **Statistics Screen** — Detailed session and lifetime statistics with protocol breakdown
- **Auto (Smart)** — Enhanced auto-detection with better protocol identification
- **ASK (Fast)** — Optimized for HID H10301 and EM4100 protocols
- **PSK (Indala)** — Optimized for Indala and AWID protocols

## ✨ Enhanced User Experience

- **Real-time Progress** — Live session counters and success rate tracking
- **Smart Transitions** — Automatic screen transitions with optimal timing
- **Enhanced Feedback** — Better visual and audio feedback for all operations
- **Error Recovery** — Improved retry logic with detailed error messages
- **Session Summary** — Comprehensive statistics with success rate analysis

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
