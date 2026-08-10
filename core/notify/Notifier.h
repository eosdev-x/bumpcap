#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "core/model/SourceId.h"

namespace kh::state {
class StateStore;
}

namespace kh::notify {

class Notifier : public QObject {
    Q_OBJECT

public:
    explicit Notifier(QObject *parent = nullptr);

    void setApplicationName(const QString &application_name);
    void setStateStore(kh::state::StateStore *state_store);
    void notify(const QString &summary,
                const QString &body,
                const QStringList &actions = QStringList());
    void notify(const QString &summary, const QString &body, const QString &icon);
    void notifyNewKernel(kh::model::SourceId source_id,
                         const QString &version,
                         const QString &summary,
                         const QString &body,
                         const QString &icon = QStringLiteral("org.bumpcap.Bumpcap"));

signals:
    void notificationSent(uint notificationId);
    void notificationFailed(QString errorMessage);

private:
    void sendNotification(const QString &summary,
                          const QString &body,
                          const QString &icon,
                          const QStringList &actions);

    QString application_name_ = QStringLiteral("Bumpcap");
    kh::state::StateStore *state_store_ = nullptr;
};

}  // namespace kh::notify
