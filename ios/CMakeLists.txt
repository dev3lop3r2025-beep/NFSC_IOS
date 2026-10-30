# =============================================================================
#  Need for Speed: Carbon - iOS App (arm64, iOS 16.0+)
#
#  Configure with the preset in ios/CMakePresets.json:
#      cmake --preset ios
#      cmake --build --preset ios
#      cmake --build --preset ios --target ipa    ios/build/NFSCarbon.ipa
# =============================================================================

cmake_minimum_required(VERSION 3.25)

# Defaults for iOS ARM64 native target before project()
if(NOT CMAKE_SYSTEM_NAME)
    set(CMAKE_SYSTEM_NAME "iOS")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR)
    set(CMAKE_SYSTEM_PROCESSOR "arm64")
endif()
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64")
endif()
if(NOT CMAKE_OSX_SYSROOT)
    set(CMAKE_OSX_SYSROOT "iphoneos")
endif()
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "16.0")
endif()
set(REX_PLATFORM_IOS 1 CACHE BOOL "Build for iOS platform" FORCE)
add_compile_definitions(REX_PLATFORM_IOS=1)

# Compiler resolution: ensure clang from the iphoneos SDK toolchain is preferred
if(APPLE AND NOT CMAKE_C_COMPILER)
    execute_process(
        COMMAND xcrun --sdk iphoneos --find clang
        OUTPUT_VARIABLE _IOS_CLANG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_IOS_CLANG AND EXISTS "${_IOS_CLANG}")
        set(CMAKE_C_COMPILER "${_IOS_CLANG}" CACHE FILEPATH "C compiler" FORCE)
    endif()
