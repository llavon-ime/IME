include(FindPackageHandleStandardArgs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FCITX5_CORE IMPORTED_TARGET Fcitx5Core)

find_package_handle_standard_args(Fcitx5 REQUIRED_VARS FCITX5_CORE_LINK_LIBRARIES)

if(Fcitx5_FOUND AND NOT TARGET Fcitx5::Core)
    add_library(Fcitx5::Core ALIAS PkgConfig::FCITX5_CORE)
endif()
