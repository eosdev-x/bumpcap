#include "core/model/SourceId.h"

#include <array>

namespace kh::model {
namespace {

struct SourceIdMapping {
    SourceId source_id;
    const char *key;
    const char *display_name;
};

constexpr std::array<SourceIdMapping, 5> kSourceIdMappings = {{
    {SourceId::FedoraStable, "fedora-stable", "Fedora Stable"},
    {SourceId::FedoraRawhide, "fedora-rawhide", "Fedora Rawhide"},
    {SourceId::CachyOsStable, "cachyos-stable", "CachyOS"},
    {SourceId::CachyOsLts, "cachyos-lts", "CachyOS LTS"},
    {SourceId::KernelOrgMainline, "kernelorg-mainline", "Kernel.org Mainline"},
}};

}  // namespace

QString SourceIdToString(SourceId source_id) {
    for (const SourceIdMapping &mapping : kSourceIdMappings) {
        if (mapping.source_id == source_id) {
            return QString::fromLatin1(mapping.key);
        }
    }
    return QStringLiteral("unknown");
}

QString SourceIdDisplayName(SourceId source_id) {
    for (const SourceIdMapping &mapping : kSourceIdMappings) {
        if (mapping.source_id == source_id) {
            return QString::fromUtf8(mapping.display_name);
        }
    }
    return QStringLiteral("Unknown");
}

std::optional<SourceId> SourceIdFromString(QStringView source_id) {
    for (const SourceIdMapping &mapping : kSourceIdMappings) {
        if (source_id == QLatin1String(mapping.key)) {
            return mapping.source_id;
        }
    }
    return std::nullopt;
}

}  // namespace kh::model

