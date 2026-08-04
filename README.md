<div align="center">
  <img src="resources/app_icon.png" width="128" height="128" alt="Mini Diag App Icon" />
  <h1>Mini Diag Tool</h1>
  <h3>🚀 Native Standalone Qualcomm DIAG / EFS / NV Manager</h3>
  <p><b>Direct Raw Protocol Implementation | Zero External DLLs | Pure Native C++</b></p>

  [![Version](https://img.shields.io/badge/Version-1.1.0--BETA-00e5ff.svg?style=for-the-badge&logo=rocket)](https://github.com/alisakkaf/QC-Native-Diag/releases/tag/v1.1.0)
  [![Platform](https://img.shields.io/badge/Platform-Windows_x86_/_x64-0078D6.svg?style=for-the-badge&logo=windows)](https://github.com/alisakkaf/QC-Native-Diag/releases/tag/v1.1.0)
  [![License](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](LICENSE)
  [![Website](https://img.shields.io/badge/Website-alisakkaf.com-1877F2?style=for-the-badge&logo=google-chrome)](https://alisakkaf.com/)

  <br>
  <a href="https://github.com/alisakkaf/QC-Native-Diag/releases/tag/v1.1.0">
    <img src="https://img.shields.io/badge/Download_Latest_Release_v1.1.0-Click_Here-FF0000?style=for-the-badge&logo=github" height="48">
  </a>
</div>

---

## 💡 Core Philosophy & Features

**Mini Diag Tool** is an advanced, driver-less hardware diagnostic tool built entirely from scratch in native C++ and Qt to handle Qualcomm chipsets without relying on heavy external wrappers (`QPST.dll`, `QMSL`, or third-party engines).

* **⚡ 100% Standalone:** Zero external DLL dependencies. Talks directly to COM serial ports.
* **🔌 Native HDLC & CRC-16 Engine:** Low-level implementation of HDLC framing (`0x7E`/`0x7D` escaping) and CRC-16 checksum verification for sub-millisecond response latency.
* **📶 Complete Dual-SIM (SIM 1 & SIM 2) Architecture:** Native SIM selection controls with mutual exclusion and independent identity/EFS parsing.
* **🔍 Silent SIM 2 Auto-Discovery:** Automatic background path discovery across 7 Qualcomm dual-SIM base paths (`/nv/item_files_1/`, `/policyman_1/`, etc.) with path masking.
* **⚙️ 3-Tier NV Fallback Engine:** Smart automatic fallback across Standard (`0x26`), Subsystem (`0x4B`), and Extended (`0x75`) DIAG commands, providing full compatibility with Snapdragon 8 Gen 1/2/3 chipsets.
* **🛡️ Watchdog-Protected Range Backup:** Unstoppable NV range backup with a dedicated 250ms watchdog timer (`m_backupWatchdog`) and 100ms item deadline to skip unpopulated NV IDs instantly.

---

## 📸 Interface Showcase

<div align="center">
  <h3>📊 Dashboard & Identity Parsing</h3>
  <img width="100%" alt="Mini Diag Dashboard" src="Screenshot/537237747-cfe6e848-b10a-4a2d-8828-d345f108ec05.png" />
  <br/><br/>
  <h3>⚙️ NV Manager & Advanced Hex Editor</h3>
  <img width="100%" alt="NV Manager" src="Screenshot/537237749-0781f571-48b6-4874-9a2d-b17916eb42e8.png" />
  <br/><br/>
  <h3>🖥️ Full Application View</h3>
  <img width="100%" alt="Full View" src="Screenshot/537237742-d8b43af3-27aa-4070-b390-095a2061b336.png" />
</div>

---

## 🔬 Technical Specs & Capabilities

### 📊 1. Intelligent Dashboard
- **Identity Parsing:** IMEI 1, IMEI 2 (SIM 2), ESN, MEID, IMSI.
- **Network & Carrier:** MDN (Phone Number), MCC/MNC codes, Carrier Banner.
- **System Info:** Firmware Version, Hardware Config, Mode Snapshot (Online, FTM, EDL).

### 📂 2. EFS Explorer (Modem Filesystem)
- **Directory Traversal:** Safe recursive file listing and folder operations.
- **Dual-SIM EFS Browsing:** Seamless switching between SIM 1 and SIM 2 internal modem partitions.
- **High-Speed I/O:** Upload, Download, Read, Write, and Delete modem items.

### ⚙️ 3. Advanced NV Manager (Dual View & Hex Editor)
- **Database View:** Built-in repository of critical NV items.
- **Advanced Hex Editor:** Raw memory view with Hex, ASCII Text, Binary, and Decimal formatting.
- **Subsystem & Extended NV:** Full support for high-range NV IDs (>4000) and multi-index items.

### 🛡️ 4. Range Backup & Restore Engine
- **Watchdog Protection:** Dedicated `m_backupWatchdog` timer prevents queue hangs.
- **Fast Timeout:** 100ms per unpopulated NV ID.
- **Binary (.nv) & JSON Backups:** Export and restore custom NV range backups.

### 🔧 5. Security & Device Control
- **SPC / PWD Reset:** Read, Write, or Zero-out SPC codes.
- **Security Bypass:** Unlock write permissions on protected modems.
- **Reboot Control:** Switch modes instantly (`Diag`, `FTM`, `EDL 9008`, `Reset`).

---

## 🔗 Official Links & Connect

* **Official Website:** [https://alisakkaf.com/](https://alisakkaf.com/)
* **GitHub Profile:** [https://github.com/alisakkaf/](https://github.com/alisakkaf/)
* **Repository:** [https://github.com/alisakkaf/QC-Native-Diag](https://github.com/alisakkaf/QC-Native-Diag)
* **Facebook Page:** [https://www.facebook.com/AliSakkaf.Dev/](https://www.facebook.com/AliSakkaf.Dev/)

---

## 💡 Support the Developer

If you find my tools and projects useful, consider supporting my work. Your support helps keep these projects completely free and open source!

<div align="center">

| Crypto Asset | Network | Wallet Address (Copy) | Quick Scan |
| :--- | :--- | :--- | :---: |
| ![USDT](https://img.shields.io/badge/USDT-Tether-26A17B?style=for-the-badge&logo=tether&logoColor=white) | **TRC20** | `TYLBeDA5aGNcc3WkVqf3xWPHXmsZzs2p28` | <a href="https://api.qrserver.com/v1/create-qr-code/?size=300x300&margin=10&data=TYLBeDA5aGNcc3WkVqf3xWPHXmsZzs2p28" target="_blank"><img src="https://img.shields.io/badge/Show_QR-Click_Here-black?style=flat-square&logo=qr-code" alt="QR"></a> |
| ![USDT](https://img.shields.io/badge/USDT-Tether-26A17B?style=for-the-badge&logo=tether&logoColor=white) | **BEP20** | `0x67cf27f33c80479ea96372810f9e2ee4c3b095c5` | <a href="https://api.qrserver.com/v1/create-qr-code/?size=300x300&margin=10&data=0x67cf27f33c80479ea96372810f9e2ee4c3b095c5" target="_blank"><img src="https://img.shields.io/badge/Show_QR-Click_Here-black?style=flat-square&logo=qr-code" alt="QR"></a> |
| ![BTC](https://img.shields.io/badge/BTC-Bitcoin-F7931A?style=for-the-badge&logo=bitcoin&logoColor=white) | **Bitcoin** | `bc1q97dr37h37npzarmmrv0tjz2nm50htqc7pfpzj6` | <a href="https://api.qrserver.com/v1/create-qr-code/?size=300x300&margin=10&data=bitcoin:bc1q97dr37h37npzarmmrv0tjz2nm50htqc7pfpzj6" target="_blank"><img src="https://img.shields.io/badge/Show_QR-Click_Here-black?style=flat-square&logo=qr-code" alt="QR"></a> |
| ![ETH](https://img.shields.io/badge/ETH-Ethereum-3C3C3D?style=for-the-badge&logo=ethereum&logoColor=white) | **ERC20** | `0x67cf27f33c80479ea96372810F9e2EE4C3b095C5` | <a href="https://api.qrserver.com/v1/create-qr-code/?size=300x300&margin=10&data=ethereum:0x67cf27f33c80479ea96372810F9e2EE4C3b095C5" target="_blank"><img src="https://img.shields.io/badge/Show_QR-Click_Here-black?style=flat-square&logo=qr-code" alt="QR"></a> |
| ![SOL](https://img.shields.io/badge/SOL-Solana-9945FF?style=for-the-badge&logo=solana&logoColor=white) | **Solana** | `Cbesgr4tvo4T1inNMFe46GSym2qMYjkmofbXFc77rDNK` | <a href="https://api.qrserver.com/v1/create-qr-code/?size=300x300&margin=10&data=solana:Cbesgr4tvo4T1inNMFe46GSym2qMYjkmofbXFc77rDNK" target="_blank"><img src="https://img.shields.io/badge/Show_QR-Click_Here-black?style=flat-square&logo=qr-code" alt="QR"></a> |

</div>

---

## ⚖️ License & Disclaimer

This software is provided **"AS IS"**, without warranty of any kind. The developer is not responsible for device damage caused by misuse. Refer to [LICENSE](LICENSE) and [DISCLAIMER.md](DISCLAIMER.md) for complete details.

<div align="center">
  Created with ❤️ by <b>Ali Sakkaf</b> | <a href="https://alisakkaf.com/">alisakkaf.com</a>
</div>
