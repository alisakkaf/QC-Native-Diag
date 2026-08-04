#include "include/NVManager.h"
#include "include/DiagCommands.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QTimer>
#include <QDebug>
#include <QApplication>


NVManager::NVManager(DiagProtocol *protocol, QObject *parent)
    : QObject(parent)
    , m_protocol(protocol)
    , m_currentOp(OP_NONE)
    , m_currentItemId(0)
    , m_currentIndex(0)
    , m_isExtended(false)
    , m_readTryMode(0)
    , m_backupCurrentIndex(0)
    , m_backupFailCount(0)
    , m_backupSkipCount(0)
    , m_restoreCurrentIndex(0)
    , m_restoreSuccessCount(0)
    , m_restoreFailCount(0)
{
    connect(m_protocol, &DiagProtocol::responseReceived, this, &NVManager::handleResponse);
    
    // Setup response timeout timer (2 seconds per operation)
    m_responseTimer = new QTimer(this);
    m_responseTimer->setSingleShot(true);
    m_responseTimer->setInterval(2000); // 2 seconds
    connect(m_responseTimer, &QTimer::timeout, this, &NVManager::onTimeout);

    // Setup dedicated watchdog timer for Range Backup
    m_backupWatchdog = new QTimer(this);
    m_backupWatchdog->setSingleShot(false);
    m_backupWatchdog->setInterval(250); // 250ms per item watchdog deadline
    connect(m_backupWatchdog, &QTimer::timeout, this, &NVManager::onBackupWatchdogTimeout);
}

void NVManager::onBackupWatchdogTimeout()
{
    if (m_currentOp != OP_BACKUP) {
        m_backupWatchdog->stop();
        return;
    }

    emit logMessage(QString("⏱️ Watchdog: NV %1 taking too long — skipping!").arg(m_currentItemId));
    m_backupSkipCount++;
    QTimer::singleShot(1, this, &NVManager::processBackupQueue);
}

bool NVManager::isConnected() const
{
    return m_protocol && m_protocol->isConnected();
}

const NVItem* NVManager::getItemInfo(uint16_t itemId) const
{
    return NVDatabase::instance().getItem(itemId);
}

