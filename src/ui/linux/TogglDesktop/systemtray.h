#ifndef SYSTEMTRAY_H
#define SYSTEMTRAY_H

#include <QSystemTrayIcon>
#include <QtDBus/QtDBus>

class MainWindowController;
class SettingsView;

class SystemTray : public QSystemTrayIcon
{
    Q_OBJECT
public:
    SystemTray(const QIcon &icon, MainWindowController *parent = nullptr);

    MainWindowController *mainWindow();

protected slots:
    void displaySettings(const bool open, SettingsView *settings);

    void requestIdleHint();
    void idleHintReceived(QDBusPendingCallWatcher *watcher);

    void notificationCapabilitiesReceived(QDBusPendingCallWatcher *watcher);
    uint requestNotification(uint previous,
                             const QString &title,
                             const QString &description,
                             const QString &actionAccept = QString(),
                             const QString &actionReject = QString(),
                             const QString &actionRejectAndContinue = QString(),
                             const QString &actionNew = QString());
    void notificationClosed(uint id, uint reason);
    void notificationActionInvoked(uint id, const QString &action);

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

    uint lastIdleNotification { 0 };
    uint lastReminder { 0 };

    QString lastTimeEntryGuid;
    uint64_t lastStarted;
};

#endif // SYSTEMTRAY_H
