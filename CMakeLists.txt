cmake_minimum_required(VERSION 3.25)

project(
	BeyondMastery
	VERSION 0.1.0
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
	message(FATAL_ERROR "In-source builds are not allowed.")
endif()

# ---------------------------------------------------------------------------
# CommonLibSSE-NG (git submodule, GPL-3.0-or-later)
# PRD §4: SE 1.5.97 + AE 1.6.x, no VR.
# ---------------------------------------------------------------------------
set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)  # trampolines with custom asm for the hooks

add_subdirectory(lib/commonlibsse-ng EXCLUDE_FROM_ALL)

# add_commonlibsse_plugin() lives in the submodule's cmake/CommonLibSSE.cmake. When
# CommonLibSSE is built from source (our case) its CMakeLists only includes that file
# on the prebuilt-binary path, so consumers must include it themselves. Same pattern as
# the submodule's own test_consumer_live/CMakeLists.txt.
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include("${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake")

find_path(SIMPLEINI_INCLUDE_DIRS "SimpleIni.h" REQUIRED)

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
set(SOURCES
	src/PCH.h
	src/Curves.h
	src/HookSites.h
	src/Patterns.h
	src/Stubs.h
	src/Skills.h
	src/Hooks.h
	src/Hooks.cpp
	src/Config.h
	src/Config.cpp
	src/State.h
	src/State.cpp
	src/Resources.h
	src/Resources.cpp
	src/Menu.h
	src/Menu.cpp
	src/main.cpp
)

# StructCompatibility is left at NG's default, Independent (SKSE's NoStructUse),
# and that is a verified claim rather than an accident. We do touch a gameplay
# structure whose layout changed at 1.6.629 -- Actor's ActorValueOwner moved
# from 0xB0 to 0xB8 -- but only through NG's RUNTIME_CAST_ACCESSOR_VERSIONED,
# which picks the offset from the running game's version at call time. One
# binary is therefore correct on both sides of 629, which is the "EXTREME
# EFFORT ... truly generic" case SKSE's PluginManager.cpp allows. STRUCT_DEPENDENT
# would be the wrong declaration: it cannot describe a binary spanning 629.
#
# It stays true only while every runtime-variant member is reached through NG's
# runtime accessors -- see "Working on hooks" in CLAUDE.md.
add_commonlibsse_plugin(
	${PROJECT_NAME}
	NAME "BeyondMastery"
	AUTHOR "Joel Hirsbrunner"
	USE_ADDRESS_LIBRARY
	SOURCES ${SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE src ${SIMPLEINI_INCLUDE_DIRS})
target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)
target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