QString NVManager::formatNVValue(uint16_t itemId, const QByteArray &data) const
{
    if (data.isEmpty()) {
        return "[No Data]";
    }
    
    const NVItem *item = getItemInfo(itemId);
    QString itemName = item ? item->name.toLower() : "";
    QString dataType = item ? item->dataType.toLower() : "";
    
    // === SPECIAL CASES BY NAME / ID ===
    
    // SPC (Service Programming Code) - Always show as 6 ASCII digits
    if (itemName.contains("spc")) {
        QString spc;
        for (int i = 0; i < data.size() && i < 6; i++) {
            char c = data[i];
            if (c >= '0' && c <= '9') {
                spc += c;
            } else {
                spc += QString::number((uint8_t)c % 10);
            }
        }
        while (spc.length() < 6) spc += "0";
        return QString("\"%1\"").arg(spc);
    }
    
    // ESN/MEID - Always show as HEX
    if (itemName.contains("esn") || itemName.contains("meid")) {
        QByteArray trimmed = data;
        while (trimmed.size() > 0 && (uint8_t)trimmed[trimmed.size()-1] == 0x00) {
            trimmed.chop(1);
        }
        if (trimmed.isEmpty()) return "0x00000000";
        
        QString hex;
        for (int i = trimmed.size() - 1; i >= 0; i--) {
            hex += QString("%1").arg((uint8_t)trimmed[i], 2, 16, QChar('0')).toUpper();
        }
        return QString("0x%1").arg(hex);
    }
    
    // MCC (ID 176) / MNC (ID 177)
    if (itemId == 176 || itemId == 177 || itemName == "mcc" || itemName == "mnc") {
        if (data.size() >= 2) {
            uint16_t val = ((uint8_t)data[0]) | (((uint8_t)data[1]) << 8);
            if (val == 0xFFFF || val == 0x0000) return "Not Set";
            return QString::number(val);
        }
    }

    // IMEI - BCD format with length byte
    if (itemName.contains("imei")) {
        int startIdx = 0;
        if (data.size() > 1 && (uint8_t)data[0] > 0 && (uint8_t)data[0] < 16) {
            startIdx = 1;
        }
        
        QString result;
        for (int i = startIdx; i < data.size(); i++) {
            uint8_t byte = (uint8_t)data[i];
            if (byte == 0x00 || byte == 0xFF) continue;
            
            uint8_t low = byte & 0x0F;
            uint8_t high = (byte >> 4) & 0x0F;
            
            if (low <= 9) result += QString::number(low);
            if (high <= 9) result += QString::number(high);
        }
        
        if (result.isEmpty()) return "Not Set";
        return result;
    }
    
    // IMSI, MDN, MIN - BCD format
    if (itemName.contains("imsi") || itemName.contains("mdn") || 
        itemName.contains("min") || dataType == "bcd") {
        int startIdx = 0;
        if (data.size() > 1 && (uint8_t)data[0] > 0 && (uint8_t)data[0] < 20) {
            startIdx = 1;
        }
        
        QString result;
        for (int i = startIdx; i < data.size(); i++) {
            uint8_t byte = (uint8_t)data[i];
            if (byte == 0x00 || byte == 0xFF) continue;
            
            uint8_t low = byte & 0x0F;
            uint8_t high = (byte >> 4) & 0x0F;
            
            if (low <= 9) result += QString::number(low);
            if (high <= 9) result += QString::number(high);
        }
        
        if (result.isEmpty()) return "Not Set";
        return result;
    }

    // Explicit String/ASCII types or Text Items
    if (dataType == "string" || dataType == "ascii" || dataType == "text" ||
        itemName.contains("name") || itemName.contains("banner") || 
        itemName.contains("ver") || itemName.contains("carrier")) {
        QString text;
        for (char c : data) {
            if (c == 0) break;
            if (c >= 32 && c <= 126) text += c;
        }
        if (!text.trimmed().isEmpty()) {
            return QString("\"%1\"").arg(text.trimmed());
        }
    }
    
    // Check for contiguous NUL-terminated ASCII string from start
    int validAsciiLen = 0;
    for (int i = 0; i < data.size(); i++) {
        char c = data[i];
        if (c == 0) break;
        if (c >= 32 && c <= 126) {
            validAsciiLen++;
        } else {
            validAsciiLen = 0;
            break;
        }
    }
    if (validAsciiLen >= 3) {
        QString text = QString::fromLatin1(data.left(validAsciiLen));
        return QString("\"%1\"").arg(text);
    }
    
    // === NUMERIC VALUES ===
    
    // 1 byte - show as decimal and hex
    if (data.size() == 1) {
        uint8_t val = (uint8_t)data[0];
        return QString("%1 (0x%2)").arg(val).arg(val, 2, 16, QChar('0')).toUpper();
    }
    
    // 2 bytes - Little Endian uint16
    if (data.size() == 2) {
        uint16_t val = ((uint8_t)data[0]) | (((uint8_t)data[1]) << 8);
        return QString("%1 (0x%2)").arg(val).arg(val, 4, 16, QChar('0')).toUpper();
    }
    
    // 4 bytes - Little Endian uint32
    if (data.size() == 4) {
        uint32_t val = ((uint8_t)data[0]) | 
                      (((uint8_t)data[1]) << 8) |
                      (((uint8_t)data[2]) << 16) | 
                      (((uint8_t)data[3]) << 24);
        return QString("%1 (0x%2)").arg(val).arg(val, 8, 16, QChar('0')).toUpper();
    }
    
    // Default: Show as hex dump
    QString hex = data.toHex(' ').toUpper();
    if (hex.length() > 50) {
        hex = hex.left(47) + "...";
    }
    return QString("[%1B] %2").arg(data.size()).arg(hex);
}


// === Core NV Operations ===

// Core read functions
void NVManager::readNV(uint16_t itemId)
{
    if (!isConnected()) {
        emit logMessage("❌ Device not connected");
        return;
    }
    
    const NVItem *item = getItemInfo(itemId);
    QString itemName = item ? item->name : QString::number(itemId);
    
    emit logMessage(QString("📖 Reading NV %1 (ID: %2)...").arg(itemName).arg(itemId));
    
    m_currentOp = OP_READ;
    m_currentItemId = itemId;
    m_currentIndex = 0;
    m_isExtended = false;
    
    m_responseTimer->start();
    
    // Auto-detect: Use Subsystem command for High IDs
    if (isSubsysItem(itemId)) {
        emit logMessage(QString("📖 Reading Subsystem NV %1...").arg(itemId));
        sendSubsysNVRead(itemId);
    } else {
        emit logMessage(QString("📖 Reading NV %1...").arg(itemId));
        sendNVRead(itemId);
    }
}

