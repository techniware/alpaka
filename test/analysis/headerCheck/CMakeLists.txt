#
# Copyright 2014-2015 Benjamin Worpitz
#
# This file is part of alpaka.
#
# alpaka is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# alpaka is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with alpaka.
# If not, see <http://www.gnu.org/licenses/>.
#

################################################################################
# Required CMake version.
################################################################################

CMAKE_MINIMUM_REQUIRED(VERSION 2.8.12)

SET_PROPERTY(GLOBAL PROPERTY USE_FOLDERS ON)

################################################################################
# Project.
################################################################################

SET(_SOURCE_DIR "src/")
SET(_PROJECT_NAME "headerCheck")

PROJECT(${_PROJECT_NAME})

#-------------------------------------------------------------------------------
# Find alpaka.
#-------------------------------------------------------------------------------

SET(ALPAKA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../" CACHE STRING  "The location of the alpaka library")

LIST(APPEND CMAKE_MODULE_PATH "${ALPAKA_ROOT}")
FIND_PACKAGE("alpaka" REQUIRED)

#-------------------------------------------------------------------------------
# Common.
#-------------------------------------------------------------------------------

INCLUDE("${ALPAKA_ROOT}cmake/common.cmake")
INCLUDE("${ALPAKA_ROOT}cmake/dev.cmake")

#-------------------------------------------------------------------------------
# Boost.Test.
#-------------------------------------------------------------------------------
FIND_PACKAGE(Boost QUIET COMPONENTS unit_test_framework)
IF(NOT Boost_UNIT_TEST_FRAMEWORK_FOUND)
    MESSAGE(FATAL_ERROR "Required alpaka dependency Boost.Test could not be found!")

ELSE()
    LIST(APPEND _INCLUDE_DIRECTORIES_PRIVATE ${Boost_INCLUDE_DIRS})
    LIST(APPEND _LINK_LIBRARIES_PRIVATE ${Boost_LIBRARIES})
ENDIF()

#-------------------------------------------------------------------------------
# Create source files.
#-------------------------------------------------------------------------------

SET(_ALPAKA_INCLUDE_DIRECTORY "${_ALPAKA_ROOT_DIR}/include")
SET(_ALPAKA_SUFFIXED_INCLUDE_DIR "${_ALPAKA_INCLUDE_DIRECTORY}/alpaka")
append_recursive_files("${_ALPAKA_SUFFIXED_INCLUDE_DIR}" "hpp" "_ALPAKA_FILES_HEADER")

SET(_GENERATED_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/src")

FILE(REMOVE_RECURSE ${_GENERATED_SOURCE_DIR})

FOREACH(_HEADER_FILE ${_ALPAKA_FILES_HEADER})
    # Remove the parent directory from the path.
    # NOTE: This is not correct because it does not only replace at the beginning of the string.
    #  "STRING(REGEX REPLACE" would be correct if there was an easy way to escape arbitrary strings.
    STRING(
        REPLACE "${_ALPAKA_SUFFIXED_INCLUDE_DIR}/" ""
        _HEADER_FILE
        "${_HEADER_FILE}")
    SET(_SOURCE_FILE "${_GENERATED_SOURCE_DIR}/${_HEADER_FILE}.cpp")
    #MESSAGE("${_SOURCE_FILE}")
    FILE(WRITE "${_SOURCE_FILE}" "#include <alpaka/${_HEADER_FILE}>\n#include <alpaka/${_HEADER_FILE}>\n")
ENDFOREACH()

#-------------------------------------------------------------------------------
# Add library.
#-------------------------------------------------------------------------------

append_recursive_files_add_to_src_group("${_GENERATED_SOURCE_DIR}" "${_GENERATED_SOURCE_DIR}" "cpp" "_FILES_SOURCE")
append_recursive_files_add_to_src_group("${_SOURCE_DIR}" "${_SOURCE_DIR}" "cpp" "_FILES_SOURCE")

INCLUDE_DIRECTORIES(
    ${_INCLUDE_DIRECTORIES_PRIVATE}
    ${alpaka_INCLUDE_DIRS})
ADD_DEFINITIONS(
    ${alpaka_DEFINITIONS} ${ALPAKA_DEV_COMPILE_OPTIONS})
# Always add all files to the target executable build call to add them to the build project.
ALPAKA_ADD_EXECUTABLE(
    ${_PROJECT_NAME}
    ${_FILES_SOURCE})
# Set the link libraries for this library (adds libs, include directories, defines and compile options).
TARGET_LINK_LIBRARIES(
    ${_PROJECT_NAME}
    PUBLIC "alpaka" ${_LINK_LIBRARIES_PRIVATE})

SET_TARGET_PROPERTIES(headerCheck PROPERTIES FOLDER "test/analysis")