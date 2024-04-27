cmake_minimum_required(VERSION 3.27)
set(APP_BUILD_OPTIONS)
set(APP_LINK_OPTIONS)
set(APP_BUILD_LIB)
set(APP_EXTRA_C_FILES)

# Zig makes cross-compiling easier and ignores glibc version issue
# target can be: x86_64-macos-none, x86_64-macos.11.0-none, x86_64-windows-gnu, x86_64-linux-gnu, x86_64-linux-gnu.2.17, ...
# Using `zig targets | jq .libc` to get the list of targets
option(ZIG_TARGET "Setting a target and using `zig cc/c++` to compile" OFF)
# example: -DZIG_NIGHTLY="0.12.0-dev.3631+c4587dc9f"
option(ZIG_NIGHTLY "Download nightly build instead of release version if zig is not in the system path" OFF)

option(PACKAGE_FILE "Package static files" OFF)

if (ZIG_TARGET)
    include(${CMAKE_SOURCE_DIR}/cmake/zig.cmake)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/cmake/zig.toolchain.cmake")
endif ()

project(cloud_clipboard)
if (MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static" CACHE STRING "" FORCE)
endif ()
if (WIN32)
    list(APPEND APP_BUILD_LIB ws2_32)
endif ()

include(FetchContent)
FetchContent_Declare(mongoose GIT_REPOSITORY https://github.com/cesanta/mongoose.git
        GIT_TAG 7.13)
FetchContent_MakeAvailable(mongoose)

FetchContent_Declare(stb GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG ae721c50eaf761660b4f90cc590453cdb0c2acd0)
FetchContent_MakeAvailable(stb)

if (PACKAGE_FILE)
    list(APPEND APP_BUILD_OPTIONS -DPACKAGE_FILE -DMG_ENABLE_PACKED_FS=1)
    list(APPEND APP_EXTRA_C_FILES ${CMAKE_BINARY_DIR}/static.c)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/static.c
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND ${ZIG} cc -o ${CMAKE_BINARY_DIR}/pack ${mongoose_SOURCE_DIR}/test/pack.c
        COMMAND ${CMAKE_BINARY_DIR}/pack static/* static/css/* static/js/* static/fonts/* > ${CMAKE_BINARY_DIR}/static.c
    )
else ()
    file(COPY ${CMAKE_SOURCE_DIR}/static DESTINATION ${CMAKE_BINARY_DIR})
endif ()

file(GLOB_RECURSE MAIN_SRC ${CMAKE_SOURCE_DIR}/src/*.c)
add_executable(${PROJECT_NAME}
        ${MAIN_SRC}
        ${APP_EXTRA_C_FILES}
        ${mongoose_SOURCE_DIR}/mongoose.c)

target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${mongoose_SOURCE_DIR}
        ${stb_SOURCE_DIR}
)

if(ZIG_TARGET_ARCH MATCHES "mips")
    list(APPEND APP_BUILD_OPTIONS -msoft-float)
    list(APPEND APP_LINK_OPTIONS -msoft-float)
endif ()

target_compile_options(${PROJECT_NAME} PRIVATE -DMG_ENABLE_DIRLIST=0 -DMG_ENABLE_CUSTOM_LOG=1 -DMG_ENABLE_IPV6=1 ${APP_BUILD_OPTIONS} )
target_link_libraries(${PROJECT_NAME} ${APP_BUILD_LIB})
target_link_options(${PROJECT_NAME} PRIVATE ${APP_LINK_OPTIONS})