cmake_minimum_required(VERSION 3.31.3)

project(TORRENT)

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
    "${TORRENT_SRC_DIR}/bencode_parser.cpp" 
    "${TORRENT_SRC_DIR}/peer.cpp" 
    "${TORRENT_SRC_DIR}/peer_manager.cpp" 
    "${TORRENT_SRC_DIR}/utility.cpp" 
    "${TORRENT_SRC_DIR}/client.cpp" 
    "${TORRENT_SRC_DIR}/pieces.cpp" 
)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Boost REQUIRED COMPONENTS url)
find_package(Boost REQUIRED COMPONENTS asio)
find_package(Boost REQUIRED COMPONENTS endian)
find_package(Boost REQUIRED COMPONENTS log)

add_executable(torrent ${SRC_FILES} ${HEADER_FILES})

target_link_libraries(torrent PRIVATE Boost::asio)
target_link_libraries(torrent PRIVATE Boost::url)
target_link_libraries(torrent PRIVATE Boost::endian)
target_link_libraries(torrent PRIVATE Boost::log)

# open ubsan
target_compile_options(torrent PRIVATE "-fsanitize=undefined,address")
target_link_options(torrent PRIVATE "-fsanitize=undefined,address")

# Asio uses random access handle in windows and io uring in linux.
if (NOT WIN32)
    # So if we are in linux we need to open io_uring in order to use asio files.
    target_compile_definitions(torrent PRIVATE BOOST_ASIO_HAS_IO_URING)
    target_link_libraries(torrent PRIVATE uring)
endif (WIN32)


include(cmake/CompilerWarnings.cmake)
set_project_warnings(
  torrent
  ON
  ""
  ""
  ""
  "")

target_include_directories(torrent PRIVATE 
    ${TORRENT_INCLUDE_DIR} 
)

set_target_properties(
    torrent PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

