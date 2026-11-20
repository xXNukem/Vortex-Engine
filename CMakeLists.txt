cmake_minimum_required(VERSION 3.21)

project(VortexEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

option(VORTEX_ENABLE_SDL3 "Build the SDL3 audio and input backends" ON)

if(VORTEX_ENABLE_SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()

include(CTest)

add_subdirectory(src/vortex)

if(BUILD_TESTING)
    add_subdirectory(tests/vortex)
endif()

# Installable Vortex Engine package.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set_target_properties(vortex_cpu PROPERTIES EXPORT_NAME CPU)
set_target_properties(vortex_audio PROPERTIES EXPORT_NAME Audio)

install(TARGETS vortex_cpu vortex_audio
    EXPORT VortexTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

set(VORTEX_HAS_AUDIO_SDL OFF)
set(VORTEX_HAS_INPUT_SDL OFF)

if(TARGET vortex_audio_sdl)
    set(VORTEX_HAS_AUDIO_SDL ON)
    set_target_properties(vortex_audio_sdl PROPERTIES EXPORT_NAME AudioSDL)

    install(TARGETS vortex_audio_sdl
        EXPORT VortexTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(TARGET vortex_input_sdl)
    set(VORTEX_HAS_INPUT_SDL ON)
    set_target_properties(vortex_input_sdl PROPERTIES EXPORT_NAME InputSDL)

    install(TARGETS vortex_input_sdl
        EXPORT VortexTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(DIRECTORY include/vortex/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/vortex
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
        PATTERN "*.tpp"
)

install(EXPORT VortexTargets
    FILE VortexTargets.cmake
    NAMESPACE Vortex::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Vortex
)

configure_package_config_file(
    cmake/VortexConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/VortexConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Vortex
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/VortexConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/VortexConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/VortexConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Vortex
)
