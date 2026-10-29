#!/usr/bin/env bash
# ==============================================================================
#  build_ipa.sh - Automated Build & Sideload Packaging for Need for Speed: Carbon
#
#  Targets: iOS 16.0+, iPadOS 16.0+ (ARM64 Native Metal)
#  Supported Sideloaders: SideStore, LiveContainer, AltStore, TrollStore
# ==============================================================================

set -euo pipefail

# Formatting
BOLD="\033[1m"
GREEN="\033[0;32m"
CYAN="\033[0;36m"
YELLOW="\033[1;33m"
RED="\033[0;31m"
RESET="\033[0m"

echo -e "${BOLD}${CYAN}======================================================================${RESET}"
echo -e "${BOLD}${CYAN}     NEED FOR SPEED: CARBON (XBOX 360) - iOS ARM64 BUILD PIPELINE      ${RESET}"
echo -e "${BOLD}${CYAN}======================================================================${RESET}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

PROJECT_NAME="NFSCarbon"
BUILD_DIR="${SCRIPT_DIR}/build"
IPA_OUTPUT="${BUILD_DIR}/NFS-Carbon-iOS-unsigned.ipa"

# 1. Dependency Checks
echo -e "\n${BOLD}${YELLOW}[1/5] Checking build dependencies...${RESET}"
command -v python3 >/dev/null 2>&1 || { echo -e "${RED}[ERROR] python3 is required.${RESET}"; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo -e "${RED}[ERROR] cmake is required (brew install cmake).${RESET}"; exit 1; }
command -v ninja >/dev/null 2>&1 || { echo -e "${RED}[ERROR] ninja is required (brew install ninja).${RESET}"; exit 1; }

echo -e "${GREEN}[OK] Python, CMake, and Ninja detected.${RESET}"

# 2. Asset & AppIcon Preparation
echo -e "\n${BOLD}${YELLOW}[2/5] Verifying AppIcon and assets catalog...${RESET}"
if [ ! -f "${SCRIPT_DIR}/ios/resources/Assets.xcassets/AppIcon.appiconset/AppIcon-1024.png" ]; then
    echo -e "${CYAN}Generating AppIcon catalog via tools/generate_appicon.py...${RESET}"
    python3 "${SCRIPT_DIR}/tools/generate_appicon.py"
fi
echo -e "${GREEN}[OK] AppIcon catalog verified.${RESET}"

# 3. Code Generation / Cache Restoration
echo -e "\n${BOLD}${YELLOW}[3/5] Checking generated C++ recompilation sources...${RESET}"
if [ ! -f "${SCRIPT_DIR}/generated/default/sources.cmake" ]; then
    echo -e "${CYAN}Running code generator / cache unpacker...${RESET}"
    python3 "${SCRIPT_DIR}/tools/generate_code.py" --unpack-cache || true
fi

if [ -f "${SCRIPT_DIR}/generated/default/sources.cmake" ]; then
    echo -e "${GREEN}[OK] Generated C++ sources present.${RESET}"
else
    echo -e "${YELLOW}[NOTE] No generated sources found. App will build in probe mode unless default.xex is provided.${RESET}"
fi

# 3.5. Prepare ReXGlue iOS SDK
echo -e "\n${BOLD}${YELLOW}[3.5/5] Preparing ReXGlue iOS SDK...${RESET}"
python3 "${SCRIPT_DIR}/tools/ios/prepare_sdk.py" --sdk "${SCRIPT_DIR}/../rexglue-sdk-ios"
if [ -d "${SCRIPT_DIR}/../rexglue-sdk-ios" ]; then
    (cd "${SCRIPT_DIR}/../rexglue-sdk-ios" && git tag -f v0.10.0 HEAD 2>/dev/null || true)
fi

# 4. Configure & Compile via CMake
echo -e "\n${BOLD}${YELLOW}[4/5] Configuring and building iOS targets...${RESET}"
mkdir -p "${BUILD_DIR}"

cd "${SCRIPT_DIR}/ios"
if command -v xcrun >/dev/null 2>&1; then
    export CC="$(xcrun --sdk iphoneos --find clang 2>/dev/null || which clang)"
    export CXX="$(xcrun --sdk iphoneos --find clang++ 2>/dev/null || which clang++)"
    export OBJC="${CC}"
    export OBJCXX="${CXX}"
fi
cmake --preset ios \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_SYSTEM_PROCESSOR=arm64 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0 \
    -DREX_PLATFORM_IOS=1 \
    ${CC:+-DCMAKE_C_COMPILER="${CC}"} \
    ${CXX:+-DCMAKE_CXX_COMPILER="${CXX}"} \
    ${OBJC:+-DCMAKE_OBJC_COMPILER="${OBJC}"} \
    ${OBJCXX:+-DCMAKE_OBJCXX_COMPILER="${OBJCXX}"} \
    ${CC:+-DCMAKE_ASM_COMPILER="${CC}"}
cmake --build --preset ios --target ipa

cd "${SCRIPT_DIR}"

# 5. Output Verification & Checksums
echo -e "\n${BOLD}${YELLOW}[5/5] Packaging and checksum verification...${RESET}"

if [ -f "${SCRIPT_DIR}/ios/build/NFS-Carbon-iOS-unsigned.ipa" ]; then
    cp -f "${SCRIPT_DIR}/ios/build/NFS-Carbon-iOS-unsigned.ipa" "${IPA_OUTPUT}"
    cp -f "${SCRIPT_DIR}/ios/build/NFSCarbon.ipa" "${BUILD_DIR}/NFSCarbon.ipa" 2>/dev/null || true
elif [ -f "${SCRIPT_DIR}/ios/build/NFSCarbon.ipa" ]; then
    cp -f "${SCRIPT_DIR}/ios/build/NFSCarbon.ipa" "${IPA_OUTPUT}"
fi

if [ -f "${IPA_OUTPUT}" ]; then
    IPA_SIZE=$(ls -lh "${IPA_OUTPUT}" | awk '{print $5}')
    echo -e "${GREEN}✓ Successfully generated:${RESET} ${BOLD}${IPA_OUTPUT}${RESET} (${IPA_SIZE})"
    
    if command -v shasum >/dev/null 2>&1; then
        SHA=$(shasum -a 256 "${IPA_OUTPUT}" | awk '{print $1}')
        echo -e "${CYAN}SHA-256 Checksum:${RESET} ${SHA}"
        echo "${SHA}  NFS-Carbon-iOS-unsigned.ipa" > "${BUILD_DIR}/checksums.txt"
    fi

    echo -e "\n${BOLD}${GREEN}Ready for sideloading with SideStore / LiveContainer / AltStore!${RESET}"
else
    echo -e "${RED}[ERROR] Expected IPA was not found at ${IPA_OUTPUT}.${RESET}"
    exit 1
fi
