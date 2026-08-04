#ifndef DIAGCOMMANDS_H
#define DIAGCOMMANDS_H

#include <QtGlobal>

namespace DiagCmd {

    // HDLC Constants
    const quint8 HDLC_FLAG = 0x7E;
    const quint8 HDLC_ESC = 0x7D;
    const quint8 HDLC_ESC_MASK = 0x20;

    // Command Codes
    const quint8 DIAG_VERNO_F = 0x00;
    const quint8 DIAG_ESN_F = 0x01;
    const quint8 DIAG_PEEK_BYTE_F = 0x02;
    const quint8 DIAG_PEEK_WORD_F = 0x03;
    const quint8 DIAG_PEEK_DWORD_F = 0x04;
    const quint8 DIAG_POKE_BYTE_F = 0x05;
    const quint8 DIAG_POKE_WORD_F = 0x06;
    const quint8 DIAG_POKE_DWORD_F = 0x07;
    
    const quint8 DIAG_STATUS_F = 0x0C;
    
    const quint8 DIAG_NV_READ_F = 0x26;
    const uint8_t DIAG_NV_WRITE_F = 0x27;
    const uint8_t DIAG_NV_READ_EXT_F = 0x75; // Extended NV Read
    const uint8_t DIAG_NV_WRITE_EXT_F = 0x76; // Extended NV Write
    
    // Subsystem Commands (0x4B)
    const uint8_t DIAG_SUBSYS_CMD_F = 0x4B;
    
    const quint8 DIAG_SPC_F = 0x41; // Service Programming Code

    const quint8 DIAG_PROTOCOL_LOOPBACK_F = 0x7B;
    const quint8 DIAG_EXT_BUILD_ID_F = 0x7C;
}

// Define Subsys namespaces OUTSIDE of DiagCmd namespace to avoid confusion
namespace SubsysId {
    const uint8_t SUBSYS_NV = 0x0B;      // NV Items
    const uint8_t SUBSYS_WCDMA = 0x04;   // WCDMA
    const uint8_t SUBSYS_GSM = 0x05;     // GSM
}

namespace SubsysCmd {
    const uint16_t NV_READ_EXT = 0x0026;  // Subsystem NV Read
    const uint16_t NV_WRITE_EXT = 0x0027; // Subsystem NV Write
}

namespace DiagCmd { // Re-open DiagCmd for other nested namespaces

    // Subsystem IDs (for CMD 0x4B) - These are likely deprecated by SubsysId namespace
    const quint8 DI_SUBSYS_COMMON = 0;
    const quint8 DI_SUBSYS_WCDMA = 4;
    const quint8 DI_SUBSYS_GSM = 5;
    const quint8 DI_SUBSYS_EFS = 19; // 0x13

    // NV Item IDs
    namespace NVItem {
        const quint16 NV_ESN_I = 0;
        const quint16 NV_PAP_USER_ID_I = 14;
        const quint16 NV_PAP_PASSWORD_I = 15;
        const quint16 NV_UE_IMEI_I = 550;
        const quint16 NV_FTM_MODE_I = 453;
        const quint16 NV_MEID_I = 1943;
        const quint16 NV_LOCK_CODE_I = 82;
    }

    // EFS Operation Codes (Subsys 0x13)
    namespace EFS {
        const quint16 CMD_OPEN = 0x0006;
        const quint16 CMD_CLOSE = 0x0007;
        const quint16 CMD_READ = 0x0008;
        const quint16 CMD_WRITE = 0x0009;
        const quint16 CMD_UNLINK = 0x000F; // Delete
        const quint16 CMD_OPENDIR = 0x000B;
        const quint16 CMD_READDIR = 0x000C;
        const quint16 CMD_CLOSEDIR = 0x000D;
        const quint16 CMD_STAT = 0x000E;

        // Open Flags
        const qint32 O_RDONLY = 0;
        const qint32 O_WRONLY = 1;
        const qint32 O_RDWR = 2;
        const qint32 O_CREAT = 0x0200; // 01000 octal
        const qint32 O_TRUNC = 0x0400; // 02000 octal
        const qint32 O_APPEND = 0x0008;
    }

} // namespace DiagCmd

#endif // DIAGCOMMANDS_H
