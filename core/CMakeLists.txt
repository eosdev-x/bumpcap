set(KERNELHUB_CORE_HEADERS
    model/KernelInfo.h
    model/SourceId.h
    sources/CompatibilityResult.h
    sources/CachyOsCoprSource.h
    sources/CpuFeatures.h
    sources/FedoraRawhideSource.h
    sources/FedoraStableSource.h
    sources/IKernelSource.h
    sources/KernelPackageUtils.h
    sources/SourceId.h
    pkg/DnfCliBackend.h
    pkg/IPackageBackend.h
    pkg/PackageKitBackend.h
    boot/Grub2BlsManager.h
    boot/IBootloaderManager.h
    repo/KernelRepository.h
    config/ConfigManager.h
    state/StateStore.h
    net/NetworkClient.h
    notify/Notifier.h
    log/Log.h
)

set(KERNELHUB_CORE_SOURCES
    model/SourceId.cpp
    sources/CachyOsCoprSource.cpp
    sources/CpuFeatures.cpp
    sources/FedoraRawhideSource.cpp
    sources/FedoraStableSource.cpp
    sources/KernelPackageUtils.cpp
    pkg/DnfCliBackend.cpp
    pkg/PackageKitBackend.cpp
    boot/Grub2BlsManager.cpp
    repo/KernelRepository.cpp
    config/ConfigManager.cpp
    state/StateStore.cpp
    net/NetworkClient.cpp
    notify/Notifier.cpp
    log/Log.cpp
)

qt_add_resources(KERNELHUB_CORE_RESOURCES state/migrations.qrc)

add_library(bumpcap STATIC
    ${KERNELHUB_CORE_HEADERS}
    ${KERNELHUB_CORE_SOURCES}
    ${KERNELHUB_CORE_RESOURCES}
)

add_library(Bumpcap::Core ALIAS bumpcap)

set_target_properties(bumpcap PROPERTIES
    OUTPUT_NAME bumpcap
    POSITION_INDEPENDENT_CODE ON
)

target_compile_features(bumpcap PUBLIC cxx_std_17)

target_include_directories(bumpcap
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(bumpcap
    PUBLIC
        Qt6::Core
    PRIVATE
        Qt6::Concurrent
        Qt6::DBus
        Qt6::Network
        Qt6::Sql
)

target_compile_options(bumpcap PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)
