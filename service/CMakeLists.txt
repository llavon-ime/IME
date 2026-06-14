file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h" "src/*.hpp")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

include_directories(src)

add_executable(IME_Service ${SOURCES} ${HEADERS})

set(ONNXRUNTIME_NUGET_PACKAGE "Microsoft.ML.OnnxRuntime.DirectML" CACHE STRING
    "ONNX Runtime package used by IME_Service for headers/import library on this machine")
set(ONNXRUNTIME_NUGET_VERSION "1.24.4" CACHE STRING
    "ONNX Runtime NuGet package version used by IME_Service")
set(ONNXRUNTIME_NUGET_ROOT
    "$ENV{USERPROFILE}/.nuget/packages/microsoft.ml.onnxruntime.directml/${ONNXRUNTIME_NUGET_VERSION}"
    CACHE PATH "Root directory of the restored ONNX Runtime package")
option(IMESVC_COPY_ONNXRUNTIME_DLLS
    "Copy ONNX Runtime DLLs next to IME_Service. Keep this OFF for Ryzen AI/VitisAI deployments so the Ryzen AI runtime DLLs are used."
    OFF)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    if(CMAKE_VS_PLATFORM_NAME MATCHES "ARM64")
        set(ONNXRUNTIME_RUNTIME_ID "win-arm64")
    else()
        set(ONNXRUNTIME_RUNTIME_ID "win-x64")
    endif()
else()
    set(ONNXRUNTIME_RUNTIME_ID "")
endif()

set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_NUGET_ROOT}/build/native/include")
set(ONNXRUNTIME_NATIVE_DIR "${ONNXRUNTIME_NUGET_ROOT}/runtimes/${ONNXRUNTIME_RUNTIME_ID}/native")
set(ONNXRUNTIME_LIB "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime.lib")

if(ONNXRUNTIME_RUNTIME_ID AND EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h" AND EXISTS "${ONNXRUNTIME_LIB}")
    target_compile_definitions(IME_Service PRIVATE IMESVC_ENABLE_ONNX=1)
    target_include_directories(IME_Service PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
    target_link_libraries(IME_Service PRIVATE "${ONNXRUNTIME_LIB}")

    if(IMESVC_COPY_ONNXRUNTIME_DLLS)
        add_custom_command(TARGET IME_Service POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime.dll"
                "$<TARGET_FILE_DIR:IME_Service>/onnxruntime.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime_providers_shared.dll"
                "$<TARGET_FILE_DIR:IME_Service>/onnxruntime_providers_shared.dll"
        )
    endif()
else()
    target_compile_definitions(IME_Service PRIVATE IMESVC_ENABLE_ONNX=0)
    message(WARNING
        "ONNX Runtime native files were not found; ONNX backend is disabled. "
        "Expected package root: ${ONNXRUNTIME_NUGET_ROOT}"
    )
endif()

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
