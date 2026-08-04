#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QDateTime>

class Logger : public QObject
{
    Q_OBJECT

public:
    enum LogLevel { Debug, Info, Success, Warning, Error, Critical };

    static Logger* instance();

    static void log(LogLevel level, const QString& message);
    static void debug(const QString& message);
    static void info(const QString& message);
    static void success(const QString& message);
    static void warning(const QString& message);
    static void error(const QString& message);
    static void logHex(const QByteArray& data, const QString& label, LogLevel level = Debug);

signals:
    void logMessage(const QString& timestamp, Logger::LogLevel level, const QString& message);

private:
    explicit Logger(QObject* parent = nullptr);
    static Logger* m_instance;
    static QMutex m_mutex;
};

#endif // LOGGER_H
