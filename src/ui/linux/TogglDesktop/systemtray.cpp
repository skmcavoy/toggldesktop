#include "systemtray.h"
#include <QTimer>

#include "./toggl.h"
#include "./mainwindowcontroller.h"

SystemTray::SystemTray(const QIcon &icon, MainWindowController *parent) :
    QSystemTrayIcon(parent),
    idleHintTimer(new QTimer(this))
{
    qCritical() << icon.isNull();
    show();

    connect(TogglApi::instance, &TogglApi::displayIdleNotification, this, &SystemTray::displayIdleNotification);

    connect(TogglApi::instance, SIGNAL(displaySettings(bool,SettingsView*)),  // NOLINT
            this, SLOT(displaySettings(bool,SettingsView*)));  // NOLINT

    connect(TogglApi::instance, SIGNAL(displayReminder(QString,QString)),  // NOLINT
            this, SLOT(displayReminder(QString,QString)));  // NOLINT

    screensaver = new QDBusInterface("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", QDBusConnection::sessionBus(), this);
    notifications = new QDBusInterface("org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications", QDBusConnection::sessionBus(), this);

    connect(notifications, SIGNAL(NotificationClosed(uint,uint)), this, SLOT(notificationClosed(uint,uint)));
    connect(notifications, SIGNAL(ActionInvoked(uint,QString)), this, SLOT(notificationActionInvoked(uint,QString)));

    auto pendingCall = notifications->asyncCall("GetCapabilities");
    auto watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &SystemTray::notificationCapabilitiesReceived);

    connect(idleHintTimer, &QTimer::timeout, this, &SystemTray::requestIdleHint);
}

MainWindowController *SystemTray::mainWindow() {
    return qobject_cast<MainWindowController*>(parent());
}

void SystemTray::displaySettings(const bool open, SettingsView *settings) {
    if (settings->UseIdleDetection && !idleHintTimer->isActive()) {
        idleHintTimer->start(1000);
    } else if (!settings->UseIdleDetection && idleHintTimer->isActive()) {
        idleHintTimer->stop();
    }
}

void SystemTray::requestIdleHint() {
    auto pendingCall = screensaver->asyncCall("GetSessionIdleTime");
    auto watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &SystemTray::idleHintReceived);
}

void SystemTray::idleHintReceived(QDBusPendingCallWatcher *watcher) {
    QDBusPendingReply<uint> reply = *watcher;
    if (reply.isError()) {
        qCritical() << reply.error();
    }
    else {
        qulonglong value = reply.argumentAt<0>();
        TogglApi::instance->setIdleSeconds(value / 1000);
    }
    watcher->deleteLater();
}

void SystemTray::notificationCapabilitiesReceived(QDBusPendingCallWatcher *watcher) {
    QDBusPendingReply<QStringList> reply = *watcher;
    if (reply.isError()) {
        qCritical() << reply.error();
    }
    else {
        QStringList value = reply.argumentAt<0>();
        qCritical() << value;
    }
    watcher->deleteLater();
}

uint SystemTray::requestNotification(uint previous, const QString &title, const QString &description, const QString &actionAccept, const QString &actionReject, const QString &actionRejectAndContinue, const QString &actionNew) {
    QByteArray data;
    QImage im(":/icons/64x64/toggldesktop.png");
    im = im.convertToFormat(QImage::Format_RGBA8888);

    for (int i = 0; i < im.height(); i++) {
        data.append(reinterpret_cast<const char*>(im.scanLine(i)), im.width() * 4);
    }

    QStringList actions;
    if (!actionAccept.isEmpty()) {
        actions << "accept" << actionAccept;
    }
    if (!actionReject.isEmpty()) {
        actions << "reject" << actionReject;
    }
    if (!actionRejectAndContinue.isEmpty()) {
        actions << "rejectAndContinue" << actionRejectAndContinue;
    }
    if (!actionNew.isEmpty()) {
        actions << "newEntry" << actionNew;
    }
    // if no actions are specified, open toggl when the notification itself gets clicked
    if (actions.isEmpty()) {
        actions << "Default" << "Open Toggl";
    }

    // prepare the structure with the image beforehand
    // as far as I know, it cannot be done inline (without defining a custom stream operator)
    QDBusArgument imArg;
    imArg.beginStructure();
    imArg << im.width() // width in pixels
          << im.height() // height in pixels
          << im.width() * 4 // line stride (line length in bytes including padding)
          << 1 // has alpha
          << 8 // bits per pixel
          << 4 // samples per pixel (4 - ARGB format)
          << data; // byte array with pixel data
    imArg.endStructure();

    auto reply = notifications->call("Notify", // function name
                                     "TogglDesktop", // application name
                                     previous, // replaces ID
                                     "", // application icon - we need none because we pass it with the data
                                     title,
                                     description,
                                     actions, // actions - pairs - first string is identifier, second is display text
                                     QVariantMap { // hints
                                         { "icon_data", QVariant::fromValue(imArg) }
                                     },
                                     0); // timeout - 0 is never expire, -1 is default, >0 in ms
    qCritical() << reply.errorMessage();
    qCritical() << reply.arguments();
    return reply.arguments()[0].toUInt();
}

