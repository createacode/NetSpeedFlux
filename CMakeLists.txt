cmake_minimum_required(VERSION 3.21)

project(NetSpeedFlux VERSION 1.3.7 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "NetSpeedFlux 仅支持 Windows（依赖 iphlpapi / pdh / shell32 等 Win32 API）。")
endif()

# .rc 资源（图标 + 版本信息）需要 RC 语言；MinGW 下由 windres 处理。
enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# 目录级编译选项：测试目标同样需要 /utf-8（源码里有中文字面量）。
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

set(NSM_SOURCES
    src/app.cpp
    src/app_paths.cpp
    src/config.cpp
    src/logger.cpp
    src/main.cpp
    src/net_monitor.cpp
    src/overlay.cpp
    src/settings_window.cpp
    src/single_instance.cpp
    src/speed_format.cpp
    src/theme.cpp
    src/tray.cpp
    src/win_util.cpp
)

set(NSM_HEADERS
    src/app.h
    src/app_paths.h
    src/config.h
    src/logger.h
    src/net_monitor.h
    src/overlay.h
    src/settings_window.h
    src/single_instance.h
    src/speed_format.h
    src/theme.h
    src/tray.h
    src/win_util.h
)

qt_add_executable(NetSpeedFlux WIN32 ${NSM_SOURCES} ${NSM_HEADERS})

target_include_directories(NetSpeedFlux PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(NetSpeedFlux PRIVATE
    Qt6::Widgets
    iphlpapi   # GetIfTable2 / FreeMibTable
    advapi32   # RegGetValueW / RegSetValueExW
    shell32    # SHQueryUserNotificationState
    user32     # 窗口与任务栏相关 API
    gdi32      # GetPixel（任务栏背景采样）
    dwmapi     # DwmSetWindowAttribute（首帧 cloak，消除启动方框）
)

if(MINGW)
    # 静态链接 MinGW 运行库，减少外部 DLL 依赖。
    # 不用 -static：那会把 Qt 也静态化，与共享 Qt 冲突。
    target_link_options(NetSpeedFlux PRIVATE -static-libgcc -static-libstdc++)
endif()

# ---- 版本号单一来源：生成 version.h 与 app.rc ----
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h
    @ONLY
)

set(NSM_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/app.ico")
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/app.rc.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc
    @ONLY
)
target_sources(NetSpeedFlux PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc)

# 图标同时嵌入 Qt 资源，运行期不再依赖 exe 同级目录下的 app.ico。
qt_add_resources(NetSpeedFlux "nsm_icons"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/app.ico"
)

# ---- 安装与部署 ----
install(TARGETS NetSpeedFlux RUNTIME DESTINATION .)

if(COMMAND qt_generate_deploy_app_script)
    qt_generate_deploy_app_script(
        TARGET NetSpeedFlux
        OUTPUT_SCRIPT NSM_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    if(NSM_DEPLOY_SCRIPT)
        install(SCRIPT ${NSM_DEPLOY_SCRIPT})
        # 部署脚本默认把运行库放进 bin/、插件放进 plugins/，而 exe 在安装根目录。
        # 这里把 DLL 拉到 exe 同层并写 qt.conf，得到可直接压缩分发的便携目录。
        install(CODE [[
            set(_prefix "${CMAKE_INSTALL_PREFIX}")
            file(GLOB _runtime_dlls "${_prefix}/bin/*.dll")
            if(_runtime_dlls)
                file(COPY ${_runtime_dlls} DESTINATION "${_prefix}")
                file(REMOVE_RECURSE "${_prefix}/bin")
            endif()
            file(WRITE "${_prefix}/qt.conf" "[Paths]
Plugins = plugins
")
        ]])
    endif()
endif()

# ---- 测试 ----
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