void NVManager::readNVExt(uint16_t itemId, uint16_t index)
{
    if (!isConnected()) return;
    
    m_currentOp = OP_READ;
    m_currentItemId = itemId;
    m_currentIndex = index;
    m_isExtended = true;
    
    m_responseTimer->start();
    
    // Auto-detect: Use Subsystem command for High IDs
    if (isSubsysItem(itemId)) {
        emit logMessage(QString("📖 Reading Subsystem NV %1[%2]...").arg(itemId).arg(index));
        sendSubsysNVRead(itemId); // Note: Subsys handles index inside payload
    } else {
        emit logMessage(QString("📖 Reading Extended NV %1[%2]...").arg(itemId).arg(index));
        sendNVReadExt(itemId, index);
    }
}

void NVManager::writeNV(uint16_t itemId, const QByteArray &data)
{
    if (!isConnected()) {
        emit logMessage("❌ Not connected to device");
        emit nvWriteComplete(itemId, false);
        return;
    }
    
    const NVItem *item = getItemInfo(itemId);
    if (item && item->readOnly) {
        emit logMessage(QString("❌ NV %1 is READ-ONLY!").arg(item->name));
        emit nvWriteComplete(itemId, false);
        return;
    }
    
    QString itemName = item ? item->name : QString::number(itemId);
    emit logMessage(QString("📝 Writing NV %1 (ID: %2) - %3 bytes...").arg(itemName).arg(itemId).arg(data.size()));
    
    m_currentOp = OP_WRITE;
    m_currentItemId = itemId;
    m_isExtended = false;
    
    sendNVWrite(itemId, data);
}

void NVManager::writeNVExt(uint16_t itemId, uint16_t index, const QByteArray &data)
{
    if (!isConnected()) {
        emit logMessage("❌ Not connected to device");
        emit nvWriteComplete(itemId, false);
        return;
    }
    
    const NVItem *item = getItemInfo(itemId);
    if (item && item->readOnly) {
        emit logMessage(QString("❌ NV %1 is READ-ONLY!").arg(item->name));
        emit nvWriteComplete(itemId, false);
        return;
    }
    
    QString itemName = item ? item->name : QString::number(itemId);
    emit logMessage(QString("📝 Writing Extended NV %1[%2] - %3 bytes...").arg(itemName).arg(index).arg(data.size()));
    
    m_currentOp = OP_WRITE;
    m_currentItemId = itemId;
    m_currentIndex = index;
    m_isExtended = true;
    
    sendNVWriteExt(itemId, index, data);
}

// === Backup & Restore ===

void NVManager::backupItems(const QList<uint16_t> &itemIds, const QString &filename)
{
    if (!isConnected()) {
        emit logMessage("❌ Not connected to device");
        emit backupComplete(false, filename);
        return;
    }
    
    m_backupQueue.clear();
    m_backupCurrentIndex = 0;
    m_backupFilename = filename;
    
    // Build backup queue
    for (uint16_t id : itemIds) {
        const NVItem *item = getItemInfo(id);
        if (!item) continue;
        
        if (item->isExtended) {
            for (uint16_t idx = 0; idx <= item->maxIndex; idx++) {
                BackupItem bi;
                bi.id = id;
                bi.index = idx;
                bi.isExtended = true;
                bi.name = QString("%1[%2]").arg(item->name).arg(idx);
                m_backupQueue.append(bi);
            }
        } else {
            BackupItem bi;
            bi.id = id;
            bi.index = 0;
            bi.isExtended = false;
            bi.name = item->name;
            m_backupQueue.append(bi);
        }
    }
    
    emit logMessage(QString("💾 Starting backup of %1 items to: %2").arg(m_backupQueue.size()).arg(filename));
    m_currentOp = OP_BACKUP;
    processBackupQueue();
}

void NVManager::cancelOperation()
{
    m_responseTimer->stop();
    m_backupWatchdog->stop();
    m_responseTimer->setInterval(2000);
    m_currentOp = OP_NONE;
    m_backupQueue.clear();
    m_restoreQueue.clear();
    emit logMessage("⚠️ NV Operation Cancelled");
}

