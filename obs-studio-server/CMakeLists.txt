PROJECT(obs-studio-server VERSION ${obs-studio-node_VERSION})
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detect Architecture (Bitness)
IF( NOT CLANG_ANALYZE_CONFIG)
    math(EXPR BITS "8*${CMAKE_SIZEOF_VOID_P}")
ENDIF()

if(WIN32)
    set(PROJECT_PRODUCT_NAME "OBS Studio for Node.JS (Server)")
    set(PROJECT_COPYRIGHT "Streamlabs © 2017 - 2018")

    configure_file(
        ${obs-studio-node_SOURCE_DIR}/cmake/version.rc.in
        ${PROJECT_BINARY_DIR}/version.rc
        @ONLY
    )
    set(LIBOBS_PLATFORM "windows64")
endif()

if(APPLE)
    set(LIBOBS_PLATFORM "osx")
endif()

############################
# Setup crashpad binaries
############################

if(WIN32)
    download_project(
        PROJ crashpad
        URL https://obsstudionodes3.streamlabs.com/crashpad/crashpad-release-win-bd479a12-dec-2022-x86_64.tar.gz
        UPDATE_DISCONNECTED 1
    )
    set(crashpad_BUILD_BYPRODUCTS
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}base${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}util${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}common${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}client${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/bin/crashpad_database_util${CMAKE_EXECUTABLE_SUFFIX}"
        "<SOURCE_DIR>/bin/crashpad_handler${CMAKE_EXECUTABLE_SUFFIX}"
        "<SOURCE_DIR>/bin/crashpad_http_upload${CMAKE_EXECUTABLE_SUFFIX}"
    )
    set(crashpad_TARGET_LINK_LIBRARIES
        crashpad_base
        crashpad_common
        crashpad_client
        crashpad_util
    )
elseif(APPLE)
    download_project(
        PROJ crashpad
        URL https://obsstudionodes3.streamlabs.com/crashpad/crashpad-osx-3215ed90-b230201-${CMAKE_OSX_ARCHITECTURES}.zip
        UPDATE_DISCONNECTED 1
    )
    set(crashpad_BUILD_BYPRODUCTS
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}base${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}mig_output${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}util${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}common${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/lib/${CMAKE_STATIC_LIBRARY_PREFIX}client${CMAKE_STATIC_LIBRARY_SUFFIX}"
        "<SOURCE_DIR>/bin/crashpad_database_util${CMAKE_EXECUTABLE_SUFFIX}"
        "<SOURCE_DIR>/bin/crashpad_handler${CMAKE_EXECUTABLE_SUFFIX}"
        "<SOURCE_DIR>/bin/crashpad_http_upload${CMAKE_EXECUTABLE_SUFFIX}"
    )
    set(crashpad_TARGET_LINK_LIBRARIES
        crashpad_base
        crashpad_common
        crashpad_client
        crashpad_mig_output
        crashpad_util
    )
endif()

ExternalProject_Add(
    crashpad_ep
    DOWNLOAD_COMMAND ""
    SOURCE_DIR "${crashpad_SOURCE_DIR}"
    INSTALL_COMMAND ""
    BUILD_COMMAND ""
    CONFIGURE_COMMAND ""
    BUILD_BYPRODUCTS
        ${crashpad_BUILD_BYPRODUCTS}
)

# Our crashpad artifacts assume a particular format
# <dir>\bin contains external processes
# <dir>\lib contains static libraries we need to link against
# <dir>\include contains the primary include path
# <dir>\include\third_party\mini_chromium contains chromium include files

if (APPLE)
    add_library(crashpad_mig_output STATIC IMPORTED)
endif ()
add_library(crashpad_util STATIC IMPORTED)
add_library(crashpad_base STATIC IMPORTED)
add_library(crashpad_common STATIC IMPORTED)
add_library(crashpad_client STATIC IMPORTED)
add_executable(crashpad_handler IMPORTED)
add_executable(crashpad_database_util IMPORTED)
add_executable(crashpad_http_upload IMPORTED)

