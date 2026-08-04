#ifndef DIAGPROTOCOL_H
#define DIAGPROTOCOL_H

#include <QObject>
#include <QSerialPort>
#include <QByteArray>

class DiagProtocol : public QObject
{
    Q_OBJECT
public:
    explicit DiagProtocol(QObject *parent = nullptr);
    ~DiagProtocol();

    bool openPort(const QString &portName);
    void closePort();
    bool isConnected() const;

    // Asynchronous Send (Fire & Forget)
    void sendCommand(const QByteArray &cmd);
    void clearBuffer(); // Clear receive buffer
    void sendRawHDLC(const QByteArray &frame); // Send pre-encoded HDLC frame

signals:
    void responseReceived(const QByteArray &data);
    void rawLog(const QString &msg);
    void errorOccurred(const QString &err);

private slots:
    void onReadyRead();

private:
    QSerialPort *m_serial;
    QByteArray m_buffer;

    // Helpers
    QByteArray encodeHDLC(const QByteArray &data);
    QByteArray decodeHDLC(const QByteArray &data);
    uint16_t calculateCRC16(const QByteArray &data);
    void processBuffer();
};

#endif // DIAGPROTOCOL_H
