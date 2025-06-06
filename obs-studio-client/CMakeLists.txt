PROJECT(obs_studio_client VERSION ${obs-studio-node_VERSION})
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

SET(NODEJS_URL "https://artifacts.electronjs.org/headers/dist" CACHE STRING "Node.JS URL")
SET(NODEJS_NAME "iojs" CACHE STRING "Node.JS Name")
SET(NODEJS_VERSION "v29.4.3" CACHE STRING "Node.JS Version")

if(WIN32)
    # Resource VersionInfo
    set(PROJECT_PRODUCT_NAME "OBS Studio for Node.JS (Server)")
    set(PROJECT_COPYRIGHT "Streamlabs © 2017 - 2018")

    configure_file(
        ${obs-studio-node_SOURCE_DIR}/cmake/version.rc.in
        ${PROJECT_BINARY_DIR}/version.rc
        @ONLY
    )
endif()

include(NodeJS)

set(STREAMLABS_BUILD ON CACHE BOOL "Whether to build for streamlabs-obs")

nodejs_init()

if (APPLE)
    find_library(COREFOUNDATION CoreFoundation)
    find_library(COCOA Cocoa)
    find_library(AVFOUNDATION AVFoundation)
    find_library(SECURITY_FRAMEWORK Security)
    find_library(FOUNDATION Foundation)

    include_directories(
        ${COREFOUNDATION}
        ${COCOA}
        ${AVFOUNDATION}
        ${Security}
        ${Foundation}
    )
endif ()

SET(osn-client_SOURCES
    "${CMAKE_SOURCE_DIR}/source/osn-error.hpp"
    "${CMAKE_SOURCE_DIR}/source/obs-property.hpp"
    "${CMAKE_SOURCE_DIR}/source/obs-property.cpp"

    "source/shared.cpp"
    "source/shared.hpp"
    "source/utility.cpp"
    "source/utility.hpp"
    "source/utility-v8.cpp"
    "source/utility-v8.hpp"
    "source/controller.cpp"
    "source/controller.hpp"
    "source/fader.cpp"
    "source/fader.hpp"
    "source/global.cpp"
    "source/global.hpp"
    "source/input.cpp"
    "source/input.hpp"
    "source/isource.cpp"
    "source/isource.hpp"
    "source/properties.cpp"
    "source/properties.hpp"
    "source/filter.cpp"
    "source/filter.hpp"
    "source/transition.cpp"
    "source/transition.hpp"
    "source/scene.cpp"
    "source/scene.hpp"
    "source/sceneitem.cpp"
    "source/sceneitem.hpp"
    "source/nodeobs_api.cpp"
    "source/nodeobs_api.hpp"
    "source/nodeobs_service.cpp"
    "source/nodeobs_service.hpp"
    "source/nodeobs_display.cpp"
    "source/nodeobs_display.hpp"
    "source/nodeobs_settings.cpp"
    "source/nodeobs_settings.hpp"
    "source/nodeobs_autoconfig.cpp"
    "source/nodeobs_autoconfig.hpp"
    "source/main.cpp"
    "source/volmeter.cpp"
    "source/volmeter.hpp"
    "source/video.cpp"
    "source/video.hpp"
    "source/module.cpp"
    "source/module.hpp"
    "source/cache-manager.hpp"
    "source/cache-manager.cpp"
    "source/video-encoder.hpp"
    "source/video-encoder.cpp"
    "source/service.hpp"
    "source/service.cpp"
    "source/audio.hpp"
    "source/audio.cpp"
    "source/streaming.hpp"
    "source/streaming.cpp"
    "source/simple-streaming.hpp"
    "source/simple-streaming.cpp"
    "source/advanced-streaming.hpp"
    "source/advanced-streaming.cpp"
    "source/worker-signals.hpp"
    "source/delay.hpp"
    "source/delay.cpp"
    "source/reconnect.hpp"
    "source/reconnect.cpp"
    "source/network.hpp"
    "source/network.cpp"
    "source/audio-track.hpp"
    "source/audio-track.cpp"
    "source/recording.hpp"
    "source/recording.cpp"
    "source/simple-recording.hpp"
    "source/simple-recording.cpp"
    "source/audio-encoder.hpp"
    "source/audio-encoder.cpp"
    "source/advanced-recording.hpp"
    "source/advanced-recording.cpp"
    "source/replay-buffer.hpp"
    "source/replay-buffer.cpp"
    "source/simple-replay-buffer.hpp"
    "source/simple-replay-buffer.cpp"
    "source/file-output.hpp"
    "source/file-output.cpp"
    "source/advanced-replay-buffer.hpp"
    "source/advanced-replay-buffer.cpp"

    ###### callback-manager ######
    "source/callback-manager.cpp"
    "source/callback-manager.hpp"
)

