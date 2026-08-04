#include "include/DiagProtocol.h"
#include <QDebug>

DiagProtocol::DiagProtocol(QObject *parent) : QObject(parent)
{
    m_serial = new QSerialPort(this);
    connect(m_serial, &QSerialPort::readyRead, this, &DiagProtocol::onReadyRead);
}

DiagProtocol::~DiagProtocol()
{
    closePort();
}

bool DiagProtocol::openPort(const QString &portName)
{
    if (m_serial->isOpen()) m_serial->close();

    m_serial->setPortName(portName);
    m_serial->setBaudRate(QSerialPort::Baud115200);
    m_serial->setDataBits(QSerialPort::Data8);
    m_serial->setParity(QSerialPort::NoParity);
    m_serial->setStopBits(QSerialPort::OneStop);
    m_serial->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serial->open(QIODevice::ReadWrite)) {
        emit rawLog("Connected to " + portName);
        return true;
    } 
    emit errorOccurred("Failed to open port: " + m_serial->errorString());
    return false;
}

void DiagProtocol::closePort()
{
    if (m_serial->isOpen()) {
        m_serial->close();
        emit rawLog("Port closed");
    }
}

bool DiagProtocol::isConnected() const
{
    return m_serial->isOpen();
}

void DiagProtocol::sendCommand(const QByteArray &cmd)
{
    qDebug() << "CMD :" << cmd;
    if (!m_serial->isOpen()) return;

    // CRC
    uint16_t crc = calculateCRC16(cmd);
    QByteArray packet = cmd;
    packet.append((char)(crc & 0xFF));
    packet.append((char)((crc >> 8) & 0xFF));

    // HDLC
    QByteArray framed = encodeHDLC(packet);
    
    emit rawLog("TX: " + packet.toHex(' ').toUpper());

    m_serial->write(framed);
}

void DiagProtocol::clearBuffer()
{
    if (m_serial->isOpen()) {
        m_buffer.clear();
        m_serial->clear(); // Clear OS buffers
        emit rawLog("Buffer cleared");
    }
}

void DiagProtocol::sendRawHDLC(const QByteArray &frame)
{
    if (!m_serial->isOpen()) return;
    
    emit rawLog("TX Raw HDLC: " + frame.toHex(' ').toUpper());
    
    m_serial->write(frame);
}

void DiagProtocol::onReadyRead()
{
    QByteArray data = m_serial->readAll();
    m_buffer.append(data);
    processBuffer();
}

void DiagProtocol::processBuffer()
{
    // Split by 0x7E
    int idx = m_buffer.indexOf(0x7E);
    while (idx != -1) {
        QByteArray frame = m_buffer.left(idx);
        m_buffer.remove(0, idx + 1); // Remove frame + 0x7E

        if (!frame.isEmpty()) {
            QByteArray decoded = decodeHDLC(frame);
            
            // Deframed Log (Optional, noisy)
            // emit rawLog("RX Raw: " + decoded.toHex(' ').toUpper());

            // Validate CRC
            if (decoded.size() > 2) {
                uint8_t crcL = (uint8_t)decoded[decoded.size()-2];
                uint8_t crcH = (uint8_t)decoded[decoded.size()-1];
                uint16_t rxCrc = (crcH << 8) | crcL;
                
                QByteArray payload = decoded.left(decoded.size() - 2);
                uint16_t calcCrc = calculateCRC16(payload);

                if (rxCrc == calcCrc) {
                    emit rawLog("RX: " + payload.toHex(' ').toUpper());
                    emit responseReceived(payload);
                } else {
                    emit rawLog("CRC Error. RX: " + QString::number(rxCrc, 16) + " Calc: " + QString::number(calcCrc, 16));
                }
            }
        }
        
        idx = m_buffer.indexOf(0x7E); // Next frame
    }
}

QByteArray DiagProtocol::encodeHDLC(const QByteArray &data)
{
    QByteArray encoded;
    for (char b : data) {
        if ((uint8_t)b == 0x7E) {
            encoded.append((char)0x7D); encoded.append((char)0x5E);
        } else if ((uint8_t)b == 0x7D) {
            encoded.append((char)0x7D); encoded.append((char)0x5D);
        } else {
            encoded.append(b);
        }
    }
    encoded.append((char)0x7E);
    return encoded;
}

QByteArray DiagProtocol::decodeHDLC(const QByteArray &data)
{
    QByteArray decoded;
    bool escape = false;
    for (char b : data) {
        if ((uint8_t)b == 0x7D) {
            escape = true;
            continue;
        }
        if (escape) {
            decoded.append(b ^ 0x20);
            escape = false;
        } else {
            decoded.append(b);
        }
    }
    return decoded;
}

uint16_t DiagProtocol::calculateCRC16(const QByteArray &data)
{
    uint16_t crc = 0xFFFF;
    for (char b : data) {
        crc ^= (uint8_t)b;
        for (int i = 0; i < 8; ++i) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0x8408;
            else crc >>= 1;
        }
    }
    return ~crc;
}
