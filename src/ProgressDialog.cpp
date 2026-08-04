#include "include/ProgressDialog.h"
#include <QPainter>
#include <QApplication>
#include <QScreen>
#include <QPushButton>

ProgressDialog::ProgressDialog(QWidget *parent) : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);
    
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(50, 50, 50, 50);
    
    // Operation Label
    m_lblOperation = new QLabel("Processing...");
    m_lblOperation->setAlignment(Qt::AlignCenter);
    m_lblOperation->setStyleSheet("QLabel { color: #FFFFFF; font-size: 22px; font-weight: bold; }");
    
    // Status Label
    m_lblStatus = new QLabel("");
    m_lblStatus->setAlignment(Qt::AlignCenter);
    m_lblStatus->setStyleSheet("QLabel { color: #B0B0B0; font-size: 15px; }");
    m_lblStatus->setWordWrap(true);
    
    // Countdown Label
    m_lblCountdown = new QLabel("");
    m_lblCountdown->setAlignment(Qt::AlignCenter);
    m_lblCountdown->setStyleSheet("QLabel { color: #FFA726; font-size: 18px; font-weight: bold; }");
    
    // Progress Bar
    m_progress = new QProgressBar;
    m_progress->setTextVisible(false);
    m_progress->setRange(0, 100);
    m_progress->setStyleSheet(
        "QProgressBar { background: rgba(40,40,40,200); border-radius: 12px; height: 14px; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #42A5F5, stop:1 #66BB6A); border-radius: 12px; }"
    );
    
    // Cancel Button
    QPushButton *btnCancel = new QPushButton("✖ Cancel Operation");
    btnCancel->setStyleSheet(
        "QPushButton { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #E53935, stop:1 #C62828);"
        "   color: white; font-size: 15px; font-weight: bold; "
        "   padding: 12px 40px; border-radius: 10px; "
        "   border: 2px solid #FF5252;"
        "}"
        "QPushButton:hover { "
        "   background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #EF5350, stop:1 #D32F2F);"
        "}"
        "QPushButton:pressed { background: #B71C1C; }"
    );
    connect(btnCancel, &QPushButton::clicked, this, [this](){
        emit rejected();
        reject();
    });
    
    m_layout->addWidget(m_lblOperation);
    m_layout->addSpacing(25);
    m_layout->addWidget(m_lblStatus);
    m_layout->addSpacing(20);
    m_layout->addWidget(m_lblCountdown);
    m_layout->addSpacing(25);
    m_layout->addWidget(m_progress);
    m_layout->addSpacing(25);
    m_layout->addWidget(btnCancel, 0, Qt::AlignCenter);
    
    // Countdown Timer
    m_countdownTimer = new QTimer(this);
    m_remainingSeconds = 0;
    connect(m_countdownTimer, &QTimer::timeout, [this](){
        m_remainingSeconds--;
        if(m_remainingSeconds > 0) {
            m_lblCountdown->setText(QString("⏱️ Timeout in %1 seconds...").arg(m_remainingSeconds));
        } else {
            m_countdownTimer->stop();
            m_lblCountdown->setText("⚠️ Operation Timeout!");
            m_lblCountdown->setStyleSheet("QLabel { color: #FF5252; font-size: 18px; font-weight: bold; }");
            emit timeoutReached();
        }
    });
    
    resize(600, 380);
    
    if(parent) {
        move(parent->geometry().center() - rect().center());
    }
}

void ProgressDialog::setOperation(const QString &operation)
{
    m_lblOperation->setText(operation);
}

void ProgressDialog::setStatus(const QString &status)
{
    m_lblStatus->setText(status);
}

void ProgressDialog::setProgress(int value, int max)
{
    m_progress->setRange(0, max);
    m_progress->setValue(value);
}

void ProgressDialog::showIndeterminate()
{
    m_progress->setRange(0, 0);
}

void ProgressDialog::startCountdown(int seconds)
{
    m_remainingSeconds = seconds;
    m_lblCountdown->setText(QString("⏱️ Timeout in %1 seconds...").arg(m_remainingSeconds));
    m_lblCountdown->setStyleSheet("QLabel { color: #FFA726; font-size: 18px; font-weight: bold; }");
    m_countdownTimer->start(1000);
}

void ProgressDialog::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Modern dark gradient background
    QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
    gradient.setColorAt(0, QColor(30, 30, 35));
    gradient.setColorAt(1, QColor(20, 20, 25));
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(rect(), 18, 18);
    
    // Glowing blue border
    painter.setPen(QPen(QColor(66, 165, 245), 3));
    painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 16, 16);
}
