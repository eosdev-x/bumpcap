#pragma once

#include <QSet>
#include <QString>

namespace kh::sources {

enum class MicroarchitectureLevel {
    Unknown,
    X86_64V1,
    X86_64V2,
    X86_64V3,
    X86_64V4,
};

struct CpuFeatures {
    MicroarchitectureLevel level = MicroarchitectureLevel::Unknown;
    QSet<QString> flags;
    QString detectionMethod;
};

CpuFeatures DetectHostCpuFeatures();
MicroarchitectureLevel DetectMicroarchitectureLevel();
CpuFeatures DetectCpuFeaturesFromCpuInfo(const QString &cpu_info);
QString MicroarchitectureLevelToString(MicroarchitectureLevel level);
bool SupportsLevel(const CpuFeatures &features, MicroarchitectureLevel required_level);

}  // namespace kh::sources
