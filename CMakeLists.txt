cmake_minimum_required(VERSION 3.28)

project(task_queue LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(task_queue WIN32
    src/main.cpp
    src/app.cpp
    resources/task_queue.rc
)

target_include_directories(task_queue PRIVATE
    ${PROJECT_SOURCE_DIR}/include
)

target_compile_definitions(task_queue PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
)

if(MINGW)
    target_link_options(task_queue PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

target_link_libraries(task_queue PRIVATE
    comctl32
    gdiplus
)
