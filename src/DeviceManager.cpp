#include "include/DeviceManager.h"
#include <QDebug>
#include <QThread>

DeviceManager::DeviceManager(QObject *parent) : QObject(parent)
{
    m_state = STATE_IDLE;
    m_subscriptionIndex = 0;
    m_protocol = new DiagProtocol(this);
    connect(m_protocol, &DiagProtocol::rawLog, this, &DeviceManager::logMessage);
    connect(m_protocol, &DiagProtocol::responseReceived, this, &DeviceManager::handleResponse);
}

void DeviceManager::connectDevice(const QString &port)
{
    if (m_protocol->openPort(port)) {
        emit connectionChanged(true);
        emit logMessage("✅ Connected to " + port);
        // Init Handshake
        m_protocol->sendCommand(QByteArray::fromHex("7E"));
    } else {
        emit connectionChanged(false);
        emit logMessage("❌ Failed to connect to " + port);
    }
}

void DeviceManager::disconnectDevice()
{
    m_jobQueue.clear();
    m_protocol->closePort();
    m_state = STATE_IDLE;
    emit connectionChanged(false);
    emit logMessage("🔌 Disconnected");
}

bool DeviceManager::isConnected() const
{
    return m_protocol && m_protocol->isConnected();
}

// --- Public API ---
void DeviceManager::readIdentity() {
    DeviceJob job; job.type = JOB_IDENTITY;
    m_jobQueue.enqueue(job);
    processNextJob();
}

void DeviceManager::listEfsDirectory(const QString &path) {
    DeviceJob job; job.type = JOB_LIST_EFS; job.path = path;
    m_jobQueue.enqueue(job);
    processNextJob();
}

void DeviceManager::readFile(const QString &path) {
    DeviceJob job; job.type = JOB_READ_FILE; job.path = path;
    m_jobQueue.enqueue(job);
    processNextJob();
}

void DeviceManager::writeFile(const QString &path, const QByteArray &data) {
    DeviceJob job; job.type = JOB_WRITE_FILE; job.path = path; job.data = data;
    m_jobQueue.enqueue(job);
    processNextJob();
}

void DeviceManager::deleteFile(const QString &path) {
    DeviceJob job; job.type = JOB_DELETE_FILE; job.path = path;
    m_jobQueue.enqueue(job);
    processNextJob();
}

void DeviceManager::cancelOperation() {
    m_jobQueue.clear();
    m_state = STATE_IDLE;
    m_currentJob = DeviceJob();
    
    // Clear current operation data
    m_efsPath.clear();
    m_efsSeq = 0;
    m_filePath.clear();
    m_writeBuffer.clear();
    m_fileOffset = 0;

    emit logMessage("⚠️ All operations cancelled - State reset to IDLE");
}

// void DeviceManager::resetDevice()
// {
//     emit logMessage("Resetting device...");
//     m_protocol->sendCommand(QByteArray::fromHex("29"));
// }

// Advanced Device Commands
void DeviceManager::zeroSPC()
{
    if (!isConnected()) {
        emit logMessage("❌ Not connected!");
        return;
    }
    
    emit logMessage("🔓 Sending Zero SPC command...");
    m_protocol->sendRawHDLC(QByteArray::fromHex("4B0B24005D0200000E00550006003030303030300B3E7E"));
    QThread::msleep(100);
    emit logMessage("✅ Zero SPC sent");
}

void DeviceManager::bypassSecurity()
{
    if (!isConnected()) {
        emit logMessage("❌ Not connected!");
        return;
    }
    
    emit logMessage("🔓 Sending Bypass Security commands...");
    
    QStringList bypassCmds = {
        "41303030303030DF8A7E", "46FFFFFFFFFFFFFFFFFE747E",
        "465903365113726913282B7E", "462013051320130909BC4A7E",
        "46201211212013121994247E", "462009031920090615BAED7E",
        "462FF811282FF9F32337A97E", "46201411242015020217B77E",
        "4620100316197807215C727E", "462013032720130823073C7E",
        "462015062920150831BFF47E"
    };
    
    for (const QString &cmd : bypassCmds) {
        m_protocol->sendRawHDLC(QByteArray::fromHex(cmd.toLatin1()));
        QThread::msleep(50);
    }
    
    emit logMessage("✅ Bypass Security complete");
}

