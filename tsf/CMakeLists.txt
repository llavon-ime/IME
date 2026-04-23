file(GLOB_RECURSE HEADERS "src/*.h" "src/*.hpp")
file(GLOB_RECURSE SOURCES "src/*.cpp")
set(DEF_FILE "${CMAKE_CURRENT_SOURCE_DIR}/ime.def")

include_directories(src)

add_library(IME SHARED ${SOURCES} ${HEADERS} ${DEF_FILE})

target_compile_definitions(IME PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    WINVER=0x0A00        # Windows 10
    _WIN32_WINNT=0x0A00
)

target_compile_options(IME PRIVATE
    /W4         # 高警告等級
    /WX         # 警告視為錯誤
    /utf-8      # 原始碼 UTF-8 編碼
)

find_package(cppwinrt CONFIG REQUIRED)
find_package(jsoncons CONFIG REQUIRED)
find_package(utf8cpp CONFIG REQUIRED)
find_package(llama CONFIG REQUIRED)

target_link_libraries(IME PRIVATE
    Microsoft::CppWinRT
    WindowsApp
    gdiplus
    advapi32    # Registry API
    ole32       # COM
    oleaut32    # COM Automation
    ws2_32      # Winsock2 (DebugSink)

    jsoncons
    utf8::cpp
    utf8cpp::utf8cpp
    llama
)


set_target_properties(IME PROPERTIES
    OUTPUT_NAME "MyIME"
    PREFIX      ""
)
