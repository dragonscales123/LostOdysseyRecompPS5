# cmake/LoPs5.cmake
# PlayStation 5 homebrew title build, with the PS5 payload SDK's toolchain
# (toolchain/prospero.cmake) and Mihawk's PS5_Vulkan (RADV linked into the
# title). See docs/PS5.md.
#
#   LO_PS5_VULKAN_DIR   PS5_Vulkan checkout with its RADV release archive built
#   LO_PS5_TITLE_ID     PPSAxxxxx title id written to sce_sys/param.json

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

# The console's CPU is an 8-core Zen 2.
set(LO_TARGET_ARCH "znver2" CACHE STRING "Target CPU architecture (e.g. sandybridge, znver2; empty for the compiler baseline)")

# Vendor upscalers and frame generation ship Windows/Linux binaries only.
foreach(_lo_ps5_off LO_ENABLE_DLSS LO_ENABLE_FSR LO_ENABLE_STREAMLINE_FG LO_ENABLE_XESS LO_ENABLE_XESS_FG
        LO_ENABLE_VULKAN_FSR_FG LO_BUILD_AUDIO_TESTS)
    set(${_lo_ps5_off} OFF CACHE BOOL "" FORCE)
endforeach()

get_filename_component(_lo_ps5_parent "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(LO_PS5_VULKAN_DIR "${_lo_ps5_parent}/PS5_Vulkan" CACHE PATH "PS5_Vulkan checkout (RADV for the console)")
set(LO_PS5_TITLE_ID "PPSA99212" CACHE STRING "Title id of the PS5 homebrew title")
set(LO_PS5_TITLE_NAME "Lost Odyssey" CACHE STRING "Home screen name of the PS5 homebrew title")
set(LO_PS5_DIST_DIR "${CMAKE_BINARY_DIR}/dist" CACHE PATH "Where the title folder <TITLE_ID>/ is written")

if(NOT EXISTS "${LO_PS5_VULKAN_DIR}/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a")
    message(WARNING "No RADV release archive in ${LO_PS5_VULKAN_DIR}; the title link will fail. "
        "Build it with PS5_Vulkan's tools/build-radv.sh release (docs/PS5.md).")
endif()

# Every executable is linked as a PS5 title by ps5/link-title.sh, which gets
# CMake's objects and fully resolved static libraries and runs PS5_Vulkan's
# link recipe (RADV, the SDK's platform layer, the CRT, the fake SELF).
set(_lo_ps5_link_config "${CMAKE_BINARY_DIR}/ps5-link.conf")
file(WRITE "${_lo_ps5_link_config}"
    "vulkan='${LO_PS5_VULKAN_DIR}'\n"
    "sdk='${PS5_PAYLOAD_SDK}'\n"
    "title_id='${LO_PS5_TITLE_ID}'\n"
    "title_name='${LO_PS5_TITLE_NAME}'\n"
    "dist='${LO_PS5_DIST_DIR}'\n"
    "param_template='${CMAKE_SOURCE_DIR}/ps5/sce_sys/param.json'\n"
    "icon='${CMAKE_SOURCE_DIR}/ps5/sce_sys/icon0.png'\n"
    "work='${CMAKE_BINARY_DIR}/ps5-link'\n")
set(CMAKE_CXX_LINK_EXECUTABLE
    "bash \"${CMAKE_SOURCE_DIR}/ps5/link-title.sh\" \"${_lo_ps5_link_config}\" <TARGET> <OBJECTS> <LINK_LIBRARIES>")
set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE}")