void NVManager::backupRange(uint16_t startId, uint16_t endId, const QString &filename)
{
    if (!isConnected()) {
        emit logMessage("❌ Not connected to device");
        emit backupComplete(false, filename);
        return;
    }
    
    m_backupQueue.clear();
    m_backupCurrentIndex = 0;
    m_backupFailCount = 0;     // Initialize counters
    m_backupSkipCount = 0;     // Initialize counters
    m_backupFilename = filename;
    
    emit logMessage(QString("💾 Starting backup of NV range %1 to %2...").arg(startId).arg(endId));
    
    // Set fast 100ms timeout for range backup (unsupported items skip in 100ms)
    m_responseTimer->setInterval(100);

    // Build backup queue for range (use uint32_t to avoid uint16_t overflow on 65535)
    for (uint32_t id = startId; id <= endId; id++) {
        const NVItem *item = getItemInfo(id);
        
        if (id % 100 == 0) QApplication::processEvents(); // Prevent freeze during large range queuing
        
        if (item && item->isExtended) {
            // Extended NV - backup all indices
            for (uint16_t idx = 0; idx <= item->maxIndex; idx++) {
                BackupItem bi;
                bi.id = id;
                bi.index = idx;
                bi.isExtended = true;
                bi.name = QString("%1[%2]").arg(item->name).arg(idx);
                m_backupQueue.append(bi);
            }
        } else {
            // Standard NV (or unknown item)
            BackupItem bi;
            bi.id = id;
            bi.index = 0;
            bi.isExtended = false;
            bi.name = item ? item->name : QString("NV_%1").arg(id);
            m_backupQueue.append(bi);
        }
    }
    
    emit logMessage(QString("📦 Queued %1 NV items for backup").arg(m_backupQueue.size()));
    m_currentOp = OP_BACKUP;
    m_backupWatchdog->start(250); // Start watchdog (250ms deadline per item)
    
    // Start processing async to prevent UI freeze
    QTimer::singleShot(10, this, &NVManager::processBackupQueue);
}


void NVManager::restoreFromFile(const QString &filename)
{
    if (!isConnected()) {
        emit logMessage("❌ Not connected to device");
        emit restoreComplete(false, 0, 0);
        return;
    }
    
    if (!loadRestoreFromFile(filename)) {
        emit logMessage("❌ Failed to load backup file");
        emit restoreComplete(false, 0, 0);
        return;
    }
    
    m_restoreCurrentIndex = 0;
    m_restoreSuccessCount = 0;
    m_restoreFailCount = 0;
    
    emit logMessage(QString("♻️ Starting restore of %1 items from: %2").arg(m_restoreQueue.size()).arg(filename));
    m_currentOp = OP_RESTORE;
    processRestoreQueue();
}

// === Helper Methods ===

void NVManager::sendNVRead(uint16_t id)
{
    QByteArray cmd;
    cmd.append((char)DiagCmd::DIAG_NV_READ_F);
    cmd.append((char)(id & 0xFF));
    cmd.append((char)((id >> 8) & 0xFF));
    cmd.append(QByteArray(128, 0));
    m_protocol->sendCommand(cmd);
}

void NVManager::sendNVReadExt(uint16_t id, uint16_t index)
{
    QByteArray cmd;
    cmd.append((char)DiagCmd::DIAG_NV_READ_EXT_F);
    cmd.append((char)(id & 0xFF));
    cmd.append((char)((id >> 8) & 0xFF));
    cmd.append((char)(index & 0xFF));
    cmd.append((char)((index >> 8) & 0xFF));
    cmd.append(QByteArray(128, 0));
    m_protocol->sendCommand(cmd);
}

void NVManager::sendNVWrite(uint16_t id, const QByteArray &data)
{
    QByteArray cmd;
    cmd.append((char)DiagCmd::DIAG_NV_WRITE_F);
    cmd.append((char)(id & 0xFF));
    cmd.append((char)((id >> 8) & 0xFF));
    cmd.append(data);
    
    // Pad to 128 bytes if needed
    while(cmd.size() < 131) cmd.append((char)0);
    
    m_protocol->sendCommand(cmd);
}

void NVManager::sendNVWriteExt(uint16_t id, uint16_t index, const QByteArray &data)
{
    QByteArray cmd;
    cmd.append((char)DiagCmd::DIAG_NV_WRITE_EXT_F);
    cmd.append((char)(id & 0xFF));
    cmd.append((char)((id >> 8) & 0xFF));
    cmd.append((char)(index & 0xFF));
    cmd.append((char)((index >> 8) & 0xFF));
    cmd.append(data);
    
    // Pad to 128 bytes if needed
    while(cmd.size() < 133) cmd.append((char)0);
    
    m_protocol->sendCommand(cmd);
}

