#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QSystemTrayIcon>
#include <QtDBus/QtDBus>

class SystemTray : public QSystemTrayIcon
{
    Q_OBJECT
public:
    SystemTray(const QIcon &icon, QObject *parent = nullptr);


private slots:
    void requestIdleHint();
    void idleHintReceived(QDBusPendingCallWatcher *watcher);

    void notificationCapabilitiesReceived(QDBusPendingCallWatcher *watcher);

    void displayIdleNotification(
        const QString guid,
        const QString since,
        const QString duration,
        const uint64_t started,
        const QString description);

    void displayReminder(QString title, QString description);

private:
    QTimer *idleHintTimer;
    QDBusInterface *screensaver;
    QDBusInterface *notifications;
};

#endif // SYSTEMTRAY_H
