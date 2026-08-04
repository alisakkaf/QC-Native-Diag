#ifndef NVMANAGER_H
#define NVMANAGER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QMap>
#include "DiagProtocol.h"
#include "NVDatabase.h"
#include <QTimer>
class NVManager : public QObject
{
    Q_OBJECT

public:
    explicit NVManager(DiagProtocol *protocol, QObject *parent = nullptr);
    
    // Core NV Operations
    void readNV(uint16_t itemId);
    void readNVExt(uint16_t itemId, uint16_t index);
    void writeNV(uint16_t itemId, const QByteArray &data);
    void writeNVExt(uint16_t itemId, uint16_t index, const QByteArray &data);
    
    // Backup & Restore
    void backupItems(const QList<uint16_t> &itemIds, const QString &filename);
    void backupRange(uint16_t startId, uint16_t endId, const QString &filename); // Backup ID range
    void restoreFromFile(const QString &filename);
    void cancelOperation();
    
    // Utilities
    bool isConnected() const;
    const NVItem* getItemInfo(uint16_t itemId) const;
    QString formatNVValue(uint16_t itemId, const QByteArray &data) const; // Format data for display
    
signals:
    void logMessage(const QString &msg);
    void nvReadComplete(uint16_t itemId, const QByteArray &data, bool success);
    void nvWriteComplete(uint16_t itemId, bool success);
    void backupProgress(int current, int total, const QString &itemName);
    void backupComplete(bool success, const QString &filename);
    void restoreProgress(int current, int total, const QString &itemName);
    void restoreComplete(bool success, int successCount, int failCount);

public slots:
    void handleResponse(const QByteArray &data);
    void onTimeout();  // Handle operation timeout
    void onBackupWatchdogTimeout(); // Dedicated watchdog timer for range backup

private:
    DiagProtocol *m_protocol;
    
    // Current operation tracking
    enum OperationType {
        OP_NONE,
        OP_READ,
        OP_WRITE,
        OP_BACKUP,
        OP_RESTORE
    };
    
    OperationType m_currentOp;
    uint16_t m_currentItemId;
    uint16_t m_currentIndex;
    bool m_isExtended;
    int m_readTryMode;
    
    // Backup/Restore state
    struct BackupItem {
        uint16_t id;
        uint16_t index;
        bool isExtended;
        QString name;
        QByteArray data;
    };
    
    QList<BackupItem> m_backupQueue;
    QString m_backupFilename;
    int m_backupCurrentIndex;
    int m_backupFailCount;
    int m_backupSkipCount;
    
    QList<BackupItem> m_restoreQueue;
    int m_restoreCurrentIndex;
    int m_restoreSuccessCount;
    int m_restoreFailCount;
    
    QTimer *m_responseTimer;   // Timeout for each NV operation
    QTimer *m_backupWatchdog;  // Dedicated Watchdog Timer for Range Backup
    
    // Helper methods
    void sendNVRead(uint16_t id);
    void sendNVReadExt(uint16_t id, uint16_t index);
    void sendNVWrite(uint16_t id, const QByteArray &data);
    void sendNVWriteExt(uint16_t id, uint16_t index, const QByteArray &data);
    
    // Subsystem Command Helpers (Phase 1)
    void sendSubsysNVRead(uint16_t id);
    void sendSubsysNVWrite(uint16_t id, const QByteArray &data);
    bool isSubsysItem(uint16_t id) const;
    
    void processBackupQueue();
    void processRestoreQueue();
    void saveBackupToFile();
    bool loadRestoreFromFile(const QString &filename);

    
public:
    // Custom Display Helper
    struct FormattedValue {
        QString hex;
        QString text;
        QString binary;
        QString decimal;
    };
    FormattedValue formatNVValueFull(uint16_t itemId, const QByteArray &data) const;
};

#endif // NVMANAGER_H
