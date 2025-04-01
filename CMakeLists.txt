cmake_minimum_required(VERSION 3.24.0 FATAL_ERROR)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

IF(CLANG_ANALYZE_CONFIG)
	set(CMAKE_C_COMPILER_WORKS 1)
	set(CMAKE_CXX_COMPILER_WORKS 1)

	SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Xclang -fexceptions -Xclang -fcxx-exceptions")
ENDIF()

set(VERSION_FULL 0.3.21)
set(VERSION_BUILD 0)

project(obs-studio-node VERSION ${VERSION_FULL}.${VERSION_BUILD})
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

# CppCheck
IF( NOT CLANG_ANALYZE_CONFIG)
	include(cppcheck)

	cppcheck(EXCLUDE "${PROJECT_SOURCE_DIR}/node_modules")
ENDIF()

# Comfigure macos architecture and min version
iF(APPLE)
	set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=arm64] "11.0")
	set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=x86_64] "10.15")
	if (NOT CMAKE_OSX_ARCHITECTURES)
		set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
	endif()
	if ("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "arm64")
		set(CMAKE_OSX_DEPOLYMENT_TARGET "11.0")
	else()
		set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15")
	endif()
	# See CMake issue # 21854
	# https://gitlab.kitware.com/cmake/cmake/-/issues/21854
	# This is necessary because CMAKE_INSTALL_NAME_TOOL is used for obs64 (i.e. obs-studio-server).
	set(CMAKE_XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "-o linker-signed")
ENDIF()

if(WIN32)
	# Resource VersionInfo
	set(PROJECT_PRODUCT_NAME "OBS Studio for Node.JS")
	set(PROJECT_COMPANY_NAME "Streamlabs")
	set(PROJECT_COPYRIGHT "Streamlabs © 2017 - 2018")
	set(PROJECT_LEGAL_TRADEMARKS_1 "")
	set(PROJECT_LEGAL_TRADEMARKS_2 "")

	configure_file(
		${PROJECT_SOURCE_DIR}/cmake/version.rc.in
		${PROJECT_BINARY_DIR}/version.rc
		@ONLY
	)

	# ToDo: Figure out a better way to tell the test setup where to find our binaries.
	configure_file(
		${PROJECT_SOURCE_DIR}/cmake/obs.js.in
		${PROJECT_SOURCE_DIR}/tests/helpers/obs.js
		@ONLY
	)
endif()

configure_file(
	${PROJECT_SOURCE_DIR}/cmake/index.ts.in
	${PROJECT_SOURCE_DIR}/tests/osn-tests/osn/index.ts
	@ONLY
)

############################
# Setup global dependencies
############################

include(ExternalProject)
include(DownloadProject)
include(FetchContent)

if(MSVC)
	add_definitions(/MP)
	add_link_options(/debug:fastlink)
	#ignore security warnings and deprecation warnings
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
	add_definitions(-D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
endif()

if (WIN32)
	# Override a test flag
	set(StackWalker_DISABLE_TESTS true CACHE BOOL "" FORCE)

	# StackWalker (Callstack rewind
	FetchContent_Declare(
	stackwalker
	GIT_REPOSITORY https://github.com/stream-labs/StackWalker
	)

	FetchContent_GetProperties(stackwalker)
	if(NOT stackwalker_POPULATED)
	FetchContent_Populate(stackwalker)
	add_subdirectory(${stackwalker_SOURCE_DIR} ${stackwalker_BINARY_DIR} EXCLUDE_FROM_ALL)
	endif()
endif()

# Nlohmann JSON (modern JSON for C++)
FetchContent_Declare(
  nlohmannjson
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        aa0e847e5b57a00696bdcb6a834b927231b81613 # v3.10.3
)

FetchContent_GetProperties(nlohmannjson)
if(NOT nlohmannjson_POPULATED)
  FetchContent_Populate(nlohmannjson)
endif()

FetchContent_Declare(
	libcurl
	URL https://curl.se/download/curl-7.88.1.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP true
	OVERRIDE_FIND_PACKAGE
)

FetchContent_GetProperties(libcurl)
if(NOT libcurl_POPULATED)
  FetchContent_Populate(libcurl)
  add_subdirectory(${libcurl_SOURCE_DIR} ${libcurl_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

add_subdirectory(lib-streamlabs-ipc)
add_subdirectory(obs-studio-client)
add_subdirectory(obs-studio-server)

include(CPack)