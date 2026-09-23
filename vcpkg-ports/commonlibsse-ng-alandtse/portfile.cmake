# The maintained CommonLibSSE-NG, which is the only one with Skyrim 1.7.x support. The registry
# main builds from still serves CharmedBaryon's fork, unmaintained since 2024 and stuck at 3.7.0.
#
# Adapted from the overlay port in Soneka96/DovahLink#74, which found most of the traps below
# first. Each option is here because leaving it at upstream's default breaks something.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibSSE-NG
    REF 736dc64094e59232abfbcdf796cd0a063e136ec6  # v9.0.1
    SHA512 d680c93f1990288ab2adb06750cb42087b966ca97e7bf8958b2f8c9f2b27a4fb46626686362332877a30ff88951919224395b1bc5581fa11c0cf7b969137d904
    HEAD_REF ng
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DBUILD_TESTS=OFF
        # VR needs the OpenVR SDK from a git submodule, and GitHub archives don't include
        # submodules. This build is for flat Skyrim anyway.
        -DENABLE_SKYRIM_VR=OFF
        # Both only matter for hooking the game, which this plugin never does. Xbyak drags the
        # real <Windows.h> into every consumer, whose min/max macros then corrupt the library's
        # own declarations; patch safety fetches a disassembler over the network mid-build.
        -DSKSE_SUPPORT_XBYAK=OFF
        -DSKSE_SUPPORT_PATCH_SAFETY=OFF
        # A static library built with link-time optimisation forces it on the consumer's link too.
        -DCOMMONLIB_ENABLE_IPO=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake/CommonLibSSE)
vcpkg_copy_pdbs()

# add_commonlibsse_plugin() is defined here, and the package config includes it from its own
# directory - but upstream's install() never ships it.
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

# Upstream's config only declares spdlog, but the library's public link interface also exposes
# DirectXTK, so find_package(CommonLibSSE) fails with a missing target without this.
file(APPEND "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSEConfig.cmake"
    "\nfind_dependency(directxtk CONFIG)\n")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${SOURCE_PATH}/COPYING.txt"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright)