if (APPLE)
    SET(osn-client-osx_SOURCES
        ###### osx-util ######
        "${PROJECT_SOURCE_DIR}/source/util-osx.hpp"
        "${PROJECT_SOURCE_DIR}/source/util-osx.cpp"
        "${PROJECT_SOURCE_DIR}/source/util-osx-int.h"
        "${PROJECT_SOURCE_DIR}/source/util-osx-impl.h"
        "${PROJECT_SOURCE_DIR}/source/util-osx-impl.mm"
    )
    LIST(
        APPEND
        osn-client_SOURCES
        ${osn-client-osx_SOURCES}
    )
endif ()

add_nodejs_module(
    obs_studio_client
    ${osn-client_SOURCES}
)

set(PROJECT_INCLUDE_PATHS
    "${nlohmannjson_SOURCE_DIR}/single_include"
)

if(WIN32)
    target_sources(obs_studio_client PUBLIC ${PROJECT_BINARY_DIR}/version.rc)
    target_link_libraries(obs_studio_client lib-streamlabs-ipc)
else ()
    target_link_libraries(obs_studio_client lib-streamlabs-ipc ${COREFOUNDATION} ${COCOA} ${AVFOUNDATION} ${Security} ${Foundation})
endif()

target_include_directories(
    obs_studio_client
    PUBLIC
        "${CMAKE_SOURCE_DIR}/source/"
        "${CMAKE_SOURCE_DIR}/lib-streamlabs-ipc/include"
        ${PROJECT_INCLUDE_PATHS}
)

# Include N-API wrappers
execute_process(COMMAND node -p "require('node-addon-api').include"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE NODE_ADDON_API_DIR
        )
string(REPLACE "\n" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
string(REPLACE "\"" "" NODE_ADDON_API_DIR ${NODE_ADDON_API_DIR})
target_include_directories(${PROJECT_NAME} PRIVATE ${NODE_ADDON_API_DIR})
target_link_libraries(${PROJECT_NAME} ${LIBOBS_LIBRARIES})

# Define NAPI_VERSION
add_definitions(-DNAPI_VERSION=7)

#Define the OSN_VERSION
add_compile_definitions(OSN_VERSION=\"$ENV{tagartifact}\") 
set_target_properties(
    obs_studio_client
    PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION}
    PREFIX "" SUFFIX ".node"
)

target_compile_definitions(obs_studio_client PRIVATE BUILDING_NODE_EXTENSION)

if(WIN32)
    target_compile_definitions(
        obs_studio_client
        PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
            UNICODE
            _UNICODE
    )
endif()

# Compare current linked libs with prev
if(WIN32)
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_SOURCE_DIR}/dependency_checker/check_dependencies.cmd $<TARGET_FILE:obs_studio_client> ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR} $<CONFIG>
	)
endif()

IF( NOT CLANG_ANALYZE_CONFIG)
    cppcheck_add_project(${PROJECT_NAME})
ENDIF()

IF(WIN32 AND NOT CLANG_ANALYZE_CONFIG)
    install(FILES $<TARGET_PDB_FILE:obs_studio_client> DESTINATION ./ OPTIONAL)
ENDIF()
install(FILES "${CMAKE_SOURCE_DIR}/package.json" DESTINATION "./")

install(
    DIRECTORY "${PROJECT_SOURCE_DIR}/../js/"
    DESTINATION "./"
    PATTERN "*.json" EXCLUDE
)

install(
    TARGETS obs_studio_client
    RUNTIME DESTINATION "./" COMPONENT Runtime
    LIBRARY DESTINATION "./" COMPONENT Runtime
    ARCHIVE DESTINATION "./" COMPONENT Runtime    
)
