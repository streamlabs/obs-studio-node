cmake_minimum_required(VERSION 3.24.0 FATAL_ERROR)

if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
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

# MSVC reports __cplusplus as 199711L even under /std:c++20 unless this is set.
# V8 (v8config.h) hard-errors on __cplusplus <= 201703L, so the node headers
# won't compile without it.
if(MSVC)
	add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/Zc:__cplusplus>)
endif()

# The Windows CEF 6533 v4 sandbox archive is Release-only and built with /MT.
# Apply its ABI requirements only to the obs64 link closure.  In particular,
# never change the addon's runtime: it is loaded into Electron and must retain
# the dynamic CRT selected by its host.
function(osn_target_uses_static_msvc_runtime target)
	if(MSVC)
		set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")
		target_compile_definitions(
			${target}
			PRIVATE
				$<$<CONFIG:Debug>:NDEBUG>
				$<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
		)
		target_compile_options(${target} PRIVATE $<$<CONFIG:Debug>:/U_DEBUG>)
	endif()
endfunction()

function(osn_target_uses_dynamic_msvc_runtime target)
	if(MSVC)
		set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	endif()
endfunction()

# A configure-time guard against accidentally widening the CEF CRT setting to
# Electron's native addon again.
function(osn_verify_msvc_runtime target expected_runtime)
	if(MSVC)
		get_property(actual_runtime TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY)
		if(NOT actual_runtime STREQUAL expected_runtime)
			message(FATAL_ERROR "${target} must use ${expected_runtime}; found ${actual_runtime}")
		endif()
	endif()
endfunction()

# CppCheck
IF( NOT CLANG_ANALYZE_CONFIG)
	include(cppcheck)

	cppcheck(EXCLUDE "${PROJECT_SOURCE_DIR}/node_modules")
ENDIF()

# Comfigure macos architecture and min version
iF(APPLE)
	set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=arm64] "12.0")
	set(CMAKE_XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET[arch=x86_64] "12.0")
	if (NOT CMAKE_OSX_ARCHITECTURES)
		set(CMAKE_OSX_ARCHITECTURES "${CMAKE_HOST_SYSTEM_PROCESSOR}")
	endif()
	if ("${CMAKE_OSX_ARCHITECTURES}" STREQUAL "arm64")
		set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
	else()
		set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
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

option(BUILD_TESTING "Build C++ unit tests" ON)
include(CTest)

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
	GIT_REPOSITORY https://github.com/streamlabs/StackWalker
	)

	FetchContent_GetProperties(stackwalker)
	if(NOT stackwalker_POPULATED)
	FetchContent_Populate(stackwalker)
	add_subdirectory(${stackwalker_SOURCE_DIR} ${stackwalker_BINARY_DIR} EXCLUDE_FROM_ALL)
	endif()
	if(TARGET StackWalker)
		osn_target_uses_static_msvc_runtime(StackWalker)
		osn_verify_msvc_runtime(StackWalker "MultiThreaded")
	endif()
endif()

# Nlohmann JSON (modern JSON for C++)
FetchContent_Declare(
  nlohmannjson
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_TAG        55f93686c01528224f448c19128836e7df245f72 # v3.12.0
)

FetchContent_GetProperties(nlohmannjson)
if(NOT nlohmannjson_POPULATED)
  FetchContent_Populate(nlohmannjson)
endif()

if(BUILD_TESTING)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.11.0
  )

  set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
  set(CATCH_INSTALL_EXTRAS ON CACHE BOOL "" FORCE)
  set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(Catch2)
	if(TARGET Catch2)
		osn_target_uses_static_msvc_runtime(Catch2)
		osn_verify_msvc_runtime(Catch2 "MultiThreaded")
	endif()
	if(TARGET Catch2WithMain)
		osn_target_uses_static_msvc_runtime(Catch2WithMain)
		osn_verify_msvc_runtime(Catch2WithMain "MultiThreaded")
	endif()
  list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

FetchContent_Declare(
	libcurl
	URL https://curl.se/download/curl-7.88.1.tar.gz
	DOWNLOAD_EXTRACT_TIMESTAMP true
	OVERRIDE_FIND_PACKAGE
)

