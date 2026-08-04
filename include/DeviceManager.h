#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <QObject>
#include <QVariantMap>
#include <QQueue>
#include "include/DiagProtocol.h"

struct EfsEntry {
    QString name;
    bool isDir; 
    uint32_t size;
};

// Job Structs
enum JobType {
    JOB_NONE,
    JOB_IDENTITY,
    JOB_LIST_EFS,
    JOB_READ_FILE,
    JOB_WRITE_FILE,
    JOB_DELETE_FILE
};

struct DeviceJob {
    JobType type;
    QString path;       
    QByteArray data;    
};

class DeviceManager : public QObject
{
    Q_OBJECT
public:
    explicit DeviceManager(QObject *parent = nullptr);
    
    void connectDevice(const QString &port);
    void disconnectDevice();
    
    // Public API (Enqueues Jobs)
    void readIdentity();
    void listEfsDirectory(const QString &path);
    void readFile(const QString &path);
    void writeFile(const QString &path, const QByteArray &data);
    void deleteFile(const QString &path);
    void resetDevice();
    void cancelOperation(); // Reset state machine
    void abortCurrentJob(); // Skip SINGLE job without clearing queue
    
    // Advanced Device Commands
    void zeroSPC();
    void bypassSecurity();
    void offlineA();
    void offlineD();
    void rebootDevice();
    void powerOff();
    void sendCustomSPC(const QString &spc);
    void sendCustomPWD(const QString &pwd);
    
    // SIM/Subscription
    void setSubscriptionIndex(int index) { m_subscriptionIndex = index; }
    int subscriptionIndex() const { return m_subscriptionIndex; }
    
    bool isBusy() const { return m_state != STATE_IDLE; } 
    bool isConnected() const;
    DiagProtocol* protocol() const { return m_protocol; }  // Accessor for NVManager
    
signals:
    void connectionChanged(bool connected);
    void logMessage(const QString &msg);
    void identityReceived(const QVariantMap &data);
    
    // Signals
    void efsEntryReceived(const QString &path, const EfsEntry &entry); 
    void efsListComplete(const QString &path, bool success);
    
    void fileDataReceived(const QString &name, const QByteArray &data);
    void fileWriteComplete(const QString &name, bool success);
    void fileDeleteComplete(const QString &name, bool success);
    
    // Command result signals
    void spcResult(bool success, const QString &message);
    void pwdResult(bool success, const QString &message);
    
private slots:
    void handleResponse(const QByteArray &data);

private:
    DiagProtocol *m_protocol;
    
    // Job Queue
    QQueue<DeviceJob> m_jobQueue;
    DeviceJob m_currentJob;
    void processNextJob();
    
    // Data Accumulation
    QVariantMap m_tempInfo;
    QString m_efsPath;
    uint32_t m_efsHandle;
    uint32_t m_efsSeq;
    
    QString m_filePath;
    uint32_t m_fileHandle;
    uint32_t m_fileOffset;
    QByteArray m_fileData;
    QByteArray m_writeBuffer; // Buffer for write ops
    
    // State Machine
    enum State {
        STATE_IDLE,
        // Identity
        STATE_INFO_SPC,
        STATE_INFO_IMEI,
        STATE_INFO_IMEI2,
        STATE_INFO_IMEI2_ALT1,  // Try NV 2497
        STATE_INFO_IMEI2_ALT2,  // Try NV 5014
        STATE_INFO_IMSI,
        STATE_INFO_STATUS,  // CMD 0x0C
        STATE_INFO_ESN,     // NV 0
        STATE_INFO_MEID,    // NV 1943
        STATE_INFO_VERSION, // CMD 0x7C / 0x00
        STATE_INFO_MDN,     // NV 178
        STATE_INFO_BANNER,  // NV 71
        // EFS
        STATE_EFS_OPEN,
        STATE_EFS_READ,
        STATE_EFS_CLOSE,
        // File Ops
        STATE_FILE_OPEN,
        STATE_FILE_READ,
        STATE_FILE_WRITE,
        STATE_FILE_CLOSE
    };
    State m_state;
    int m_subscriptionIndex;  // 0 = SIM 1, 1 = SIM 2
    
    // Helpers
    void sendNVRead(uint16_t id);
    void sendNVReadExt(uint16_t id, uint16_t index); // Extended NV Read with index
    void sendEfsOpen(const QString &path);
    void sendEfsRead(uint32_t handle, uint32_t seq);
    void sendEfsClose(uint32_t handle);
    
    void sendFileOpen(const QString &path, int flags, int mode);
    void sendFileRead(uint32_t handle, uint32_t bytes, uint32_t offset);
    void sendFileWrite(uint32_t handle, uint32_t offset, const QByteArray &data);
    void sendFileClose(uint32_t handle);
    void sendFileUnlink(const QString &path);
    
    void processEfsStep(const QByteArray &data);
    void processFileStep(const QByteArray &data);
    
    static QString decodeBCD(const QByteArray &data);
    static QString decodeBCDInfo(const QByteArray &data, int limit);
};

#endif // DEVICEMANAGER_H
