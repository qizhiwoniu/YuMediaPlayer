cmake_minimum_required(VERSION 3.16)
project(YuMediaPlayer) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_definitions("-DUNICODE" "-D_UNICODE")                                   # Windows API 使用宽字符版本
    add_compile_options(/utf-8)                                                 # 源文件按 UTF-8 解析
endif()
    
file(GLOB_RECURSE SRC  "${CMAKE_SOURCE_DIR}/src/*.cpp" "${CMAKE_SOURCE_DIR}/logo.rc")   

# ON 使用 Win32 GUI 子系统, OFF 使用控制台子系统
option(USE_WIN32 "Use Win32 GUI subsystem" OFF)

#if(USE_WIN32)
#    add_executable(YuMediaPlayer WIN32 ${SRC})
#    target_compile_definitions(YuMediaPlayer PRIVATE USE_WIN32)
#else()
    add_executable(YuMediaPlayer ${SRC})
#endif()  

target_include_directories(YuMediaPlayer PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/thirdParty"
)

target_link_libraries(YuMediaPlayer) 

set_target_properties(YuMediaPlayer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/YuMediaPlayer"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/YuMediaPlayer"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/YuMediaPlayer"
    VS_DEBUGGER_WORKING_DIRECTORY    "${CMAKE_BINARY_DIR}/YuMediaPlayer"
)  

if(EXISTS "${CMAKE_SOURCE_DIR}/Assets")
    add_custom_command(TARGET YuMediaPlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Assets"
                "$<TARGET_FILE_DIR:YuMediaPlayer>"
        COMMENT "Copy assets contents for YuMediaPlayer"
    )
endif()
