add_executable(ime_linux_tests test_main.cpp config_tests.cpp bopomofo_tests.cpp buffer_tests.cpp protocol_tests.cpp service_app_tests.cpp llama_backend_tests.cpp service_client_tests.cpp)
target_link_libraries(ime_linux_tests PRIVATE ime_linux_shared ime_linux_service_lib)
add_test(NAME ime_linux_tests COMMAND ime_linux_tests)
set_tests_properties(ime_linux_tests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/..")

add_test(NAME ime_linux_service_smoke_predict COMMAND $<TARGET_FILE:ime-linux-service> --smoke-predict "ㄋㄧˇ")
set_tests_properties(ime_linux_service_smoke_predict PROPERTIES
    ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/tests/xdg-config-empty;XDG_RUNTIME_DIR=${CMAKE_BINARY_DIR}/tests/xdg-runtime"
    PASS_REGULAR_EXPRESSION "你"
    TIMEOUT 5
)

if(Fcitx5_FOUND)
    add_test(NAME ime_linux_fcitx5_metadata
        COMMAND ${CMAKE_COMMAND}
            -DADDON_CONF=${CMAKE_BINARY_DIR}/fcitx5/addon/ime-linux.conf
            -DINPUTMETHOD_CONF=${CMAKE_BINARY_DIR}/fcitx5/inputmethod/ime-linux.conf
            -P ${CMAKE_CURRENT_LIST_DIR}/check_fcitx5_metadata.cmake)
endif()
