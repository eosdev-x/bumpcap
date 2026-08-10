#include "core/sources/CpuFeatures.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

#include "core/log/Log.h"

namespace kh::sources {
namespace {

bool HasAllFlags(const QSet<QString> &flags, const QStringList &required) {
    for (const QString &flag : required) {
        if (!flags.contains(flag)) {
            return false;
        }
    }
    return true;
}

MicroarchitectureLevel LevelFromFlags(const QSet<QString> &flags) {
    const QStringList v2 = {
        QStringLiteral("cx16"),
        QStringLiteral("lahf_lm"),
        QStringLiteral("popcnt"),
        QStringLiteral("sse3"),
        QStringLiteral("ssse3"),
        QStringLiteral("sse4_1"),
        QStringLiteral("sse4_2"),
    };
    const QStringList v3 = {
        QStringLiteral("avx"),
        QStringLiteral("avx2"),
        QStringLiteral("bmi1"),
        QStringLiteral("bmi2"),
        QStringLiteral("f16c"),
        QStringLiteral("fma"),
        QStringLiteral("movbe"),
        QStringLiteral("xsave"),
    };
    const QStringList v4 = {
        QStringLiteral("avx512f"),
        QStringLiteral("avx512bw"),
        QStringLiteral("avx512cd"),
        QStringLiteral("avx512dq"),
        QStringLiteral("avx512vl"),
    };

    if (HasAllFlags(flags, v2) && HasAllFlags(flags, v3) && HasAllFlags(flags, v4)) {
        return MicroarchitectureLevel::X86_64V4;
    }
    if (HasAllFlags(flags, v2) && HasAllFlags(flags, v3)) {
        return MicroarchitectureLevel::X86_64V3;
    }
    if (HasAllFlags(flags, v2)) {
        return MicroarchitectureLevel::X86_64V2;
    }
    return flags.isEmpty() ? MicroarchitectureLevel::Unknown
                           : MicroarchitectureLevel::X86_64V1;
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
bool BuiltinSupportsV2() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("x86-64-v2");
}

bool BuiltinSupportsV3() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("x86-64-v3");
}

bool BuiltinSupportsV4() {
    __builtin_cpu_init();
    return __builtin_cpu_supports("x86-64-v4");
}

CpuFeatures DetectWithBuiltins() {
    CpuFeatures features;
    features.detectionMethod = QStringLiteral("__builtin_cpu_supports");
    if (BuiltinSupportsV4()) {
        features.level = MicroarchitectureLevel::X86_64V4;
    } else if (BuiltinSupportsV3()) {
        features.level = MicroarchitectureLevel::X86_64V3;
    } else if (BuiltinSupportsV2()) {
        features.level = MicroarchitectureLevel::X86_64V2;
    } else {
        features.level = MicroarchitectureLevel::X86_64V1;
    }
    return features;
}
#endif

QSet<QString> ParseFlagsLine(const QString &line) {
    const int separator = line.indexOf(QLatin1Char(':'));
    const QString flags_text = separator >= 0 ? line.mid(separator + 1) : line;
    const QStringList parts = flags_text.split(QRegularExpression(QStringLiteral("\\s+")),
                                               Qt::SkipEmptyParts);
    QSet<QString> flags;
    for (const QString &part : parts) {
        flags.insert(part.trimmed());
    }
    return flags;
}

}  // namespace

CpuFeatures DetectHostCpuFeatures() {
    static CpuFeatures cached_features;
    static bool has_cached_features = false;
    if (has_cached_features) {
        return cached_features;
    }
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    CpuFeatures builtin_features = DetectWithBuiltins();
    if (builtin_features.level != MicroarchitectureLevel::Unknown) {
        cached_features = builtin_features;
        has_cached_features = true;
        return cached_features;
    }
#endif

    QFile cpuinfo(QStringLiteral("/proc/cpuinfo"));
    if (!cpuinfo.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qCWarning(kh::log::sources) << "Unable to read /proc/cpuinfo for CPU feature detection";
        cached_features = {};
        has_cached_features = true;
        return cached_features;
    }
    cached_features = DetectCpuFeaturesFromCpuInfo(QString::fromUtf8(cpuinfo.readAll()));
    has_cached_features = true;
    return cached_features;
}

MicroarchitectureLevel DetectMicroarchitectureLevel() {
    return DetectHostCpuFeatures().level;
}

CpuFeatures DetectCpuFeaturesFromCpuInfo(const QString &cpu_info) {
    CpuFeatures features;
    features.detectionMethod = QStringLiteral("/proc/cpuinfo");
    const QStringList lines = cpu_info.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("flags")) ||
            line.startsWith(QStringLiteral("Features"))) {
            features.flags = ParseFlagsLine(line);
            break;
        }
    }
    features.level = LevelFromFlags(features.flags);
    return features;
}

QString MicroarchitectureLevelToString(MicroarchitectureLevel level) {
    switch (level) {
    case MicroarchitectureLevel::X86_64V1:
        return QStringLiteral("x86-64-v1");
    case MicroarchitectureLevel::X86_64V2:
        return QStringLiteral("x86-64-v2");
    case MicroarchitectureLevel::X86_64V3:
        return QStringLiteral("x86-64-v3");
    case MicroarchitectureLevel::X86_64V4:
        return QStringLiteral("x86-64-v4");
    case MicroarchitectureLevel::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

bool SupportsLevel(const CpuFeatures &features, MicroarchitectureLevel required_level) {
    return static_cast<int>(features.level) >= static_cast<int>(required_level);
}

}  // namespace kh::sources