void NVManager::handleResponse(const QByteArray &data)
{
    if (data.isEmpty()) return;
    if (m_currentOp == OP_NONE) return; // Ignore packets when no NV operation is active
    
    // Check for subsystem wrapper prefix (0x13) or direct command byte
    uint8_t firstByte = (uint8_t)data[0];
    uint8_t cmd = firstByte;
    int offset = 0;

    if (firstByte == 0x13 && data.size() > 1) {
        cmd = (uint8_t)data[1]; // Inner DIAG command code (0x26, 0x75, 0x4B, etc.)
        offset = 1;             // 1-byte wrapper prefix offset
    }

    // Extract item ID from packet to ensure response matches m_currentItemId
    uint16_t respItemId = 0xFFFF;
    if (cmd == DiagCmd::DIAG_NV_READ_F || cmd == DiagCmd::DIAG_NV_READ_EXT_F) {
        if (data.size() >= offset + 3) {
            respItemId = (uint8_t)data[offset + 1] | (((uint8_t)data[offset + 2]) << 8);
        }
    } else if (cmd == DiagCmd::DIAG_SUBSYS_CMD_F) {
        if (data.size() >= offset + 6) {
            respItemId = (uint8_t)data[offset + 4] | (((uint8_t)data[offset + 5]) << 8);
        }
    }

    // Check for error responses (0x42 = Bad Parameter, 0x14 = Bad Command)
    if (cmd == 0x42 || cmd == 0x14) {
        m_responseTimer->stop();
        if (m_currentOp == OP_READ) {
            // Auto-fallback: If Standard Read (0x26) failed, try Subsystem Read (0x4B)
            if (m_readTryMode == 0) {
                m_readTryMode = 1;
                emit logMessage(QString("🔄 NV %1: Retrying with Subsystem Read (0x4B)...").arg(m_currentItemId));
                m_responseTimer->start();
                sendSubsysNVRead(m_currentItemId);
                return;
            }
            // If Subsystem Read (0x4B) failed, try Extended Read (0x75)
            else if (m_readTryMode == 1) {
                m_readTryMode = 2;
                emit logMessage(QString("🔄 NV %1: Retrying with Extended Read (0x75)...").arg(m_currentItemId));
                m_responseTimer->start();
                sendNVReadExt(m_currentItemId, m_currentIndex);
                return;
            }
            // If all 3 modes failed
            else {
                emit logMessage(QString("⚠️ NV %1: Item not supported by device").arg(m_currentItemId));
                emit nvReadComplete(m_currentItemId, QByteArray(), false);
                m_currentOp = OP_NONE;
                return;
            }
        } else if (m_currentOp == OP_BACKUP) {
            m_backupFailCount++;
            QTimer::singleShot(5, this, &NVManager::processBackupQueue);
            return;
        }
    }

    // If ID mismatch and not 0xFFFF, ignore stale packet from previous NV ID
    if (respItemId != 0xFFFF && respItemId != m_currentItemId) {
        return;
    }

    // Valid response for current item - stop timeout timer
    m_responseTimer->stop();
    
    // Handle based on current operation
    if (m_currentOp == OP_READ || m_currentOp == OP_BACKUP) {
        if (cmd == DiagCmd::DIAG_NV_READ_F || cmd == DiagCmd::DIAG_NV_READ_EXT_F || cmd == DiagCmd::DIAG_SUBSYS_CMD_F) {
            
            QByteArray nvData;
            int headerSize = 3; // Default for 0x26: CMD (1B) + ID (2B)

            if (cmd == DiagCmd::DIAG_NV_READ_EXT_F) {
                headerSize = 5; // 0x75: CMD (1B) + ID (2B) + Index (2B)
            } else if (cmd == DiagCmd::DIAG_SUBSYS_CMD_F) {
                headerSize = 8; // 0x4B: CMD (1B) + SubsysID (1B) + SubCmd (2B) + ID (2B) + Index (2B)
            }

            int actualDataStart = offset + headerSize;

            if (data.size() <= actualDataStart) {
                // Header only response returned - default to zeros of item size
                int size = 128;
                const NVItem *item = getItemInfo(m_currentItemId);
                if (item && item->size > 0) size = item->size;
                nvData = QByteArray(size, 0);
                emit logMessage(QString("⚠️ Subsystem NV %1: Empty response, defaulting to %2 zeros").arg(m_currentItemId).arg(size));
            } else {
                nvData = data.mid(actualDataStart);
            }

            // Trim to actual size if known
            const NVItem *item = getItemInfo(m_currentItemId);
            if (item && item->size > 0 && nvData.size() > item->size) {
                nvData = nvData.left(item->size);
            }
            
            if (m_currentOp == OP_READ) {
                emit logMessage(QString("✅ NV %1 Read: %2 bytes").arg(m_currentItemId).arg(nvData.size()));
                emit nvReadComplete(m_currentItemId, nvData, true);
                m_currentOp = OP_NONE;
            } else if (m_currentOp == OP_BACKUP) {
                if (m_backupCurrentIndex > 0 && m_backupCurrentIndex <= m_backupQueue.size()) {
                    m_backupQueue[m_backupCurrentIndex - 1].data = nvData;
                }
                QTimer::singleShot(5, this, &NVManager::processBackupQueue);
            }

            return; // Done
        }
    }
    else if (m_currentOp == OP_WRITE || m_currentOp == OP_RESTORE) {
        if (cmd == DiagCmd::DIAG_NV_WRITE_F || cmd == DiagCmd::DIAG_NV_WRITE_EXT_F || cmd == DiagCmd::DIAG_SUBSYS_CMD_F) {
            int statusOffset = offset + ((cmd == DiagCmd::DIAG_NV_WRITE_EXT_F || cmd == DiagCmd::DIAG_SUBSYS_CMD_F) ? 5 : 3);
            bool success = (data.size() > statusOffset && (uint8_t)data[statusOffset] == 0);
            
            if (m_currentOp == OP_WRITE) {
                if (success) {
                    emit logMessage("✅ NV Write successful");
                    emit nvWriteComplete(m_currentItemId, true);
                } else {
                    emit logMessage("❌ NV Write failed");
                    emit nvWriteComplete(m_currentItemId, false);
                }
                m_currentOp = OP_NONE;
            } else if (m_currentOp == OP_RESTORE) {
                if (success) {
                    m_restoreSuccessCount++;
                } else {
                    m_restoreFailCount++;
                }
                processRestoreQueue();
            }
        }
    }
}

