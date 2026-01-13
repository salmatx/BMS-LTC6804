cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/common"
    "${CMAKE_CURRENT_LIST_DIR}/bms"
    "${CMAKE_CURRENT_LIST_DIR}/process"
    "${CMAKE_CURRENT_LIST_DIR}/http"
    "${CMAKE_CURRENT_LIST_DIR}/process/configuration"
    "${CMAKE_CURRENT_LIST_DIR}/process/network"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(bms_project)
