file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h" "src/*.hpp")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

include_directories(src)

add_executable(IME_Service ${SOURCES} ${HEADERS})

target_compile_definitions(IME_Service PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

target_compile_options(IME_Service PRIVATE
    /W4
    /WX
    /utf-8
    /await:strict
)

find_package(cppwinrt CONFIG REQUIRED)
find_package(reflectcpp CONFIG REQUIRED)
find_package(utf8cpp CONFIG REQUIRED)
find_package(llama CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)

target_link_libraries(IME_Service PRIVATE
    Microsoft::CppWinRT
    WindowsApp
    gdiplus
    advapi32
    ole32
    oleaut32
    ws2_32

    reflectcpp::reflectcpp
    utf8::cpp
    utf8cpp::utf8cpp
    llama
    asio::asio
)
