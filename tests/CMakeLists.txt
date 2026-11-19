find_package(Qt6 6.5 QUIET COMPONENTS Test)
if(NOT Qt6Test_FOUND)
    message(STATUS "未找到 Qt6::Test，跳过单元测试")
    return()
endif()

# --- 速度格式化（纯逻辑） ---
qt_add_executable(tst_speed_format
    tst_speed_format.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/speed_format.cpp
)
target_include_directories(tst_speed_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_speed_format PRIVATE Qt6::Core Qt6::Test)
add_test(NAME speed_format COMMAND tst_speed_format)

# --- 配置读写与钳制 ---
qt_add_executable(tst_config
    tst_config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/config.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/speed_format.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/theme.cpp
)
target_include_directories(tst_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_config PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME config COMMAND tst_config)

# --- 主题与对比度 ---
qt_add_executable(tst_theme
    tst_theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/theme.cpp
)
target_include_directories(tst_theme PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(tst_theme PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME theme COMMAND tst_theme)
