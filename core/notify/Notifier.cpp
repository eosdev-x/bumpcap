#include "core/notify/Notifier.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusReply>
#include <QVariantMap>

#include "core/log/Log.h"
#include "core/state/StateStore.h"

namespace kh::notify {

Notifier::Notifier(QObject *parent) : QObject(parent) {}

void Notifier::setApplicationName(const QString &application_name) {
    application_name_ = application_name.isEmpty() ? QStringLiteral("Bumpcap")
                                                   : application_name;
}

void Notifier::setStateStore(kh::state::StateStore *state_store) {
    state_store_ = state_store;
}

void Notifier::notify(const QString &summary,
                      const QString &body,
                      const QStringList &actions) {
    sendNotification(summary, body, QStringLiteral("org.bumpcap.Bumpcap"), actions);
}

void Notifier::notify(const QString &summary, const QString &body, const QString &icon) {
    sendNotification(summary, body, icon, QStringList());
}

void Notifier::notifyNewKernel(kh::model::SourceId source_id,
                               const QString &version,
                               const QString &summary,
                               const QString &body,
                               const QString &icon) {
    if (state_store_ != nullptr && state_store_->isOpen()) {
        const QStringList seen_versions = state_store_->seenVersions(source_id);
        if (seen_versions.contains(version)) {
            return;
        }
        state_store_->markSeen(source_id, version, false);
    }
    sendNotification(summary, body, icon, QStringList());
    if (state_store_ != nullptr && state_store_->isOpen()) {
        state_store_->setNotified(source_id, version, true);
    }
}

void Notifier::sendNotification(const QString &summary,
                                const QString &body,
                                const QString &icon,
                                const QStringList &actions) {
    QDBusInterface interface(QStringLiteral("org.freedesktop.Notifications"),
                             QStringLiteral("/org/freedesktop/Notifications"),
                             QStringLiteral("org.freedesktop.Notifications"),
                             QDBusConnection::sessionBus());
    if (!interface.isValid()) {
        const QString error = interface.lastError().message();
        qCWarning(kh::log::notify) << "Notification service unavailable" << error;
        emit notificationFailed(error);
        return;
    }

    QVariantList arguments;
    arguments << application_name_
              << uint(0)
              << icon
              << summary
              << body
              << actions
              << QVariantMap()
              << int(-1);

    QDBusReply<uint> reply = interface.callWithArgumentList(QDBus::AutoDetect,
                                                            QStringLiteral("Notify"),
                                                            arguments);
    if (!reply.isValid()) {
        const QString error = reply.error().message();
        qCWarning(kh::log::notify) << "Failed to send notification" << error;
        emit notificationFailed(error);
        return;
    }
    emit notificationSent(reply.value());
}

}  // namespace kh::notify
