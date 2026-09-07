# =============================================================================
#  RL Studio training engine
#
#  Builds `rl-engine`, the C++ program that actually runs Rocket League physics
#  and trains the neural network. The desktop app spawns this as a child process,
#  but it is a complete standalone command-line tool on its own.
#
#  Quick start (from the repository root):
#      pwsh -File tools/setup.ps1
#
#  Manual build:
#      cmake -S engine -B engine/build -G "Ninja Multi-Config" -DCMAKE_PREFIX_PATH=engine/libtorch
#      cmake --build engine/build --config Release --parallel
#
#  IMPORTANT: Release only. On Windows, debug and release libtorch are not
#  ABI-compatible, and we ship against the release build.
# =============================================================================
cmake_minimum_required(VERSION 3.21)

project(RLStudioEngine
    VERSION 0.1.0
    DESCRIPTION "Rocket League reinforcement-learning training engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put every binary in one place so the app knows where to find rl-engine.exe
# and so the libtorch DLLs sit next to it.
foreach(cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY${cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

option(RLS_BUILD_TESTS "Build the unit test suite" ON)
option(RLS_ENABLE_RLVISER "Build RLViser UDP streaming support" ON)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- libtorch (PyTorch C++) --------------------------------------------------
# Downloaded by tools/setup.ps1 into engine/libtorch.
if(NOT Torch_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/libtorch/share/cmake/Torch")
    set(Torch_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libtorch/share/cmake/Torch" CACHE PATH "" FORCE)
endif()

find_package(Torch QUIET)
if(NOT Torch_FOUND)
    message(FATAL_ERROR
        "libtorch was not found.\n"
        "  Expected: ${CMAKE_CURRENT_SOURCE_DIR}/libtorch\n"
        "  Fix:      pwsh -File tools/setup.ps1\n"
        "  (or pass -DCMAKE_PREFIX_PATH=<path to libtorch>)")
endif()
message(STATUS "libtorch: ${TORCH_LIBRARIES}")

# --- RocketSim (Rocket League physics, MIT) ---------------------------------
set(ROCKETSIM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/RocketSim")
if(NOT EXISTS "${ROCKETSIM_DIR}/src/RocketSim.h")
    message(FATAL_ERROR
        "RocketSim source is missing.\n"
        "  Expected: ${ROCKETSIM_DIR}\n"
        "  Fix:      pwsh -File tools/setup.ps1")
endif()
add_subdirectory("${ROCKETSIM_DIR}" "${CMAKE_BINARY_DIR}/rocketsim" EXCLUDE_FROM_ALL)

# RocketSim declares no usage requirements of its own, so publish them here.
target_include_directories(RocketSim SYSTEM PUBLIC "${ROCKETSIM_DIR}/src")
if(MSVC)
    # Third-party code is not held to our warning bar, and Bullet is warning-heavy.
    target_compile_options(RocketSim PRIVATE /W0 /wd4267 /wd4244)
endif()

# --- FlatBuffers (RLViser wire format) --------------------------------------
if(RLS_ENABLE_RLVISER)
    set(FLATBUFFERS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_INSTALL OFF CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATLIB ON CACHE BOOL "" FORCE)
    set(FLATBUFFERS_BUILD_FLATC ON CACHE BOOL "" FORCE)
    FetchContent_Declare(flatbuffers
        GIT_REPOSITORY https://github.com/google/flatbuffers.git
        GIT_TAG v24.3.25
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(flatbuffers)

    # Generate C++ headers from the vendored rlviser schemas.
    set(RLS_FBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/schemas")
    set(RLS_FBS_OUT "${CMAKE_BINARY_DIR}/generated/rlviser")
    file(MAKE_DIRECTORY "${RLS_FBS_OUT}")

    set(RLS_FBS_FILES
        "${RLS_FBS_DIR}/common.fbs"
        "${RLS_FBS_DIR}/game_state.fbs"
        "${RLS_FBS_DIR}/render.fbs"
        "${RLS_FBS_DIR}/core.fbs"
    )
    set(RLS_FBS_HEADERS
        "${RLS_FBS_OUT}/common_generated.h"
        "${RLS_FBS_OUT}/game_state_generated.h"
        "${RLS_FBS_OUT}/render_generated.h"
        "${RLS_FBS_OUT}/core_generated.h"
    )

    add_custom_command(
        OUTPUT ${RLS_FBS_HEADERS}
        COMMAND flatc --cpp --scoped-enums -o "${RLS_FBS_OUT}" -I "${RLS_FBS_DIR}" ${RLS_FBS_FILES}
        DEPENDS flatc ${RLS_FBS_FILES}
        COMMENT "Generating RLViser FlatBuffers headers"
        VERBATIM
    )
    add_custom_target(rls_flatbuffers_gen DEPENDS ${RLS_FBS_HEADERS})
endif()

# --- Catch2 (tests) ----------------------------------------------------------
if(RLS_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()

# -----------------------------------------------------------------------------
# rlstudio_core — everything except main(), so tests can link against it
# -----------------------------------------------------------------------------
file(GLOB_RECURSE RLS_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/sim/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/env/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ppo/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/learner/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ipc/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/util/*.cpp"
)
file(GLOB_RECURSE RLS_CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

if(NOT RLS_CORE_SOURCES)
    # Lets the project configure before any implementation exists.
    set(RLS_EMPTY_TU "${CMAKE_BINARY_DIR}/generated/empty.cpp")
    file(WRITE "${RLS_EMPTY_TU}" "// placeholder translation unit\n")
    set(RLS_CORE_SOURCES "${RLS_EMPTY_TU}")
endif()

add_library(rlstudio_core STATIC ${RLS_CORE_SOURCES} ${RLS_CORE_HEADERS})

target_include_directories(rlstudio_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(rlstudio_core PUBLIC
    RocketSim
    "${TORCH_LIBRARIES}"
)

if(RLS_ENABLE_RLVISER)
    target_include_directories(rlstudio_core PUBLIC "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(rlstudio_core PUBLIC flatbuffers)
    target_compile_definitions(rlstudio_core PUBLIC RLS_ENABLE_RLVISER=1)
    add_dependencies(rlstudio_core rls_flatbuffers_gen)
endif()

if(MSVC)
    target_compile_options(rlstudio_core PRIVATE /W4 /permissive- /Zc:__cplusplus /MP /bigobj)
    # libtorch headers are noisy; silence only what they trigger, not our own bugs.
    target_compile_options(rlstudio_core PRIVATE /wd4251 /wd4275)
    target_compile_definitions(rlstudio_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(rlstudio_core PUBLIC ws2_32)
else()
    target_compile_options(rlstudio_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# -----------------------------------------------------------------------------
# rl-engine — the command-line executable
# -----------------------------------------------------------------------------
file(GLOB RLS_CLI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/*.cpp")
if(RLS_CLI_SOURCES)
    add_executable(rl-engine ${RLS_CLI_SOURCES})
    target_link_libraries(rl-engine PRIVATE rlstudio_core)
    if(MSVC)
        target_compile_options(rl-engine PRIVATE /W4 /permissive- /Zc:__cplusplus /MP)
    endif()

    # libtorch ships as DLLs that must sit beside the executable on Windows.
    if(MSVC)
        file(GLOB TORCH_DLLS "${TORCH_INSTALL_PREFIX}/lib/*.dll")
        add_custom_command(TARGET rl-engine POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TORCH_DLLS} $<TARGET_FILE_DIR:rl-engine>
            COMMAND_EXPAND_LISTS
            COMMENT "Copying libtorch DLLs next to rl-engine.exe"
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
if(RLS_BUILD_TESTS)
    file(GLOB_RECURSE RLS_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    if(RLS_TEST_SOURCES)
        enable_testing()
        add_executable(rlstudio_tests ${RLS_TEST_SOURCES})
        target_link_libraries(rlstudio_tests PRIVATE rlstudio_core Catch2::Catch2WithMain)
        if(MSVC)
            target_compile_options(rlstudio_tests PRIVATE /W4 /permissive- /Zc:__cplusplus /MP /bigobj)
            file(GLOB TORCH_DLLS "${TORCH_INSTALL_PREFIX}/lib/*.dll")
            add_custom_command(TARGET rlstudio_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${TORCH_DLLS} $<TARGET_FILE_DIR:rlstudio_tests>
                COMMAND_EXPAND_LISTS
            )
        endif()

        list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
        include(Catch)
        catch_discover_tests(rlstudio_tests)
    endif()
endif()

# -----------------------------------------------------------------------------
message(STATUS "")
message(STATUS "RL Studio engine ${PROJECT_VERSION}")
message(STATUS "  Tests:   ${RLS_BUILD_TESTS}")
message(STATUS "  RLViser: ${RLS_ENABLE_RLVISER}")
message(STATUS "  Torch:   ${TORCH_INSTALL_PREFIX}")
message(STATUS "")
