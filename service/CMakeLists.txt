file(GLOB_RECURSE HEADERS CONFIGURE_DEPENDS "src/*.h" "src/*.hpp")
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "src/*.cpp")

include_directories(src)

add_executable(llavon-ime-service ${SOURCES} ${HEADERS})

option(IMESVC_ENABLE_ONNX_BACKEND
    "Build llavon-ime service with ONNX Runtime backend support."
    OFF)

set(ONNXRUNTIME_NUGET_PACKAGE "Microsoft.ML.OnnxRuntime.DirectML" CACHE STRING
    "Fallback ONNX Runtime NuGet package used by llavon-ime service for headers/import library")
set(ONNXRUNTIME_NUGET_VERSION "1.24.4" CACHE STRING
    "Fallback ONNX Runtime NuGet package version used by llavon-ime service")
set(ONNXRUNTIME_NUGET_ROOT
    "$ENV{USERPROFILE}/.nuget/packages/microsoft.ml.onnxruntime.directml/${ONNXRUNTIME_NUGET_VERSION}"
    CACHE PATH "Root directory of the fallback ONNX Runtime NuGet package")
set(ONNXRUNTIME_ROOT "" CACHE PATH
    "Root directory of an ONNX Runtime install. Prefer Ryzen AI/VitisAI ONNX Runtime for NPU builds.")
option(IMESVC_COPY_ONNXRUNTIME_DLLS
    "Copy ONNX Runtime DLLs next to llavon-ime-service so Windows loads the intended Ryzen AI/VitisAI runtime."
    ON)

if(IMESVC_ENABLE_ONNX_BACKEND)
    if(NOT ONNXRUNTIME_ROOT)
        if(EXISTS "C:/Program Files/RyzenAI/1.7.1/onnxruntime/lib/onnxruntime.lib")
            set(ONNXRUNTIME_ROOT "C:/Program Files/RyzenAI/1.7.1/onnxruntime" CACHE PATH
                "Root directory of an ONNX Runtime install. Prefer Ryzen AI/VitisAI ONNX Runtime for NPU builds."
                FORCE)
        endif()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        if(CMAKE_VS_PLATFORM_NAME MATCHES "ARM64")
            set(ONNXRUNTIME_RUNTIME_ID "win-arm64")
        else()
            set(ONNXRUNTIME_RUNTIME_ID "win-x64")
        endif()
    else()
        set(ONNXRUNTIME_RUNTIME_ID "")
    endif()

    if(ONNXRUNTIME_ROOT)
        if(EXISTS "${ONNXRUNTIME_ROOT}/include/onnxruntime/core/session/onnxruntime_cxx_api.h")
            set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include/onnxruntime/core/session")
        else()
            set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_ROOT}/include")
        endif()
        get_filename_component(ONNXRUNTIME_INSTALL_ROOT "${ONNXRUNTIME_ROOT}" DIRECTORY)
        if(EXISTS "${ONNXRUNTIME_INSTALL_ROOT}/deployment/onnxruntime.dll")
            set(ONNXRUNTIME_NATIVE_DIR "${ONNXRUNTIME_INSTALL_ROOT}/deployment")
        elseif(EXISTS "${ONNXRUNTIME_ROOT}/bin/onnxruntime.dll")
            set(ONNXRUNTIME_NATIVE_DIR "${ONNXRUNTIME_ROOT}/bin")
        else()
            set(ONNXRUNTIME_NATIVE_DIR "${ONNXRUNTIME_ROOT}/lib")
        endif()
        set(ONNXRUNTIME_LIB "${ONNXRUNTIME_ROOT}/lib/onnxruntime.lib")
    else()
        set(ONNXRUNTIME_INCLUDE_DIR "${ONNXRUNTIME_NUGET_ROOT}/build/native/include")
        set(ONNXRUNTIME_NATIVE_DIR "${ONNXRUNTIME_NUGET_ROOT}/runtimes/${ONNXRUNTIME_RUNTIME_ID}/native")
        set(ONNXRUNTIME_LIB "${ONNXRUNTIME_NATIVE_DIR}/onnxruntime.lib")
    endif()

    if(ONNXRUNTIME_RUNTIME_ID AND EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h" AND EXISTS "${ONNXRUNTIME_LIB}")
        target_compile_definitions(llavon-ime-service PRIVATE IMESVC_ENABLE_ONNX=1)
        target_include_directories(llavon-ime-service PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(llavon-ime-service PRIVATE "${ONNXRUNTIME_LIB}")

        if(IMESVC_COPY_ONNXRUNTIME_DLLS)
            file(GLOB ONNXRUNTIME_RUNTIME_DLLS CONFIGURE_DEPENDS "${ONNXRUNTIME_NATIVE_DIR}/*.dll")
            add_custom_command(TARGET llavon-ime-service POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${ONNXRUNTIME_RUNTIME_DLLS}
                    "$<TARGET_FILE_DIR:llavon-ime-service>"
            )
        endif()
    else()
        target_compile_definitions(llavon-ime-service PRIVATE IMESVC_ENABLE_ONNX=0)
        message(WARNING
            "ONNX Runtime native files were not found; ONNX backend is disabled. "
            "Checked ONNXRUNTIME_ROOT='${ONNXRUNTIME_ROOT}' and fallback package root='${ONNXRUNTIME_NUGET_ROOT}'"
        )
    endif()
else()
    target_compile_definitions(llavon-ime-service PRIVATE IMESVC_ENABLE_ONNX=0)
    message(STATUS "ONNX backend disabled by IMESVC_ENABLE_ONNX_BACKEND=OFF")
endif()

target_compile_definitions(llavon-ime-service PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
)

target_compile_options(llavon-ime-service PRIVATE
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

target_link_libraries(llavon-ime-service PRIVATE
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