void DeviceManager::offlineA()
{
    if (!isConnected()) return;
    emit logMessage("🔌 Offline Mode A - Please disconnect device");
    m_protocol->sendRawHDLC(QByteArray::fromHex("290000E9597E"));
}

void DeviceManager::offlineD()
{
    if (!isConnected()) return;
    emit logMessage("🔌 Offline Mode D - Please disconnect device");
    m_protocol->sendRawHDLC(QByteArray::fromHex("29010031407E"));
}

void DeviceManager::rebootDevice()
{
    if (!isConnected()) return;
    emit logMessage("🔄 Rebooting device...");
    m_protocol->sendRawHDLC(QByteArray::fromHex("290200596A7E"));
}

void DeviceManager::powerOff()
{
    if (!isConnected()) return;
    emit logMessage("⚡ Powering off device...");
    m_protocol->sendRawHDLC(QByteArray::fromHex("290600390D7E"));
}

void DeviceManager::sendCustomSPC(const QString &spc)
{
    if (!isConnected() || spc.length() != 6) {
        emit logMessage("❌ SPC must be 6 digits!");
        return;
    }
    
    emit logMessage("📤 Sending custom SPC: " + spc);
    
    QByteArray cmd;
    cmd.append((char)0x41);
    
    for (int i = 0; i < 6; i++) {
        int digit = spc[i].digitValue();
        cmd.append((char)digit);
    }
    
    m_protocol->sendCommand(cmd);
}

void DeviceManager::sendCustomPWD(const QString &pwd)
{
    if (!isConnected() || pwd.length() != 16) {
        emit logMessage("❌ PWD must be 16 digits!");
        return;
    }
    
    emit logMessage("📤 Sending custom PWD");
    
    QByteArray cmd;
    cmd.append((char)0x46);
    cmd.append((char)0x20);
    
    for (int i = 0; i < 16; i += 2) {
        int high, low;
        
        QChar c1 = pwd[i].toUpper();
        if (c1.isDigit()) {
            high = c1.digitValue();
        } else if (c1 >= 'A' && c1 <= 'F') {
            high = 10 + (c1.unicode() - 'A');
        } else {
            high = 0;
        }
        
        QChar c2 = pwd[i+1].toUpper();
        if (c2.isDigit()) {
            low = c2.digitValue();
        } else if (c2 >= 'A' && c2 <= 'F') {
            low = 10 + (c2.unicode() - 'A');
        } else {
            low = 0;
        }
        
        cmd.append((char)((high << 4) | low));
    }
    
    m_protocol->sendCommand(cmd);
}

