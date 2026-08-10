add_executable(bumpcap-cli main.cpp)

target_compile_features(bumpcap-cli PRIVATE cxx_std_17)

target_link_libraries(bumpcap-cli
    PRIVATE
        Bumpcap::Core
        Qt6::Core
        Qt6::DBus
        Qt6::Sql
)

target_compile_options(bumpcap-cli PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS bumpcap-cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