void NVManager::processBackupQueue()
{
    // Allow UI to update and prevent freeze
    QApplication::processEvents();
    if (m_currentOp != OP_BACKUP) return;
    
    if (m_backupCurrentIndex >= m_backupQueue.size()) {
        // Backup complete - save to file
        m_responseTimer->stop();
        m_backupWatchdog->stop();
        m_responseTimer->setInterval(2000); // Restore normal timeout
        saveBackupToFile();
        
        int successCount = m_backupCurrentIndex - m_backupFailCount - m_backupSkipCount;
        emit logMessage(QString("✅ Backup complete: %1 success, %2 failed, %3 skipped")
                       .arg(successCount).arg(m_backupFailCount).arg(m_backupSkipCount));
        emit backupComplete(true, m_backupFilename);
        m_currentOp = OP_NONE;
        return;
    }
    
    // Safety check for queue bounds
    if (m_backupCurrentIndex < 0 || m_backupCurrentIndex >= m_backupQueue.size()) {
        m_backupWatchdog->stop();
        emit logMessage("❌ Backup queue index out of bounds!");
        emit backupComplete(false, m_backupFilename);
        m_currentOp = OP_NONE;
        return;
    }
    
    const BackupItem &item = m_backupQueue[m_backupCurrentIndex];
    
    // Show progress with error stats
    int successCount = m_backupCurrentIndex - m_backupFailCount - m_backupSkipCount;
    emit backupProgress(m_backupCurrentIndex + 1, m_backupQueue.size(), 
                       QString("%1 (%2 OK, %3 ERR)").arg(item.name).arg(successCount).arg(m_backupFailCount));
    
    m_currentItemId = item.id;
    m_currentIndex = item.index;
    m_isExtended = item.isExtended;
    
    m_backupCurrentIndex++;
    
    // Reset watchdog deadline (250ms) for this exact item
    m_backupWatchdog->start(250);

    // Explicitly reset & start timeout timer for this exact item
    m_responseTimer->stop();
    m_responseTimer->start();
    
    if (item.isExtended) {
        sendNVReadExt(item.id, item.index);
    } else {
        sendNVRead(item.id);
    }
}


void NVManager::processRestoreQueue()
{
    if (m_restoreCurrentIndex >= m_restoreQueue.size()) {
        // Restore complete
        emit logMessage(QString("✅ Restore complete: %1 success, %2 failed")
                       .arg(m_restoreSuccessCount).arg(m_restoreFailCount));
        emit restoreComplete(true, m_restoreSuccessCount, m_restoreFailCount);
        m_currentOp = OP_NONE;
        return;
    }
    
    const BackupItem &item = m_restoreQueue[m_restoreCurrentIndex];
    emit restoreProgress(m_restoreCurrentIndex + 1, m_restoreQueue.size(), item.name);
    
    m_currentItemId = item.id;
    m_currentIndex = item.index;
    m_isExtended = item.isExtended;
    
    if (item.isExtended) {
        sendNVWriteExt(item.id, item.index, item.data);
    } else {
        sendNVWrite(item.id, item.data);
    }
    
    m_restoreCurrentIndex++;
}

