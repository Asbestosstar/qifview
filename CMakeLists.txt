cmake_minimum_required(VERSION 3.20)
project(qifviewer VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

option(QIFVIEWER_FETCH_DEPS "Fetch third-party dependencies if not supplied by the parent build" ON)
option(QIFVIEWER_BUILD_GUI "Build the SDL3 graphical QIF viewer" ON)
option(QIFVIEWER_BUILD_CHECK "Build the headless qifcheck utility" ON)

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
            GIT_TAG release-3.4.16
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(SDL3)
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
