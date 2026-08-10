set(KERNELHUB_HELPER_SOURCES
    main.cpp
    HelperService.cpp
    HelperService.h
)

if(NOT PolkitQt6-1_FOUND)
    message(FATAL_ERROR "KERNELHUB_BUILD_HELPER requires PolkitQt6-1")
endif()

add_executable(bumpcap-helper ${KERNELHUB_HELPER_SOURCES})

target_compile_features(bumpcap-helper PRIVATE cxx_std_17)

target_include_directories(bumpcap-helper PRIVATE ${PROJECT_SOURCE_DIR})

target_link_libraries(bumpcap-helper
    PRIVATE
        Qt6::Core
        Qt6::DBus
        PolkitQt6-1::Core
)

target_compile_options(bumpcap-helper PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS bumpcap-helper
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/bumpcap)

install(FILES org.bumpcap.Helper1.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/system-services)

install(FILES org.bumpcap.Helper1.conf
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/dbus-1/system.d)

install(FILES org.bumpcap.policy
        DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