void NVManager::saveBackupToFile()
{
    // Determine format based on file extension
    bool isBinaryFormat = m_backupFilename.toLower().endsWith(".nv");
    
    if (isBinaryFormat) {
        // Binary .nv format
        QFile file(m_backupFilename);
        if (!file.open(QIODevice::WriteOnly)) {
            emit logMessage(QString("❌ Failed to create .nv file: %1").arg(m_backupFilename));
            return;
        }
        
        // Header: "NVBK" + Version
        file.write("NVBK", 4);
        file.write("\x01", 1); // Version 1
        
        // Item count (uint32_t, little-endian)
        uint32_t count = m_backupQueue.size();
        file.write((char*)&count, 4);
        
        // Write each item
        for (const BackupItem &bi : m_backupQueue) {
            // ID (uint16_t)
            file.write((char*)&bi.id, 2);
            
            // Index (uint16_t) - for extended NV
            file.write((char*)&bi.index, 2);
            
            // Extended flag (uint8_t)
            uint8_t extFlag = bi.isExtended ? 1 : 0;
            file.write((char*)&extFlag, 1);
            
            // Data size (uint16_t)
            uint16_t dataSize = bi.data.size();
            file.write((char*)&dataSize, 2);
            
            // Data
            if (dataSize > 0) {
                file.write(bi.data);
            }
        }
        
        file.close();
        emit logMessage(QString("💾 Saved %1 items to binary .nv file").arg(count));
        
    } else {
        // JSON format (original)
        QJsonObject root;
        root["version"] = "1.0";
        root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["total_items"] = m_backupQueue.size();
        
        QJsonArray itemsArray;
        for (const BackupItem &bi : m_backupQueue) {
            QJsonObject itemObj;
            itemObj["id"] = bi.id;
            itemObj["name"] = bi.name;
            itemObj["index"] = bi.index;
            itemObj["extended"] = bi.isExtended;
            itemObj["value"] = QString(bi.data.toHex());
            itemsArray.append(itemObj);
        }
        
        root["nv_items"] = itemsArray;
        
        QFile file(m_backupFilename);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            file.close();
            emit logMessage(QString("💾 Saved %1 items to JSON file").arg(m_backupQueue.size()));
        }
    }
}

bool NVManager::loadRestoreFromFile(const QString &filename)
{
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    m_restoreQueue.clear();
    
    // Check file format
    bool isBinaryFormat = filename.toLower().endsWith(".nv");
    
    if (isBinaryFormat) {
        // Binary .nv format
        QByteArray data = file.readAll();
        file.close();
        
        if (data.size() < 9) return false; // Minimum: header + version + count
        
        // Verify header
        if (data.mid(0, 4) != "NVBK") {
            emit logMessage("❌ Invalid .nv file header");
            return false;
        }
        
        uint8_t version = data[4];
        if (version != 0x01) {
            emit logMessage(QString("❌ Unsupported .nv version: %1").arg(version));
            return false;
        }
        
        // Read item count
        uint32_t count = *((uint32_t*)(data.data() + 5));
        int offset = 9; // After header + version + count
        
        emit logMessage(QString("📂 Loading %1 items from .nv file...").arg(count));
        
        // Read each item
        for (uint32_t i = 0; i < count && offset < data.size(); i++) {
            if (offset + 7 > data.size()) break; // Minimum item header
            
            BackupItem bi;
            bi.id = *((uint16_t*)(data.data() + offset));
            offset += 2;
            
            bi.index = *((uint16_t*)(data.data() + offset));
            offset += 2;
            
            // Fix ambiguous comparison: explicitly cast char to int
            bi.isExtended = ((int)data[offset] == 1); 
            offset++;
            
            uint16_t dataSize = *((uint16_t*)(data.data() + offset));
            offset += 2;
            
            if (offset + dataSize > data.size()) break;
            
            bi.data = data.mid(offset, dataSize);
            offset += dataSize;
            
            bi.name = QString("NV_%1").arg(bi.id);
            m_restoreQueue.append(bi);
        }
        
    } else {
        // JSON format (original)
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        
        if (!doc.isObject()) return false;
        
        QJsonObject root = doc.object();
        QJsonArray itemsArray = root["nv_items"].toArray();
        
        for (const QJsonValue &val : itemsArray) {
            QJsonObject itemObj = val.toObject();
            
            BackupItem bi;
            bi.id = itemObj["id"].toInt();
            bi.name = itemObj["name"].toString();
            bi.index = itemObj["index"].toInt();
            bi.isExtended = itemObj["extended"].toBool();
            bi.data = QByteArray::fromHex(itemObj["value"].toString().toLatin1());
            
            m_restoreQueue.append(bi);
        }
    }
    
    return !m_restoreQueue.isEmpty();
}