void SystemTray::notificationClosed(uint id, uint reason) {
    Q_UNUSED(reason);
    if (id == lastIdleNotification)
        lastIdleNotification = 0;
    if (id == lastReminder)
        lastReminder = 0;
}

void SystemTray::notificationActionInvoked(uint id, const QString &action) {
    if (id == lastReminder && action == "default") {
        mainWindow()->setWindowState(Qt::WindowActive);
    }
    else if (id == lastIdleNotification) {
        if (action == "accept") {
            // do nothing
            mainWindow()->setWindowState(Qt::WindowActive);
        }
        else if (action == "reject") {
            TogglApi::instance->discardTimeAt(lastTimeEntryGuid, lastStarted, false);
            mainWindow()->setWindowState(Qt::WindowActive);
        }
        else if (action == "rejectAndContinue") {
            TogglApi::instance->discardTimeAndContinue(lastTimeEntryGuid, lastStarted);
            mainWindow()->setWindowState(Qt::WindowActive);
        }
        else if (action== "newEntry") {
            TogglApi::instance->discardTimeAt(lastTimeEntryGuid, lastStarted, true);
            mainWindow()->setWindowState(Qt::WindowActive);
        }
    }
}

void SystemTray::displayIdleNotification(
        const QString guid,
        const QString since,
        const QString duration,
        const uint64_t started,
        const QString description) {
    lastTimeEntryGuid = guid;
    lastStarted = started;
    QString title = QString("%1 (%2)").arg(since).arg(duration);
    lastIdleNotification = requestNotification(lastIdleNotification, title, description, "Keep the time", "Discard the time", "Discard time and continue", "Add idle time as a new time entry");
}

void SystemTray::displayReminder(QString title, QString description) {
    lastReminder = requestNotification(lastReminder, title, description);
}

/*
IdleNotificationDialog::IdleNotificationDialog(QWidget *parent)
    : QDialog(parent),
  ui(new Ui::IdleNotificationDialog),
  idleStarted(0),
  timer(new QTimer(this)),
  timeEntryGUID("") {
    ui->setupUi(this);

    connect(
        TogglApi::instance,
        SIGNAL(displayIdleNotification(QString,QString,QString,uint64_t,QString)),  // NOLINT
        this,
        SLOT(displayIdleNotification(QString,QString,QString,uint64_t,QString)));  // NOLINT

    connect(TogglApi::instance, SIGNAL(displaySettings(bool,SettingsView*)),  // NOLINT
            this, SLOT(displaySettings(bool,SettingsView*)));  // NOLINT

    connect(timer, SIGNAL(timeout()), this, SLOT(timeout()));  // NOLINT

    connect(TogglApi::instance, SIGNAL(displayStoppedTimerState()),  // NOLINT
            this, SLOT(displayStoppedTimerState()));  // NOLINT

    connect(TogglApi::instance, SIGNAL(displayLogin(bool,uint64_t)),  // NOLINT
            this, SLOT(displayLogin(bool,uint64_t)));  // NOLINT

    setWindowTitle("Idle Detection");
}

void IdleNotificationDialog::displayLogin(const bool open,
        const uint64_t user_id) {
    if (open || !user_id) {
        hide();
    }
}
void IdleNotificationDialog::displayIdleNotification(
    const QString guid,
    const QString since,
    const QString duration,
    const uint64_t started,
    const QString description) {
    idleStarted = started;
    timeEntryGUID = guid;

    ui->idleSince->setText(since);
    ui->idleDuration->setText(duration);

    ui->timeEntryDescriptionLabel->setText(description);

    show();
}

void IdleNotificationDialog::timeout() {
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        return;
    }
    XScreenSaverInfo *info = XScreenSaverAllocInfo();
    if (XScreenSaverQueryInfo(display, DefaultRootWindow(display), info)) {
        uint64_t idleSeconds = info->idle / 1000;
        TogglApi::instance->setIdleSeconds(idleSeconds);
    }
    XFree(info);
    XCloseDisplay(display);
}

void IdleNotificationDialog::on_pushButton_clicked() {
    TogglApi::instance->discardTimeAt(timeEntryGUID, idleStarted, true);
    hide();
}
*/

/*
bool MainWindowController::hasTrayIcon() const {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 5, 0))
    return true;
#endif
    QString currentDesktop = QProcessEnvironment::systemEnvironment().value(
        "XDG_CURRENT_DESKTOP", "");
    return "Unity" != currentDesktop;
}
*/