void DeviceManager::abortCurrentJob() {
    if(m_state == STATE_IDLE) return;

    emit logMessage("⚠️ Aborting current job");

    if (m_currentJob.type == JOB_READ_FILE) {
        emit fileDataReceived(m_filePath, QByteArray());
    } else if (m_currentJob.type == JOB_WRITE_FILE) {
        emit fileWriteComplete(m_filePath, false);
    } else if (m_currentJob.type == JOB_DELETE_FILE) {
        emit fileDeleteComplete(m_filePath, false);
    } else if (m_currentJob.type == JOB_LIST_EFS) {
        emit efsListComplete(m_efsPath, false);
    }

    m_state = STATE_IDLE;
    processNextJob();
}
#include <QThread>
// --- Job Processor ---
void DeviceManager::processNextJob()
{
    if (m_state != STATE_IDLE) return;
    if (m_jobQueue.isEmpty()) return;

    m_currentJob = m_jobQueue.dequeue();

    switch (m_currentJob.type) {
    case JOB_IDENTITY:
        m_tempInfo.clear();
        m_state = STATE_INFO_SPC;
        emit logMessage("Reading Identity...");
        
        // Clear buffer before critical SPC command
        m_protocol->clearBuffer();
        QThread::msleep(50);
        
        // Send SPC & PWD reset commands (payload only - CRC/HDLC added by sendCommand)
        emit logMessage("Sending SPC/PWD reset commands...");
        
        // Command 1: Reset SPC/PWD (subsystem command)
        // Original: 4B0B2400780200000E00550080000030303030303065887D5E355C7E
        // Payload only (without CRC 65887D5E355C and HDLC 7E):
        m_protocol->sendCommand(QByteArray::fromHex("4B0B2400780200000E0055008000003030303030"));
        QThread::msleep(100);
        
        // Command 2: Reset SPC/PWD confirmation
        // Original: 4B0B24005D0200000E00550006003030303030300B3E7D5E355C7E
        // Payload only (without CRC 0B3E7D5E355C and HDLC 7E):
        m_protocol->sendCommand(QByteArray::fromHex("4B0B24005D0200000E005500060030303030303030"));
        QThread::msleep(100);
        
        // Command 3: Password reset (SPR)
        // Original: 462013032720130823073C7E
        // Payload only (without CRC 23073C and HDLC 7E):
        m_protocol->sendCommand(QByteArray::fromHex("4620130327201308"));
        QThread::msleep(100);
        
        // Now send SPC command
        emit logMessage("Sending SPC command...");
        m_protocol->sendCommand(QByteArray::fromHex("41000000000000"));
        break;

    case JOB_LIST_EFS:
        m_efsPath = m_currentJob.path;
        m_state = STATE_EFS_OPEN;
        sendEfsOpen(m_efsPath);
        break;

    case JOB_READ_FILE:
        m_filePath = m_currentJob.path;
        m_fileData.clear();
        m_fileOffset = 0;
        m_state = STATE_FILE_OPEN;
        emit logMessage("Reading: " + m_filePath);
        sendFileOpen(m_filePath, 0, 0); // O_RDONLY
        break;

    case JOB_WRITE_FILE:
    {
        m_filePath = m_currentJob.path;
        m_writeBuffer = m_currentJob.data;
        m_fileOffset = 0;
        m_state = STATE_FILE_OPEN;
        emit logMessage("Writing: " + m_filePath + " (" + QString::number(m_writeBuffer.size()) + " B)");

        // QLIB uses paths WITHOUT leading "/" for write operations
        QString writePath = m_filePath;
        if(writePath.startsWith("/")) writePath.remove(0, 1);

        // Try 0x242 = O_WRONLY(1) | O_CREAT(0x40) | O_TRUNC(0x200)
        // Some devices prefer this over O_RDWR
        sendFileOpen(writePath, 0x242, 0x1FF);
        break;
    }

    case JOB_DELETE_FILE:
        emit logMessage("Deleting: " + m_currentJob.path);
        sendFileUnlink(m_currentJob.path);
        m_state = STATE_FILE_CLOSE; // Wait for Generic Resp
        break;

    default: break;
    }
}

