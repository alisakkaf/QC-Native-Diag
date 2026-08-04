#include "include/MainWindow.h"
#include "include/ProgressDialog.h"
#include "qapplication.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSerialPortInfo>
#include <QHeaderView>
#include <QLineEdit>
#include <QFileDialog>
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QBuffer>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>
#include <QSplitter>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QDate>

#include <QSpinBox>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_manager = new DeviceManager(this);
    m_progressDlg = nullptr;
    m_deleteCurrentIndex = 0;
    m_downloadCurrentIndex = 0;
    m_uploadCurrentIndex = 0;
    m_recursiveOp = RecursiveOp::None;
    m_sim2ProbeIndex = 0;
    m_sim2Probing = false;

    m_listTimeout = new QTimer(this);
    m_listTimeout->setSingleShot(true);
    m_listTimeout->setInterval(1500);

    m_operationTimeout = new QTimer(this);
    m_operationTimeout->setSingleShot(true);
    m_operationTimeout->setInterval(5000);

    loadIcons();
    setupUi();
    setupDarkTheme();

    // Initialize NV Manager
    m_nvManager = new NVManager(m_manager->protocol(), this);
    connect(m_nvManager, &NVManager::logMessage, this, &MainWindow::onLog);
    connect(m_nvManager, &NVManager::nvReadComplete, this, &MainWindow::onNVReadComplete);
    connect(m_nvManager, &NVManager::nvWriteComplete, this, &MainWindow::onNVWriteComplete);
    connect(m_nvManager, &NVManager::backupProgress, this, &MainWindow::onNVBackupProgress);
    connect(m_nvManager, &NVManager::backupComplete, this, &MainWindow::onNVBackupComplete);
    connect(m_nvManager, &NVManager::restoreProgress, this, &MainWindow::onNVRestoreProgress);
    connect(m_nvManager, &NVManager::restoreComplete, this, &MainWindow::onNVRestoreComplete);

    connect(m_manager, &DeviceManager::connectionChanged, this, &MainWindow::on_manager_connected);
    connect(m_manager, &DeviceManager::logMessage, this, &MainWindow::onLog);
    connect(m_manager, &DeviceManager::identityReceived, this, &MainWindow::onIdentityData);
    connect(m_manager, &DeviceManager::efsEntryReceived, this, &MainWindow::onEfsEntry);
    connect(m_manager, &DeviceManager::efsListComplete, this, &MainWindow::onEfsListComplete);
    connect(m_manager, &DeviceManager::fileDataReceived, this, &MainWindow::onFileDataReceived);
    connect(m_manager, &DeviceManager::fileWriteComplete, this, &MainWindow::onFileWritten);
    connect(m_manager, &DeviceManager::fileDeleteComplete, this, &MainWindow::onFileDeleted);

    // Command result connections
    connect(m_manager, &DeviceManager::spcResult, this, [this](bool success, const QString &message){
        m_operationTimeout->stop();
        hideGlobalProgress();
        if(success) {
            QMessageBox::information(this, "✅ SPC Success", "SPC command was accepted by the device.");
        } else {
            QMessageBox::warning(this, "❌ SPC Failed", message + "\n\nThe device rejected your SPC code.\nPlease try a different code.");
        }
    });

    connect(m_manager, &DeviceManager::pwdResult, this, [this](bool success, const QString &message){
        m_operationTimeout->stop();
        hideGlobalProgress();
        if(success) {
            QMessageBox::information(this, "✅ PWD Success", "PWD command was accepted by the device.");
        } else {
            QMessageBox::warning(this, "❌ PWD Failed", message + "\n\nThe device rejected your PWD.\nPlease try a different password.");
        }
    });


    connect(m_listTimeout, &QTimer::timeout, [this](){
        if(!m_progressDlg) {
            showProgress("📂 Loading Directory", "Please wait...");
            if(m_progressDlg) {
                m_progressDlg->startCountdown(10);
                connect(m_progressDlg, &ProgressDialog::timeoutReached, this, [this](){
                    markFolderAsCorrupted(m_currentOperationPath);
                    m_manager->cancelOperation();
                    hideProgress();
                });
            }
        }
    });

    connect(m_operationTimeout, &QTimer::timeout, [this](){
        if(m_progressDlg) {
            onLog("⚠️ Operation timeout for: " + m_currentOperationPath);

            // Smart Skip Logic for Recursive Operations (Scanning OR Processing)
            if(m_recursiveOp != RecursiveOp::None ||
               !m_downloadFileList.isEmpty() ||
               !m_uploadFileList.isEmpty() ||
               !m_deleteFileList.isEmpty()) {

                 onLog("🔄 Skipping current item to prevent freeze...");
                 m_manager->abortCurrentJob(); // This triggers failure signal -> next item
            } else {
                 markFolderAsCorrupted(m_currentOperationPath);
                 m_manager->cancelOperation();
                 hideProgress();
            }
        } else if(m_globalProgress->maximum() == 0 && !m_lblProgressText->text().contains("Ready")) {
            // Global operation timeout (ReadInfo, Commands, etc.)
            hideGlobalProgress();

            QString operation = m_currentOperationPath.isEmpty() ? "Operation" : m_currentOperationPath;

            QMessageBox::critical(this, "⚠️ Operation Timeout",
                QString("Device did not respond to: %1\n\n"
                "❌ Possible causes:\n"
                "• Wrong COM port selected\n"
                "• Device not responding\n"
                "• Connection lost\n\n"
                "💡 Try:\n"
                "1. Select a different COM port\n"
                "2. Reconnect the device\n"
                "3. Restart the application").arg(operation));

            onLog("⚠️ " + operation + " Timeout - No response from device");
            m_manager->cancelOperation();
        }
    });

    onRefresh();
    resize(1100, 750);
    setWindowTitle(VersionInfo::getWindowTitle());

    // --- Connections ---
    // Port & Connection
    connect(m_btnRefreshPorts, &QPushButton::clicked, this, &MainWindow::onRefresh);
    connect(m_connectBtn, &QPushButton::clicked, this, &MainWindow::onConnect);

    // Advanced Commands
    connect(m_btnZeroSPC, &QPushButton::clicked, this, &MainWindow::onZeroSPC);
    connect(m_btnBypassSec, &QPushButton::clicked, this, &MainWindow::onBypassSecurity);
    // Reboot menu actions already connected in setupUi

    // Custom SPC/PWD
    connect(m_btnSendSPC, &QPushButton::clicked, this, &MainWindow::onSendCustomSPC);
    connect(m_btnSendPWD, &QPushButton::clicked, this, &MainWindow::onSendCustomPWD);

    // SIM 1 / SIM 2 toggle (mutual exclusion)
    connect(m_chkSim1, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_chkSim2->setChecked(false);
            onSimSelectionChanged();
        } else if (!m_chkSim2->isChecked()) {
            m_chkSim1->setChecked(true); // At least one must be selected
        }
    });
    connect(m_chkSim2, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_chkSim1->setChecked(false);
            onSimSelectionChanged();
        } else if (!m_chkSim1->isChecked()) {
            m_chkSim2->setChecked(true); // At least one must be selected
        }
    });

    // Global Progress & Stop
    connect(m_btnStopAll, &QPushButton::clicked, this, &MainWindow::onStopAll);

    // --- Application Branding ---
    setWindowIcon(QIcon(":/app_icon.png"));

    // Help Menu
    QMenu *helpMenu = menuBar()->addMenu("Help");
    QAction *aboutAction = helpMenu->addAction("About");
    aboutAction->setIcon(QIcon(":/app_icon.png"));
    connect(aboutAction, &QAction::triggered, this, &MainWindow::showAboutDialog);

    this->m_tabs->setEnabled(false);
}


void MainWindow::showProgress(const QString &operation, const QString &status)
{
    if(!m_progressDlg) {
        m_progressDlg = new ProgressDialog(this);

        connect(m_progressDlg, &QDialog::rejected, this, [this](){
            m_listTimeout->stop();
            m_operationTimeout->stop();
            m_currentOperationPath.clear();
            m_manager->cancelOperation();
            onLog("⚠️ Operation cancelled by user");
            hideProgress();
        });
    }
    m_progressDlg->setOperation(operation);
    m_progressDlg->setStatus(status);
    m_progressDlg->showIndeterminate();
    m_progressDlg->show();
    QApplication::processEvents();
}

void MainWindow::hideProgress()
{
    if(m_progressDlg) {
        m_progressDlg->hide();
        m_progressDlg->deleteLater();
        m_progressDlg = nullptr;
    }
}

void MainWindow::loadIcons()
{
    QByteArray folderBytes = QByteArray::fromBase64("8J+TgQ==");
    QString folderStr = QString::fromUtf8(folderBytes);

    m_iconFolder = QPixmap(32, 32);
    m_iconFolder.fill(Qt::transparent);
    QPainter p(&m_iconFolder);
    p.setFont(QFont("Segoe UI Emoji", 20));
    p.drawText(m_iconFolder.rect(), Qt::AlignCenter, folderStr);
    p.end();

    m_iconFile = style()->standardIcon(QStyle::SP_FileIcon).pixmap(24, 24);
}

void MainWindow::setupUi() {
    QWidget *central = new QWidget;

    setCentralWidget(central);
    QVBoxLayout *mainLay = new QVBoxLayout(central);
    mainLay->setSpacing(5);
    mainLay->setContentsMargins(5, 5, 5, 5);

    // --- 1. Top Section: Connection & Control ---
    QGroupBox *grpConn = new QGroupBox("Connection & Device Control");
    grpConn->setStyleSheet("QGroupBox { font-weight: bold; border: 1px solid #444; margin-top: 10px; } QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 5px; }");
    QVBoxLayout *connLay = new QVBoxLayout(grpConn);
    connLay->setSpacing(5);

    // Row 1: Port & Connect
    QHBoxLayout *row1 = new QHBoxLayout;
    m_portBox = new QComboBox;
    m_portBox->setMinimumWidth(200);
    
    m_chkSim1 = new QCheckBox("SIM 1");
    m_chkSim2 = new QCheckBox("SIM 2");
    m_chkSim1->setChecked(true);
    m_chkSim2->setChecked(false);
    m_chkSim1->setStyleSheet("QCheckBox { color: #00ff88; font-weight: bold; } QCheckBox::indicator:checked { background-color: #00aa55; border: 1px solid #00ff88; } QCheckBox::indicator { border: 1px solid #555; width: 14px; height: 14px; }");
    m_chkSim2->setStyleSheet("QCheckBox { color: #ffaa00; font-weight: bold; } QCheckBox::indicator:checked { background-color: #aa7700; border: 1px solid #ffaa00; } QCheckBox::indicator { border: 1px solid #555; width: 14px; height: 14px; }");
    
    m_btnRefreshPorts = new QPushButton("Refresh");
    m_connectBtn = new QPushButton("Connect");
    m_connectBtn->setMinimumWidth(100);
    // Style Connect Button
    m_connectBtn->setStyleSheet("QPushButton { background-color: #005500; color: white; font-weight: bold; padding: 5px; } QPushButton:hover { background-color: #007700; }");

    row1->addWidget(new QLabel("Port:"));
    row1->addWidget(m_portBox, 1);
    row1->addWidget(m_chkSim1);
    row1->addWidget(m_chkSim2);
    row1->addWidget(m_btnRefreshPorts);
    row1->addWidget(m_connectBtn);
    connLay->addLayout(row1);

    // Row 2: Advanced Commands
    QHBoxLayout *row2 = new QHBoxLayout;

    m_btnZeroSPC = new QPushButton("🔓 Zero SPC");
    m_btnZeroSPC->setToolTip("Send Zero SPC Command");

    m_btnBypassSec = new QPushButton("🛡️ Bypass Security");
    m_btnBypassSec->setToolTip("Send 11 Security Bypass Commands");

    m_btnReboot = new QPushButton("🔄 Reboot Device");
    QMenu *bootMenu = new QMenu(this);
    bootMenu->addAction("🔄 Reboot Normal", this, &MainWindow::onReboot);
    bootMenu->addSeparator();
    bootMenu->addAction("🔌 Offline Mode A", this, &MainWindow::onOfflineA);
    bootMenu->addAction("🔌 Offline Mode D", this, &MainWindow::onOfflineD);
    bootMenu->addAction("⚡ Power Off", this, &MainWindow::onPowerOff);
    m_btnReboot->setMenu(bootMenu);

    row2->addWidget(m_btnZeroSPC);
    row2->addWidget(m_btnBypassSec);
    row2->addWidget(m_btnReboot);
    connLay->addLayout(row2);

    // Row 3: Custom SPC/PWD
    QHBoxLayout *row3 = new QHBoxLayout;

    m_editCustomSPC = new QLineEdit;
    m_editCustomSPC->setPlaceholderText("SPC (6 digits)");
    m_editCustomSPC->setMaxLength(6);
    m_editCustomSPC->setText("000000"); // Default

    m_btnSendSPC = new QPushButton("Send SPC");

    m_editCustomPWD = new QLineEdit;
    m_editCustomPWD->setPlaceholderText("PWD (16 hex chars)");
    m_editCustomPWD->setMaxLength(16);
    m_editCustomPWD->setText("FFFFFFFFFFFFFFFF"); // Default

    m_btnSendPWD = new QPushButton("Send PWD");

    row3->addWidget(new QLabel("Custom SPC:"));
    row3->addWidget(m_editCustomSPC);
    row3->addWidget(m_btnSendSPC);
    row3->addSpacing(15);
    row3->addWidget(new QLabel("Custom PWD:"));
    row3->addWidget(m_editCustomPWD);
    row3->addWidget(m_btnSendPWD);
    connLay->addLayout(row3);

    mainLay->addWidget(grpConn);

    // --- 2. Middle Section: Tabs ---
    m_tabs = new QTabWidget;
    // Create tabs
    QWidget *tabDash = new QWidget; setupDashboard(tabDash);
    QWidget *tabEfs = new QWidget; setupEFS(tabEfs);  // Fixed: setupEfs → setupEFS
    QWidget *tabCmd = new QWidget;
    m_console = new QTextEdit;
    m_console->setReadOnly(true);
    m_console->setFont(QFont("Consolas", 9));
    m_console->setStyleSheet("background-color: #1e1e1e; color: #00ff00;");

    m_tabs->addTab(tabDash, "📱 Dashboard");
    m_tabs->addTab(tabEfs, "📁 EFS File Browser");

    // NV Manager tab
    QWidget *tabNV = new QWidget;
    setupNV(tabNV);
    m_tabs->addTab(tabNV, "🔧 NV Manager");

    m_tabs->addTab(m_console, "💻 Console");

    mainLay->addWidget(m_tabs);

    // --- 3. Bottom Section: Global Progress ---
    QHBoxLayout *progLay = new QHBoxLayout;

    m_lblProgressText = new QLabel("Ready");
    m_lblProgressText->setMinimumWidth(150);

    m_globalProgress = new QProgressBar;
    m_globalProgress->setRange(0, 100);
    m_globalProgress->setValue(0);
    m_globalProgress->setTextVisible(true);
    m_globalProgress->setStyleSheet("QProgressBar { border: 1px solid #555; border-radius: 3px; text-align: center; } QProgressBar::chunk { background-color: #007acc; width: 10px; margin: 0.5px; }");

    m_btnStopAll = new QPushButton("⛔ STOP");
    m_btnStopAll->setStyleSheet("background-color: #aa0000; color: white; font-weight: bold; padding: 3px 10px;");

    progLay->addWidget(m_lblProgressText);
    progLay->addWidget(m_globalProgress, 1);
    progLay->addWidget(m_btnStopAll);

    mainLay->addLayout(progLay);
}

