#include "systemtray.h"
#include <QTimer>

#include "./toggl.h"

SystemTray::SystemTray(const QIcon &icon, QObject *parent) :
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

    auto pendingCall = notifications->asyncCall("GetCapabilities");
    auto watcher = new QDBusPendingCallWatcher(pendingCall, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, &SystemTray::notificationCapabilitiesReceived);

    // ask every 5 seconds
    idleHintTimer->setInterval(5000);
    connect(idleHintTimer, &QTimer::timeout, this, &SystemTray::requestIdleHint);
    idleHintTimer->start();

    QImage im = icon.pixmap(128, 128).toImage();

    QByteArray data((char*)im.bits(), im.width() * im.height() * 4);

    qCritical() << "Image size:" << data.count() << "vs" << im.size();

    QDBusArgument imArg;
    imArg.beginStructure();
    imArg << im.width() << im.height() << im.width() << 1 << 8 << 4 << data;
    imArg.endStructure();

    QDBusMessage r = notifications->call("Notify", "TogglDesktop",
                                0U,
                                "",
                                "TITLE",
                                "DESCRIPTION",
                                QStringList {"action1", "ACTION 1 LABEL", "action2", "ACTION 2 LABEL"},
                                QVariantMap {
                                    {
                                        "icon_data",
                                        QVariant::fromValue(imArg)
                                    }
                                },
                                0
                      );
    qCritical() << r.errorMessage();
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

void SystemTray::displayIdleNotification(
        const QString guid,
        const QString since,
        const QString duration,
        const uint64_t started,
        const QString description) {
    Q_UNUSED(guid); Q_UNUSED(since); Q_UNUSED(duration); Q_UNUSED(started);
    showMessage("", description, icon());
}

void SystemTray::displayReminder(QString title, QString description) {
    showMessage(title, description, icon());
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

IdleNotificationDialog::~IdleNotificationDialog() {
    delete ui;
}

void IdleNotificationDialog::displayLogin(const bool open,
        const uint64_t user_id) {
    if (open || !user_id) {
        hide();
    }
}

void IdleNotificationDialog::displayStoppedTimerState() {
    hide();
}

void IdleNotificationDialog::on_keepTimeButton_clicked() {
    hide();
}

void IdleNotificationDialog::on_discardTimeButton_clicked() {
    TogglApi::instance->discardTimeAt(timeEntryGUID, idleStarted, false);
    hide();
}

void IdleNotificationDialog::on_discardTimeAndContinueButton_clicked() {
    TogglApi::instance->discardTimeAndContinue(timeEntryGUID, idleStarted);
    hide();
}

void IdleNotificationDialog::displaySettings(
    const bool open,
    SettingsView *settings) {
    if (settings->UseIdleDetection && !timer->isActive()) {
        timer->start(1000);
    } else if (!settings->UseIdleDetection && timer->isActive()) {
        timer->stop();
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