# From this, we get the following targets:
#   crashpad_base
#   crashpad_mig_output (macOS only)
#   crashpad_util
#   crashpad_common
#   crashpad_client

ExternalProject_Get_Property(crashpad_ep source_dir)

set_property(TARGET crashpad_base PROPERTY IMPORTED_LOCATION
    "${source_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}base${CMAKE_STATIC_LIBRARY_SUFFIX}")

if (APPLE)
    set_property(TARGET crashpad_mig_output PROPERTY IMPORTED_LOCATION
        "${source_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}mig_output${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif ()

set_property(TARGET crashpad_util PROPERTY IMPORTED_LOCATION
    "${source_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}util${CMAKE_STATIC_LIBRARY_SUFFIX}")

set_property(TARGET crashpad_common PROPERTY IMPORTED_LOCATION
    "${source_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}common${CMAKE_STATIC_LIBRARY_SUFFIX}")

set_property(TARGET crashpad_client PROPERTY IMPORTED_LOCATION
    "${source_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}client${CMAKE_STATIC_LIBRARY_SUFFIX}")

set_property(TARGET crashpad_handler PROPERTY IMPORTED_LOCATION
    "${source_dir}/bin/crashpad_handler${CMAKE_EXECUTABLE_SUFFIX}")

set_property(TARGET crashpad_database_util PROPERTY IMPORTED_LOCATION
    "${source_dir}/bin/crashpad_database_util${CMAKE_EXECUTABLE_SUFFIX}")

set_property(TARGET crashpad_http_upload PROPERTY IMPORTED_LOCATION
    "${source_dir}/bin/crashpad_http_upload${CMAKE_EXECUTABLE_SUFFIX}")

target_include_directories(
    crashpad_base
    INTERFACE "${source_dir}/include/third_party/mini_chromium/mini_chromium"
)

target_include_directories(
    crashpad_client
    INTERFACE "${source_dir}/include"
)

add_library(crashpad INTERFACE)

target_link_libraries(
    crashpad
    INTERFACE
        ${crashpad_TARGET_LINK_LIBRARIES}
)

target_link_libraries(crashpad_util INTERFACE crashpad_client)

add_dependencies(crashpad_base crashpad_ep)
add_dependencies(crashpad_client crashpad_common)
add_dependencies(crashpad_client crashpad_base)
add_dependencies(crashpad_util crashpad_client)
if (APPLE)
    add_dependencies(crashpad_util crashpad_mig_output)
endif ()

if (APPLE)
    find_library(COREFOUNDATION CoreFoundation)
    find_library(COCOA Cocoa)
    find_library(IOSURF IOSurface)
    find_library(GLKIT GLKit)
    find_library(AVFOUNDATION AVFoundation)
    find_library(IOKit IOKit)
    find_library(SECURITY_LIBRARY Security)
    find_library(BSM_LIBRARY bsm)
endif ()

# Getting LIBOBS_VERSION from azure script
file(STRINGS ../.github/workflows/main.yml VERSION_LINE REGEX "LibOBSVersion")
string(REGEX MATCH "[^: ]*$" CURRENT_VERSION ${VERSION_LINE})

set(
    LIBOBS_VERSION
    ${CURRENT_VERSION}
    CACHE STRING "Version of libobs being used"
)

set(
    LIBOBS_BUILD_TYPE
    "release"
    CACHE STRING "Build type of libobs artifact"
)

if(WIN32)
	set (OBS_URL_EXTENSION "")
elseif(APPLE)
	set (OBS_URL_EXTENSION "-${CMAKE_OSX_ARCHITECTURES}")
endif()

string(
	CONCAT OSN_DEFAULT_LIBOBS_URL
	"https://obsstudios3.streamlabs.com/"
	"libobs-${LIBOBS_PLATFORM}-${LIBOBS_BUILD_TYPE}-${LIBOBS_VERSION}${OBS_URL_EXTENSION}.7z"
)

set(
    OSN_LIBOBS_URL
    ${OSN_DEFAULT_LIBOBS_URL}
    CACHE STRING "Location of libobs archive to base on"
)

download_project(
    PROJ libobs
    URL ${OSN_LIBOBS_URL}
    UPDATE_DISCONNECTED 1
)

if (WIN32)
	include("${libobs_SOURCE_DIR}/cmake/LibObsConfig.cmake")
elseif(APPLE)
	include("${libobs_SOURCE_DIR}/Frameworks/libobs.Framework/Resources/cmake/LibobsConfig.cmake")
endif()

if (WIN32)
	set(PROJECT_LIBRARIES
		lib-streamlabs-ipc
		OBS::libobs
		dwmapi.lib
        dxgi.lib
	)
	set(PROJECT_INCLUDE_PATHS
		"${CMAKE_SOURCE_DIR}/source"
		"${PROJECT_SOURCE_DIR}/source"
		"${lib-streamlabs-ipc_SOURCE_DIR}/include"
		"${libobs_SOURCE_DIR}/include"
		"${stackwalker_SOURCE_DIR}/Main/StackWalker"
		"${nlohmannjson_SOURCE_DIR}/single_include"
		"${source_dir}/include/third_party/mini_chromium/mini_chromium"
		"${source_dir}/include"
	)
else ()
	set(PROJECT_LIBRARIES
		lib-streamlabs-ipc
		OBS::libobs
	)
	set(PROJECT_INCLUDE_PATHS
		"${CMAKE_SOURCE_DIR}/source"
		"${PROJECT_SOURCE_DIR}/source"
		"${lib-streamlabs-ipc_SOURCE_DIR}/include"
		"${libobs_SOURCE_DIR}/Frameworks/libobs.framework/Headers"
		"${nlohmannjson_SOURCE_DIR}/single_include"
		"${source_dir}/include/third_party/mini_chromium/mini_chromium"
		"${source_dir}/include"
	)
endif ()

set(PROJECT_DATA "${PROJECT_SOURCE_DIR}/resources")

SET(osn-server_SOURCES
    "${CMAKE_SOURCE_DIR}/source/osn-error.hpp"
    "${CMAKE_SOURCE_DIR}/source/obs-property.hpp"
    "${CMAKE_SOURCE_DIR}/source/obs-property.cpp"

    ###### obs-studio-node ######
    "${PROJECT_SOURCE_DIR}/source/main.cpp"
    "${PROJECT_SOURCE_DIR}/source/shared.cpp"
    "${PROJECT_SOURCE_DIR}/source/shared.hpp"
    "${PROJECT_SOURCE_DIR}/source/utility.cpp"
    "${PROJECT_SOURCE_DIR}/source/utility.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-nodeobs.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-nodeobs.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-calldata.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-calldata.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-common.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-common.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-display.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-display.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-fader.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-fader.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-filter.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-filter.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-global.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-global.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-video-encoder.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-video-encoder.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-input.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-input.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-module.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-module.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-configuration.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-configuration.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-data-model.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-output.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-output.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-system-info.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio-bitrate.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio-bitrate.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-output.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-output.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-properties.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-properties.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-scene.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-scene.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-sceneitem.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-sceneitem.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-service.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-service.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-source.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-source.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-transition.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-transition.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-video.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-video.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-volmeter.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-volmeter.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-streaming.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-streaming.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-simple-streaming.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-simple-streaming.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-advanced-streaming.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-advanced-streaming.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-enhanced-broadcasting-display-stats-tracker.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-enhanced-broadcasting-display-stats-tracker.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-enhanced-broadcasting-advanced-streaming.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-enhanced-broadcasting-advanced-streaming.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-enhanced-broadcasting-simple-streaming.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-enhanced-broadcasting-simple-streaming.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-delay.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-delay.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-reconnect.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-reconnect.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-network.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-network.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio-track.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio-track.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-recording.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-recording.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-simple-recording.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-simple-recording.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio-encoder.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-audio-encoder.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-advanced-recording.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-advanced-recording.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-replay-buffer.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-replay-buffer.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-simple-replay-buffer.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-simple-replay-buffer.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-file-output.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-file-output.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-advanced-replay-buffer.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-advanced-replay-buffer.hpp"
    "${PROJECT_SOURCE_DIR}/source/osn-encoders.cpp"
    "${PROJECT_SOURCE_DIR}/source/osn-encoders.hpp"

    ###### utlity graphics ######
    "${PROJECT_SOURCE_DIR}/source/gs-limits.h"
    "${PROJECT_SOURCE_DIR}/source/gs-vertex.h"
    "${PROJECT_SOURCE_DIR}/source/gs-vertex.cpp"
    "${PROJECT_SOURCE_DIR}/source/gs-vertexbuffer.h"
    "${PROJECT_SOURCE_DIR}/source/gs-vertexbuffer.cpp"

    ###### node-obs ######
    "${PROJECT_SOURCE_DIR}/source/nodeobs_api.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_api.h"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_audio_encoders.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_audio_encoders.h"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_autoconfig.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_autoconfig.h"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_configManager.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_configManager.hpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_display.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_display.h"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_content.h"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_common.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_service.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_service.h"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_settings.cpp"
    "${PROJECT_SOURCE_DIR}/source/nodeobs_settings.h"
    "${PROJECT_SOURCE_DIR}/source/util-memory.cpp"
    "${PROJECT_SOURCE_DIR}/source/util-memory.h"

    ###### crash-manager ######
    "${PROJECT_SOURCE_DIR}/source/util-crashmanager.cpp"
    "${PROJECT_SOURCE_DIR}/source/util-crashmanager.h"
    "${PROJECT_SOURCE_DIR}/source/util-metricsprovider.cpp"
    "${PROJECT_SOURCE_DIR}/source/util-metricsprovider.h"

    ###### miscellaneous utils ######
    "${PROJECT_SOURCE_DIR}/source/util-censor.cpp"
    "${PROJECT_SOURCE_DIR}/source/util-censor.h"

    ###### callback-manager ######
    "${PROJECT_SOURCE_DIR}/source/callback-manager.cpp"
    "${PROJECT_SOURCE_DIR}/source/callback-manager.h"

    ###### memory-manager ######
    "${PROJECT_SOURCE_DIR}/source/memory-manager.cpp"
    "${PROJECT_SOURCE_DIR}/source/memory-manager.h"
)

if (APPLE)
    SET(osn-server-osx_SOURCES
        "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-system-info-osx.mm"

        ###### osx-util ######
        "${PROJECT_SOURCE_DIR}/source/util-osx.hpp"
        "${PROJECT_SOURCE_DIR}/source/util-osx.cpp"
        "${PROJECT_SOURCE_DIR}/source/util-osx-int.h"
        "${PROJECT_SOURCE_DIR}/source/util-osx-impl.h"
        "${PROJECT_SOURCE_DIR}/source/util-osx-impl.mm"
    )
    LIST(
        APPEND
        osn-server_SOURCES
        ${osn-server-osx_SOURCES}
    )
elseif(WIN32)
    LIST(
        APPEND
        osn-server_SOURCES
        "${PROJECT_SOURCE_DIR}/source/osn-multitrack-video-system-info.cpp"
    )
endif ()

set(OSN_SERVER_CORE_SOURCES ${osn-server_SOURCES})
list(REMOVE_ITEM OSN_SERVER_CORE_SOURCES "${PROJECT_SOURCE_DIR}/source/main.cpp")

add_library(
    obs-studio-server-lib STATIC
    ${OSN_SERVER_CORE_SOURCES}
)

IF(WIN32)
    target_compile_definitions(
        obs-studio-server-lib
        PUBLIC
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
    )
ENDIF()

target_include_directories(obs-studio-server-lib PUBLIC ${PROJECT_INCLUDE_PATHS})

if(WIN32)
    target_link_libraries(obs-studio-server-lib PUBLIC ${PROJECT_LIBRARIES} optimized crashpad strmiids StackWalker)
else()
    target_link_libraries(obs-studio-server-lib PUBLIC ${PROJECT_LIBRARIES} crashpad ${COREFOUNDATION} ${COCOA} ${IOSURF} ${GLKIT} ${AVFOUNDATION} ${IOKit} ${SECURITY_LIBRARY} ${BSM_LIBRARY})
endif()

target_link_libraries(obs-studio-server-lib PUBLIC libcurl)

add_executable(
    ${PROJECT_NAME}
    "${PROJECT_SOURCE_DIR}/source/main.cpp"
)

if(WIN32)
    target_sources(${PROJECT_NAME} PUBLIC "${PROJECT_BINARY_DIR}/version.rc")
endif()

target_link_libraries(${PROJECT_NAME} obs-studio-server-lib)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS)
endif()