FetchContent_GetProperties(libcurl)
if(NOT libcurl_POPULATED)
  set(CURL_USE_OPENSSL OFF CACHE BOOL "")
  set(SSH2CURL_USE_LIB OFF CACHE BOOL "")
  set(CURL_USE_LIBSSH2 OFF CACHE BOOL "")
  set(CMAKE_DISABLE_FIND_PACKAGE_Perl   ON CACHE BOOL "" FORCE)
  set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB   ON CACHE BOOL "" FORCE)
  set(CMAKE_DISABLE_FIND_PACKAGE_LibPSL ON CACHE BOOL "" FORCE)
  set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
  set(USE_LIBPSL  OFF CACHE BOOL "" FORCE)
  set(CURL_ZLIB   OFF CACHE BOOL "" FORCE)

if(APPLE)
  # Enable Secure Transport on Apple platforms
  set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
  set(CMAKE_USE_SCHANNEL OFF CACHE BOOL "" FORCE)
else()
  set(CMAKE_USE_SCHANNEL ON CACHE BOOL "" FORCE)
endif()

  FetchContent_Populate(libcurl)
  add_subdirectory(${libcurl_SOURCE_DIR} ${libcurl_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

if(TARGET libcurl)
  osn_target_uses_static_msvc_runtime(libcurl)
  osn_verify_msvc_runtime(libcurl "MultiThreaded")
endif()

add_subdirectory(lib-streamlabs-ipc)

if(WIN32)
  # lib-streamlabs-ipc is a shared static archive for both binaries. Clone its
  # target-local build settings into a second archive so the Electron addon
  # remains /MD while obs64 gets the /MT CEF-compatible objects.
  get_target_property(OSN_IPC_SOURCES lib-streamlabs-ipc SOURCES)
  get_target_property(OSN_IPC_INCLUDE_DIRECTORIES lib-streamlabs-ipc INCLUDE_DIRECTORIES)
  get_target_property(OSN_IPC_COMPILE_DEFINITIONS lib-streamlabs-ipc COMPILE_DEFINITIONS)
  get_target_property(OSN_IPC_COMPILE_OPTIONS lib-streamlabs-ipc COMPILE_OPTIONS)
  get_target_property(OSN_IPC_LINK_LIBRARIES lib-streamlabs-ipc LINK_LIBRARIES)
  get_target_property(OSN_IPC_SOURCE_DIRECTORY lib-streamlabs-ipc SOURCE_DIR)
  get_property(
    OSN_IPC_DIRECTORY_COMPILE_DEFINITIONS
    DIRECTORY "${OSN_IPC_SOURCE_DIRECTORY}"
    PROPERTY COMPILE_DEFINITIONS
  )

  add_library(lib-streamlabs-ipc-obs64 STATIC ${OSN_IPC_SOURCES})
  if(OSN_IPC_INCLUDE_DIRECTORIES)
    target_include_directories(lib-streamlabs-ipc-obs64 PRIVATE ${OSN_IPC_INCLUDE_DIRECTORIES})
  endif()
  if(OSN_IPC_COMPILE_DEFINITIONS)
    target_compile_definitions(lib-streamlabs-ipc-obs64 PRIVATE ${OSN_IPC_COMPILE_DEFINITIONS})
  endif()
  if(OSN_IPC_DIRECTORY_COMPILE_DEFINITIONS)
    target_compile_definitions(lib-streamlabs-ipc-obs64 PRIVATE ${OSN_IPC_DIRECTORY_COMPILE_DEFINITIONS})
  endif()
  if(OSN_IPC_COMPILE_OPTIONS)
    target_compile_options(lib-streamlabs-ipc-obs64 PRIVATE ${OSN_IPC_COMPILE_OPTIONS})
  endif()
  if(OSN_IPC_LINK_LIBRARIES)
    target_link_libraries(lib-streamlabs-ipc-obs64 PRIVATE ${OSN_IPC_LINK_LIBRARIES})
  endif()

  osn_target_uses_dynamic_msvc_runtime(lib-streamlabs-ipc)
  osn_target_uses_static_msvc_runtime(lib-streamlabs-ipc-obs64)
  osn_verify_msvc_runtime(lib-streamlabs-ipc "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  osn_verify_msvc_runtime(lib-streamlabs-ipc-obs64 "MultiThreaded")
endif()

add_subdirectory(obs-studio-client)
add_subdirectory(obs-studio-server)

include(CPack)

if(APPLE)
# Copies ALL libcurl.dylib files for obs64 after the install process completes.
install(CODE "
    file(GLOB_RECURSE DYLIB_FILES \"${libcurl_BINARY_DIR}/lib/*.dylib\")
    foreach(file \${DYLIB_FILES})
        file(COPY \${file} DESTINATION \"${CMAKE_INSTALL_PREFIX}/bin\")
    endforeach()
")
endif()
