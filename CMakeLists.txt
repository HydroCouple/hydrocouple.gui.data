# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Caleb Buahin. All rights reserved.
cmake_minimum_required(VERSION 3.21)
project(OpenSWMMData VERSION 0.1.0 LANGUAGES CXX)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build configuration" FORCE)
endif()
find_package(OpenSWMMVisSdk 1.5 CONFIG REQUIRED)
find_package(GDAL REQUIRED)
openswmmvis_add_extension(openswmm_data
    ID openswmm.gui.data CAPTION "Data sources" VERSION 0.1.0
    VENDOR "OpenSWMM" LICENSE "GPL-3.0-or-later"
    SOURCES src/Extension.cpp src/PreviewPanel.cpp src/PreviewPanel.hpp src/Catalog.cpp src/Catalog.hpp)
target_link_libraries(openswmm_data PRIVATE GDAL::GDAL)
option(OPENSWMM_DATA_BUILD_TESTS "Build offline catalogue/geometry checks" ON)
if(OPENSWMM_DATA_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    enable_testing()
    add_executable(test_catalog tests/test_catalog.cpp src/Catalog.cpp)
    set_target_properties(test_catalog PROPERTIES AUTOMOC ON)
    target_compile_features(test_catalog PRIVATE cxx_std_20)
    target_include_directories(test_catalog PRIVATE src)
    target_link_libraries(test_catalog PRIVATE Qt6::Test GDAL::GDAL)
    add_test(NAME data_catalog COMMAND test_catalog)
    add_executable(test_preview tests/test_preview.cpp src/PreviewPanel.cpp src/PreviewPanel.hpp src/Catalog.cpp)
    set_target_properties(test_preview PROPERTIES AUTOMOC ON)
    target_compile_features(test_preview PRIVATE cxx_std_20)
    target_include_directories(test_preview PRIVATE src)
    target_link_libraries(test_preview PRIVATE OpenSWMMVis::Sdk Qt6::Test GDAL::GDAL)
    target_compile_definitions(test_preview PRIVATE
        SDK_DATA_PREVIEW_VERIFICATION_DIRECTORY="${PROJECT_SOURCE_DIR}/tests/verification/extension_events_2026-10/ev5/preview")
    add_test(NAME data_preview COMMAND test_preview)
    set_tests_properties(data_preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
endif()

# Use the same docs/ working directory and HTML layout as the GUI.
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    add_custom_target(doc
        COMMAND "${DOXYGEN_EXECUTABLE}" Doxyfile
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs"
        COMMENT "Generate the themed Data Sources reference"
        VERBATIM)
endif()