// === Subsystem Command Implementation (Phase 1) ===

bool NVManager::isSubsysItem(uint16_t id) const
{
    // Heuristic: IDs > 4000 or specific ranges often use Subsystem commands
    return (id >= 4000); 
}

void NVManager::sendSubsysNVRead(uint16_t id)
{
    // Structure: [CMD 0x4B] [SubsysID 0x0B] [SubCommand 0x0026] [ItemID 2 bytes] [Reserved/Index 2 bytes]
    QByteArray cmd;
    cmd.append((char)DiagCmd::DIAG_SUBSYS_CMD_F);
    cmd.append((char)SubsysId::SUBSYS_NV);
    
    // Subsys Command: NV_READ_EXT (0x0026) - Little Endian
    cmd.append((char)(SubsysCmd::NV_READ_EXT & 0xFF));
    cmd.append((char)(SubsysCmd::NV_READ_EXT >> 8));
    
    // Item ID - Little Endian
    cmd.append((char)(id & 0xFF));
    cmd.append((char)(id >> 8));
    
    // Index/Reserved (0 for now) - Little Endian
    cmd.append((char)0x00);
    cmd.append((char)0x00);
    
    m_protocol->sendCommand(cmd);
}

void NVManager::sendSubsysNVWrite(uint16_t id, const QByteArray &data)
{
    // Structure: [CMD 0x4B] [SubsysID 0x0B] [SubCommand 0x0027] [ItemID 2 bytes] [Index 2 bytes] [Data...]
    QByteArray cmd;
    cmd.append((char)DiagCmd::DIAG_SUBSYS_CMD_F);
    cmd.append((char)SubsysId::SUBSYS_NV);
    
    // Subsys Command: NV_WRITE_EXT (0x0027)
    cmd.append((char)(SubsysCmd::NV_WRITE_EXT & 0xFF));
    cmd.append((char)(SubsysCmd::NV_WRITE_EXT >> 8));
    
    // Item ID
    cmd.append((char)(id & 0xFF));
    cmd.append((char)(id >> 8));
    
    // Index (0)
    cmd.append((char)0x00);
    cmd.append((char)0x00);
    
    // Data
    cmd.append(data);
    
    m_protocol->sendCommand(cmd);
}

// === Custom Display Implementation ===

NVManager::FormattedValue NVManager::formatNVValueFull(uint16_t itemId, const QByteArray &data) const
{
    Q_UNUSED(itemId); // Suppress unused warning
    FormattedValue fv;
    
    if (data.isEmpty()) {
        fv.hex = "[Empty]";
        fv.text = "";
        fv.binary = "";
        fv.decimal = "";
        return fv;
    }
    
    // 1. Hex
    fv.hex = data.toHex(' ').toUpper();
    
    // 2. Text (ASCII)
    QString textStr;
    for (char c : data) {
        if (c >= 32 && c <= 126) textStr += c;
        else textStr += '.';
    }
    fv.text = textStr;
    
    // 3. Binary & Decimal
    if (data.size() <= 8) {
        // Treat as number for small sizes
        uint64_t val = 0;
        // Little Endian read
        for(int i = 0; i < data.size(); i++) {
            val |= ((uint64_t)((uint8_t)data[i])) << (i * 8);
        }
        
        fv.decimal = QString::number(val);
        fv.binary = QString::number(val, 2);
    } else {
        fv.decimal = "[Too Long]";
        fv.binary = "[Too Long]";
    }
    
    return fv;
}

void NVManager::onTimeout()
{
    emit logMessage(QString("⏱️ Timeout on NV %1 - skipping").arg(m_currentItemId));
    
    if (m_currentOp == OP_BACKUP) {
        m_backupSkipCount++;
        QTimer::singleShot(1, this, &NVManager::processBackupQueue); // Instant skip to next item
    } else if (m_currentOp == OP_RESTORE) {
        m_restoreFailCount++;
        processRestoreQueue();
    } else if (m_currentOp == OP_READ) {
        emit nvReadComplete(m_currentItemId, QByteArray(), false);
        m_currentOp = OP_NONE;
    } else if (m_currentOp == OP_WRITE) {
        emit nvWriteComplete(m_currentItemId, false);
        m_currentOp = OP_NONE;
    }
}
