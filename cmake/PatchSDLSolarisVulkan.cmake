# QIF Viewer Solaris Vulkan compatibility patch for SDL3.
#
# SDL 3.4.x and current SDL main restrict SDL_VULKAN to a platform allow-list
# that does not include SOLARIS.  SDL's X11 Vulkan implementation itself is
# usable on Solaris when the system supplies a Vulkan loader/ICD and Xlib/XCB
# WSI (for example from a Mesa build configured with Vulkan support).
#
# This patch is intentionally narrow: it only adds SOLARIS to SDL_VULKAN's
# CMake dependency expression.  QIF Viewer then explicitly requests
# SDL_VULKAN=ON and SDL_RENDER_VULKAN=ON.  If SDL changes the expression in an
# incompatible way, configuration stops rather than silently building without
# Vulkan.

if(NOT DEFINED SDL_CMAKE OR SDL_CMAKE STREQUAL "")
    message(FATAL_ERROR "SDL_CMAKE must name SDL's CMakeLists.txt")
endif()
if(NOT EXISTS "${SDL_CMAKE}")
    message(FATAL_ERROR "SDL CMakeLists.txt not found: ${SDL_CMAKE}")
endif()

file(READ "${SDL_CMAKE}" _sdl)

# Already patched/upstream-supported: nothing to do.
string(REGEX MATCH "dep_option\\(SDL_VULKAN[^\n]*SOLARIS" _already "${_sdl}")
if(_already)
    message(STATUS "SDL3 Solaris Vulkan platform gate already permits SOLARIS")
    return()
endif()

# Known SDL 3.x allow-list forms. Keep this explicit so a future SDL refactor
# cannot be modified accidentally.
set(_candidates
    "ANDROID OR APPLE OR LINUX OR FREEBSD OR OPENBSD OR WINDOWS OR CYGWIN OR OHOS|ANDROID OR APPLE OR LINUX OR FREEBSD OR OPENBSD OR SOLARIS OR WINDOWS OR CYGWIN OR OHOS"
    "ANDROID OR APPLE OR LINUX OR FREEBSD OR OPENBSD OR WINDOWS OR CYGWIN|ANDROID OR APPLE OR LINUX OR FREEBSD OR OPENBSD OR SOLARIS OR WINDOWS OR CYGWIN"
    "ANDROID OR APPLE OR LINUX OR FREEBSD OR OPENBSD OR WINDOWS|ANDROID OR APPLE OR LINUX OR FREEBSD OR OPENBSD OR SOLARIS OR WINDOWS"
)

set(_patched FALSE)
foreach(_pair IN LISTS _candidates)
    string(REPLACE "|" ";" _parts "${_pair}")
    list(GET _parts 0 _old)
    list(GET _parts 1 _new)
    string(FIND "${_sdl}" "${_old}" _where)
    if(NOT _where EQUAL -1)
        string(REPLACE "${_old}" "${_new}" _sdl "${_sdl}")
        set(_patched TRUE)
        break()
    endif()
endforeach()

if(NOT _patched)
    message(FATAL_ERROR
        "Could not safely patch SDL_VULKAN for Solaris. SDL's CMake platform "
        "gate has changed. Inspect ${SDL_CMAKE} and update "
        "cmake/PatchSDLSolarisVulkan.cmake (or use an SDL build that already "
        "contains the Solaris Vulkan change).")
endif()

file(WRITE "${SDL_CMAKE}" "${_sdl}")
file(READ "${SDL_CMAKE}" _verify)
string(REGEX MATCH "dep_option\\(SDL_VULKAN[^\n]*SOLARIS" _ok "${_verify}")
if(NOT _ok)
    message(FATAL_ERROR "SDL Solaris Vulkan patch verification failed")
endif()
message(STATUS "Patched SDL3 CMake platform gate to permit Vulkan on Solaris")
