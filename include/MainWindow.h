#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QTreeWidget>
#include <QLineEdit>
#include <QTabWidget>
#include <QCheckBox>
#include <QTableWidget>
#include <QTimer>
#include <QMap>
#include <QPixmap>
#include "DeviceManager.h"
#include "NVManager.h"
#include "Version.h"
#include "qprogressbar.h"

class ProgressDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    DeviceManager *m_manager;
    NVManager *m_nvManager;  // NV Manager instance
    
    // UI Dashboard
    QComboBox *m_portBox;
    QCheckBox *m_chkSim1;
    QCheckBox *m_chkSim2;
    QPushButton *m_connectBtn;
    QPushButton *m_btnZeroSPC;        // New: Zero SPC button
    QPushButton *m_btnBypassSec;      // New: Bypass Security button
    QPushButton *m_btnReboot;         // New: Reboot with dropdown
    
    // Custom SPC/PWD
    QLineEdit *m_editCustomSPC;      // New: Custom SPC input
    QPushButton *m_btnSendSPC;       // New: Send custom SPC
    QLineEdit *m_editCustomPWD;      // New: Custom PWD input
    QPushButton *m_btnSendPWD;       // New: Send custom PWD
    
    QLabel *m_lblStatus;
    QLabel *m_lblMode;
    QLabel *m_lblImei;
    QLabel *m_lblImei2;
    QLabel *m_lblImsi;
    QLabel *m_lblEsn;
    QLabel *m_lblMeid;
    QLabel *m_lblVersion;
    QLabel *m_lblMdn;
    QLabel *m_lblBanner;
    
    // SIM 2 Dashboard Labels
    QLabel *m_lblImeiSim2;
    QLabel *m_lblImsiSim2;
    QLabel *m_lblMdnSim2;
    QLabel *m_lblBannerSim2;

    QTextEdit *m_console;
    QTabWidget *m_tabs;
    
    // Global Progress System
    QProgressBar *m_globalProgress;   // New: Global progress bar
    QLabel *m_lblProgressText;        // New: Progress status text
    QPushButton *m_btnStopAll;        // New: Stop all operations
    
    // UI EFS
    QTreeWidget *m_efsTree;
    QLineEdit *m_pathEdit;
    
    // Custom NV Lookup UI
    QLineEdit *m_customNvIdInput;
    QPushButton *m_btnCustomRead;
    QLineEdit *m_customResHex;
    QLineEdit *m_customResText;
    QLineEdit *m_customResBin;
    QLineEdit *m_customResDec;
    QTextEdit *m_txtHexDump;  // DFS-style hex dump panel

    
    // Existing UI NV Manager
    QTableWidget *m_nvTable;
    QComboBox *m_nvCategoryFilter;
    QLineEdit *m_nvSearchBox;
    QLabel *m_nvItemIdLabel;
    QLabel *m_nvItemNameLabel;
    QLabel *m_nvItemTypeLabel;
    QLabel *m_nvItemSizeLabel;
    QLabel *m_nvItemDescLabel;
    QLineEdit *m_nvCurrentValueHex;
    QLineEdit *m_nvCurrentValueText;  // ASCII/Text display
    QLineEdit *m_nvNewValueHex;
    QPushButton *m_btnNVRead;
    QPushButton *m_btnNVWrite;
    QPushButton *m_btnNVRefresh;
    QPushButton *m_btnNVClear;
    QPushButton *m_btnNVBackup;
    QPushButton *m_btnNVBackupAll;
    QPushButton *m_btnNVRestore;
    QPushButton *m_btnNVRestoreAll;
    
    // Action Buttons
    QPushButton *m_btnRefreshPorts;
    QPushButton *m_btnRefresh;
    QPushButton *m_btnDownload;
    QPushButton *m_btnUploadFile;
    QPushButton *m_btnUploadFolder;
    QPushButton *m_btnDelete;
    
    // Icons
    QMap<QString,QPixmap> m_icons;
    QMap<QString, QTreeWidgetItem*> m_nodeMap;
    QPixmap m_iconFolder;
    QPixmap m_iconFile;
    
    // Auto-Refresh tracking
    QString m_lastWrittenPath;
    QString m_lastDeletedPath;
    
    // Recursive delete tracking
    QList<QString> m_deleteFileList;
    int m_deleteCurrentIndex;
    
    // Recursive Operation State
    enum class RecursiveOp { None, Download, Delete };
    RecursiveOp m_recursiveOp;
    QQueue<QString> m_scanQueue;
    QStringList m_targetFileList;
    QString m_recursiveRootPath;
    
    // Recursive download tracking
    QList<QString> m_downloadFileList;
    int m_downloadCurrentIndex;
    QString m_downloadBasePath;
    
    // Recursive upload tracking
    QStringList m_uploadFileList;
    int m_uploadCurrentIndex;
    
    // Progress Dialog
    ProgressDialog *m_progressDlg;
    QTimer *m_listTimeout;
    QTimer *m_operationTimeout;
    QString m_currentOperationPath;
    
    // SIM 2 Path Auto-Discovery
    QStringList m_sim2ProbePaths;
    int m_sim2ProbeIndex;
    bool m_sim2Probing;
    QString m_sim2DiscoveredRoot;  // Successfully discovered SIM 2 root
    
