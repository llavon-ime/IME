cmake_minimum_required(VERSION 3.20)
project(IME_Linux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})

option(IME_LINUX_BUILD_TESTS "Build Linux tests" ON)
include(LlamaCpp)

function(ime_linux_install_destination output directory)
    if(IS_ABSOLUTE "${directory}")
        file(RELATIVE_PATH relative_directory "${CMAKE_INSTALL_PREFIX}" "${directory}")
        if(NOT relative_directory MATCHES "^\\.\\.")
            set(${output} "${relative_directory}" PARENT_SCOPE)
            return()
        endif()
    endif()

    set(${output} "${directory}" PARENT_SCOPE)
endfunction()

add_library(ime_linux_shared STATIC shared/ime_linux_shared.cpp)
set_target_properties(ime_linux_shared PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ime_linux_shared PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/shared")
target_compile_options(ime_linux_shared PRIVATE -Wall -Wextra -Wpedantic)

find_package(nlohmann_json CONFIG REQUIRED)
target_sources(ime_linux_shared PRIVATE shared/config/config.cpp)
target_sources(ime_linux_shared PRIVATE
    shared/text/utf.cpp
    shared/bopomofo/keymap.cpp
    shared/bopomofo/syllable.cpp
    shared/bopomofo/table_engine.cpp
    shared/buffer/composition_buffer.cpp
    shared/engine/fallback_engine.cpp
    shared/engine/service_client.cpp
    shared/protocol/protocol.cpp
    shared/protocol/json_codec.cpp
    shared/ipc/unix_socket.cpp
)
target_link_libraries(ime_linux_shared PUBLIC nlohmann_json::nlohmann_json)

add_library(ime_linux_service_lib STATIC service/service_app.cpp service/llama_backend.cpp)
set_target_properties(ime_linux_service_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ime_linux_service_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ime_linux_service_lib PUBLIC ime_linux_shared)
target_compile_definitions(ime_linux_service_lib PRIVATE IME_LINUX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(ime_linux_service_lib PRIVATE -Wall -Wextra -Wpedantic)
ime_linux_configure_llama(ime_linux_service_lib)

add_executable(ime-linux-service service/service_main.cpp)
target_link_libraries(ime-linux-service PRIVATE ime_linux_service_lib)
set_target_properties(ime-linux-service PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/service")
install(TARGETS ime-linux-service RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(CODE "
    file(REMOVE \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/ime-linux-config\")
    file(REMOVE \"\$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/applications/ime-linux-config.desktop\")
")

find_package(Fcitx5 QUIET)
if(Fcitx5_FOUND)
    find_package(Fcitx5Utils QUIET CONFIG)
    if(NOT DEFINED FCITX_INSTALL_ADDONDIR)
        set(FCITX_INSTALL_ADDONDIR "${CMAKE_INSTALL_FULL_LIBDIR}/fcitx5")
    endif()
    if(NOT DEFINED FCITX_INSTALL_PKGDATADIR)
        set(FCITX_INSTALL_PKGDATADIR "${CMAKE_INSTALL_FULL_DATADIR}/fcitx5")
    endif()
    ime_linux_install_destination(IME_LINUX_FCITX_ADDONDIR "${FCITX_INSTALL_ADDONDIR}")
    ime_linux_install_destination(IME_LINUX_FCITX_PKGDATADIR "${FCITX_INSTALL_PKGDATADIR}")

    add_library(ime-fcitx5-addon MODULE fcitx5/ime_engine.cpp fcitx5/ime_config.cpp)
    target_include_directories(ime-fcitx5-addon PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(ime-fcitx5-addon PRIVATE Fcitx5::Core ime_linux_shared)
    target_compile_definitions(ime-fcitx5-addon PRIVATE IME_LINUX_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(ime-fcitx5-addon PRIVATE -Wall -Wextra -Wpedantic)
    set_target_properties(ime-fcitx5-addon PROPERTIES PREFIX "")
    configure_file(fcitx5/addon.conf.in "${CMAKE_BINARY_DIR}/fcitx5/addon/ime-linux.conf" @ONLY)
    configure_file(fcitx5/inputmethod.conf.in "${CMAKE_BINARY_DIR}/fcitx5/inputmethod/ime-linux.conf" @ONLY)
    install(TARGETS ime-fcitx5-addon LIBRARY DESTINATION "${IME_LINUX_FCITX_ADDONDIR}")
    install(FILES "${CMAKE_BINARY_DIR}/fcitx5/addon/ime-linux.conf"
            DESTINATION "${IME_LINUX_FCITX_PKGDATADIR}/addon")
    install(FILES "${CMAKE_BINARY_DIR}/fcitx5/inputmethod/ime-linux.conf"
            DESTINATION "${IME_LINUX_FCITX_PKGDATADIR}/inputmethod")
else()
    message(STATUS "fcitx5 development files not found; skipping ime-fcitx5-addon")
endif()

if(IME_LINUX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
