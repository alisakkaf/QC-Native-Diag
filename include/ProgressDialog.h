#ifndef PROGRESSDIALOG_H
#define PROGRESSDIALOG_H

#include <QDialog>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QGraphicsBlurEffect>
#include <QTimer>

class ProgressDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ProgressDialog(QWidget *parent = nullptr);
    
    void setOperation(const QString &operation);
    void setStatus(const QString &status);
    void setProgress(int value, int max = 100);
    void showIndeterminate();
    void startCountdown(int seconds);
    
signals:
    void timeoutReached();
    
protected:
    void paintEvent(QPaintEvent *event) override;
    
private:
    QLabel *m_lblOperation;
    QLabel *m_lblStatus;
    QLabel *m_lblCountdown;
    QProgressBar *m_progress;
    QVBoxLayout *m_layout;
    
    QTimer *m_countdownTimer;
    int m_remainingSeconds;
};

#endif // PROGRESSDIALOG_H
