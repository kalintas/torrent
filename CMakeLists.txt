cmake_minimum_required(VERSION 3.31.3)

project(TORRENT)

include(cmake/CompilerWarnings.cmake)

set(TORRENT_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include")
set(TORRENT_SRC_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/src")

set(
    HEADER_FILES 
    "${TORRENT_INCLUDE_DIR}/bencode_parser.hpp"
    "${TORRENT_INCLUDE_DIR}/client.hpp"
)
set(
    SRC_FILES 
    "${TORRENT_SRC_DIR}/main.cpp" 
)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost REQUIRED COMPONENTS url)
find_package(Boost REQUIRED COMPONENTS asio)
find_package(Boost REQUIRED COMPONENTS endian)

add_executable(torrent ${SRC_FILES} ${HEADER_FILES})

target_link_libraries(torrent PRIVATE Boost::asio)
target_link_libraries(torrent PRIVATE Boost::url)
target_link_libraries(torrent PRIVATE Boost::endian)

set_project_warnings(torrent ON "" "" "" "")

target_include_directories(torrent PRIVATE 
    ${TORRENT_INCLUDE_DIR} 
)

set_target_properties(
    torrent PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