void MainWindow::setupDashboard(QWidget *p) {
    QGridLayout *g = new QGridLayout(p);

    // Group 1: Network & Status
    QGroupBox *boxNet = new QGroupBox("Network && Status");
    QGridLayout *glNet = new QGridLayout(boxNet);
    m_lblStatus = new QLabel("Disconnected");
    m_lblStatus->setStyleSheet("color: red; font-weight: bold;");
    m_lblMode = new QLabel("-");

    glNet->addWidget(new QLabel("Connection:"),0,0); glNet->addWidget(m_lblStatus,0,1);
    glNet->addWidget(new QLabel("Mode:"),1,0); glNet->addWidget(m_lblMode,1,1);

    // Group 2: User Identity (SIM 1)
    QGroupBox *boxSim = new QGroupBox("Identity (SIM 1)");
    QGridLayout *glSim = new QGridLayout(boxSim);
    m_lblImei = new QLabel("-");
    m_lblImei2 = new QLabel("-");
    m_lblImsi = new QLabel("-");

    glSim->addWidget(new QLabel("IMEI 1:"),0,0); glSim->addWidget(m_lblImei,0,1);
    glSim->addWidget(new QLabel("IMEI 2:"),1,0); glSim->addWidget(m_lblImei2,1,1);
    glSim->addWidget(new QLabel("IMSI:"),2,0); glSim->addWidget(m_lblImsi,2,1);

    // New Fields
    m_lblMdn = new QLabel("-");
    m_lblBanner = new QLabel("-");
    glSim->addWidget(new QLabel("Phone:"),3,0); glSim->addWidget(m_lblMdn,3,1);
    glSim->addWidget(new QLabel("Carrier:"),4,0); glSim->addWidget(m_lblBanner,4,1);

    // Group 2b: SIM 2 Identity
    QGroupBox *boxSim2 = new QGroupBox("Identity (SIM 2)");
    boxSim2->setStyleSheet("QGroupBox { border: 1px solid #885500; } QGroupBox::title { color: #ffaa00; }");
    QGridLayout *glSim2 = new QGridLayout(boxSim2);
    m_lblImeiSim2 = new QLabel("-");
    m_lblImsiSim2 = new QLabel("-");
    m_lblMdnSim2 = new QLabel("-");
    m_lblBannerSim2 = new QLabel("-");

    glSim2->addWidget(new QLabel("IMEI:"),0,0); glSim2->addWidget(m_lblImeiSim2,0,1);
    glSim2->addWidget(new QLabel("IMSI:"),1,0); glSim2->addWidget(m_lblImsiSim2,1,1);
    glSim2->addWidget(new QLabel("Phone:"),2,0); glSim2->addWidget(m_lblMdnSim2,2,1);
    glSim2->addWidget(new QLabel("Carrier:"),3,0); glSim2->addWidget(m_lblBannerSim2,3,1);

    // Group 3: Hardware Info
    QGroupBox *boxHw = new QGroupBox("Hardware");
    QGridLayout *glHw = new QGridLayout(boxHw);
    m_lblEsn = new QLabel("-");
    m_lblMeid = new QLabel("-");
    m_lblVersion = new QLabel("-");
    m_lblVersion->setWordWrap(true);

    glHw->addWidget(new QLabel("ESN:"),0,0); glHw->addWidget(m_lblEsn,0,1);
    glHw->addWidget(new QLabel("MEID:"),1,0); glHw->addWidget(m_lblMeid,1,1);
    glHw->addWidget(new QLabel("Version:"),2,0); glHw->addWidget(m_lblVersion,2,1);

    // Read Button
    QPushButton *btn = new QPushButton("Read Info");
    btn->setMinimumHeight(40);
    connect(btn, &QPushButton::clicked, this, &MainWindow::onReadInfo);

    QPushButton *btnReset = new QPushButton("Reset Device");
    btnReset->setMinimumHeight(40);
    // Dark red style for Reset
    btnReset->setStyleSheet("QPushButton { background-color: #550000; color: white; border: 1px solid #aa0000; padding: 5px; } QPushButton:hover { background-color: #770000; }");
    connect(btnReset, &QPushButton::clicked, this, [this](){
        m_manager->resetDevice();
        onLog("🔄 Sending Reset Command...");
    });

    // Main Layout
    g->addWidget(boxNet, 0, 0);
    g->addWidget(boxSim, 0, 1);
    g->addWidget(boxSim2, 1, 0);  // SIM 2 Identity
    g->addWidget(boxHw, 1, 1);
    g->addWidget(btn, 2, 0, 1, 1);
    g->addWidget(btnReset, 2, 1, 1, 1); // Next to Read Info
    g->setRowStretch(3, 1); // Spacer
}

void MainWindow::setupEFS(QWidget *p) {
    QVBoxLayout *l = new QVBoxLayout(p);

    m_efsTree = new QTreeWidget;
    m_efsTree->setHeaderLabels({"Name", "Size", "Type"});
    m_efsTree->setColumnWidth(0, 300);
    m_efsTree->setContextMenuPolicy(Qt::CustomContextMenu);

    QTreeWidgetItem *root = new QTreeWidgetItem(m_efsTree);
    root->setText(0, "/");
    root->setText(2, "Directory");
    root->setIcon(0, m_iconFolder);
    root->setData(0, Qt::UserRole, "/");
    m_nodeMap["/"] = root;

    QHBoxLayout *h1 = new QHBoxLayout;
    m_pathEdit = new QLineEdit("/");
    QPushButton *btnGo = new QPushButton("📂 List EFS");
    connect(btnGo, &QPushButton::clicked, this, &MainWindow::onListEfs);

    h1->addWidget(new QLabel("Path:"));
    h1->addWidget(m_pathEdit, 1);
    h1->addWidget(btnGo);

    QHBoxLayout *h2 = new QHBoxLayout;
    m_btnRefresh = new QPushButton("🔄 Refresh");
    m_btnDownload = new QPushButton("📥 Download");
    m_btnUploadFile = new QPushButton("📤 Upload File");
    m_btnUploadFolder = new QPushButton("📁 Upload Folder");
    m_btnDelete = new QPushButton("🗑️ Delete");

    m_btnRefresh->setEnabled(false);
    m_btnDownload->setEnabled(false);
    m_btnUploadFile->setEnabled(false);
    m_btnUploadFolder->setEnabled(false);
    m_btnDelete->setEnabled(false);

    connect(m_btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshCurrent);
    connect(m_btnDownload, &QPushButton::clicked, this, &MainWindow::onDownloadSelected);
    connect(m_btnUploadFile, &QPushButton::clicked, this, &MainWindow::onUploadFileToSelected);
    connect(m_btnUploadFolder, &QPushButton::clicked, this, &MainWindow::onWriteFolderToSelected);
    connect(m_btnDelete, &QPushButton::clicked, this, &MainWindow::onDeleteSelected);

    h2->addWidget(m_btnRefresh);
    h2->addWidget(m_btnDownload);
    h2->addWidget(m_btnUploadFile);
    h2->addWidget(m_btnUploadFolder);
    h2->addWidget(m_btnDelete);
    h2->addStretch();

    l->addLayout(h1);
    l->addLayout(h2);
    l->addWidget(m_efsTree);

    connect(m_efsTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::onContextMenu);
    connect(m_efsTree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::onTreeDoubleClicked);
    connect(m_efsTree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onSelectionChanged);
    connect(m_efsTree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item){
        if(item->childCount() == 1 && item->child(0)->text(0) == "__dummy__") {
            delete item->takeChild(0);
            QString path = item->data(0, Qt::UserRole).toString();

            // Strict Reset: Ensure we are NOT in recursive mode
            m_recursiveOp = RecursiveOp::None;
            m_scanQueue.clear();

            m_listTimeout->stop();
            m_operationTimeout->stop();
            m_currentOperationPath = path;

            m_listTimeout->start();
            m_operationTimeout->start();
            m_manager->listEfsDirectory(path);
        }
    });
}