// --- Response Handler ---
void DeviceManager::handleResponse(const QByteArray &data)
{
    if (data.isEmpty()) return;
    uint8_t cmd = (uint8_t)data[0];

    auto finishJob = [this]() {
        m_state = STATE_IDLE;
        processNextJob();
    };

    switch (m_state) {
    case STATE_INFO_SPC:
        // Wait for SPC response (0x41) or subsystem (0x13) or error (0x14/0x42). Ignore preamble reset responses (0x4B, 0x46).
        if (cmd == 0x41 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            m_state = STATE_INFO_IMEI;
            sendNVRead(550);
        }
        break;
    case STATE_INFO_IMEI:
        if (cmd == 0x26 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if (cmd == 0x26 && data.size() > 3) {
                QString imeiStr = decodeBCD(data.mid(3));
                bool isValid = (imeiStr.length() >= 14);
                bool allZeros = true;
                for (QChar c : imeiStr) {
                    if (c != '0') { allZeros = false; break; }
                }
                if (isValid && !allZeros) {
                    m_tempInfo["imei"] = imeiStr;
                    qDebug() << "✓ IMEI 1 Set to:" << imeiStr;
                } else {
                    m_tempInfo["imei"] = "-";
                    qDebug() << "✗ IMEI 1 is invalid or all zeros";
                }
            } else {
                m_tempInfo["imei"] = "-";
            }
            m_state = STATE_INFO_IMEI2;
            sendNVReadExt(550, 1);
        }
        break;
    case STATE_INFO_IMEI2:
        qDebug() << "IMEI2 Response - CMD:" << QString::number(cmd, 16) << "Data:" << data.toHex();

        if (data.size() > 10) {
            int offset = 3;
            if (cmd == 0x13 && data.size() > 1 && (uint8_t)data[1] == 0x75) {
                offset = 6;
            } else if (cmd == 0x75) {
                offset = 5;
            } else if (cmd == 0x26) {
                offset = 3;
            }

            if (data.size() > offset + 9) {
                QByteArray imeiData = data.mid(offset, 9);
                QString i = decodeBCD(imeiData);

                bool isValid = (i.length() >= 14);
                bool allZeros = true;
                for (QChar c : i) {
                    if (c != '0') { allZeros = false; break; }
                }

                if (isValid && !allZeros) {
                    m_tempInfo["imei2"] = i;
                    m_tempInfo["dual_sim"] = true;
                    qDebug() << "✓ IMEI2 Set to:" << i;
                } else {
                    qDebug() << "✗ IMEI2 rejected - Invalid or all zeros";
                }
            }
        }
        
        m_state = STATE_INFO_IMSI;
        sendNVRead(1192);
        break;
    case STATE_INFO_IMEI2_ALT1:
    case STATE_INFO_IMEI2_ALT2:
        m_state = STATE_INFO_IMSI;
        sendNVRead(1192);
        break;
    case STATE_INFO_IMSI:
        if (cmd == 0x26 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if (cmd == 0x26 && data.size() > 3) {
                QByteArray raw = data.mid(3, 10);
                bool isEmpty = true;
                for(int i=0; i<qMin(raw.size(), 10); i++) {
                    uint8_t b = (uint8_t)raw[i];
                    if(b != 0xFF && b != 0x00) { isEmpty = false; break; }
                }

                if(isEmpty) {
                    m_tempInfo["imsi"] = "-";
                } else {
                    QString val = decodeBCD(raw);
                    bool allZeros = true;
                    for (QChar c : val) {
                        if (c != '0') { allZeros = false; break; }
                    }
                    m_tempInfo["imsi"] = allZeros ? "-" : val;
                }
            } else {
                m_tempInfo["imsi"] = "-";
            }
            m_state = STATE_INFO_STATUS;
            m_protocol->sendCommand(QByteArray::fromHex("0C"));
        }
        break;
    case STATE_INFO_STATUS:
        if (cmd == 0x0C || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if ((cmd == 0x0C || cmd == 0x13) && data.size() >= 2) {
                uint8_t s = (data.size() >= 9) ? (uint8_t)data[8] : 0x03;
                QString stat;
                if(s == 0x03 || s == 0x01) stat = "Online";
                else if(s == 0x00) stat = "Offline";
                else stat = "Mode " + QString::number(s);
                m_tempInfo["mode"] = stat;
            } else {
                m_tempInfo["mode"] = "Online";
            }
            m_state = STATE_INFO_ESN;
            sendNVRead(0);
        }
        break;
    case STATE_INFO_ESN:
        if (cmd == 0x26 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if (cmd == 0x26 && data.size() >= 7) {
                QByteArray raw = data.mid(3, 4);
                std::reverse(raw.begin(), raw.end());
                m_tempInfo["esn"] = raw.toHex().toUpper();
            } else {
                m_tempInfo["esn"] = "-";
            }
            m_state = STATE_INFO_MEID;
            sendNVRead(1943);
        }
        break;
    case STATE_INFO_MEID:
        if (cmd == 0x26 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if (cmd == 0x26 && data.size() >= 10) {
                QByteArray raw = data.mid(3, 7);
                std::reverse(raw.begin(), raw.end());
                m_tempInfo["meid"] = raw.toHex().toUpper();
            } else {
                m_tempInfo["meid"] = "-";
            }
            m_state = STATE_INFO_VERSION;
            m_protocol->sendCommand(QByteArray::fromHex("7C"));
        }
        break;
    case STATE_INFO_VERSION:
        if (cmd == 0x7C || cmd == 0x00 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if ((cmd == 0x7C || cmd == 0x00 || cmd == 0x13) && data.size() > 1) {
                QString ver;
                for(char c : data) {
                    if(c >= 32 && c <= 126) ver.append(c);
                }
                m_tempInfo["version"] = ver.trimmed();
            } else {
                m_tempInfo["version"] = "-";
            }
            m_state = STATE_INFO_MDN;
            sendNVRead(178);
        }
        break;
    case STATE_INFO_MDN:
        if (cmd == 0x26 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if (cmd == 0x26 && data.size() > 3) {
                QString mdnAscii;
                for(int i=4; i<qMin(data.size(), 14); ++i) {
                    char c = data[i];
                    if(c >= 32 && c <= 126) mdnAscii.append(c);
                }
                if (mdnAscii.length() > 5) {
                    m_tempInfo["mdn"] = mdnAscii;
                } else {
                    m_tempInfo["mdn"] = decodeBCDInfo(data.mid(3), 10);
                }
            } else {
                m_tempInfo["mdn"] = "-";
            }
            m_state = STATE_INFO_BANNER;
            sendNVRead(71);
        }
        break;
    case STATE_INFO_BANNER:
        if (cmd == 0x26 || cmd == 0x13 || cmd == 0x14 || cmd == 0x42) {
            if (cmd == 0x26 && data.size() > 3) {
                QString banner;
                for(int i=3; i<data.size() && data[i]!=0; ++i) {
                    char c = data[i];
                    if(c >= 32 && c <= 126) banner.append(c);
                }
                m_tempInfo["banner"] = banner.trimmed();
            } else {
                m_tempInfo["banner"] = "-";
            }
            emit identityReceived(m_tempInfo);
            finishJob();
        }
        break;

    // EFS
    case STATE_EFS_OPEN:
        if (cmd == 0x4B && data.size() >= 8) {
            uint32_t err = (uint8_t)data[8];
            if (err == 0) {
                m_efsHandle = (uint8_t)data[4] | ((uint8_t)data[5] << 8);
                m_state = STATE_EFS_READ;
                m_efsSeq = 1;
                sendEfsRead(m_efsHandle, m_efsSeq);
            } else {
                emit logMessage("Dir Open Failed. Err: " + QString::number(err));
                emit efsListComplete(m_efsPath,false);
                finishJob();
            }
        }
        break;
    case STATE_EFS_READ:
        if (cmd == 0x4B) processEfsStep(data);
        break;

    case STATE_EFS_CLOSE:
        if (cmd == 0x4B) {
            emit efsListComplete(m_efsPath, true);
            emit logMessage("✅ Listings Complete: " + m_efsPath);
            finishJob();
        }
        break;

    // File
    case STATE_FILE_OPEN:
        if (cmd == 0x4B && data.size() >= 12) {
            uint32_t err = (uint8_t)data[8] | ((uint8_t)data[9] << 8);
            if (err == 0) {
                m_fileHandle = (uint8_t)data[4] | ((uint8_t)data[5] << 8) | ((uint8_t)data[6] << 16) | ((uint8_t)data[7] << 24);

                if (m_currentJob.type == JOB_WRITE_FILE) {
                    m_state = STATE_FILE_WRITE;
                    int chunk = qMin(m_writeBuffer.size(), 512);
                    sendFileWrite(m_fileHandle, m_fileOffset, m_writeBuffer.mid(0, chunk));
                } else {
                    m_state = STATE_FILE_READ;
                    sendFileRead(m_fileHandle, 1024, m_fileOffset);
                }
            } else {
                emit logMessage("File Open Failed. Err: " + QString::number(err));

                // Critical: Emit completion signals even on failure!
                if (m_currentJob.type == JOB_READ_FILE) {
                    emit fileDataReceived(m_filePath, QByteArray());
                } else if (m_currentJob.type == JOB_WRITE_FILE) {
                    emit fileWriteComplete(m_filePath, false); // Failed
                } else if (m_currentJob.type == JOB_DELETE_FILE) {
                    emit fileDeleteComplete(m_filePath, false); // Failed
                }

                finishJob();
            }
        }
        break;

    case STATE_FILE_READ:
        if (cmd == 0x4B) processFileStep(data);
        break;

    case STATE_FILE_WRITE:
        if (cmd == 0x4B && data.size() >= 20) {
            uint32_t written = (uint8_t)data[12] | ((uint8_t)data[13]<<8);
            uint32_t err = (uint8_t)data[16];

            if (err != 0) {
                emit logMessage("Write Error: " + QString::number(err));
                m_state = STATE_FILE_CLOSE;
                sendFileClose(m_fileHandle);
                return;
            }

            m_fileOffset += written;
            if (written < (uint32_t)m_writeBuffer.size()) {
                m_writeBuffer.remove(0, written);
                int chunk = qMin(m_writeBuffer.size(), 512);
                sendFileWrite(m_fileHandle, m_fileOffset, m_writeBuffer.mid(0, chunk));
            } else {
                m_state = STATE_FILE_CLOSE;
                sendFileClose(m_fileHandle);
            }
        }
        break;

    case STATE_FILE_CLOSE:
        if (cmd == 0x4B) {
            if (m_currentJob.type == JOB_READ_FILE) {
                emit fileDataReceived(m_filePath, m_fileData);
                emit logMessage("✅ Read Done: " + QString::number(m_fileData.size()) + " bytes");
            } else if (m_currentJob.type == JOB_WRITE_FILE) {
                emit fileWriteComplete(m_filePath, true); // Success
                emit logMessage("✅ Write Done.");
            } else if (m_currentJob.type == JOB_DELETE_FILE) {
                emit fileDeleteComplete(m_filePath, true); // Success
                emit logMessage("✅ Delete Done.");
            }
            finishJob();
        }
        break;

    default:
        // Intelligently handle unexpected responses
        // Advanced commands (raw HDLC) don't need state machine processing
        if (m_state == STATE_IDLE) {
            // When DeviceManager is idle, responses belong to raw commands or NVManager — return silently
            return;
        }
        
        qDebug() << "⚠️ Unexpected response - State:" << m_state << "CMD:" << QString::number(cmd, 16);
        if (m_state >= STATE_INFO_SPC && m_state <= STATE_INFO_BANNER) {
            if (m_state == STATE_INFO_IMEI2 || m_state == STATE_INFO_IMEI2_ALT1 || m_state == STATE_INFO_IMEI2_ALT2) {
                m_state = STATE_INFO_IMSI;
                sendNVRead(1192);
            } else {
                finishJob();
            }
        }
        break;
    }
}

