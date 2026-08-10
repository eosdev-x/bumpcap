#pragma once

#include <QMetaType>
#include <QString>
#include <QStringView>

#include <optional>

namespace kh::model {

enum class SourceId {
    FedoraStable,
    FedoraRawhide,
    CachyOsStable,
    CachyOsLts,
    KernelOrgMainline,
};

QString SourceIdToString(SourceId source_id);
QString SourceIdDisplayName(SourceId source_id);
std::optional<SourceId> SourceIdFromString(QStringView source_id);

}  // namespace kh::model

Q_DECLARE_METATYPE(kh::model::SourceId)

