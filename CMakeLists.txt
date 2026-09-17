cmake_minimum_required(VERSION 3.20)
project(qifviewer VERSION 0.5.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(QIFVIEWER_FETCH_DEPS "Fetch third-party dependencies if not supplied by the parent build" ON)
option(QIFVIEWER_BUILD_GUI "Build the SDL3 graphical QIF viewer" ON)
option(QIFVIEWER_BUILD_CHECK "Build the headless qifcheck utility" ON)
option(QIFVIEWER_SOLARIS_SDL_VULKAN_PATCH "Patch fetched SDL3 so its Vulkan renderer can be enabled on Solaris" ON)
option(QIFVIEWER_INSTALL_QIF_SCHEMAS "Install bundled QIF 3.0 XSD/XSLT conformance resources" ON)
set(QIFVIEWER_SDL_GIT_TAG "release-3.4.16" CACHE STRING "SDL3 git tag/commit used when QIFVIEWER_FETCH_DEPS=ON")

if(CMAKE_SYSTEM_NAME STREQUAL "SunOS" OR CMAKE_SYSTEM_NAME STREQUAL "Solaris")
    set(QIFVIEWER_TARGET_SOLARIS TRUE)
else()
    set(QIFVIEWER_TARGET_SOLARIS FALSE)
endif()

# A parent project or offline build can provide any of these targets first.
if(NOT TARGET pugixml::pugixml)
    if(QIFVIEWER_FETCH_DEPS)
        set(PUGIXML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(PUGIXML_BUILD_SHARED_AND_STATIC_LIBS OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            pugixml
            GIT_REPOSITORY https://github.com/zeux/pugixml.git
            GIT_TAG v1.16
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(pugixml)
    else()
        find_package(pugixml CONFIG REQUIRED)
    endif()
endif()

if(QIFVIEWER_BUILD_GUI AND NOT TARGET SDL3::SDL3)
    if(QIFVIEWER_FETCH_DEPS)
        set(SDL_SHARED OFF CACHE BOOL "" FORCE)
        set(SDL_STATIC ON CACHE BOOL "" FORCE)
        set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            SDL3
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG ${QIFVIEWER_SDL_GIT_TAG}
            GIT_SHALLOW TRUE
        )
        if(QIFVIEWER_TARGET_SOLARIS AND QIFVIEWER_SOLARIS_SDL_VULKAN_PATCH)
            # SDL 3.4.x/current main gate SDL_VULKAN with a platform allow-list
            # that omits Solaris. Fetch first, apply our narrow compatibility
            # patch, then explicitly request the Vulkan render backend.
            FetchContent_GetProperties(SDL3)
            if(NOT sdl3_POPULATED)
                FetchContent_Populate(SDL3)
            endif()
            execute_process(
                COMMAND ${CMAKE_COMMAND}
                    -DSDL_CMAKE=${sdl3_SOURCE_DIR}/CMakeLists.txt
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchSDLSolarisVulkan.cmake
                RESULT_VARIABLE _qifviewer_sdl_patch_result
            )
            if(NOT _qifviewer_sdl_patch_result EQUAL 0)
                message(FATAL_ERROR "Failed to enable the SDL3 Solaris Vulkan build path")
            endif()
            set(SDL_X11 ON CACHE BOOL "" FORCE)
            set(SDL_VULKAN ON CACHE BOOL "" FORCE)
            set(SDL_RENDER_VULKAN ON CACHE BOOL "" FORCE)
            message(STATUS "QIF Viewer: requesting SDL3 X11 + Vulkan renderer support on Solaris")
            add_subdirectory(${sdl3_SOURCE_DIR} ${sdl3_BINARY_DIR} EXCLUDE_FROM_ALL)
            if(NOT SDL_VULKAN OR NOT SDL_RENDER_VULKAN)
                message(FATAL_ERROR
                    "SDL3 configured without the requested Solaris Vulkan renderer. "
                    "Use a Solaris Vulkan loader/ICD with X11 WSI, or disable "
                    "QIFVIEWER_SOLARIS_SDL_VULKAN_PATCH and provide a compatible SDL3 build.")
            endif()
        else()
            FetchContent_MakeAvailable(SDL3)
        endif()
    else()
        find_package(SDL3 CONFIG REQUIRED)
    endif()
endif()

# earcut is header-only. Fetching only the source avoids pulling in its test setup.
if(NOT DEFINED QIFVIEWER_EARCUT_INCLUDE_DIR)
    if(QIFVIEWER_FETCH_DEPS)
        FetchContent_Declare(
            earcut
            GIT_REPOSITORY https://github.com/mapbox/earcut.hpp.git
            GIT_TAG v2.2.4
            GIT_SHALLOW TRUE
        )
        FetchContent_GetProperties(earcut)
        if(NOT earcut_POPULATED)
            FetchContent_Populate(earcut)
        endif()
        set(QIFVIEWER_EARCUT_INCLUDE_DIR "${earcut_SOURCE_DIR}/include")
    else()
        find_path(QIFVIEWER_EARCUT_INCLUDE_DIR mapbox/earcut.hpp REQUIRED)
    endif()
endif()

add_library(qifcore STATIC
    src/qif_model.cpp
)
target_include_directories(qifcore
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PRIVATE "${QIFVIEWER_EARCUT_INCLUDE_DIR}"
)
target_link_libraries(qifcore PUBLIC pugixml::pugixml)

if(MSVC)
    target_compile_options(qifcore PRIVATE /W4 /permissive-)
else()
    target_compile_options(qifcore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(QIFVIEWER_BUILD_CHECK)
    add_executable(qifcheck src/qifcheck.cpp)
    target_link_libraries(qifcheck PRIVATE qifcore)
    install(TARGETS qifcheck RUNTIME DESTINATION bin)
endif()

if(QIFVIEWER_BUILD_GUI)
    add_executable(qifviewer src/main.cpp)
    target_link_libraries(qifviewer PRIVATE qifcore SDL3::SDL3)
    if(APPLE)
        set_target_properties(qifviewer PROPERTIES MACOSX_BUNDLE FALSE)
    endif()
    install(TARGETS qifviewer RUNTIME DESTINATION bin)
endif()

# Install conformance tooling alongside the viewer. qifvalidate is the main
# entry point; the other tools are useful for package/type audits and for
# verifying a Solaris SDL source tree.
install(PROGRAMS tools/validate_qif.py DESTINATION bin RENAME qifvalidate)
install(PROGRAMS
    tools/full_spec_audit.py
    tools/schema_coverage.py
    tools/verify_sdl_solaris_vulkan.py
    DESTINATION bin
)
if(QIFVIEWER_INSTALL_QIF_SCHEMAS)
    install(DIRECTORY spec/qif3/xsd DESTINATION share/qifviewer/qif3)
endif()
install(FILES
    README.md
    BUILDING_SOLARIS.md
    QIF_SCHEMA_COVERAGE.md
    FULL_SPEC_ROADMAP.md
    SAMPLE_VALIDATION.md
    UNLICENSE
    LICENSE
    DESTINATION share/qifviewer
)

include(CTest)
if(BUILD_TESTING)
    add_executable(qif_camera_math_test tests/camera_math_test.cpp)
    target_include_directories(qif_camera_math_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    add_test(NAME qif_camera_math COMMAND qif_camera_math_test)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME qif_schema_inventory
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/schema_inventory_test.py)
        add_test(NAME qif_full_spec_audit
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/full_spec_audit.py
                         --schema-root ${CMAKE_CURRENT_SOURCE_DIR}/spec/qif3/xsd)
        add_test(NAME qif_sdl_solaris_vulkan_patch
                 COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sdl_solaris_vulkan_patch_test.py)
        execute_process(
            COMMAND ${Python3_EXECUTABLE} -c "import lxml"
            RESULT_VARIABLE QIFVIEWER_HAVE_PYTHON_LXML
            OUTPUT_QUIET ERROR_QUIET
        )
        if(QIFVIEWER_HAVE_PYTHON_LXML EQUAL 0)
            add_test(NAME qif_minimal_full_conformance
                     COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_qif.py
                             --full ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/minimal.qif)
            add_test(NAME qif_external_reference_conformance
                     COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/validate_qif.py
                             --full ${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance/external-source.qif)
        endif()
    endif()
endif()
