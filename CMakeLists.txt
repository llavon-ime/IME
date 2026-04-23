cmake_minimum_required(VERSION 3.20)
project(TSF_IME VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
    message(FATAL_ERROR "此專案需要 MSVC 編譯器（Windows TSF IME 僅支援 MSVC）")
endif()

# 預設 Release 建置
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# add_subdirectory(tsf_service) # wil version
add_subdirectory(tsf)         # cppwinrt version