void DeviceManager::processEfsStep(const QByteArray &data)
{
    if (data.size() < 40) {
        m_state = STATE_EFS_CLOSE;
        sendEfsClose(m_efsHandle);
        return;
    }

    uint32_t err = (uint8_t)data[12] | ((uint8_t)data[13] << 8);
    if (err != 0) {
        m_state = STATE_EFS_CLOSE;
        sendEfsClose(m_efsHandle);
        return;
    }

    EfsEntry entry;
    int nameStart = -1;
    for (int i=40; i<data.size(); i++) {
        if (data.at(i) != 0) { nameStart = i; break; }
    }

    if (nameStart != -1) {
        entry.name = QString::fromLatin1(data.mid(nameStart).constData()).trimmed();

        if (entry.name.isEmpty()) {
            m_state = STATE_EFS_CLOSE;
            sendEfsClose(m_efsHandle);
            return;
        }

        int type = (uint8_t)data[16];
        entry.isDir = (type == 1);
        entry.size = (uint8_t)data[20] | ((uint8_t)data[21] << 8) | ((uint8_t)data[22] << 16) | ((uint8_t)data[23] << 24);

        emit efsEntryReceived(m_efsPath, entry);

        if (m_efsSeq > 5000) {
            m_state = STATE_EFS_CLOSE;
            sendEfsClose(m_efsHandle);
            return;
        }

        m_efsSeq++;
        sendEfsRead(m_efsHandle, m_efsSeq);
    } else {
        m_state = STATE_EFS_CLOSE;
        sendEfsClose(m_efsHandle);
    }
}

