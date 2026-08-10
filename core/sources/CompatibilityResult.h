#pragma once

#include <QMetaType>
#include <QString>

namespace kh::sources {

struct CompatibilityResult {
    bool compatible = true;
    QString reason;
};

}  // namespace kh::sources

Q_DECLARE_METATYPE(kh::sources::CompatibilityResult)

