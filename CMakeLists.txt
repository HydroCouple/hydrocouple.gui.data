# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Caleb Buahin. All rights reserved.
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMData VERSION 0.1.0 LANGUAGES CXX)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
# Reuse the exact host SDK during a GUI source build. An independently configured
# plugin still consumes the installed package, including its compatibility checks.
if(TARGET OpenSWMMVis::Sdk)
    if(NOT COMMAND openswmmvis_add_extension OR
       NOT EXISTS "${OpenSWMMVisSdk_MANIFEST}")
        message(FATAL_ERROR "The parent SDK must provide openswmmvis_add_extension and OpenSWMMVisSdk_MANIFEST")
    endif()
    file(READ "${OpenSWMMVisSdk_MANIFEST}" _openswmm_data_sdk_manifest)
    string(JSON _openswmm_data_sdk_api GET "${_openswmm_data_sdk_manifest}" sdkApiVersion)
    if(_openswmm_data_sdk_api VERSION_LESS "1.12" OR
       NOT _openswmm_data_sdk_api VERSION_LESS "2.0")
        message(FATAL_ERROR "OpenSWMM Data requires GUI SDK 1.12 or a compatible later 1.x API")
    endif()
else()
    find_package(OpenSWMMVisSdk 1.12 CONFIG REQUIRED)
endif()
find_package(GDAL REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Svg)
openswmmvis_add_extension(openswmm_data
    ID openswmm.gui.data CAPTION "Data sources" VERSION 0.1.0
    VENDOR "OpenSWMM" LICENSE "GPL-3.0-or-later"
    SOURCES src/Extension.cpp src/PreviewPanel.cpp src/PreviewPanel.hpp src/Catalog.cpp src/Catalog.hpp
            src/PayloadDownload.cpp src/PayloadDownload.hpp src/OperationProgress.hpp
            src/CatalogueFilter.cpp src/CatalogueFilter.hpp src/CommandIcons.cpp src/CommandIcons.hpp
            src/Precipitation.cpp src/Precipitation.hpp src/StandardStormCurves.hpp src/NoaaClient.cpp src/NoaaClient.hpp
            src/StormPanel.cpp src/StormPanel.hpp
            resources/openswmm_data_icons.qrc)
set_target_properties(openswmm_data PROPERTIES AUTORCC ON)
target_link_libraries(openswmm_data PRIVATE GDAL::GDAL Qt6::Svg)
# Source consumers opt into these tests; a standalone Data build keeps them on.
option(OPENSWMM_DATA_BUILD_TESTS "Build offline catalogue/geometry checks" ${PROJECT_IS_TOP_LEVEL})
if(OPENSWMM_DATA_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(test_precipitation tests/test_precipitation.cpp src/Precipitation.cpp)
    set_target_properties(test_precipitation PROPERTIES AUTOMOC ON)
    target_compile_features(test_precipitation PRIVATE cxx_std_20)
    target_include_directories(test_precipitation PRIVATE src)
    target_link_libraries(test_precipitation PRIVATE Qt6::Test)
    add_test(NAME data_precipitation COMMAND test_precipitation)
    add_executable(test_noaa_client tests/test_noaa_client.cpp src/NoaaClient.cpp src/NoaaClient.hpp
                                    src/Precipitation.cpp)
    set_target_properties(test_noaa_client PROPERTIES AUTOMOC ON)
    target_compile_features(test_noaa_client PRIVATE cxx_std_20)
    target_include_directories(test_noaa_client PRIVATE src)
    target_link_libraries(test_noaa_client PRIVATE OpenSWMMVis::Sdk Qt6::Test)
    add_test(NAME data_noaa_client COMMAND test_noaa_client)
    add_executable(test_stormpanel tests/test_stormpanel.cpp src/StormPanel.cpp src/StormPanel.hpp
                                  src/NoaaClient.cpp src/NoaaClient.hpp src/Precipitation.cpp)
    set_target_properties(test_stormpanel PROPERTIES AUTOMOC ON)
    target_compile_features(test_stormpanel PRIVATE cxx_std_20)
    target_include_directories(test_stormpanel PRIVATE src)
    target_link_libraries(test_stormpanel PRIVATE OpenSWMMVis::Sdk Qt6::Test)
    target_compile_definitions(test_stormpanel PRIVATE
        SDK_STORM_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/design_storms")
    add_test(NAME data_stormpanel COMMAND test_stormpanel)
    set_tests_properties(data_stormpanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    add_executable(test_catalog tests/test_catalog.cpp src/Catalog.cpp)
    set_target_properties(test_catalog PROPERTIES AUTOMOC ON)
    target_compile_features(test_catalog PRIVATE cxx_std_20)
    target_include_directories(test_catalog PRIVATE src)
    target_link_libraries(test_catalog PRIVATE Qt6::Test GDAL::GDAL)
    add_test(NAME data_catalog COMMAND test_catalog)
    add_executable(test_preview tests/test_preview.cpp src/PreviewPanel.cpp src/PreviewPanel.hpp src/Catalog.cpp
                                src/CatalogueFilter.cpp src/PayloadDownload.cpp src/PayloadDownload.hpp)
    set_target_properties(test_preview PROPERTIES AUTOMOC ON)
    target_compile_features(test_preview PRIVATE cxx_std_20)
    target_include_directories(test_preview PRIVATE src)
    target_link_libraries(test_preview PRIVATE OpenSWMMVis::Sdk Qt6::Test GDAL::GDAL)
    target_compile_definitions(test_preview PRIVATE
        SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/extension_events_2026-10/ev5/preview")
    add_test(NAME data_preview COMMAND test_preview)
    set_tests_properties(data_preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(test_payload tests/test_payload.cpp src/PayloadDownload.cpp src/PayloadDownload.hpp src/Catalog.cpp)
    set_target_properties(test_payload PROPERTIES AUTOMOC ON)
    target_compile_features(test_payload PRIVATE cxx_std_20)
    target_include_directories(test_payload PRIVATE src)
    target_link_libraries(test_payload PRIVATE OpenSWMMVis::Sdk Qt6::Test GDAL::GDAL)
    target_compile_definitions(test_payload PRIVATE
        SDK_DATA_PAYLOAD_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/payload_downloads"
        SDK_DATA_PAYLOAD_FIXTURE_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/data")
    add_test(NAME data_payload COMMAND test_payload)
    set_tests_properties(data_payload PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
    add_executable(test_commandicons tests/test_commandicons.cpp src/CommandIcons.cpp resources/openswmm_data_icons.qrc)
    set_target_properties(test_commandicons PROPERTIES AUTOMOC ON AUTORCC ON)
    target_compile_features(test_commandicons PRIVATE cxx_std_20)
    target_include_directories(test_commandicons PRIVATE src)
    target_link_libraries(test_commandicons PRIVATE Qt6::Widgets Qt6::Svg Qt6::Test)
    target_compile_definitions(test_commandicons PRIVATE
        SDK_DATA_ICON_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/plugin_ribbon_2026-10/icons")
    add_test(NAME data_command_icons COMMAND test_commandicons)
    set_tests_properties(data_command_icons PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30)
endif()

# Use the same docs/ working directory and HTML layout as the GUI.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    if(PROJECT_IS_TOP_LEVEL)
        set(_openswmm_data_doc_target doc)
    else()
        set(_openswmm_data_doc_target openswmm_data_doc)
    endif()
    add_custom_target(${_openswmm_data_doc_target}
        COMMAND "${DOXYGEN_EXECUTABLE}" Doxyfile
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        COMMENT "Generate the themed Data Sources reference"
        VERBATIM)
endif()