endif()
if(APPLE AND NOT CMAKE_CXX_COMPILER)
    execute_process(
        COMMAND xcrun --sdk iphoneos --find clang++
        OUTPUT_VARIABLE _IOS_CLANGXX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(_IOS_CLANGXX AND EXISTS "${_IOS_CLANGXX}")
        set(CMAKE_CXX_COMPILER "${_IOS_CLANGXX}" CACHE FILEPATH "CXX compiler" FORCE)
    endif()
endif()

project(nfscarbon_ios LANGUAGES C CXX OBJC OBJCXX ASM)

if(NOT IOS)
    message(FATAL_ERROR "This CMakeLists.txt is for iOS: configure with  cmake --preset ios")
endif()

set(NFSCARBON_REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." CACHE PATH "NFS Carbon Recompiled repository root")
set(NFSCARBON_SDK "${NFSCARBON_REPO}/../rexglue-sdk-ios" CACHE PATH
    "ReXGlue SDK prepared for iOS (tools/ios/prepare_sdk.py)")
set(NFSCARBON_BUNDLE_ID "com.recomp.nfscarbon.ios" CACHE STRING "App bundle identifier")
set(NFSCARBON_GEN_DIR "${NFSCARBON_REPO}/generated/default" CACHE PATH
    "Generated code for Carbon (tools/generate_code.py)")
set(NFSCARBON_JOBS_RECOMP "2" CACHE STRING "Generated files compiled at once")
get_filename_component(NFSCARBON_REPO "${NFSCARBON_REPO}" ABSOLUTE)
get_filename_component(NFSCARBON_SDK "${NFSCARBON_SDK}" ABSOLUTE)

# Locate MoltenVK.framework (checks local SDK marker or fallback)
set(_mvk_marker "${NFSCARBON_SDK}/.nfsc-ios/MoltenVK/v1.4.1.ready")
if(NOT EXISTS "${_mvk_marker}")
    set(_mvk_marker "${NFSCARBON_SDK}/.nfsmw-ios/MoltenVK/v1.4.1.ready")
endif()

if(EXISTS "${_mvk_marker}")
    file(STRINGS "${_mvk_marker}" _mvk_rel LIMIT_COUNT 1)
    set(NFSCARBON_MOLTENVK_FW "${NFSCARBON_SDK}/.nfsc-ios/MoltenVK/${_mvk_rel}/ios-arm64/MoltenVK.framework")
    if(NOT EXISTS "${NFSCARBON_MOLTENVK_FW}/MoltenVK")
        set(NFSCARBON_MOLTENVK_FW "${NFSCARBON_SDK}/.nfsmw-ios/MoltenVK/${_mvk_rel}/ios-arm64/MoltenVK.framework")
    endif()
endif()

# Fallback auto-discovery if marker path wasn't exact
if(NOT EXISTS "${NFSCARBON_MOLTENVK_FW}/MoltenVK")
    file(GLOB_RECURSE _found_mvk
        "${NFSCARBON_SDK}/.nfsc-ios/*/ios-arm64/MoltenVK.framework"
        "${NFSCARBON_SDK}/.nfsmw-ios/*/ios-arm64/MoltenVK.framework"
        "${NFSCARBON_SDK}/.nfsc-ios/MoltenVK.framework"
        "${NFSCARBON_SDK}/.nfsmw-ios/MoltenVK.framework"
    )
    if(_found_mvk)
        list(GET _found_mvk 0 NFSCARBON_MOLTENVK_FW)
    endif()
endif()

if(EXISTS "${NFSCARBON_MOLTENVK_FW}")
    message(STATUS "NFSCarbon: Found MoltenVK.framework at ${NFSCARBON_MOLTENVK_FW}")
else()
    message(WARNING "NFSCarbon: MoltenVK.framework not found in SDK; will not be bundled")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REXGLUE_USE_VULKAN ON CACHE BOOL "" FORCE)
set(REXGLUE_USE_D3D12 OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_DESKTOP_SDL_BACKENDS OFF CACHE BOOL "" FORCE)

add_compile_definitions(
    $<$<NOT:$<STREQUAL:$<TARGET_PROPERTY:NAME>,nfscarbon_recomp>>:REX_HAS_FIDELITYFX_SDK=1>)

if(EXISTS "${NFSCARBON_SDK}/CMakeLists.txt")
    add_subdirectory("${NFSCARBON_SDK}" rexglue-sdk)
endif()

# Header-only interface library for SDK dependencies
add_library(nfscarbon_headers INTERFACE)
if(TARGET imgui)
    target_include_directories(nfscarbon_headers INTERFACE
        $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
if(TARGET renderdoc)
    target_link_libraries(nfscarbon_headers INTERFACE renderdoc)
endif()
if(TARGET Vulkan::Headers)
    target_link_libraries(nfscarbon_headers INTERFACE Vulkan::Headers)
endif()
if(NOT IOS)
    if(TARGET Vulkan::Loader)
        target_link_libraries(nfscarbon_headers INTERFACE Vulkan::Loader)
    elseif(TARGET vulkan)
        target_link_libraries(nfscarbon_headers INTERFACE vulkan)
    endif()
endif()
if(TARGET GPUOpen::VulkanMemoryAllocator)
    target_link_libraries(nfscarbon_headers INTERFACE GPUOpen::VulkanMemoryAllocator)
endif()
if(TARGET spirv-tools-headers)
    target_link_libraries(nfscarbon_headers INTERFACE spirv-tools-headers)
endif()

# -----------------------------------------------------------------------------
#  Generated Game Code (PowerPC to ARM64 C++)
# -----------------------------------------------------------------------------
set(NFSCARBON_WITH_GAME OFF)
if(EXISTS "${NFSCARBON_GEN_DIR}/sources.cmake" AND EXISTS "${NFSCARBON_GEN_DIR}/nfsc_recomp.0.cpp")
    set(NFSCARBON_WITH_GAME ON)
    include("${NFSCARBON_GEN_DIR}/sources.cmake")

    add_library(nfscarbon_recomp STATIC ${GENERATED_SOURCES})
    target_include_directories(nfscarbon_recomp PRIVATE
        "${NFSCARBON_REPO}"
        "${NFSCARBON_REPO}/src"
        "${NFSCARBON_GEN_DIR}")
    if(TARGET rex::runtime)
        target_link_libraries(nfscarbon_recomp PUBLIC rex::runtime nfscarbon_headers)
        rexglue_apply_target_settings(nfscarbon_recomp)
    endif()

    if(EXISTS "${NFSCARBON_GEN_DIR}/nfscarbon_pch.h")
        target_precompile_headers(nfscarbon_recomp PRIVATE "${NFSCARBON_GEN_DIR}/nfscarbon_pch.h")
    endif()
    target_compile_options(nfscarbon_recomp PRIVATE -gline-tables-only -Wno-unused-variable)
    if(EXISTS "${NFSCARBON_GEN_DIR}/nfsc_register.cpp")
        set_source_files_properties("${NFSCARBON_GEN_DIR}/nfsc_register.cpp"
            PROPERTIES COMPILE_OPTIONS "-O1")
    endif()
    set_property(GLOBAL APPEND PROPERTY JOB_POOLS recomp=${NFSCARBON_JOBS_RECOMP})
    set_target_properties(nfscarbon_recomp PROPERTIES JOB_POOL_COMPILE recomp)
    message(STATUS "NFSCarbon: compiling with game code (${NFSCARBON_GEN_DIR})")
else()
    message(STATUS "NFSCarbon: no generated code found in ${NFSCARBON_GEN_DIR}. "
                   "Run: python3 tools/generate_code.py")
endif()

# -----------------------------------------------------------------------------
#  Application Target (NFSCarbon.app)
# -----------------------------------------------------------------------------
set(NFSCARBON_VERSION "1.0.0")
set(NFSCARBON_VERSION_CODE "1")
set(NFSCARBON_DISPLAY_NAME "NFS Carbon")
set(NFSCARBON_IOS_MIN "${CMAKE_OSX_DEPLOYMENT_TARGET}")
set(NFSCARBON_EXECUTABLE "NFSCarbon")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
               "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)

set(NFSCARBON_APP_SOURCES
    src/ios_main.mm
    src/launcher.mm
    src/save_manager.mm
    src/save_import.cpp
    src/save_convert.cpp
    src/boot_trace.cpp
    src/probe.cpp
    src/probe_apple.mm
    src/probe_memory.cpp
    src/battery.mm
    src/gpu_capture.mm
    src/ingame_options.mm
    src/display_rate.mm
    src/lifecycle.mm
    src/memory_report.mm
    src/touch_controls.mm
    src/audio/coreaudio_driver.mm
    src/achievements_export.cpp
    src/adaptive_qos.cpp
    src/frame_clock.cpp
    src/render_probe.cpp
    src/native_metal_probe.mm
    src/native_renderer.mm
    src/native_draw_census.cpp
    # Carbon Core Systems
    ${NFSCARBON_REPO}/src/carbon_missing_entries.cpp
    ${NFSCARBON_REPO}/src/carbon_nativo_enlaces.cpp
    ${NFSCARBON_REPO}/src/carbon_nativo_ganchos.cpp
    ${NFSCARBON_REPO}/src/render_mode_hook.cpp
    ${NFSCARBON_REPO}/src/ring_wait_hook.cpp
    ${NFSCARBON_REPO}/src/dispatch_tolerance.cpp
)

if(NFSCARBON_WITH_GAME)
    list(APPEND NFSCARBON_APP_SOURCES src/game_app.cpp)
endif()

add_executable(nfscarbon MACOSX_BUNDLE ${NFSCARBON_APP_SOURCES})
set_target_properties(nfscarbon PROPERTIES
    OUTPUT_NAME "${NFSCARBON_EXECUTABLE}"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    INSTALL_RPATH "@loader_path/Frameworks")

target_compile_options(nfscarbon PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_include_directories(nfscarbon PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${NFSCARBON_REPO}"
    "${NFSCARBON_REPO}/src"
    "${NFSCARBON_SDK}/include"
    "${NFSCARBON_GEN_DIR}"
)

set(NFSCARBON_GAME_LANGUAGE "1" CACHE STRING "Game language ID (1=English)")
set(NFSCARBON_GAME_COUNTRY "103" CACHE STRING "Game country ID (103=USA)")

target_compile_definitions(nfscarbon PRIVATE
    REXGLUE_BUILD_CONFIG="$<CONFIG>"
    NFSCARBON_WITH_GAME=$<BOOL:${NFSCARBON_WITH_GAME}>
    NFSMW_WITH_GAME=$<BOOL:${NFSCARBON_WITH_GAME}>
    NFSCARBON_GAME_LANGUAGE="${NFSCARBON_GAME_LANGUAGE}"
    NFSMW_GAME_LANGUAGE="${NFSCARBON_GAME_LANGUAGE}"
    NFSCARBON_GAME_COUNTRY="${NFSCARBON_GAME_COUNTRY}"
    NFSMW_GAME_COUNTRY="${NFSCARBON_GAME_COUNTRY}"
    TITLE_ID="454107EC"
)

target_link_libraries(nfscarbon PRIVATE
    nfscarbon_headers
    "-framework Foundation"
    "-framework UIKit"
    "-framework Metal"
    "-framework MetalFX"
    "-framework QuartzCore"
    "-framework CoreImage"
    "-framework UniformTypeIdentifiers"
    "-framework AudioToolbox"
    "-framework AVFoundation"
    "-framework GameController"
    z
)

if(TARGET rex::runtime)
    target_link_libraries(nfscarbon PRIVATE rex::runtime)
endif()

if(NFSCARBON_WITH_GAME)
    target_link_libraries(nfscarbon PRIVATE nfscarbon_recomp)
endif()

if(TARGET rexgpu-xenos)
    add_dependencies(nfscarbon rexgpu-xenos)
endif()

# -----------------------------------------------------------------------------
#  Packaging & Code Signing Target (Ad-Hoc for Sideloading)
# -----------------------------------------------------------------------------
set(_app "$<TARGET_BUNDLE_DIR:nfscarbon>")
set(_fw "${_app}/Frameworks")

if(TARGET rexruntime)
    set(_copy_rexruntime COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rexruntime>" "${_fw}/")
    set(_sign_rexruntime COMMAND codesign --force --sign - --timestamp=none "${_fw}/$<TARGET_FILE_NAME:rexruntime>" 2>/dev/null || true)
    set(_dep_rexruntime rexruntime)
else()
    set(_copy_rexruntime COMMAND ${CMAKE_COMMAND} -E true)
    set(_sign_rexruntime COMMAND ${CMAKE_COMMAND} -E true)
    set(_dep_rexruntime "")
endif()

if(TARGET rexgpu-xenos)
    set(_copy_rexgpu COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:rexgpu-xenos>" "${_fw}/")
    set(_sign_rexgpu COMMAND codesign --force --sign - --timestamp=none "${_fw}/$<TARGET_FILE_NAME:rexgpu-xenos>" 2>/dev/null || true)
    set(_dep_rexgpu rexgpu-xenos)
else()
    set(_copy_rexgpu COMMAND ${CMAKE_COMMAND} -E true)
    set(_sign_rexgpu COMMAND ${CMAKE_COMMAND} -E true)
    set(_dep_rexgpu "")
endif()

if(EXISTS "${NFSCARBON_MOLTENVK_FW}")
    set(_copy_mvk_cmd COMMAND ${CMAKE_COMMAND} -E copy_directory "${NFSCARBON_MOLTENVK_FW}" "${_fw}/MoltenVK.framework")
    set(_sign_mvk_cmd COMMAND codesign --force --sign - --timestamp=none "${_fw}/MoltenVK.framework" 2>/dev/null || true)
else()
    set(_copy_mvk_cmd COMMAND ${CMAKE_COMMAND} -E true)
    set(_sign_mvk_cmd COMMAND ${CMAKE_COMMAND} -E true)
endif()

add_custom_target(nfscarbon_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_fw}"
    ${_copy_rexruntime}
    ${_copy_rexgpu}
    ${_copy_mvk_cmd}
    COMMAND xcrun actool "${CMAKE_CURRENT_SOURCE_DIR}/resources/Assets.xcassets"
            --compile "${_app}" --platform iphoneos
            --minimum-deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
            --target-device iphone --target-device ipad --app-icon AppIcon
            --output-partial-info-plist "${CMAKE_CURRENT_BINARY_DIR}/actool-partial.plist"
            --output-format human-readable-text --errors --warnings || true
    COMMAND xattr -cr "${_app}" || true
    ${_sign_mvk_cmd}
    ${_sign_rexruntime}
    ${_sign_rexgpu}
    COMMAND codesign --force --sign - --timestamp=none
            --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/nfscarbon.entitlements" "${_app}" || true
    COMMENT "NFSCarbon: Packaging and signing NFSCarbon.app"
    VERBATIM
)
add_dependencies(nfscarbon_package nfscarbon ${_dep_rexruntime} ${_dep_rexgpu})

# -----------------------------------------------------------------------------
#  IPA Generation Target
# -----------------------------------------------------------------------------
add_custom_target(ipa
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${CMAKE_CURRENT_BINARY_DIR}/ipa"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/ipa/Payload"
    COMMAND ditto "${_app}" "${CMAKE_CURRENT_BINARY_DIR}/ipa/Payload/NFSCarbon.app"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/NFSCarbon.ipa"
    COMMAND ${CMAKE_COMMAND} -E rm -f "${CMAKE_CURRENT_BINARY_DIR}/NFS-Carbon-iOS-unsigned.ipa"
    COMMAND ${CMAKE_COMMAND} -E chdir "${CMAKE_CURRENT_BINARY_DIR}/ipa"
            ditto -c -k --sequesterRsrc --keepParent Payload "${CMAKE_CURRENT_BINARY_DIR}/NFS-Carbon-iOS-unsigned.ipa"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/NFS-Carbon-iOS-unsigned.ipa"
            "${CMAKE_CURRENT_BINARY_DIR}/NFSCarbon.ipa"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_SOURCE_DIR}/build"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/NFS-Carbon-iOS-unsigned.ipa"
            "${CMAKE_CURRENT_SOURCE_DIR}/build/NFS-Carbon-iOS-unsigned.ipa"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/NFSCarbon.ipa"
            "${CMAKE_CURRENT_SOURCE_DIR}/build/NFSCarbon.ipa"
    COMMENT "NFSCarbon: Generated unsigned IPA package at build/NFS-Carbon-iOS-unsigned.ipa"
    VERBATIM
)
add_dependencies(ipa nfscarbon_package)