private:
    void setupUi();
    void loadIcons();                         // Restored
    void setupDarkTheme();                    // Restored
    void setupDashboard(QWidget *p);          // Restored
    void setupCommands(QWidget *p);
    void setupEFS(QWidget *p);
    void setupNV(QWidget *p);
    void setupDatabaseView(QWidget *p);       // NEW: Separated database view
    void setupAdvancedNVEditor(QWidget *p);   // NEW: Advanced hex editor
    void setupAdvanced(QWidget *p);
    void populateNVTable(const QList<NVItem> &items);
    
    // Helpers
    QTreeWidgetItem* findOrCreateParent(const QString &path);
    void showProgress(const QString &operation, const QString &status = "");
    void hideProgress();
    void markFolderAsCorrupted(const QString &path);
    
    // SIM Helpers
    int getActiveSubscription() const;
    void resetEfsTree();
    void startSim2Probe();  // Auto-discover SIM 2 EFS root
    void refreshFolder(const QString &path);
    
    // Global Progress
    void showGlobalProgress(const QString &text, int max = 0);
    void updateGlobalProgress(int value, const QString &text = "");
    void hideGlobalProgress();
    
private slots:
    // Advanced Command Slots
    void onZeroSPC();
    void onBypassSecurity();
    void onReboot();
    void onOfflineA();
    void onOfflineD();
    void onPowerOff();
    void onSendCustomSPC();
    void onSendCustomPWD();
    void onStopAll();
    
    // SIM switch slot
    void onSimSelectionChanged();
    
    // Existing slots
    void onRefresh();
    void onConnect();
    void onReadInfo();
    void onListEfs();
    
    void onSelectionChanged();
    void onRefreshCurrent();
    void onDownloadSelected();
    void onUploadFileToSelected();
    void onWriteFolderToSelected();
    void onDeleteSelected();
    void onContextMenu(const QPoint &pos);
    void onTreeDoubleClicked(QTreeWidgetItem *item, int column);
    
    void onEfsEntry(const QString &parentPath, const EfsEntry &entry);
    void onEfsListComplete(const QString &path, bool success);
    void onFileDataReceived(const QString &name, const QByteArray &data);
    void onFileWritten(const QString &name, bool success);
    void onFileDeleted(const QString &name, bool success);
    
    // Recursive Helpers
    void startRecursiveScan(const QString &path, RecursiveOp op);
    void executeRecursiveOp();
    void onLog(const QString &msg);
    void onIdentityData(const QVariantMap &data);
    void on_manager_connected(bool ok);
    
    // NV Manager Slots
    void onNVCategoryChanged(int index);
    void onNVSearchChanged(const QString &text);
    void onNVTableSelectionChanged();
    void onNVRead();
    void onNVWrite();
    void onNVRefresh();
    void onNVClear();
    void onNVBackup();
    void onNVBackupAll();
    void onNVRestore();
    void onNVRestoreAll();
    void onCustomNVRead(); // New Slot
    void onNVReadComplete(uint16_t itemId, const QByteArray &data, bool success);
    void onNVWriteComplete(uint16_t itemId, bool success);
    void onNVBackupProgress(int current, int total, const QString &itemName);
    void onNVBackupComplete(bool success, const QString &filename);
    void onNVRestoreProgress(int current, int total, const QString &itemName);
    void onNVRestoreComplete(bool success, int successCount, int failCount);
    
    // About Dialog
    void showAboutDialog();
};


#endif // MAINWINDOW_H
