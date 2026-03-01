cmake_minimum_required(VERSION 3.10)
project(D3D9WrapperRotatingCube)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    main.cpp
    ../../d3d9.cpp
)

add_executable(d3d9-rotating-cube ${SOURCES})

target_include_directories(d3d9-rotating-cube PRIVATE ../..)

if(EMSCRIPTEN)
    set(EMSCRIPTEN_LINK_FLAGS
        "-s USE_WEBGL2=1"
        "-s FULL_ES3=1"
        "-s WASM=1"
        "-s ALLOW_MEMORY_GROWTH=1"
        "--shell-file ${CMAKE_CURRENT_SOURCE_DIR}/shell.html"
        "--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/test.png@test.png"
    )

    string(REPLACE ";" " " EMSCRIPTEN_LINK_FLAGS_STR "${EMSCRIPTEN_LINK_FLAGS}")

    set_target_properties(d3d9-rotating-cube PROPERTIES
        LINK_FLAGS "${EMSCRIPTEN_LINK_FLAGS_STR}"
        SUFFIX ".html"
    )

    message(STATUS "Configuring for Emscripten/WebAssembly build")
endif()
