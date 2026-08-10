#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <QStringList>

#include <optional>

#include "core/model/SourceId.h"
#include "core/sources/CompatibilityResult.h"

namespace kh::model {

enum class KernelStatus {
    Available,
    Installed,
    InstalledRunning,
    UpdateAvailable,
};

struct KernelInfo {
    SourceId sourceId = SourceId::FedoraStable;
    QString sourceDisplayName;
    QString version;
    QString shortVersion;
    QDateTime releaseDate;
    KernelStatus status = KernelStatus::Available;
    bool isPinned = false;
    QString notes;
    QStringList subPackages;
    QString changelog;
    QString sha256;
    std::optional<kh::sources::CompatibilityResult> compatibility;
};

}  // namespace kh::model

Q_DECLARE_METATYPE(kh::model::KernelStatus)
Q_DECLARE_METATYPE(kh::model::KernelInfo)