void DeviceManager::processFileStep(const QByteArray &data)
{
    if (data.size() < 20) return;
    uint32_t bytesRead = (uint8_t)data[12] | ((uint8_t)data[13] << 8);

    if (bytesRead == 0) {
        m_state = STATE_FILE_CLOSE;
        sendFileClose(m_fileHandle);
        return;
    }

    int headerSize = 20;
    if (data.size() > headerSize) {
        m_fileData.append(data.mid(headerSize, bytesRead));
        m_fileOffset += bytesRead;
        sendFileRead(m_fileHandle, 1024, m_fileOffset);
    } else {
        m_state = STATE_FILE_CLOSE;
        sendFileClose(m_fileHandle);
    }
}

// Helpers
void DeviceManager::sendNVRead(uint16_t id) {
    QByteArray cmd; cmd.append((char)0x26); cmd.append((char)(id&0xFF)); cmd.append((char)((id>>8)&0xFF)); cmd.append(QByteArray(128,0));
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendNVReadExt(uint16_t id, uint16_t index) {
    // CMD 0x75: Extended NV Read
    // Format: [CMD] [NV_ID_LOW] [NV_ID_HIGH] [INDEX_LOW] [INDEX_HIGH] [BUFFER(128)]
    QByteArray cmd;
    cmd.append((char)0x75);                    // Extended NV Read command
    cmd.append((char)(id&0xFF));               // NV ID low byte
    cmd.append((char)((id>>8)&0xFF));          // NV ID high byte
    cmd.append((char)(index&0xFF));            // Index low byte
    cmd.append((char)((index>>8)&0xFF));       // Index high byte
    cmd.append(QByteArray(128, 0));            // Buffer
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendEfsOpen(const QString &path) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x0B); cmd.append((char)0);
    cmd.append(path.toUtf8()); cmd.append((char)0);
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendEfsRead(uint32_t handle, uint32_t seq) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x0C); cmd.append((char)0);
    cmd.append((char)(handle&0xFF)); cmd.append((char)((handle>>8)&0xFF)); cmd.append((char)((handle>>16)&0xFF)); cmd.append((char)((handle>>24)&0xFF));
    cmd.append((char)(seq&0xFF)); cmd.append((char)((seq>>8)&0xFF)); cmd.append((char)0); cmd.append((char)0);
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendEfsClose(uint32_t handle) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x0D); cmd.append((char)0);
    cmd.append((char)(handle&0xFF)); cmd.append((char)((handle>>8)&0xFF)); cmd.append((char)((handle>>16)&0xFF)); cmd.append((char)((handle>>24)&0xFF));
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendFileOpen(const QString &path, int flags, int mode) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x02); cmd.append((char)0);
    cmd.append((char)(flags&0xFF)); cmd.append((char)((flags>>8)&0xFF)); cmd.append((char)((flags>>16)&0xFF)); cmd.append((char)((flags>>24)&0xFF));
    cmd.append((char)(mode&0xFF)); cmd.append((char)((mode>>8)&0xFF)); cmd.append((char)((mode>>16)&0xFF)); cmd.append((char)((mode>>24)&0xFF));
    cmd.append(path.toUtf8()); cmd.append((char)0);
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendFileRead(uint32_t handle, uint32_t bytes, uint32_t offset) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x04); cmd.append((char)0);
    cmd.append((char)(handle&0xFF)); cmd.append((char)((handle>>8)&0xFF)); cmd.append((char)((handle>>16)&0xFF)); cmd.append((char)((handle>>24)&0xFF));
    cmd.append((char)(bytes&0xFF)); cmd.append((char)((bytes>>8)&0xFF)); cmd.append((char)((bytes>>16)&0xFF)); cmd.append((char)((bytes>>24)&0xFF));
    cmd.append((char)(offset&0xFF)); cmd.append((char)((offset>>8)&0xFF)); cmd.append((char)((offset>>16)&0xFF)); cmd.append((char)((offset>>24)&0xFF));
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendFileWrite(uint32_t handle, uint32_t offset, const QByteArray &data) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x05); cmd.append((char)0);
    cmd.append((char)(handle&0xFF)); cmd.append((char)((handle>>8)&0xFF)); cmd.append((char)((handle>>16)&0xFF)); cmd.append((char)((handle>>24)&0xFF));
    cmd.append((char)(offset&0xFF)); cmd.append((char)((offset>>8)&0xFF)); cmd.append((char)((offset>>16)&0xFF)); cmd.append((char)((offset>>24)&0xFF));
    cmd.append(data);
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendFileClose(uint32_t handle) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x03); cmd.append((char)0);
    cmd.append((char)(handle&0xFF)); cmd.append((char)((handle>>8)&0xFF)); cmd.append((char)((handle>>16)&0xFF)); cmd.append((char)((handle>>24)&0xFF));
    m_protocol->sendCommand(cmd);
}
void DeviceManager::sendFileUnlink(const QString &path) {
    QByteArray cmd; cmd.append((char)0x4B); cmd.append((char)0x13); cmd.append((char)0x08); cmd.append((char)0);
    cmd.append(path.toUtf8()); cmd.append((char)0);
    m_protocol->sendCommand(cmd);
}

