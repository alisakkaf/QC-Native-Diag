#include "Logger.h"
#include <QDebug>

Logger* Logger::m_instance = nullptr;
QMutex Logger::m_mutex;

Logger::Logger(QObject* parent) : QObject(parent) {}

Logger* Logger::instance()
{
    QMutexLocker locker(&m_mutex);
    if (!m_instance) {
        m_instance = new Logger();
    }
    return m_instance;
}

void Logger::log(LogLevel level, const QString& message)
{
    if (!m_instance) return;
    
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    emit m_instance->logMessage(timestamp, level, message);
    
    // Also print to console for debugging
    qDebug() << qPrintable(timestamp) << level << qPrintable(message);
}

void Logger::debug(const QString& message)    { log(Debug, message); }
void Logger::info(const QString& message)     { log(Info, message); }
void Logger::success(const QString& message)  { log(Success, message); }
void Logger::warning(const QString& message)  { log(Warning, message); }
void Logger::error(const QString& message)    { log(Error, message); }

void Logger::logHex(const QByteArray& data, const QString& label, LogLevel level)
{
    QString hexStr = QString(data.toHex().toUpper());
    // Insert spaces for readability? Optional.
    if (hexStr.length() > 60) hexStr = hexStr.left(60) + "...";
    log(level, QString("%1: %2").arg(label, hexStr));
}
