cmake_minimum_required(VERSION 3.16)
project(jptxt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(JPTXT_CORE
    src/doc.cpp
    src/syntax.cpp
    src/search.cpp
    src/app.cpp
)

if(WIN32)
    add_executable(jptxt WIN32
        src/win32.cpp
        ${JPTXT_CORE}
        src/jptxt.rc
    )
    target_include_directories(jptxt PRIVATE src)
    target_compile_definitions(jptxt PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(jptxt PRIVATE comdlg32 shell32 user32 gdi32)
    if(MSVC)
        target_compile_options(jptxt PRIVATE /utf-8 /W3 /D_CRT_SECURE_NO_WARNINGS)
        set_property(TARGET jptxt PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
elseif(APPLE)
    enable_language(OBJCXX)
    add_executable(jptxt src/cocoa.mm ${JPTXT_CORE})
    target_include_directories(jptxt PRIVATE src)
    target_link_libraries(jptxt PRIVATE "-framework Cocoa")
    target_compile_options(jptxt PRIVATE -O2 -fobjc-arc)
else()
    add_executable(jptxt src/x11.cpp ${JPTXT_CORE})
    target_include_directories(jptxt PRIVATE src)
    find_package(X11 REQUIRED)
    target_link_libraries(jptxt PRIVATE ${X11_LIBRARIES})
    target_include_directories(jptxt PRIVATE ${X11_INCLUDE_DIR})
    target_compile_options(jptxt PRIVATE -O2 -Wall)
endif()