void DeviceManager::resetDevice() {
    // CMD 0x29 (Mode Change) Subcmd 0x02 (Reset)
    QByteArray packet; packet.append((char)0x29); packet.append((char)0x02); packet.append((char)0x00);
    if(m_protocol) m_protocol->sendCommand(packet);
}

QString DeviceManager::decodeBCD(const QByteArray &data) {
    // Standard decoding usually expects length byte at 0
    if(data.isEmpty()) return "";
    int len = (uint8_t)data[0];
    int start = 1;
    if(len > data.size()-1 || len <= 0) { len = data.size(); start = 0; }

    QString res = "";
    for(int i=start; i<start+len && i<data.size(); ++i) {
        uint8_t b = (uint8_t)data[i];
        uint8_t l = b & 0xF; if(l<=9) res.append(QString::number(l));
        uint8_t h = (b>>4) & 0xF; if(h<=9) res.append(QString::number(h));
    }
    return res;
}

QString DeviceManager::decodeBCDInfo(const QByteArray &data, int limit) {
    if(data.isEmpty()) return "";
    int len = qMin((int)data.size(), limit);

    QString res = "";
    for(int i=0; i<len; ++i) {
        uint8_t b = (uint8_t)data[i];
        uint8_t l = b & 0xF; if(l<=9) res.append(QString::number(l));
        uint8_t h = (b>>4) & 0xF; if(h<=9) res.append(QString::number(h));
    }

    // Trim zero fillers if result is just zeros
    bool allZero = true;
    for(QChar c : res) if(c!='0') allZero=false;
    if(allZero) return "-";

    return res;
}
