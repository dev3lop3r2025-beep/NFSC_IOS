# =============================================================================
#  Need for Speed: Carbon - iOS App (arm64, iOS 16.0+)
#
#  Configure with the preset in ios/CMakePresets.json:
#      cmake --preset ios
#      cmake --build --preset ios
#      cmake --build --preset ios --target ipa    ios/build/NFSCarbon.ipa
# =============================================================================

cmake_minimum_required(VERSION 3.25)
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
set(NFSCARBON_JOBS_RECOMP "4" CACHE STRING "Generated files compiled at once")
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
if(EXISTS "${NFSCARBON_GEN_DIR}/sources.cmake")
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

target_compile_definitions(nfscarbon PRIVATE
    REXGLUE_BUILD_CONFIG="$<CONFIG>"
    NFSCARBON_WITH_GAME=$<BOOL:${NFSCARBON_WITH_GAME}>
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

add_custom_target(nfscarbon_package ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_fw}"
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<TARGET_EXISTS:rexruntime>,copy_if_different,true>
            "$<TARGET_FILE:rexruntime>" "${_fw}/"
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<TARGET_EXISTS:rexgpu-xenos>,copy_if_different,true>
            "$<TARGET_FILE:rexgpu-xenos>" "${_fw}/"
    COMMAND ${CMAKE_COMMAND} -E $<IF:$<EXISTS:${NFSCARBON_MOLTENVK_FW}>,copy_directory,true>
            "${NFSCARBON_MOLTENVK_FW}" "${_fw}/MoltenVK.framework"
    COMMAND xcrun actool "${CMAKE_CURRENT_SOURCE_DIR}/resources/Assets.xcassets"
            --compile "${_app}" --platform iphoneos
            --minimum-deployment-target "${CMAKE_OSX_DEPLOYMENT_TARGET}"
            --target-device iphone --target-device ipad --app-icon AppIcon
            --output-partial-info-plist "${CMAKE_CURRENT_BINARY_DIR}/actool-partial.plist"
            --output-format human-readable-text --errors --warnings || true
    COMMAND xattr -cr "${_app}" || true
    COMMAND codesign --force --sign - --timestamp=none "${_fw}/MoltenVK.framework" 2>/dev/null || true
    COMMAND codesign --force --sign - --timestamp=none
            --entitlements "${CMAKE_CURRENT_SOURCE_DIR}/nfscarbon.entitlements" "${_app}" || true
    COMMENT "NFSCarbon: Packaging and signing NFSCarbon.app"
    VERBATIM
)
add_dependencies(nfscarbon_package nfscarbon)

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
