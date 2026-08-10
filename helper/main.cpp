#include <QCoreApplication>
#include <QDBusConnection>
#include <QLoggingCategory>

#include "helper/HelperService.h"

Q_LOGGING_CATEGORY(helperMainLog, "bumpcap.helper.main")

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("bumpcap-helper"));
    QCoreApplication::setOrganizationName(QStringLiteral("Bumpcap"));

    kh::helper::HelperService service;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCCritical(helperMainLog) << "Unable to connect to the system D-Bus";
        return 2;
    }
    if (!bus.registerObject(QStringLiteral("/org/bumpcap/Helper1"), &service,
                            QDBusConnection::ExportAllSlots |
                                QDBusConnection::ExportAllSignals)) {
        qCCritical(helperMainLog) << "Unable to register helper object";
        return 2;
    }
    if (!bus.registerService(QStringLiteral("org.bumpcap.Helper1"))) {
        qCCritical(helperMainLog) << "Unable to own org.bumpcap.Helper1";
        return 2;
    }

    qCInfo(helperMainLog) << "Bumpcap helper is ready";
    return app.exec();
}