void MainWindow::setupDarkTheme() {
    setStyleSheet(R"(
        QMainWindow { background-color: #1e1e1e; color: #eee; }
        QGroupBox { border: 1px solid #444; margin-top: 8px; padding: 10px; }
        QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; color: #ffffff; }
        QLabel { color: #cccccc; }
        QPushButton { background-color: #333; color: #fff; border: 1px solid #555; padding: 5px; }
        QPushButton:hover { background-color: #444; }
        QPushButton:disabled { background-color: #222; color: #666; }
        QComboBox { background-color: #333; color: #fff; border: 1px solid #555; padding: 3px; }
        QLineEdit { background-color: #2a2a2a; color: #fff; border: 1px solid #555; padding: 3px; }
        QTextEdit { background-color: #1a1a1a; color: #00ff00; border: 1px solid #444; }
        QTreeWidget { background-color: #1e1e1e; color: #eee; border: 1px solid #444; }
        QTreeWidget::item:selected { background-color: #007acc; }
        QTabWidget::pane { border: 1px solid #444; background-color: #1e1e1e; }
        QTabBar::tab { background-color: #2d2d2d; color: #ccc; padding: 8px 16px; border: 1px solid #444; }
        QTabBar::tab:selected { background-color: #007acc; color: #fff; }

        /* QMessageBox Dark Theme */
        QMessageBox { background-color: #2d2d2d; color: #ffffff; }
        QMessageBox QLabel { color: #e0e0e0; background-color: transparent; }
        QMessageBox QPushButton {
            background-color: #0066cc;
            color: white;
            border: 1px solid #0052a3;
            padding: 6px 16px;
            min-width: 70px;
            border-radius: 3px;
        }
        QMessageBox QPushButton:hover { background-color: #0077dd; }
        QMessageBox QPushButton:pressed { background-color: #0052a3; }
        QMessageBox QDialogButtonBox { background-color: transparent; }
    )");
}

// ============ SLOT IMPLEMENTATIONS ============

void MainWindow::onRefresh() {
    m_portBox->clear();
    for(auto &p : QSerialPortInfo::availablePorts()) {
        // Display: "COM3 - USB Serial Port"
        // Store: "COM3"
        qDebug() << p.portName();
        QString displayName = p.portName();
        if(!p.description().isEmpty()) {
            displayName += " - " + p.description();
        }
        m_portBox->addItem(displayName, p.portName());
    }
}

void MainWindow::onConnect() {
    if(m_manager->isConnected()) {
        m_manager->disconnectDevice();
        this->m_tabs->setDisabled(true);

    } else {
        QString portName = m_portBox->currentData().toString();
        if(portName.isEmpty()) {
            QMessageBox::warning(this, "Error", "No port selected.");
            return;
        }
        this->m_tabs->setDisabled(false);
        m_manager->connectDevice(portName);
    }
}

void MainWindow::onReadInfo() {
    if(!m_manager->isConnected()) {
        QMessageBox::warning(this, "Not Connected", "Please connect to device first!");
        onLog("❌ Read Info Failed - Not connected");
        return;
    }

    showGlobalProgress("Reading Device Information...", 0);
    onLog("📖 Reading device identity information...");

    // Set operation timeout (10 seconds)
    m_currentOperationPath = "Read Identity";
    m_operationTimeout->start();

    m_manager->readIdentity();
}

void MainWindow::onListEfs() {
    if(!m_manager->isConnected()) {
        QMessageBox::warning(this, "Not Connected", "Please connect to device first!\n\nPlease connect to your device before browsing EFS.");
        onLog("❌ List EFS Failed - Not connected");
        return;
    }

    // SIM 2: If no discovered root yet, start auto-probe
    if (getActiveSubscription() == 1 && m_sim2DiscoveredRoot.isEmpty()) {
        startSim2Probe();
        return;
    }

    QString p = m_pathEdit->text();
    if(p.isEmpty()) p = "/";
    if(!p.endsWith("/") && p != "/") p+="/";

    // SIM 2 path translation: User sees relative path (e.g. / or /policyman/), send real path to EFS engine
    QString actualPath = p;
    if (getActiveSubscription() == 1 && !m_sim2DiscoveredRoot.isEmpty()) {
        if (p == "/") {
            actualPath = m_sim2DiscoveredRoot;
        } else if (!p.startsWith(m_sim2DiscoveredRoot)) {
            if (p.startsWith("/")) {
                actualPath = m_sim2DiscoveredRoot + p.mid(1);
            } else {
                actualPath = m_sim2DiscoveredRoot + p;
            }
        }
    }

    // Mask internal base path in path edit (show clean relative path like / or /policyman/)
    if (getActiveSubscription() == 1 && !m_sim2DiscoveredRoot.isEmpty() && actualPath.startsWith(m_sim2DiscoveredRoot)) {
        QString relPath = actualPath.mid(m_sim2DiscoveredRoot.length() - 1);
        if (relPath.isEmpty()) relPath = "/";
        m_pathEdit->setText(relPath);
    } else {
        m_pathEdit->setText(p);
    }

    QTreeWidgetItem *parent = findOrCreateParent(actualPath);
    if(parent) {
        while(parent->childCount() > 0) delete parent->takeChild(0);
    }

    // Strict Reset
    m_recursiveOp = RecursiveOp::None;
    m_scanQueue.clear();

    m_listTimeout->stop();
    m_operationTimeout->stop();
    m_currentOperationPath = actualPath;
    m_listTimeout->start();
    m_operationTimeout->start();
    m_manager->listEfsDirectory(actualPath);
}

void MainWindow::onRefreshCurrent() {
    QTreeWidgetItem *item = m_efsTree->currentItem();
    if(!item) return;
    QString path = item->data(0, Qt::UserRole).toString();
    if(item->text(2) == "Directory") {
        m_pathEdit->setText(path);
        onListEfs();
    }
}

void MainWindow::onDownloadSelected() {
    QTreeWidgetItem *item = m_efsTree->currentItem();
    if(!item) return;

    QString path = item->data(0, Qt::UserRole).toString();
    bool isDir = (item->text(2) == "Directory");

    if(isDir) {
        // Use Smart Scanner for directory download
        startRecursiveScan(path, RecursiveOp::Download);
    } else {
        // Single file download
        QString fileName = path.section('/', -1);
        QString savePath = QFileDialog::getSaveFileName(this, "Save File", fileName);
        if(savePath.isEmpty()) return;

        m_downloadFileList.clear();
        m_downloadFileList.append(path);
        m_downloadBasePath = QFileInfo(savePath).path(); // Just directory for single file?
        // Actually for single file we usually just write to file directly.
        // But to reuse logic, let's just do direct read.

        QFile f(savePath);
        if(f.open(QIODevice::WriteOnly)) f.close(); // Check writable

        m_manager->readFile(path);

        showProgress("📥 Downloading", "Downloading file...");
        // Saving happens in onFileDataReceived which needs to know target.
        // Needs a member var for single download path target.
        m_downloadBasePath = savePath; // We'll handle single file special case in onFileDataReceived
    }
}

void MainWindow::onUploadFileToSelected()
{
    QTreeWidgetItem *item = m_efsTree->currentItem();
    if(!item) return;
    QString path = item->data(0, Qt::UserRole).toString();

    QString f = QFileDialog::getOpenFileName(this, "Select File to Upload");
    if(f.isEmpty()) return;

    QString target = path;
    if(item->text(2) == "Directory") {
        if(!target.endsWith("/")) target += "/";
        target += QFileInfo(f).fileName();
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("⚠️ Confirm Write Operation");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(QString(
        "<h3>Upload File</h3>"
        "<p><b>File:</b> %1</p>"
        "<p><b>Target:</b> %2</p>"
        "<hr><p><b>⚠️ This will overwrite if file exists!</b></p>"
        "<p>Always backup important files first.</p>"
    ).arg(QFileInfo(f).fileName()).arg(target));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);

    if(msgBox.exec() != QMessageBox::Yes) return;

    QFile file(f);
    if(file.open(QIODevice::ReadOnly)) {
        m_lastWrittenPath = target;
        showProgress("📤 Uploading File", "Writing: " + target);
        m_manager->writeFile(target, file.readAll());
        file.close();
    }
}

void MainWindow::onWriteFolderToSelected() {
    QTreeWidgetItem *item = m_efsTree->currentItem();
    if(!item || item->text(2) != "Directory") return;

    QString targetPath = item->data(0, Qt::UserRole).toString();
    QString dir = QFileDialog::getExistingDirectory(this, "Select Folder to Upload");
    if(dir.isEmpty()) return;

    QString folderName = QFileInfo(dir).fileName();

    // Count files
    int fileCount = 0;
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while(it.hasNext()) { it.next(); fileCount++; }

    if(fileCount == 0) {
        QMessageBox::information(this, "Empty Folder", "The selected folder is empty.");
        return;
    }

    // Ask user: Upload folder itself or just contents?
    QMessageBox choiceBox(this);
    choiceBox.setWindowTitle("📁 Folder Upload Options");
    choiceBox.setIcon(QMessageBox::Question);
    choiceBox.setText(QString(
        "<h3>Upload Options</h3>"
        "<p><b>Folder:</b> %1</p>"
        "<p><b>Files:</b> %2</p>"
        "<p><b>Target:</b> %3</p>"
    ).arg(folderName).arg(fileCount).arg(targetPath));

    QPushButton *btnWithFolder = choiceBox.addButton("📁 Upload Folder + Contents", QMessageBox::ActionRole);
    QPushButton *btnContentsOnly = choiceBox.addButton("📄 Upload Contents Only", QMessageBox::ActionRole);
    choiceBox.addButton(QMessageBox::Cancel);

    choiceBox.exec();

    if(choiceBox.clickedButton() == (QAbstractButton*)choiceBox.button(QMessageBox::Cancel)) {
        return;
    }

    bool uploadFolderItself = (choiceBox.clickedButton() == btnWithFolder);
    (void)btnContentsOnly;

    QString finalTarget = targetPath;
    if(!finalTarget.endsWith("/")) finalTarget += "/";

    if(uploadFolderItself) {
        // Check if folder already exists
        QString fullTargetPath = finalTarget + folderName;
        QTreeWidgetItem *parentItem = m_nodeMap.value(finalTarget, nullptr);
        bool folderExists = false;

        if(parentItem) {
            for(int i = 0; i < parentItem->childCount(); i++) {
                if(parentItem->child(i)->text(0) == folderName &&
                   parentItem->child(i)->text(2) == "Directory") {
                    folderExists = true;
                    break;
                }
            }
        }

        if(folderExists) {
            QMessageBox warnBox(this);
            warnBox.setWindowTitle("⚠️ Folder Exists");
            warnBox.setIcon(QMessageBox::Warning);
            warnBox.setText(QString(
                "<h3>Folder Already Exists</h3>"
                "<p>A folder with this name already exists at:</p>"
                "<p><b>%2</b></p>"
            ).arg(folderName).arg(fullTargetPath));

            QPushButton *btnMerge = warnBox.addButton("Merge/Overwrite", QMessageBox::ActionRole);
            QPushButton *btnRoot = warnBox.addButton("Upload to Root Instead", QMessageBox::ActionRole);
            warnBox.addButton(QMessageBox::Cancel);

            warnBox.exec();

            if(warnBox.clickedButton() == (QAbstractButton*)warnBox.button(QMessageBox::Cancel)) {
                return;
            } else if(warnBox.clickedButton() == btnRoot) {
                finalTarget = "/";
            }
            (void)btnMerge;
        }

        finalTarget += folderName + "/";
    }

    // Final confirmation
    QMessageBox msgBox(this);
    msgBox.setWindowTitle("⚠️ Confirm Upload");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setText(QString(
        "<h3>Upload Folder</h3>"
        "<p><b>Files:</b> %1</p>"
        "<p><b>Target:</b> %2</p>"
        "<hr><p><b>⚠️ Existing files will be overwritten!</b></p>"
    ).arg(fileCount).arg(finalTarget));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

    if(msgBox.exec() != QMessageBox::Yes) return;

    // Collect all files to upload first
    QStringList filePairs; // localPath|efsPath pairs
    QString efsTarget = finalTarget;
    if(efsTarget.startsWith("/")) efsTarget.remove(0, 1);

    QDirIterator iter(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while(iter.hasNext()) {
        QString localPath = iter.next();
        QString relPath = localPath.mid(dir.length());
        relPath.replace("\\", "/");

        // Remove leading slashes
        while(relPath.startsWith("/")) {
            relPath.remove(0, 1);
        }

        QString targetFilePath = efsTarget;
        if(!targetFilePath.isEmpty() && !targetFilePath.endsWith("/")) {
            targetFilePath += "/";
        }
        targetFilePath += relPath;

        filePairs.append(localPath + "|" + targetFilePath);
    }

    if(filePairs.isEmpty()) {
        QMessageBox::information(this, "Empty Folder", "No files to upload.");
        return;
    }

    // Now start uploading one by one
    m_uploadFileList = filePairs;
    m_uploadCurrentIndex = 0;
    m_lastWrittenPath = finalTarget;

    showProgress("📁 Uploading Folder", QString("Uploading %1 files...").arg(filePairs.size()));
    if(m_progressDlg) {
        m_progressDlg->setProgress(0, filePairs.size());
    }

    // Upload first file
    QStringList parts = m_uploadFileList[0].split("|");
    if(parts.size() == 2) {
        QFile file(parts[0]);
        if(file.open(QIODevice::ReadOnly)) {
            m_manager->writeFile(parts[1], file.readAll());
            file.close();
        }
    }
}

void MainWindow::onDeleteSelected() {
    QTreeWidgetItem *item = m_efsTree->currentItem();
    if(!item) return;

    QString path = item->data(0, Qt::UserRole).toString();
    bool isDir = (item->text(2) == "Directory");

    if(isDir) {
        // Use Smart Scanner for directory delete
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("🗑️ Confirm Delete");
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText(QString(
            "<h3>Delete Folder?</h3>"
            "<p><b>Folder:</b> %1</p>"
            "<p>This will scan and delete all contents.</p>"
            "<hr><p><b>⚠️ IRREVERSIBLE ACTION!</b></p>"
        ).arg(path));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

        if(msgBox.exec() != QMessageBox::Yes) return;

        startRecursiveScan(path, RecursiveOp::Delete);
    } else {
        // Single file delete
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("🗑️ Confirm Delete");
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText(QString(
            "<h3>Delete File</h3>"
            "<p><b>File:</b> %1</p>"
            "<hr><p><b>⚠️ IRREVERSIBLE!</b></p>"
        ).arg(path));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

        if(msgBox.exec() != QMessageBox::Yes) return;

        showProgress("🗑️ Deleting File", "Deleting: " + path);
        m_manager->deleteFile(path);
    }
}

void MainWindow::onContextMenu(const QPoint &pos) {
    QTreeWidgetItem *item = m_efsTree->itemAt(pos);
    QMenu menu(this);

    if(item) {
        QString path = item->data(0, Qt::UserRole).toString();
        bool isDir = (item->text(2) == "Directory");

        if(isDir) {
            menu.addAction("🔄 Refresh", [this, path](){ m_pathEdit->setText(path); onListEfs(); });
            menu.addAction("📤 Upload File Here...", [this](){ onUploadFileToSelected(); });
            menu.addAction("📁 Upload Folder Here...", [this](){ onWriteFolderToSelected(); });
            menu.addAction("📥 Download Folder...", [this](){ onDownloadSelected(); });
            menu.addAction("🗑️ Delete (Recursive)", [this](){ onDeleteSelected(); });
        } else {
            menu.addAction("📥 Download", [this](){ onDownloadSelected(); });
            menu.addAction("🗑️ Delete", [this](){ onDeleteSelected(); });
        }
    } else {
        menu.addAction("🔄 Refresh Root", [this](){ m_pathEdit->setText("/"); onListEfs(); });
    }

    menu.exec(m_efsTree->viewport()->mapToGlobal(pos));
}

void MainWindow::onTreeDoubleClicked(QTreeWidgetItem *item, int) {
    if(!item) return;
    if(item->text(2) == "Directory") {
        QString path = item->data(0, Qt::UserRole).toString();
        m_pathEdit->setText(path);

        // Strict Reset
        m_recursiveOp = RecursiveOp::None;
        m_scanQueue.clear();

        onListEfs(); // onListEfs also resets, but explicit here is safer/clearer
    }
    // Files: do nothing on double-click (user must use Download button)
}

// ============ HELPERS ============

QTreeWidgetItem* MainWindow::findOrCreateParent(const QString &path) {
    if(path == "/") return m_nodeMap.value("/", nullptr);

    // Normalize path: ensure it ends with / for directories
    QString normalized = path;
    if(!normalized.endsWith("/")) normalized += "/";

    // Check if already exists in map
    if(m_nodeMap.contains(normalized)) return m_nodeMap[normalized];

    // Safety: Check if this path is UNDER our known roots
    // Find the deepest root that is a prefix of this path
    QString bestRoot;
    for (auto it = m_nodeMap.constBegin(); it != m_nodeMap.constEnd(); ++it) {
        if (normalized.startsWith(it.key()) && it.key().length() > bestRoot.length()) {
            bestRoot = it.key();
        }
    }
    if (bestRoot.isEmpty()) {
        // No known root is a prefix — cannot create parent safely
        qDebug() << "⚠️ findOrCreateParent: No root found for path:" << normalized;
        return nullptr;
    }

    // Get parent path
    QString parentPath = normalized;
    // Remove trailing slash, then remove last segment
    if (parentPath.endsWith("/")) parentPath.chop(1);
    int lastSlash = parentPath.lastIndexOf('/');
    if (lastSlash >= 0) {
        parentPath = parentPath.left(lastSlash + 1);
    } else {
        parentPath = "/";
    }
    if (parentPath.isEmpty()) parentPath = "/";

    // Safety: Don't recurse above our known root
    if (!m_nodeMap.contains(parentPath) && parentPath.length() <= bestRoot.length()) {
        return m_nodeMap.value(bestRoot, nullptr);
    }

    // Recursively get/create parent
    QTreeWidgetItem *parentItem = findOrCreateParent(parentPath);
    if(!parentItem) return nullptr;

    // Get folder name
    QString name = normalized.section('/', -2, -2);
    if(name.isEmpty()) return nullptr;

    // Check if child already exists
    for(int i=0; i<parentItem->childCount(); ++i) {
        if(parentItem->child(i)->text(0) == name) {
            QTreeWidgetItem *found = parentItem->child(i);
            m_nodeMap[normalized] = found;
            return found;
        }
    }

    // Create new directory item
    QTreeWidgetItem *newItem = new QTreeWidgetItem(parentItem);
    newItem->setText(0, name);
    newItem->setText(2, "Directory");
    newItem->setIcon(0, m_iconFolder);
    newItem->setData(0, Qt::UserRole, normalized);
    m_nodeMap[normalized] = newItem;

    return newItem;
}

void MainWindow::refreshFolder(const QString &path) {
    // Smart refresh: only refresh specific folder without clearing whole tree
    QString refreshPath = path;
    if(!refreshPath.endsWith("/")) refreshPath += "/";

    QTreeWidgetItem *folderItem = m_nodeMap.value(refreshPath, nullptr);
    if(folderItem) {
        // Clear only this folder's children
        while(folderItem->childCount() > 0) {
            delete folderItem->takeChild(0);
        }
    }

    // List this folder to repopulate
    m_manager->listEfsDirectory(refreshPath);
}

// ============ DEVICE MANAGER RESPONSES ============

void MainWindow::onEfsEntry(const QString &parentPath, const EfsEntry &entry) {
    // Skip tree updates during SIM 2 path probing (we only care if listing succeeds)
    if (m_sim2Probing) return;
    // 1. Scanner Logic
    if(m_recursiveOp != RecursiveOp::None) {
        QString fullPath = parentPath;
        if(!fullPath.endsWith("/")) fullPath += "/";
        fullPath += entry.name;

        if(entry.isDir) {
           m_scanQueue.enqueue(fullPath);
        } else {
           m_targetFileList.append(fullPath);
        }
    }

    // 2. UI Update Logic (Original)
    QTreeWidgetItem *p = findOrCreateParent(parentPath);
    if(!p) return;

    if(p->childCount() == 1 && p->child(0)->text(0) == "__dummy__") {
        delete p->takeChild(0);
    }

    for(int i=0; i<p->childCount(); ++i) {
        if(p->child(i)->text(0)==entry.name) return; // Already exists
    }

    QTreeWidgetItem *item = new QTreeWidgetItem(p);
    item->setText(0, entry.name);
    item->setText(1, QString::number(entry.size));
    item->setText(2, entry.isDir ? "Directory" : "File");
    item->setIcon(0, entry.isDir ? m_iconFolder : m_iconFile);

    QString full = parentPath;
    if(!full.endsWith("/")) full+="/";
    full += entry.name;

    if(entry.isDir) {
        if(!full.endsWith("/")) full+="/";
        m_nodeMap[full] = item;

        QTreeWidgetItem *dummy = new QTreeWidgetItem(item);
        dummy->setText(0, "__dummy__");
    }

    item->setData(0, Qt::UserRole, full);
}

void MainWindow::onEfsListComplete(const QString &path, bool success) {
    m_listTimeout->stop();
    m_operationTimeout->stop();

    // === SIM 2 Auto-Probe Handler ===
    if (m_sim2Probing) {
        if (success) {
            // Found working SIM 2 root!
            m_sim2DiscoveredRoot = path;
            if (!m_sim2DiscoveredRoot.endsWith("/")) m_sim2DiscoveredRoot += "/";
            m_sim2Probing = false;
            qDebug() << "✅ SIM 2 Auto-Probe SUCCESS: Root =" << m_sim2DiscoveredRoot;
            onLog(QString("✅ SIM 2 EFS root discovered successfully"));
            hideGlobalProgress();

            // Rebuild tree with discovered root
            m_efsTree->clear();
            m_nodeMap.clear();

            QTreeWidgetItem *root = new QTreeWidgetItem(m_efsTree);
            root->setText(0, "/ (SIM 2)");
            root->setText(2, "Directory");
            root->setIcon(0, m_iconFolder);
            root->setData(0, Qt::UserRole, m_sim2DiscoveredRoot);
            m_nodeMap[m_sim2DiscoveredRoot] = root;
            m_pathEdit->setText("/");  // Hide internal path from user

            // Now do the real listing
            onListEfs();
        } else {
            // Try next path
            m_sim2ProbeIndex++;
            updateGlobalProgress(m_sim2ProbeIndex, QString("Trying path %1/%2...").arg(m_sim2ProbeIndex + 1).arg(m_sim2ProbePaths.size()));
            if (m_sim2ProbeIndex < m_sim2ProbePaths.size()) {
                QString nextPath = m_sim2ProbePaths[m_sim2ProbeIndex];
                qDebug() << "🔍 SIM 2 Probe" << m_sim2ProbeIndex + 1 << "/" << m_sim2ProbePaths.size() << ":" << nextPath;
                onLog(QString("🔍 Probing SIM 2 path %1/%2...").arg(m_sim2ProbeIndex + 1).arg(m_sim2ProbePaths.size()));

                m_currentOperationPath = nextPath;
                m_operationTimeout->start();
                m_manager->listEfsDirectory(nextPath);
            } else {
                // All paths tried, none worked
                m_sim2Probing = false;
                hideGlobalProgress();
                qDebug() << "❌ SIM 2 Auto-Probe FAILED: No valid root found. Tried" << m_sim2ProbePaths.size() << "paths";
                onLog("❌ SIM 2: No valid EFS root found on this device");

                QMessageBox::warning(this, "SIM 2 Not Found",
                    "❌ Could not find SIM 2 EFS partition on this device.\n\n"
                    "Possible reasons:\n"
                    "• Device does not support dual SIM\n"
                    "• Device uses eSIM (different access method)\n"
                    "• SIM 2 slot is empty\n"
                    "• Device requires special unlock first\n\n"
                    "💡 Try:\n"
                    "1. Insert a SIM card in slot 2\n"
                    "2. Use Zero SPC / Bypass Security first\n"
                    "3. Switch back to SIM 1 and try EFS there");
            }
        }
        return;
    }

    if(!success) {
        onLog("⚠️ List Failed/Skipped: " + path);
    }

    // Scanner Logic
    if(m_recursiveOp != RecursiveOp::None) {
        if(!m_scanQueue.isEmpty()) {
            QString nextPath = m_scanQueue.dequeue();
            // Update scanning progress with helpful stats
            showProgress("🔍 Scanning...",
                         QString("Directory: %1\nFiles Found: %2\n\nChecking: %3")
                         .arg(path).arg(m_targetFileList.size()).arg(nextPath));

            m_currentOperationPath = nextPath;
            m_manager->listEfsDirectory(nextPath);
            m_listTimeout->start();
            m_operationTimeout->start();
        } else {
            // Scan Complete
            executeRecursiveOp();
        }
    } else {
        // Standard UI Refresh
        if(success) {
            onLog("✅ Listed: " + path);
            QTreeWidgetItem *p = findOrCreateParent(path);
            if(p) {
                m_efsTree->expandItem(p);
            }
        }
        hideProgress();
    }

    // Clear op path if we are done or moving to next (set above)
    if(m_recursiveOp == RecursiveOp::None) {
       m_currentOperationPath.clear();
    }
}

void MainWindow::markFolderAsCorrupted(const QString &path) {
    if(path != m_currentOperationPath && !m_currentOperationPath.isEmpty()) {
        onLog("⚠️ Skipping corrupt mark (not current operation): " + path);
        return;
    }

    QTreeWidgetItem *item = nullptr;

    QString searchPath = path;
    if(!searchPath.endsWith("/")) searchPath += "/";
    item = m_nodeMap.value(searchPath, nullptr);

    if(!item) {
        QTreeWidgetItemIterator it(m_efsTree);
        while (*it) {
            QString itemPath = (*it)->data(0, Qt::UserRole).toString();
            if(itemPath == path || itemPath == searchPath) {
                item = *it;
                break;
            }
            ++it;
        }
    }

    if(item) {
        QFont f = item->font(0);
        f.setStrikeOut(true);
        f.setBold(true);
        item->setFont(0, f);
        item->setToolTip(0, "⚠️ Corrupted or inaccessible directory");
        item->setDisabled(false);

        onLog("⚠️ Directory is corrupted or inaccessible: " + path);
    } else {
        onLog("⚠️ Could not mark folder (not found): " + path);
    }

    m_currentOperationPath.clear();
}

void MainWindow::onFileDataReceived(const QString &name, const QByteArray &data) {
    // Error Handling: If data is empty, it means read failed (protected file, etc.)
    if(data.isEmpty()) {
        onLog("⚠️ Skipped protected/unreadable file: " + name);
        if(!m_downloadFileList.isEmpty()) {
           // Proceed to next file in queue
        } else {
           hideProgress();
           QMessageBox::warning(this, "Error", "Failed to read file (Protected or Access Denied): " + name);
           return;
        }
    } else {
        // Validation logic - if we are in recursive mode
       if(!m_downloadFileList.isEmpty()) {
            QString relativePath = name;
            // Try to make relative path clean
            if(relativePath.startsWith(m_recursiveRootPath)) {
                relativePath.remove(0, m_recursiveRootPath.length());
            }
            while(relativePath.startsWith("/")) relativePath.remove(0, 1);

            QString targetPath = m_downloadBasePath;
            if(!targetPath.endsWith("/")) targetPath += "/";
            targetPath += relativePath;

            QFileInfo fi(targetPath);
            QDir d = fi.dir();
            if(!d.exists()) d.mkpath(".");

            QFile f(targetPath);
            if(f.open(QIODevice::WriteOnly)) {
                f.write(data);
                f.close();
            }
       } else {
           // Single file download
           QString s = m_downloadBasePath;
           if(!s.isEmpty()) {
                QFile f(s);
                if(f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    f.close();
                    onLog("✅ Saved: "+s);
                }
            }
       }
    }

    if(!m_downloadFileList.isEmpty()) {
        m_downloadCurrentIndex++;

        if(m_progressDlg) {
            m_progressDlg->setProgress(m_downloadCurrentIndex, m_downloadFileList.size());

            QString currentFile = m_downloadFileList.value(m_downloadCurrentIndex);
            int remaining = m_downloadFileList.size() - m_downloadCurrentIndex;

            QString status = QString(
                "📥 <b>Downloading...</b><br>"
                "📄 <b>File:</b> %1<br>"
                "📊 <b>Progress:</b> %2 / %3<br>"
                "⏳ <b>Remaining:</b> %4 files"
            ).arg(QFileInfo(currentFile).fileName())
             .arg(m_downloadCurrentIndex)
             .arg(m_downloadFileList.size())
             .arg(remaining);

            m_progressDlg->setStatus(status);
            QApplication::processEvents();
        }

        if(m_downloadCurrentIndex < m_downloadFileList.size()) {
            m_currentOperationPath = m_downloadFileList[m_downloadCurrentIndex]; // Track for timeout log
            m_operationTimeout->start(); // RESTART WATCHDOG
            m_manager->readFile(m_downloadFileList[m_downloadCurrentIndex]);
        } else {
            hideProgress();
            onLog(QString("✅ Folder download complete: %1 files saved to %2").arg(m_downloadFileList.size()).arg(m_downloadBasePath));
            m_downloadFileList.clear();
            m_downloadCurrentIndex = 0;
            m_downloadBasePath.clear();
        }
    } else {
         hideProgress();
         QString parent = name.section('/',0,-2);
         if(parent.isEmpty()) parent="/";
         if(!parent.endsWith("/")) parent+="/";
         refreshFolder(parent);
    }
}

void MainWindow::onFileWritten(const QString &name, bool success) {
    if(success) {
        onLog("✅ Write Success: " + name);
    } else {
        onLog("⚠️ Write Skipped (Protected/Error): " + name);
    }

    // Check if we are in recursive upload mode
    if(!m_uploadFileList.isEmpty()) {
        m_uploadCurrentIndex++;

        if(m_progressDlg) {
            m_progressDlg->setProgress(m_uploadCurrentIndex, m_uploadFileList.size());

            // Get current file being uploaded (about to start next one or just finished one?
            // We incremented index, checking next one.
            QString nextFilePair = m_uploadFileList.value(m_uploadCurrentIndex);
            QString targetPath = nextFilePair.split("|").last();
            int remaining = m_uploadFileList.size() - m_uploadCurrentIndex;

            QString status = QString(
                "📤 <b>Uploading...</b><br>"
                "📄 <b>File:</b> %1<br>"
                "📊 <b>Progress:</b> %2 / %3<br>"
                "⏳ <b>Remaining:</b> %4 files"
            ).arg(QFileInfo(targetPath).fileName())
             .arg(m_uploadCurrentIndex)
             .arg(m_uploadFileList.size())
             .arg(remaining);

            m_progressDlg->setStatus(status);
            QApplication::processEvents();
        }

        if(m_uploadCurrentIndex < m_uploadFileList.size()) {
            // Upload next file
            QStringList parts = m_uploadFileList[m_uploadCurrentIndex].split("|");
            // parts[0] = localPath, parts[1] = targetPath
            if(parts.size() == 2) {
                QFile f(parts[0]);
                if(f.open(QIODevice::ReadOnly)) { // Sync read, fast
                    m_currentOperationPath = parts[1]; // Track target
                    m_operationTimeout->start(); // RESTART WATCHDOG
                    m_manager->writeFile(parts[1], f.readAll());
                    f.close();
                } else {
                     onLog("⚠️ Failed to open local file: " + parts[0]);
                     onFileWritten("SKIPPED_ERROR", false);
                }
            } else {
                onFileWritten("SKIPPED_INVALID", false);
            }
        } else {
            // All done
            hideProgress();
            onLog(QString("✅ Folder upload complete: %1 files").arg(m_uploadFileList.size()));

            // Smart refresh
            QString parent = m_lastWrittenPath;
            if(!parent.endsWith("/")) parent += "/";
            refreshFolder(parent);

            m_uploadFileList.clear();
            m_uploadCurrentIndex = 0;
            m_lastWrittenPath.clear();
        }
        return;
    }

    // Single file write handling
    if(!m_lastWrittenPath.isEmpty()) {
        return;
    }

    hideProgress();

    QString parent = name.section('/',0,-2);
    if(parent.isEmpty()) parent="/";
    if(!parent.endsWith("/")) parent+="/";
    refreshFolder(parent);
}

void MainWindow::onFileDeleted(const QString &name, bool success) {
    if(success) {
        onLog("✅ Deleted: " + name);
    } else {
        onLog("⚠️ Delete Skipped (Protected/Error): " + name);
    }

    if(!m_deleteFileList.isEmpty() && m_deleteCurrentIndex < m_deleteFileList.size()) {
        m_deleteCurrentIndex++;

        if(m_progressDlg) {
            m_progressDlg->setProgress(m_deleteCurrentIndex, m_deleteFileList.size());

            QString currentFile = m_deleteFileList.value(m_deleteCurrentIndex);
            int remaining = m_deleteFileList.size() - m_deleteCurrentIndex;

            QString status = QString(
                "🗑️ <b>Deleting...</b><br>"
                "📄 <b>File:</b> %1<br>"
                "📊 <b>Progress:</b> %2 / %3<br>"
                "⏳ <b>Remaining:</b> %4 files"
            ).arg(QFileInfo(currentFile).fileName())
             .arg(m_deleteCurrentIndex)
             .arg(m_deleteFileList.size())
             .arg(remaining);

            m_progressDlg->setStatus(status);
            QApplication::processEvents();
        }

        if(m_deleteCurrentIndex < m_deleteFileList.size()) {
            m_currentOperationPath = m_deleteFileList[m_deleteCurrentIndex];
            m_operationTimeout->start(); // RESTART WATCHDOG
            m_manager->deleteFile(m_deleteFileList[m_deleteCurrentIndex]);
        } else {
            hideProgress();
            onLog(QString("✅ Folder delete complete: %1 files processed").arg(m_deleteFileList.size()));

            QString parent = m_pathEdit->text();
            if(parent.isEmpty()) parent = "/";
            if(!parent.endsWith("/")) parent += "/";
            refreshFolder(parent);

            m_deleteFileList.clear();
            m_deleteCurrentIndex = 0;
        }
    } else {
        hideProgress();

        QString parent = name.section('/',0,-2);
        if(parent.isEmpty()) parent="/";
        if(!parent.endsWith("/")) parent+="/";
        refreshFolder(parent);
    }
}

void MainWindow::onSelectionChanged() {
    QTreeWidgetItem *item = m_efsTree->currentItem();
    if(!item) {
        m_btnRefresh->setEnabled(false);
        m_btnDownload->setEnabled(false);
        m_btnUploadFile->setEnabled(false);
        m_btnUploadFolder->setEnabled(false);
        m_btnDelete->setEnabled(false);
        return;
    }

    bool isDir = (item->text(2) == "Directory");
    m_btnRefresh->setEnabled(isDir);
    m_btnDownload->setEnabled(true);
    m_btnUploadFile->setEnabled(isDir);
    m_btnUploadFolder->setEnabled(isDir);
    m_btnDelete->setEnabled(true);
}

void MainWindow::onLog(const QString &msg) {
    if(m_console) m_console->append(msg);
}

void MainWindow::onIdentityData(const QVariantMap &d) {
    // Stop operation timeout
    m_operationTimeout->stop();

    // Update SIM 1 UI labels
    m_lblImei->setText(d.value("imei", "-").toString());
    m_lblImei2->setText(d.value("imei2", "-").toString());
    m_lblImsi->setText(d.value("imsi", "-").toString());
    m_lblMdn->setText(d.value("mdn", "-").toString());
    m_lblBanner->setText(d.value("banner", "-").toString());

    QString mode = d.value("mode", "Unknown").toString();
    m_lblMode->setText(mode);
    if(mode == "Online") m_lblMode->setStyleSheet("color: lime; font-weight: bold;");
    else m_lblMode->setStyleSheet("color: orange;");

    m_lblEsn->setText(d.value("esn", "-").toString());
    m_lblMeid->setText(d.value("meid", "-").toString());
    m_lblVersion->setText(d.value("version", "-").toString());

    // Update SIM 2 labels (from IMEI2 and dual_sim flag)
    if (d.value("dual_sim", false).toBool()) {
        m_lblImeiSim2->setText(d.value("imei2", "-").toString());
        m_lblImeiSim2->setStyleSheet("color: #ffaa00;");
    } else {
        m_lblImeiSim2->setText("Not Detected");
        m_lblImeiSim2->setStyleSheet("color: #666;");
    }

    // Hide progress and log success
    hideGlobalProgress();
    onLog("✅ Device information retrieved successfully");
}

void MainWindow::on_manager_connected(bool ok) {
    if(ok) {
        m_connectBtn->setText("Disconnect");
        m_lblStatus->setText("Connected");
        m_lblStatus->setStyleSheet("color: lime; font-weight: bold;");

        // NOTE: Auto-execute removed - user can manually trigger Zero SPC/Bypass if needed
        onLog("✅ Connected - Device ready");

    } else {
        m_connectBtn->setText("Connect");
        m_lblStatus->setText("Disconnected");
        m_lblStatus->setStyleSheet("color: red; font-weight: bold;");
        m_lblMode->setText("-");
        m_lblMode->setStyleSheet("color: gray;");
    }
}

// ============ RECURSIVE SCANNER HELPERS ============

void MainWindow::startRecursiveScan(const QString &path, RecursiveOp op) {
    m_recursiveOp = op;
    m_recursiveRootPath = path;
    if(!m_recursiveRootPath.endsWith("/")) m_recursiveRootPath += "/";

    m_scanQueue.clear();
    m_targetFileList.clear();

    m_scanQueue.enqueue(m_recursiveRootPath);

    showProgress("🔍 Scanning...", "Discovering files/folders in " + path);
    if(m_progressDlg) {
        m_progressDlg->showIndeterminate();
        m_progressDlg->setStatus("Indexing directory structure...");
    }

    // Kick off first list
    if(!m_scanQueue.isEmpty()) {
        QString first = m_scanQueue.dequeue();
        m_currentOperationPath = first;
        m_manager->listEfsDirectory(first);
        m_listTimeout->start();
        m_operationTimeout->start();
    }
}

void MainWindow::executeRecursiveOp() {
    // 1. Check results
    if(m_targetFileList.isEmpty()) {
        hideProgress();
        QMessageBox::information(this, "Scan Complete", "No files found to process.");
        m_recursiveOp = RecursiveOp::None;
        return;
    }

    // 2. Handle Operation
    if(m_recursiveOp == RecursiveOp::Download) {
        hideProgress(); // Hide scan progress

        QString saveDir = QFileDialog::getExistingDirectory(this, "Select Download Folder");
        if(saveDir.isEmpty()) {
            m_recursiveOp = RecursiveOp::None;
            return;
        }

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("📥 Confirm Download");
        msgBox.setIcon(QMessageBox::Question);
        msgBox.setText(QString(
            "<h3>Download Discovered Files</h3>"
            "<p><b>Source:</b> %1</p>"
            "<p><b>Files Found:</b> %2</p>"
            "<p><b>Save to:</b> %3</p>"
        ).arg(m_recursiveRootPath).arg(m_targetFileList.size()).arg(saveDir));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

        if(msgBox.exec() != QMessageBox::Yes) {
            m_recursiveOp = RecursiveOp::None;
            return;
        }

        m_downloadFileList = m_targetFileList;
        m_downloadCurrentIndex = 0;
        m_downloadBasePath = saveDir;

        showProgress("📥 Downloading Folder", QString("Downloading %1 files...").arg(m_downloadFileList.size()));
        if(m_progressDlg) m_progressDlg->setProgress(0, m_downloadFileList.size());

        m_recursiveOp = RecursiveOp::None; // Scan done

        // Start first file
        m_currentOperationPath = m_downloadFileList[0];
        m_operationTimeout->start();
        m_manager->readFile(m_downloadFileList[0]);

    } else if (m_recursiveOp == RecursiveOp::Delete) {
        hideProgress();

        QMessageBox msgBox(this);
        msgBox.setWindowTitle("🗑️ Confirm Recursive Delete");
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setText(QString(
            "<h3>Delete Discovered Files</h3>"
            "<p><b>Target:</b> %1</p>"
            "<p><b>Files to Delete:</b> %2</p>"
            "<hr><p><b>⚠️ IRREVERSIBLE!</b> All found files will be deleted.</p>"
        ).arg(m_recursiveRootPath).arg(m_targetFileList.size()));
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

        if(msgBox.exec() != QMessageBox::Yes) {
            m_recursiveOp = RecursiveOp::None;
            return;
        }

        m_deleteFileList = m_targetFileList;
        m_deleteCurrentIndex = 0;

        showProgress("🗑️ Deleting Folder", QString("Deleting %1 files...").arg(m_deleteFileList.size()));
        if(m_progressDlg) m_progressDlg->setProgress(0, m_deleteFileList.size());

        m_recursiveOp = RecursiveOp::None; // Scan done

        // Start first file
        m_currentOperationPath = m_deleteFileList[0];
        m_operationTimeout->start();
        m_manager->deleteFile(m_deleteFileList[0]);
    } else {
        m_recursiveOp = RecursiveOp::None;
        hideProgress();
    }
}
// ============ ADVANCED COMMAND SLOTS ============

void MainWindow::onZeroSPC() {
    if(!m_manager->isConnected()) {
        QMessageBox::warning(this, "Not Connected", "Please connect to device first!");
        onLog("❌ Zero SPC Failed - Not connected");
        return;
    }

    showGlobalProgress("Sending Zero SPC Command...", 0);
    onLog("📤 Sending Zero SPC...");
    m_manager->zeroSPC();

    QTimer::singleShot(300, this, [this](){
        hideGlobalProgress();
        onLog("✅ Zero SPC Command Sent Successfully");
    });
}

void MainWindow::onBypassSecurity() {
    if(!m_manager->isConnected()) {
        QMessageBox::warning(this, "Not Connected", "Please connect to device first!");
        onLog("❌ Bypass Security Failed - Not connected");
        return;
    }

    showGlobalProgress("Sending 11 Security Bypass Commands...", 11);
    onLog("📤 Starting Security Bypass (11 commands)...");

    // Simulate progress updates
    for(int i = 1; i <= 11; i++) {
        QTimer::singleShot(i * 60, this, [this, i](){
            updateGlobalProgress(i, QString("Bypass Command %1/11").arg(i));
        });
    }

    m_manager->bypassSecurity();

    QTimer::singleShot(700, this, [this](){
        hideGlobalProgress();
        onLog("✅ Security Bypass Complete (11/11 commands sent)");
    });
}

void MainWindow::onReboot() {
    if(!m_manager->isConnected()) {
        QMessageBox::warning(this, "Not Connected", "Please connect to device first!");
        return;
    }

    int ret = QMessageBox::question(this, "Reboot Device",
                                    "Are you sure you want to REBOOT the device?\n\nThe device will restart.",
                                    QMessageBox::Yes | QMessageBox::No);
    if(ret == QMessageBox::Yes) {
        showGlobalProgress("Rebooting Device...", 0);
        onLog("🔄 Sending Reboot Command...");
        m_manager->rebootDevice();

        QTimer::singleShot(500, this, [this](){
            hideGlobalProgress();
            onLog("✅ Reboot Command Sent - Device will restart");
        });
    }
}

void MainWindow::onOfflineA() {
    if(!m_manager->isConnected()) return;

    showGlobalProgress("Sending Offline Mode A...", 0);
    onLog("🔌 Sending Offline Mode A...");
    m_manager->offlineA();

    QTimer::singleShot(300, this, [this](){
        hideGlobalProgress();
        onLog("✅ Offline Mode A Sent - Please disconnect device now");
    });
}

void MainWindow::onOfflineD() {
    if(!m_manager->isConnected()) return;

    showGlobalProgress("Sending Offline Mode D...", 0);
    onLog("🔌 Sending Offline Mode D...");
    m_manager->offlineD();

    QTimer::singleShot(300, this, [this](){
        hideGlobalProgress();
        onLog("✅ Offline Mode D Sent - Please disconnect device now");
    });
}

void MainWindow::onPowerOff() {
    if(!m_manager->isConnected()) return;

    int ret = QMessageBox::warning(this, "Power Off Device",
                                   "⚠️ Are you sure you want to POWER OFF the device?\n\nThe device will shut down completely.",
                                   QMessageBox::Yes | QMessageBox::No);
    if(ret == QMessageBox::Yes) {
        showGlobalProgress("Powering Off Device...", 0);
        onLog("⚡ Sending Power Off Command...");
        m_manager->powerOff();

        QTimer::singleShot(500, this, [this](){
            hideGlobalProgress();
            onLog("✅ Power Off Command Sent - Device shutting down");
        });
    }
}

void MainWindow::onSendCustomSPC() {
    if(!m_manager->isConnected()) {
        QMessageBox::warning(this, "Not Connected", "Please connect to device first!");
        onLog("❌ Custom SPC Failed - Not connected");
        return;
    }

    QString spc = m_editCustomSPC->text().trimmed();

    if(spc.length() != 6) {
        QMessageBox::warning(this, "Invalid SPC", "❌ SPC must be exactly 6 digits!");
        onLog("❌ Custom SPC Failed - Invalid length: " + QString::number(spc.length()) + " (expected 6)");
        return;
    }

    bool allDigits = true;
    for(QChar c : spc) {
        if(!c.isDigit()) {
            allDigits = false;
            break;
        }
    }

    if(!allDigits) {
        QMessageBox::warning(this, "Invalid SPC", "❌ SPC must contain only digits (0-9)!");
        onLog("❌ Custom SPC Failed - Contains non-digit characters");
        return;
    }

    onLog("📤 Sending Custom SPC: " + spc);
    m_manager->sendCustomSPC(spc);
}

void MainWindow::onSendCustomPWD() {
    if(!m_manager->isConnected()) {
        QMessageBox::warning(this, "Not Connected", "Please connect to device first!");
        onLog("❌ Custom PWD Failed - Not connected");
        return;
    }

    QString pwd = m_editCustomPWD->text().trimmed().toUpper();

    if(pwd.length() != 16) {
        QMessageBox::warning(this, "Invalid PWD", "❌ PWD must be exactly 16 hex characters!");
        onLog("❌ Custom PWD Failed - Invalid length: " + QString::number(pwd.length()) + " (expected 16)");
        return;
    }

    bool allHex = true;
    for(QChar c : pwd) {
        if(!c.isDigit() && (c < 'A' || c > 'F')) {
            allHex = false;
            break;
        }
    }

    if(!allHex) {
        QMessageBox::warning(this, "Invalid PWD", "❌ PWD must contain only hex characters (0-9, A-F)!");
        onLog("❌ Custom PWD Failed - Contains invalid hex characters");
        return;
    }

    onLog("📤 Sending Custom PWD: " + pwd);
    m_manager->sendCustomPWD(pwd);
}

void MainWindow::onStopAll() {
    showGlobalProgress("Stopping All Operations...", 0);
    onLog("⛔ STOP Requested - Canceling all operations");
    m_manager->cancelOperation();
    if (m_nvManager) {
        m_nvManager->cancelOperation();
    }

    QTimer::singleShot(200, this, [this](){
        hideGlobalProgress();
        onLog("✅ All operations stopped");
    });
}

// ============ GLOBAL PROGRESS SYSTEM ============

void MainWindow::showGlobalProgress(const QString &text, int max) {
    m_lblProgressText->setText(text);
    m_globalProgress->setRange(0, max);
    m_globalProgress->setValue(0);
    if(max == 0) m_globalProgress->setRange(0, 0); // Indeterminate
}

void MainWindow::updateGlobalProgress(int value, const QString &text) {
    if(!text.isEmpty()) m_lblProgressText->setText(text);
    m_globalProgress->setValue(value);
}

void MainWindow::hideGlobalProgress() {
    m_lblProgressText->setText("Ready");
    m_globalProgress->setRange(0, 100);
    m_globalProgress->setValue(0);
}


void MainWindow::setupNV(QWidget *p)
{
    QVBoxLayout *mainLayout = new QVBoxLayout(p);

    // Create Tab Widget for NV Manager
    QTabWidget *nvTabs = new QTabWidget;

    // Tab 1: Database View (Original)
    QWidget *dbViewTab = new QWidget;
    setupDatabaseView(dbViewTab);
    nvTabs->addTab(dbViewTab, "📚 Database View");

    // Populate table with all NV items
    populateNVTable(NVDatabase::instance().getAllItems());

    // Tab 2: Advanced Hex Editor (New)
    QWidget *hexEditorTab = new QWidget;
    setupAdvancedNVEditor(hexEditorTab);
    nvTabs->addTab(hexEditorTab, "🔬 Advanced Hex Editor");

    mainLayout->addWidget(nvTabs);
}

// Original Database View (Restored)
void MainWindow::setupDatabaseView(QWidget *p)
{
    QVBoxLayout *lay = new QVBoxLayout(p);

    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    QWidget *leftWidget = new QWidget;
    QVBoxLayout *leftLay = new QVBoxLayout(leftWidget);

    // Database view (NV Table)
    // Top Controls
    QHBoxLayout *topLay = new QHBoxLayout;
    topLay->addWidget(new QLabel("Category:"));

    m_nvCategoryFilter = new QComboBox;
    m_nvCategoryFilter->addItem("All Categories");
    QStringList categories = NVDatabase::instance().getCategories();
    for(const QString &cat : categories) {
        m_nvCategoryFilter->addItem(cat);
    }
    topLay->addWidget(m_nvCategoryFilter);

    topLay->addWidget(new QLabel("Search:"));
    m_nvSearchBox = new QLineEdit;
    m_nvSearchBox->setPlaceholderText("Search by ID or Name...");
    topLay->addWidget(m_nvSearchBox);

    leftLay->addLayout(topLay);

    // NV Items Table
    m_nvTable = new QTableWidget(0, 5);
    m_nvTable->setHorizontalHeaderLabels({"ID", "Name", "Category", "Size", "Type"});
    m_nvTable->horizontalHeader()->setStretchLastSection(true);
    m_nvTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_nvTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_nvTable->setAlternatingRowColors(true);
    m_nvTable->setSortingEnabled(true);
    leftLay->addWidget(m_nvTable);

    // Right Panel - Details & Operations
    QWidget *rightWidget = new QWidget;
    QVBoxLayout *rightLay = new QVBoxLayout(rightWidget);

    // Item Details Group
    QGroupBox *detailsBox = new QGroupBox("Selected Item Details");
    QGridLayout *detailsLay = new QGridLayout(detailsBox);

    detailsLay->addWidget(new QLabel("ID:"), 0, 0);
    m_nvItemIdLabel = new QLabel("-");
    detailsLay->addWidget(m_nvItemIdLabel, 0, 1);

    detailsLay->addWidget(new QLabel("Name:"), 0, 2);
    m_nvItemNameLabel = new QLabel("-");
    detailsLay->addWidget(m_nvItemNameLabel, 0, 3);

    detailsLay->addWidget(new QLabel("Type:"), 1, 0);
    m_nvItemTypeLabel = new QLabel("-");
    detailsLay->addWidget(m_nvItemTypeLabel, 1, 1);

    detailsLay->addWidget(new QLabel("Size:"), 1, 2);
    m_nvItemSizeLabel = new QLabel("-");
    detailsLay->addWidget(m_nvItemSizeLabel, 1, 3);

    detailsLay->addWidget(new QLabel("Description:"), 2, 0);
    m_nvItemDescLabel = new QLabel("-");
    m_nvItemDescLabel->setWordWrap(true);
    detailsLay->addWidget(m_nvItemDescLabel, 2, 1, 1, 3);

    detailsLay->addWidget(new QLabel("Current Value (Hex):"), 3, 0);
    m_nvCurrentValueHex = new QLineEdit;
    m_nvCurrentValueHex->setReadOnly(true);
    m_nvCurrentValueHex->setStyleSheet("background-color: #2a2a2a; font-family: Consolas;");
    detailsLay->addWidget(m_nvCurrentValueHex, 3, 1, 1, 3);

    detailsLay->addWidget(new QLabel("Current Value (Text):"), 4, 0);
    m_nvCurrentValueText = new QLineEdit;
    m_nvCurrentValueText->setReadOnly(true);
    m_nvCurrentValueText->setStyleSheet("background-color: #2a2a2a; font-family: Consolas; color: #88ff88;");
    detailsLay->addWidget(m_nvCurrentValueText, 4, 1, 1, 3);

    detailsLay->addWidget(new QLabel("New Value (Hex):"), 5, 0);
    m_nvNewValueHex = new QLineEdit;
    m_nvNewValueHex->setPlaceholderText("Enter hex value (e.g., AA BB CC DD)");
    m_nvNewValueHex->setStyleSheet("font-family: Consolas;");
    detailsLay->addWidget(m_nvNewValueHex, 5, 1, 1, 3);

    QHBoxLayout *btnLay = new QHBoxLayout;
    m_btnNVRead = new QPushButton("📖 Read");
    m_btnNVWrite = new QPushButton("📝 Write");
    m_btnNVRefresh = new QPushButton("🔄 Refresh");
    m_btnNVClear = new QPushButton("🗑️ Clear");

    btnLay->addWidget(m_btnNVRead);
    btnLay->addWidget(m_btnNVWrite);
    btnLay->addWidget(m_btnNVRefresh);
    btnLay->addWidget(m_btnNVClear);
    btnLay->addStretch();

    detailsLay->addLayout(btnLay, 6, 0, 1, 4);
    
    rightLay->addWidget(detailsBox);

    // Backup/Restore Section
    QGroupBox *backupBox = new QGroupBox("Backup/Restore");
    QHBoxLayout *backupLay = new QHBoxLayout(backupBox);
    m_btnNVBackup = new QPushButton("💾 Backup Selected");
    m_btnNVBackupAll = new QPushButton("💾 Backup All");
    m_btnNVRestore = new QPushButton("♻️ Restore");
    m_btnNVRestoreAll = new QPushButton("♻️ Restore All");
    backupLay->addWidget(m_btnNVBackup);
    backupLay->addWidget(m_btnNVBackupAll);
    backupLay->addWidget(m_btnNVRestore);
    backupLay->addWidget(m_btnNVRestoreAll);
    
    rightLay->addWidget(backupBox);
    rightLay->addStretch();

    // Add widgets to splitter
    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 1);

    lay->addWidget(splitter);

    // Connect signals (only once)
    connect(m_nvCategoryFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onNVCategoryChanged);
    connect(m_nvSearchBox, &QLineEdit::textChanged, this, &MainWindow::onNVSearchChanged);
    connect(m_nvTable, &QTableWidget::itemSelectionChanged, this, &MainWindow::onNVTableSelectionChanged);
    connect(m_btnNVRead, &QPushButton::clicked, this, &MainWindow::onNVRead);
    connect(m_btnNVWrite, &QPushButton::clicked, this, &MainWindow::onNVWrite);
    connect(m_btnNVRefresh, &QPushButton::clicked, this, &MainWindow::onNVRefresh);
    connect(m_btnNVClear, &QPushButton::clicked, this, &MainWindow::onNVClear);
    connect(m_btnNVBackup, &QPushButton::clicked, this, &MainWindow::onNVBackup);
    connect(m_btnNVBackupAll, &QPushButton::clicked, this, &MainWindow::onNVBackupAll);
    connect(m_btnNVRestore, &QPushButton::clicked, this, &MainWindow::onNVRestore);
    connect(m_btnNVRestoreAll, &QPushButton::clicked, this, &MainWindow::onNVRestoreAll);
}

// === Advanced Hex Editor Tab (DFS Style) ===
void MainWindow::setupAdvancedNVEditor(QWidget *parent)

{
    QVBoxLayout *mainLay = new QVBoxLayout(parent);
    
    // Top: Range Selector
    QGroupBox *rangeGroup = new QGroupBox("NV Range Selection");
    QHBoxLayout *rangeLay = new QHBoxLayout(rangeGroup);
    
    rangeLay->addWidget(new QLabel("Start:"));
    QSpinBox *spinStart = new QSpinBox;
    spinStart->setRange(0, 65535);
    spinStart->setValue(0);
    spinStart->setFixedWidth(80);
    rangeLay->addWidget(spinStart);
    
    rangeLay->addWidget(new QLabel("End:"));
    QSpinBox *spinEnd = new QSpinBox;
    spinEnd->setRange(0, 65535);
    spinEnd->setValue(6553);
    spinEnd->setFixedWidth(80);
    rangeLay->addWidget(spinEnd);
    
    QPushButton *btnLoadRange = new QPushButton("📥 Load Range");
    rangeLay->addWidget(btnLoadRange);
    rangeLay->addStretch();
    
    rangeLay->addWidget(new QLabel("|  Direct Read ID:"));
    m_customNvIdInput = new QLineEdit;
    m_customNvIdInput->setFixedWidth(70);
    m_customNvIdInput->setPlaceholderText("ID");
    rangeLay->addWidget(m_customNvIdInput);
    
    m_btnCustomRead = new QPushButton("➡️ Read");
    connect(m_btnCustomRead, &QPushButton::clicked, this, &MainWindow::onCustomNVRead);
    rangeLay->addWidget(m_btnCustomRead);
    
    mainLay->addWidget(rangeGroup);
    
    // Middle: Split View (Grid + Hex Dump)
    QSplitter *splitter = new QSplitter(Qt::Horizontal);
    
    // Left: NV Items Grid
    QGroupBox *gridBox = new QGroupBox("NV Items");
    QVBoxLayout *gridLay = new QVBoxLayout(gridBox);
    
    QTableWidget *nvGrid = new QTableWidget;
    nvGrid->setObjectName("nvGrid");
    nvGrid->setColumnCount(10);
    nvGrid->setRowCount(11);
    nvGrid->horizontalHeader()->hide();
    nvGrid->verticalHeader()->hide();
    nvGrid->setSelectionMode(QAbstractItemView::SingleSelection);
    nvGrid->setShowGrid(true);
    for(int i=0; i<10; i++) nvGrid->setColumnWidth(i, 55);
    
    // Populate grid (0-109)
    for (int row = 0; row < 11; row++) {
        for (int col = 0; col < 10; col++) {
            int nvId = row * 10 + col;
            QTableWidgetItem *item = new QTableWidgetItem(QString("%1").arg(nvId, 5, 10, QChar('0')));
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(Qt::UserRole, nvId);
            nvGrid->setItem(row, col, item);
        }
    }
    gridLay->addWidget(nvGrid);
    splitter->addWidget(gridBox);
    
    // Right: Hex Dump (DFS Style)
    QGroupBox *hexBox = new QGroupBox("Selected NVI");
    QVBoxLayout *hexLay = new QVBoxLayout(hexBox);
    
    // Hex dump header
    QString header = "     00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F";
    QLabel *lblHeader = new QLabel(header);
    lblHeader->setFont(QFont("Consolas", 9));
    lblHeader->setStyleSheet("color: #888;");
    hexLay->addWidget(lblHeader);
    
    // Hex dump content (like DFS)
    m_txtHexDump = new QTextEdit;
    m_txtHexDump->setObjectName("txtHexDump");
    m_txtHexDump->setReadOnly(true);
    m_txtHexDump->setFont(QFont("Consolas", 10));
    m_txtHexDump->setStyleSheet("background-color: #1a1a1a; color: #00ff00;");
    m_txtHexDump->setPlaceholderText("Select an NV item and click Read...");
    hexLay->addWidget(m_txtHexDump);

    
    splitter->addWidget(hexBox);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    
    mainLay->addWidget(splitter, 2);
    
    // Bottom: Data Editor
    QGroupBox *displayGroup = new QGroupBox("Selected NV Data");
    QVBoxLayout *dispLay = new QVBoxLayout(displayGroup);
    
    QLabel *lblSelectedInfo = new QLabel("Selected: None");
    lblSelectedInfo->setStyleSheet("font-weight: bold; color: #00AA00;");
    dispLay->addWidget(lblSelectedInfo);
    
    // Multi-format Grid
    QGridLayout *formatLay = new QGridLayout;

    // Row 0: Hex Dump (Large)
    formatLay->addWidget(new QLabel("Hex Data:"), 0, 0);
    m_customResHex = new QLineEdit;
    m_customResHex->setReadOnly(false);
    m_customResHex->setFont(QFont("Consolas", 10));
    formatLay->addWidget(m_customResHex, 0, 1);

    // Row 1: Text
    formatLay->addWidget(new QLabel("ASCII:"), 1, 0);
    m_customResText = new QLineEdit;
    m_customResText->setReadOnly(true);
    formatLay->addWidget(m_customResText, 1, 1);

    // Row 2: Binary
    formatLay->addWidget(new QLabel("Binary:"), 2, 0);
    m_customResBin = new QLineEdit;
    m_customResBin->setReadOnly(true);
    formatLay->addWidget(m_customResBin, 2, 1);

    // Row 3: Decimal
    formatLay->addWidget(new QLabel("Decimal:"), 3, 0);
    m_customResDec = new QLineEdit;
    m_customResDec->setReadOnly(true);
    formatLay->addWidget(m_customResDec, 3, 1);

    dispLay->addLayout(formatLay);

    // Buttons
       // Action Buttons
    QHBoxLayout *actionLay = new QHBoxLayout;
    QPushButton *btnReadSelected = new QPushButton("📖 Read");
    QPushButton *btnWriteSel = new QPushButton("✍️ Write");
    QPushButton *btnBackupSel = new QPushButton("💾 Backup ID");
    QPushButton *btnBackupRange = new QPushButton("📚 Backup Range");
    
    btnWriteSel->setStyleSheet("background-color: #8B0000; color: white;");
    
    actionLay->addWidget(btnReadSelected);
    actionLay->addWidget(btnWriteSel);
    actionLay->addWidget(btnBackupSel);
    actionLay->addWidget(btnBackupRange);
    actionLay->addStretch();
    
    dispLay->addLayout(actionLay);
    mainLay->addWidget(displayGroup, 0);
    
    // === CONNECTIONS ===
    
    // Grid Selection
    connect(nvGrid, &QTableWidget::itemSelectionChanged, this, [=]() {
        QList<QTableWidgetItem*> selected = nvGrid->selectedItems();
        if (selected.isEmpty()) return;
        uint16_t nvId = selected.first()->data(Qt::UserRole).toUInt();
        lblSelectedInfo->setText(QString("Selected ID: %1").arg(nvId));
        m_customNvIdInput->setText(QString::number(nvId));
    });
    
    // Load Range
    connect(btnLoadRange, &QPushButton::clicked, this, [=]() {
        int start = spinStart->value();
        int end = spinEnd->value();
        if(end < start) end = start + 100;
        
        showGlobalProgress("Loading Grid...", 0);
        int count = end - start + 1;
        int rows = (count + 9) / 10;
        nvGrid->setRowCount(rows);
        nvGrid->clear();
        
        for (int i = start; i <= end; i++) {
            int idx = i - start;
            int row = idx / 10;
            int col = idx % 10;
            QTableWidgetItem *item = new QTableWidgetItem(QString("%1").arg(i, 5, 10, QChar('0')));
            item->setTextAlignment(Qt::AlignCenter);
            item->setData(Qt::UserRole, i);
            nvGrid->setItem(row, col, item);
        }
        hideGlobalProgress();
    });
    
    // Read Selected
    connect(btnReadSelected, &QPushButton::clicked, this, [=]() {
        QString idStr = m_customNvIdInput->text();
        if(idStr.isEmpty()) {
            QMessageBox::warning(this, "No ID", "Please enter or select an NV ID first!");
            return;
        }
        
        bool ok;
        uint16_t id = idStr.toUShort(&ok);
        if(!ok || id > 65535) {
            QMessageBox::warning(this, "Invalid ID", "Please enter a valid NV ID (0-65535)!");
            return;
        }
        
        showGlobalProgress(QString("Reading NV %1...").arg(id), 0);
        m_nvManager->readNV(id);
    });

    
    // Write Selected
    connect(btnWriteSel, &QPushButton::clicked, this, [=]() {
        QString idStr = m_customNvIdInput->text();
        QString hexData = m_customResHex->text();
        if(idStr.isEmpty() || hexData.isEmpty()) {
            QMessageBox::warning(this, "Error", "Enter NV ID and Hex data!");
            return;
        }
        QByteArray data = QByteArray::fromHex(hexData.replace(" ", "").toUtf8());
        if(data.isEmpty()) {
            QMessageBox::warning(this, "Error", "Invalid hex!");
            return;
        }
        if(QMessageBox::question(this, "Confirm", QString("Write to NV %1?").arg(idStr)) != QMessageBox::Yes) return;
        showGlobalProgress("Writing NV...", 0);
        uint16_t id = idStr.toUShort();
        m_nvManager->writeNV(id, data);
    });
    
    // Backup Single ID
    connect(btnBackupSel, &QPushButton::clicked, this, [=]() {
        QString idStr = m_customNvIdInput->text();
        if(idStr.isEmpty()) {
            QMessageBox::warning(this, "No ID", "Please enter or select an NV ID first!");
            return;
        }
        
        uint16_t id = idStr.toUShort();
        QString fn = QFileDialog::getSaveFileName(this, "Backup NV Item", 
            QString("nv_%1.nv").arg(id), "NV Files (*.nv)");
        if(fn.isEmpty()) return;
        
        showGlobalProgress(QString("Backing up NV %1...").arg(id), 1);
        onLog(QString("📦 Backing up single NV item: %1").arg(id));
        
        QList<uint16_t> list; 
        list << id;
        m_nvManager->backupItems(list, fn);
    });

    
    // Backup Range (Direct - Not from Database)
    connect(btnBackupRange, &QPushButton::clicked, this, [=]() {
        int start = spinStart->value();
        int end = spinEnd->value();
        
        if(end < start) {
            QMessageBox::warning(this, "Invalid Range", "End must be >= Start!");
            return;
        }
        
        int count = end - start + 1;
        int estMinutes = qMax(1, count / 30); // ~30 items per minute estimate
        int estSeconds = (count * 2) % 60;    // ~2 seconds per item
        
        QString timeStr;
        if(estMinutes >= 2) {
            timeStr = QString("~%1-%2 minutes").arg(estMinutes).arg(estMinutes + 1);
        } else {
            timeStr = QString("~%1 minute %2 seconds").arg(estMinutes).arg(estSeconds);
        }
        
        int ret = QMessageBox::question(this, "📦 Backup Range",
            QString("📊 Backup NV Range: %1 to %2\n\n"
                    "📁 Total Items: %3 NV items\n"
                    "⏱️ Estimated Time: %4\n\n"
                    "⚠️ Do not disconnect during backup!\n\n"
                    "Continue?").arg(start).arg(end).arg(count).arg(timeStr),
            QMessageBox::Yes | QMessageBox::No);
        
        if(ret != QMessageBox::Yes) return;
        
        QString fn = QFileDialog::getSaveFileName(this, "💾 Save Backup File", 
            QString("NV_Range_%1_%2.nv").arg(start).arg(end), "NV Files (*.nv);;All Files (*)");
        if(fn.isEmpty()) return;
        
        // Show progress with total count
        showGlobalProgress(QString("Backing up %1 items...").arg(count), count);
        onLog(QString("📦 Starting Range Backup: NV %1 to %2 (%3 items)").arg(start).arg(end).arg(count));
        
        // Use backupRange for direct range backup
        m_nvManager->backupRange(start, end, fn);
    });
}



void MainWindow::populateNVTable(const QList<NVItem> &items)
{
    m_nvTable->setSortingEnabled(false);  // Disable sorting during insert
    m_nvTable->setRowCount(0);

    for(const NVItem &item : items) {
        int row = m_nvTable->rowCount();
        m_nvTable->insertRow(row);

        QTableWidgetItem *idItem = new QTableWidgetItem();
        idItem->setData(Qt::DisplayRole, item.id);
        idItem->setData(Qt::UserRole, item.id);
        m_nvTable->setItem(row, 0, idItem);
        
        m_nvTable->setItem(row, 1, new QTableWidgetItem(item.name));
        m_nvTable->setItem(row, 2, new QTableWidgetItem(item.category));
        m_nvTable->setItem(row, 3, new QTableWidgetItem(item.isExtended ? "Extended" : "Standard"));
        m_nvTable->setItem(row, 4, new QTableWidgetItem(QString("%1 bytes").arg(item.size)));
    }
    
    m_nvTable->setSortingEnabled(true);  // Re-enable sorting after insert
}


void MainWindow::onNVCategoryChanged(int index)
{
    if(index == 0) {
        populateNVTable(NVDatabase::instance().getAllItems());
    } else {
        QString category = m_nvCategoryFilter->currentText();
        populateNVTable(NVDatabase::instance().getItemsByCategory(category));
    }
}

void MainWindow::onNVSearchChanged(const QString &text)
{
    if(text.isEmpty()) {
        onNVCategoryChanged(m_nvCategoryFilter->currentIndex());
    } else {
        populateNVTable(NVDatabase::instance().searchItems(text));
    }
}

void MainWindow::onNVTableSelectionChanged()
{
    QList<QTableWidgetItem*> selected = m_nvTable->selectedItems();
    if(selected.isEmpty()) {
        m_nvItemIdLabel->setText("-");
        m_nvItemNameLabel->setText("-");
        m_nvItemTypeLabel->setText("-");
        m_nvItemSizeLabel->setText("-");
        m_nvItemDescLabel->setText("-");
        return;
    }

    int row = selected.first()->row();
    uint16_t itemId = m_nvTable->item(row, 0)->data(Qt::UserRole).toUInt();

    const NVItem *item = NVDatabase::instance().getItem(itemId);
    if(item) {
        m_nvItemIdLabel->setText(QString::number(item->id));
        m_nvItemNameLabel->setText(item->name);
        m_nvItemTypeLabel->setText(item->isExtended ? "Extended" : "Standard");
        m_nvItemSizeLabel->setText(QString("%1 bytes").arg(item->size));
        m_nvItemDescLabel->setText(item->description);
    }
}

void MainWindow::onNVRead()
{
    QList<QTableWidgetItem*> selected = m_nvTable->selectedItems();
    if(selected.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select an NV item first!");
        return;
    }

    int row = selected.first()->row();
    uint16_t itemId = m_nvTable->item(row, 0)->data(Qt::UserRole).toUInt();

    const NVItem *item = NVDatabase::instance().getItem(itemId);
    if(!item) return;

    showGlobalProgress("Reading NV Item...", 0);

    if(item->isExtended) {
        m_nvManager->readNVExt(itemId, 0);
    } else {
        m_nvManager->readNV(itemId);
    }
}

void MainWindow::onNVWrite()
{
    QList<QTableWidgetItem*> selected = m_nvTable->selectedItems();
    if(selected.isEmpty()) {
        QMessageBox::warning(this, "No Selection", "Please select an NV item first!");
        return;
    }

    QString hexValue = m_nvNewValueHex->text().trimmed().remove(' ');
    if(hexValue.isEmpty()) {
        QMessageBox::warning(this, "No Value", "Please enter a hex value to write!");
        return;
    }

    int row = selected.first()->row();
    uint16_t itemId = m_nvTable->item(row, 0)->data(Qt::UserRole).toUInt();

    const NVItem *item = NVDatabase::instance().getItem(itemId);
    if(!item) return;

    if(item->readOnly) {
        QMessageBox::critical(this, "Read-Only Item",
            QString("NV item '%1' is read-only and cannot be modified!").arg(item->name));
        return;
    }

    int ret = QMessageBox::question(this, "Confirm Write",
        QString("Are you sure you want to write to NV item:\n\n"
                "ID: %1\n"
                "Name: %2\n"
                "New Value: %3\n\n"
                "⚠️ This operation cannot be undone!").arg(item->id).arg(item->name).arg(hexValue),
        QMessageBox::Yes | QMessageBox::No);

    if(ret != QMessageBox::Yes) return;

    QByteArray data = QByteArray::fromHex(hexValue.toLatin1());

    showGlobalProgress("Writing NV Item...", 0);

    if(item->isExtended) {
        m_nvManager->writeNVExt(itemId, 0, data);
    } else {
        m_nvManager->writeNV(itemId, data);
    }
}

void MainWindow::onNVRefresh()
{
    onNVRead();
}

void MainWindow::onNVClear()
{
    m_nvCurrentValueHex->clear();
    m_nvCurrentValueText->clear();
    m_nvNewValueHex->clear();
}

void MainWindow::onNVBackup()
{
    QString filename = QFileDialog::getSaveFileName(this, "Backup NV Items", "",
        "NV Backup Files (*.nv *.json);;NV Binary (*.nv);;JSON Files (*.json)");
    if(filename.isEmpty()) return;

    QList<uint16_t> itemIds;
    for(int i = 0; i < m_nvTable->rowCount(); i++) {
        uint16_t id = m_nvTable->item(i, 0)->data(Qt::UserRole).toUInt();
        itemIds.append(id);
    }

    m_nvManager->backupItems(itemIds, filename);
}

#include <QInputDialog>
void MainWindow::onNVBackupAll()
{
    QString filename = QFileDialog::getSaveFileName(this, "Backup All NV Items", "nv_backup_all.nv",
        "NV Backup Files (*.nv *.json);;NV Binary (*.nv);;JSON Files (*.json)");
    if(filename.isEmpty()) return;

    // Fixed range: 0-6999 (full Qualcomm range)
    int endId = 6999;

    int ret = QMessageBox::question(this, "Backup All NV Items",
        QString("This will backup ALL NV items (0 to %1).\n\n"
                "⏱️ Estimated time: ~2-3 minutes\n"
                "⚠️ Invalid items will be skipped automatically\n"
                "✅ Progress will be shown in real-time\n\n"
                "Continue?").arg(endId),
        QMessageBox::Yes | QMessageBox::No);

    if(ret != QMessageBox::Yes) return;

    onLog(QString("📦 Starting FULL NV backup: 0 to %1...").arg(endId));
    m_nvManager->backupRange(0, endId, filename);
}

void MainWindow::onNVRestore()
{
    QString filename = QFileDialog::getOpenFileName(this, "Restore NV Items", "",
        "NV Backup Files (*.nv *.json);;NV Binary (*.nv);;JSON Files (*.json)");
    if(filename.isEmpty()) return;

    int ret = QMessageBox::warning(this, "Confirm Restore",
        "⚠️ WARNING: This will overwrite existing NV items with values from the backup file!\n\n"
        "Are you sure you want to continue?",
        QMessageBox::Yes | QMessageBox::No);

    if(ret == QMessageBox::Yes) {
        m_nvManager->restoreFromFile(filename);
    }
}

void MainWindow::onNVRestoreAll()
{
    QString filename = QFileDialog::getOpenFileName(this, "Restore All NV Items", "",
        "NV Backup Files (*.nv *.json);;NV Binary (*.nv);;JSON Files (*.json)");
    if(filename.isEmpty()) return;

    int ret = QMessageBox::critical(this, "⚠️ RESTORE ALL - CRITICAL WARNING",
        "⚠️⚠️⚠️ CRITICAL WARNING ⚠️⚠️⚠️\n\n"
        "This will restore ALL NV items from the backup file!\n\n"
        "This operation:\n"
        "• Will OVERWRITE all current NV values\n"
        "• May take several minutes\n"
        "• CANNOT be undone\n"
        "• May affect device functionality if backup is incompatible\n\n"
        "Are you ABSOLUTELY SURE you want to continue?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if(ret != QMessageBox::Yes) return;

    onLog("⚠️ Starting FULL NV restore - this may take several minutes...");
    m_nvManager->restoreFromFile(filename);
}

void MainWindow::onCustomNVRead()
{
    QString idStr = m_customNvIdInput->text();
    if (idStr.isEmpty()) return;

    bool ok;
    uint16_t id = idStr.toUShort(&ok);
    if (!ok) {
        QMessageBox::warning(this, "Invalid ID", "Please enter a valid numeric NV ID (0-65535)");
        return;
    }

    onLog(QString("🔍 Custom Read Request: NV %1").arg(id));

    // Clear previous results
    m_customResHex->clear();
    m_customResText->clear();
    m_customResBin->clear();
    m_customResDec->clear();

    // Trigger Read
    m_nvManager->readNV(id);

    // Note: The result will be handled in onNVReadComplete
}

void MainWindow::onNVReadComplete(uint16_t itemId, const QByteArray &data, bool success)
{
    hideGlobalProgress();

    if(success) {
        // Display Hex
        QString hexValue = data.toHex(' ').toUpper();
        m_nvCurrentValueHex->setText(hexValue);

        // Display Formatted Value using NVManager's professional formatter
        QString formattedValue = m_nvManager->formatNVValue(itemId, data);
        m_nvCurrentValueText->setText(formattedValue);

        // === UPDATE CUSTOM LOOKUP FIELDS ===
        NVManager::FormattedValue fv = m_nvManager->formatNVValueFull(itemId, data);
        m_customResHex->setText(fv.hex);
        m_customResText->setText(fv.text);
        m_customResBin->setText(fv.binary);
        m_customResDec->setText(fv.decimal);
        
        // === UPDATE HEX DUMP PANEL (DFS Style) ===
        if(m_txtHexDump) {
            QString hexDump;
            int len = data.size();
            for(int offset = 0; offset < len; offset += 16) {
                // Address
                hexDump += QString("%1  ").arg(offset, 4, 16, QChar('0')).toUpper();
                
                // Hex bytes
                QString hexPart;
                QString asciiPart;
                for(int i = 0; i < 16; i++) {
                    if(offset + i < len) {
                        unsigned char byte = data[offset + i];
                        hexPart += QString("%1 ").arg(byte, 2, 16, QChar('0')).toUpper();
                        asciiPart += (byte >= 32 && byte < 127) ? QChar(byte) : '.';
                    } else {
                        hexPart += "   ";
                        asciiPart += " ";
                    }
                }
                hexDump += hexPart + " " + asciiPart + "\n";
            }
            m_txtHexDump->setPlainText(hexDump);
        }

        // Log
        onLog(QString("✅ NV %1 Read: %2 bytes | %3")
              .arg(itemId)
              .arg(data.size())
              .arg(hexValue.left(40)));
    } else {
        m_nvCurrentValueHex->clear();
        m_nvCurrentValueText->setText("[Read Failed]");
        if(m_txtHexDump) m_txtHexDump->setPlainText("Read Failed");
        QMessageBox::warning(this, "Read Failed", "Failed to read NV item!");
    }
}


void MainWindow::onNVWriteComplete(uint16_t itemId, bool success)
{
    Q_UNUSED(itemId);
    hideGlobalProgress();

    if(success) {
        onLog("✅ NV Write successful");
        QMessageBox::information(this, "Success", "NV item written successfully!");
        m_nvNewValueHex->clear();
    } else {
        QMessageBox::critical(this, "Write Failed", "Failed to write NV item!");
    }
}

void MainWindow::onNVBackupProgress(int current, int total, const QString &itemName)
{
    int percent = (total > 0) ? (current * 100 / total) : 0;
    QString msg = QString("💾 Backup: %1/%2 (%3%) - %4")
                    .arg(current).arg(total).arg(percent).arg(itemName);
    updateGlobalProgress(current, msg);
}

void MainWindow::onNVBackupComplete(bool success, const QString &filename)
{
    hideGlobalProgress();

    if(success) {
        onLog("✅ Backup complete: " + filename);
        QMessageBox::information(this, "✅ Backup Complete",
            QString("Successfully backed up NV items!\n\n"
                    "📁 File: %1").arg(filename));
    } else {
        QMessageBox::critical(this, "❌ Backup Failed", "Failed to backup NV items!");
    }
}


void MainWindow::onNVRestoreProgress(int current, int total, const QString &itemName)
{
    updateGlobalProgress(current, QString("Restoring %1/%2: %3").arg(current).arg(total).arg(itemName));
}

void MainWindow::onNVRestoreComplete(bool success, int successCount, int failCount)
{
    hideGlobalProgress();

    if(success) {
        onLog(QString("✅ Restore complete: %1 success, %2 failed").arg(successCount).arg(failCount));
        QMessageBox::information(this, "Restore Complete",
            QString("NV Restore Results:\n\n✅ Success: %1\n❌ Failed: %2").arg(successCount).arg(failCount));
    } else {
        QMessageBox::critical(this, "Restore Failed", "Failed to restore NV items!");
    }
}

void MainWindow::showAboutDialog()
{
    QDialog aboutDlg(this);
    aboutDlg.setWindowTitle(QString("About %1").arg(VersionInfo::APP_NAME));
    aboutDlg.setFixedSize(540, 620);
    aboutDlg.setWindowIcon(QIcon(":/app_icon.png"));

    // Modern Dark Theme
    aboutDlg.setStyleSheet(
        "QDialog { background-color: #1e1e24; color: #e0e0e0; font-family: 'Segoe UI', Arial, sans-serif; }"
        "QLabel { color: #e0e0e0; }"
        "QFrame#cardFrame { background-color: #282c34; border: 1px solid #3e4451; border-radius: 8px; }"
    );

    QVBoxLayout *layout = new QVBoxLayout(&aboutDlg);
    layout->setSpacing(12);
    layout->setContentsMargins(24, 24, 24, 20);

    // 1. App Logo & Title Block (Header)
    QHBoxLayout *headerLayout = new QHBoxLayout;
    
    QLabel *logoLbl = new QLabel;
    QPixmap logo(":/app_icon.png");
    if (!logo.isNull()) {
        logoLbl->setPixmap(logo.scaled(76, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    headerLayout->addWidget(logoLbl);
    headerLayout->addSpacing(14);

    QVBoxLayout *headerTextLayout = new QVBoxLayout;
    QLabel *titleLbl = new QLabel(QString("<h2 style='color:#00e5ff; margin:0; font-size:22px;'>%1</h2>").arg(VersionInfo::APP_NAME));
    QLabel *versionLbl = new QLabel(QString("<p style='color:#abb2bf; margin:2px 0 0 0; font-size:13px;'>Version <b>%1</b> (%2) | Built: <b>%3</b></p>")
                                     .arg(VersionInfo::APP_VERSION)
                                     .arg(VersionInfo::APP_STATUS)
                                     .arg(VersionInfo::getBuildDate()));
    QLabel *devLbl = new QLabel(QString("<p style='color:#ffffff; margin:4px 0 0 0; font-size:14px;'>Developed by <b style='color:#00e5ff;'>%1</b></p>")
                                  .arg(VersionInfo::APP_DEVELOPER_FULL));

    headerTextLayout->addWidget(titleLbl);
    headerTextLayout->addWidget(versionLbl);
    headerTextLayout->addWidget(devLbl);
    headerLayout->addLayout(headerTextLayout);
    headerLayout->addStretch();

    layout->addLayout(headerLayout);
    layout->addSpacing(4);

    // 2. Links Container Card
    QFrame *cardFrame = new QFrame;
    cardFrame->setObjectName("cardFrame");
    QVBoxLayout *cardLayout = new QVBoxLayout(cardFrame);
    cardLayout->setSpacing(12);
    cardLayout->setContentsMargins(18, 16, 18, 16);

    // Header for links
    QLabel *linksHeader = new QLabel("<b style='color:#00e5ff; font-size:13px; letter-spacing:1px;'>🔗 OFFICIAL LINKS & CONNECT</b>");
    cardLayout->addWidget(linksHeader);
    cardLayout->addSpacing(4);

    // Helper lambda to create elegant link rows
    auto createLinkRow = [](const QString &iconPath, const QString &text, const QString &url, const QString &accentColor) -> QLabel* {
        QLabel *lbl = new QLabel;
        lbl->setTextFormat(Qt::RichText);
        lbl->setText(QString("<a href='%1' style='text-decoration:none; color:%2; font-weight:bold; font-size:13px;'>"
                             "<img src='%3' width='24' height='24' style='vertical-align:middle;'>&nbsp;&nbsp;%4"
                             "</a>").arg(url).arg(accentColor).arg(iconPath).arg(text));
        lbl->setTextInteractionFlags(Qt::TextBrowserInteraction);
        lbl->setOpenExternalLinks(true);
        lbl->setCursor(Qt::PointingHandCursor);
        return lbl;
    };

    // Website Link
    cardLayout->addWidget(createLinkRow(":/website_icon.png", "Official Website (alisakkaf.com)", VersionInfo::URL_WEBSITE, "#00e5ff"));
    
    // GitHub Profile Link
    cardLayout->addWidget(createLinkRow(":/github_icon.png", "GitHub Profile (@alisakkaf)", VersionInfo::URL_GITHUB_PROFILE, "#ffffff"));

    // GitHub Repository Star Link
    cardLayout->addWidget(createLinkRow(":/star_icon.png", "Star QC-Native-Diag Repository on GitHub ⭐", VersionInfo::URL_GITHUB_REPO, "#ffd700"));

    // Facebook Profile Link
    cardLayout->addWidget(createLinkRow(":/facebook_icon.png", "Follow on Facebook", VersionInfo::URL_FACEBOOK, "#1877f2"));

    layout->addWidget(cardFrame);
    layout->addSpacing(6);

    // 3. Gift Message & Disclaimer
    QLabel *giftLbl = new QLabel("<b style='color:#98c379; font-size:12px;'>A gift to the mobile & hardware engineering community ❤️</b>");
    giftLbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(giftLbl);

    QLabel *disclaimerLbl = new QLabel(
        "<p style='color:#e06c75; font-size:11px; margin:4px 0 0 0; text-align:center; line-height:1.3;'>"
        "<b>DISCLAIMER:</b> Provided 'AS IS' without warranty of any kind. "
        "The developer is not responsible for device damage or misuse."
        "</p>");
    disclaimerLbl->setWordWrap(true);
    disclaimerLbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(disclaimerLbl);

    layout->addStretch();

    // 4. Close Button
    QPushButton *closeBtn = new QPushButton("Close");
    closeBtn->setStyleSheet(
        "QPushButton { "
        "   background-color: #007acc; color: white; border: none; "
        "   padding: 8px 36px; border-radius: 5px; font-weight: bold; font-size: 13px;"
        "} "
        "QPushButton:hover { background-color: #0098ff; }"
        "QPushButton:pressed { background-color: #005c99; }"
    );
    closeBtn->setCursor(Qt::PointingHandCursor);
    connect(closeBtn, &QPushButton::clicked, &aboutDlg, &QDialog::accept);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    aboutDlg.exec();
}

// ============ SIM SELECTION HELPERS ============

int MainWindow::getActiveSubscription() const
{
    return m_chkSim2->isChecked() ? 1 : 0;
}

void MainWindow::resetEfsTree()
{
    // Clear the entire EFS tree
    m_efsTree->clear();
    m_nodeMap.clear();

    // Cancel any pending operations
    m_recursiveOp = RecursiveOp::None;
    m_scanQueue.clear();
    m_targetFileList.clear();
    m_downloadFileList.clear();
    m_uploadFileList.clear();
    m_deleteFileList.clear();
    m_listTimeout->stop();
    m_operationTimeout->stop();
    m_sim2Probing = false;
    m_sim2ProbeIndex = 0;

    // Determine root path based on active SIM
    QString rootPath;
    QString rootLabel;
    if (getActiveSubscription() == 1) {
        if (!m_sim2DiscoveredRoot.isEmpty()) {
            rootPath = m_sim2DiscoveredRoot;
        } else {
            rootPath = "/";  // Placeholder until auto-probe discovers real path
        }
        rootLabel = "/ (SIM 2)";
    } else {
        rootPath = "/";
        rootLabel = "/";
    }

    // Create new root node
    QTreeWidgetItem *root = new QTreeWidgetItem(m_efsTree);
    root->setText(0, rootLabel);
    root->setText(2, "Directory");
    root->setIcon(0, m_iconFolder);
    root->setData(0, Qt::UserRole, rootPath);
    m_nodeMap[rootPath] = root;

    // Update path edit - hide actual path for SIM 2
    if (getActiveSubscription() == 1) {
        m_pathEdit->setText("/");
    } else {
        m_pathEdit->setText(rootPath);
    }
}

void MainWindow::onSimSelectionChanged()
{
    int sim = getActiveSubscription();
    QString simName = (sim == 0) ? "SIM 1" : "SIM 2";
    onLog(QString("📱 Switched to %1").arg(simName));

    // Update DeviceManager subscription
    m_manager->setSubscriptionIndex(sim);

    // Clear SIM 2 discovered root so probe runs fresh
    if (sim == 1) {
        m_sim2DiscoveredRoot.clear();
    }

    // Reset EFS tree with new root for the selected SIM
    resetEfsTree();

    // Update window title to indicate active SIM
    setWindowTitle(VersionInfo::getWindowTitle(simName));
}

void MainWindow::startSim2Probe()
{
    qDebug() << "============================================";
    qDebug() << "🔍 SIM 2 AUTO-PROBE: Starting path discovery...";
    qDebug() << "============================================";

    // Known Qualcomm dual-SIM EFS paths (ordered by probability)
    m_sim2ProbePaths.clear();
    m_sim2ProbePaths << "/nv/item_files_1/"         // Most common: subscription 1 NV items
                     << "/nv_item_files_1/"          // Alternative flat path
                     << "/policyman_1/"              // Direct policyman for SIM 2
                     << "/data/nv/item_files_1/"     // Some Samsung/newer Qualcomm
                     << "/nv/item_files/modem_1/"    // Modem-based subscription
                     << "/sd/nv/item_files_1/"       // SD partition variant
                     << "/readonly/nv/item_files_1/"; // Read-only partition

    m_sim2ProbeIndex = 0;
    m_sim2Probing = true;

    qDebug() << "🔍 Paths to try:" << m_sim2ProbePaths.size();
    for (int i = 0; i < m_sim2ProbePaths.size(); i++) {
        qDebug() << "  [" << (i+1) << "]" << m_sim2ProbePaths[i];
    }

    onLog(QString("🔍 Discovering SIM 2 EFS root... (trying %1 known paths)").arg(m_sim2ProbePaths.size()));
    showGlobalProgress("Discovering SIM 2 EFS...", m_sim2ProbePaths.size());

    // Start probing first path
    QString firstPath = m_sim2ProbePaths[0];
    qDebug() << "🔍 SIM 2 Probe 1/" << m_sim2ProbePaths.size() << ":" << firstPath;

    m_currentOperationPath = firstPath;
    m_operationTimeout->start();
    m_manager->listEfsDirectory(firstPath);
}
