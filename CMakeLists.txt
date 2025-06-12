# 要使用 CMake 构建此项目：
# 1. 创建一个构建目录（例如，在项目根目录下 mkdir build）
# 2. 进入构建目录 (cd build)
# 3. 运行 CMake 以生成构建文件:
#    cmake ..
#    (对于 MinGW，您可能需要指定生成器: cmake .. -G "MinGW Makefiles")
# 4. 运行构建命令:
#    cmake --build .
#    (或者，如果使用 MinGW Makefiles: mingw32-make)

cmake_minimum_required(VERSION 3.10)
project(ShotOcr_C)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置源文件
set(SOURCES
    src/main.cpp
    src/ScreenShotOCR.cpp
    src/ToastWindow.cpp
)

# 设置头文件目录
include_directories(include)

# 创建可执行文件
add_executable(${PROJECT_NAME} ${SOURCES})

# 链接Windows系统库
target_link_libraries(${PROJECT_NAME}
    user32
    gdi32
    gdiplus
    wininet
    shlwapi
    ole32
    shell32
    advapi32
)

# 设置Windows子系统（如果不想要控制台窗口）
set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE)

# 设置编译选项
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W3)
    # 为MSVC设置静态链接运行时库
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /MT")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /MTd")
    # 或者更通用的方式，修改 CMAKE_CXX_FLAGS
    # string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
    # string(REPLACE "/MDd" "/MTd" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
    # 为GCC/Clang设置静态链接
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    # 对于GCC/Clang，通常还需要静态链接libgcc和libstdc++
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
endif()
