set(KERNELHUB_GUI_HEADERS
    MainWindow.h
    DetailPanel.h
    ProgressDialog.h
    SettingsDialog.h
    TrayIcon.h
    models/KernelTableModel.h
    models/KernelBadgeDelegate.h
    theme/Theme.h
    theme/ThemeManager.h
    theme/Animations.h
    widgets/StatusBadge.h
    widgets/SourceBadge.h
    widgets/KernelCard.h
    widgets/SearchFilter.h
    widgets/AnimatedRefreshButton.h
)

set(KERNELHUB_GUI_SOURCES
    main.cpp
    MainWindow.cpp
    DetailPanel.cpp
    ProgressDialog.cpp
    SettingsDialog.cpp
    TrayIcon.cpp
    models/KernelTableModel.cpp
    models/KernelBadgeDelegate.cpp
    theme/ThemeManager.cpp
    theme/Animations.cpp
    widgets/StatusBadge.cpp
    widgets/SourceBadge.cpp
    widgets/KernelCard.cpp
    widgets/SearchFilter.cpp
    widgets/AnimatedRefreshButton.cpp
)

qt_add_executable(bumpcap_gui
    ${KERNELHUB_GUI_HEADERS}
    ${KERNELHUB_GUI_SOURCES}
    resources/bumpcap.qrc
)

target_include_directories(bumpcap_gui
    PRIVATE
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/core
)

target_link_libraries(bumpcap_gui
    PRIVATE
        Bumpcap::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::DBus
        Qt6::Sql
        Qt6::Concurrent
        Qt6::Svg
)

target_compile_features(bumpcap_gui PRIVATE cxx_std_17)

target_compile_options(bumpcap_gui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

set_target_properties(bumpcap_gui PROPERTIES
    OUTPUT_NAME bumpcap
)

install(TARGETS bumpcap_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