#Define the OSN_VERSION
add_compile_definitions(OSN_VERSION=\"$ENV{tagartifact}\")
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "obs${BITS}")

if(WIN32)
    set_target_properties(
        ${PROJECT_NAME}
        PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION}
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE)

    target_link_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:DEBUG>:/DEBUG:FULL>")
    target_link_options(${PROJECT_NAME} PUBLIC "$<$<CONFIG:RELWITHDEBINFO>:/DEBUG:FULL>")
else()
    set_target_properties(
        ${PROJECT_NAME}
        PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION}
    )
    target_compile_definitions(${PROJECT_NAME} PRIVATE)
    set_property(
        TARGET ${PROJECT_NAME}
        APPEND
        PROPERTY INSTALL_RPATH
        "@loader_path/" "@executable_path/"
    )
ENDIF()

IF( NOT CLANG_ANALYZE_CONFIG)
    cppcheck_add_project(${PROJECT_NAME})
ENDIF()

# Compare current linked libs with prev
if(WIN32)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_SOURCE_DIR}/dependency_checker/check_dependencies.cmd $<TARGET_FILE:obs-studio-server> ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} $<CONFIG>
	)
endif()

set(PROGRAM_PERMISSIONS_DEFAULT
    OWNER_WRITE OWNER_READ OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

if (WIN32)
	install(TARGETS obs-studio-server RUNTIME DESTINATION "./" PERMISSIONS ${PROGRAM_PERMISSIONS_DEFAULT} COMPONENT Runtime)
ELSE()
	install(TARGETS obs-studio-server RUNTIME DESTINATION "./bin" PERMISSIONS ${PROGRAM_PERMISSIONS_DEFAULT} COMPONENT Runtime)
	set(_COMMAND
		"${CMAKE_INSTALL_NAME_TOOL} \\
		-change @rpath/libobs.framework/Versions/A/libobs @executable_path/../Frameworks/libobs.framework/Versions/A/libobs \\
		\\\"\${CMAKE_INSTALL_PREFIX}/bin/obs64\\\""
	)
	install(CODE "execute_process(COMMAND /bin/sh -c \"${_COMMAND}\")")
ENDIF()

IF(WIN32 AND NOT CLANG_ANALYZE_CONFIG)
    install(FILES $<TARGET_PDB_FILE:obs-studio-server> DESTINATION "./" OPTIONAL)
ENDIF()
install(DIRECTORY ${PROJECT_DATA} DESTINATION "./" OPTIONAL USE_SOURCE_PERMISSIONS)
install(DIRECTORY ${crashpad_SOURCE_DIR}/bin/ DESTINATION "./" USE_SOURCE_PERMISSIONS)

IF(WIN32)
	install(DIRECTORY "${libobs_SOURCE_DIR}/data/" DESTINATION "./data" USE_SOURCE_PERMISSIONS)
else ()
	install(DIRECTORY "${libobs_SOURCE_DIR}/Frameworks/libobs.framework/Versions/A/Resources/" DESTINATION "./data/libobs" FILES_MATCHING PATTERN "*.effect" PATTERN "cmake" EXCLUDE)
endif()

if (WIN32)
    install(
        DIRECTORY "${libobs_SOURCE_DIR}/bin/64bit/"
        DESTINATION "./" PATTERN "*.lib" EXCLUDE
    )
else ()
	install(
		DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks"
		DESTINATION "./" USE_SOURCE_PERMISSIONS PATTERN "Qt*" EXCLUDE
		PATTERN "Sparkle*" EXCLUDE
	)
	install(
		DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/MacOS/"
		DESTINATION "./bin" USE_SOURCE_PERMISSIONS PATTERN "OBS" EXCLUDE
	)
    # Install library files for unit tests & xcode
	install(
		DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks"
		DESTINATION "${libobs_SOURCE_DIR}" USE_SOURCE_PERMISSIONS
        FILES_MATCHING PATTERN "*.dylib"
	)
endif ()

if (WIN32)
install(
    DIRECTORY "${libobs_SOURCE_DIR}/obs-plugins/"
    DESTINATION "./obs-plugins"
)
else ()
install(
	DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/PlugIns"
	DESTINATION "./" USE_SOURCE_PERMISSIONS PATTERN "frontend*" EXCLUDE
)
# Disable temporarily
#install(
#    DIRECTORY "${libobs_SOURCE_DIR}/data/obs-plugins/slobs-virtual-cam/"
#    DESTINATION "./data/obs-plugins/slobs-virtual-cam" USE_SOURCE_PERMISSIONS
#)
endif()

# This is required for macOS tests. Otherwise CEF will not be able to start child processes.
# The root folder for Contents/Frameworks should have the .app extenstion!
if (APPLE)
    install(
        DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks/Chromium Embedded Framework.framework"
        DESTINATION "../../Contents/Frameworks" USE_SOURCE_PERMISSIONS
    )
    install(
        DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks/obs64 Helper (GPU).app"
        DESTINATION "../../Contents/Frameworks" USE_SOURCE_PERMISSIONS
    )
    install(
        DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks/obs64 Helper (Plugin).app"
        DESTINATION "../../Contents/Frameworks" USE_SOURCE_PERMISSIONS
    )
    install(
        DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks/obs64 Helper (Renderer).app"
        DESTINATION "../../Contents/Frameworks" USE_SOURCE_PERMISSIONS
    )
    install(
        DIRECTORY "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks/obs64 Helper.app"
        DESTINATION "../../Contents/Frameworks" USE_SOURCE_PERMISSIONS
    )
endif()

if(BUILD_TESTING)
    include(Catch)

    add_executable(
        obs_studio_server_unit_tests
        "tests/test-osn-source.cpp"
        "tests/obs-setup.cpp"
        "tests/obs-setup.hpp"
    )

    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" OSN_SOURCE_DIR_PATH)
    file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}" OSN_TEST_WD_PATH)

    target_compile_definitions(
        obs_studio_server_unit_tests
        PRIVATE
            OSN_SOURCE_DIR="${OSN_SOURCE_DIR_PATH}"
            OSN_TEST_WD="${OSN_TEST_WD_PATH}"
    )

    target_link_libraries(
        obs_studio_server_unit_tests
        PRIVATE
            Catch2::Catch2WithMain
            obs-studio-server-lib
    )

    if(APPLE)
        add_custom_command(
            TARGET obs_studio_server_unit_tests
            POST_BUILD
            COMMAND /usr/bin/codesign --force --sign - "$<TARGET_FILE:obs_studio_server_unit_tests>"
            COMMENT "Ad-hoc signing obs_studio_server_unit_tests before Catch2 test discovery"
            VERBATIM
        )
    endif()

    set(_osn_server_test_dl_paths
        "$<TARGET_FILE_DIR:Catch2::Catch2>"
        "$<TARGET_FILE_DIR:Catch2::Catch2WithMain>"
        "$<TARGET_FILE_DIR:libcurl>"
    )

    if(APPLE)
        list(APPEND _osn_server_test_dl_paths "${libobs_SOURCE_DIR}/OBS.app/Contents/Frameworks")
    elseif(WIN32)
        list(APPEND _osn_server_test_dl_paths "${libobs_SOURCE_DIR}/bin/64bit")
    endif()

    catch_discover_tests(
        obs_studio_server_unit_tests
        TEST_PREFIX "obs_studio_server_unit_tests::"
        DL_PATHS
            ${_osn_server_test_dl_paths}
    )
endif()
