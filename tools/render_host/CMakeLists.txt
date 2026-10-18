# Host renderer: cmake -S tools/render_host -B out/render_host && cmake --build out/render_host
cmake_minimum_required(VERSION 3.16)
project(render_host CXX)
set(CMAKE_CXX_STANDARD 17)

set(APP ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)
set(THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party)
# the same OpenXR headers as the app's loader (org.khronos.openxr:openxr_loader_for_android 1.1.65)
set(OPENXR_TAG release-1.1.65)
foreach(h openxr.h openxr_platform_defines.h openxr_reflection.h)
    if(NOT EXISTS ${CMAKE_BINARY_DIR}/include/openxr/${h})
        file(DOWNLOAD https://raw.githubusercontent.com/KhronosGroup/OpenXR-SDK/${OPENXR_TAG}/include/openxr/${h}
             ${CMAKE_BINARY_DIR}/include/openxr/${h} STATUS st)
    endif()
endforeach()

# the font the app finds on Android (/system/fonts/Roboto-Regular.ttf), for screenshots that look like the headset
set(ROBOTO ${CMAKE_BINARY_DIR}/Roboto-Regular.ttf)
if(NOT EXISTS ${ROBOTO})
    file(DOWNLOAD https://raw.githubusercontent.com/googlefonts/roboto/v2.138/src/hinted/Roboto-Regular.ttf ${ROBOTO}
         STATUS st)
endif()

add_executable(render_host main.cpp xr_stubs.cpp ${APP}/scene.cpp ${APP}/font.cpp ${APP}/gfx.cpp ${APP}/ui.cpp ${APP}/mr.cpp
               ${APP}/mrlog.cpp ${APP}/state.cpp ${APP}/demo_room.cpp ${APP}/xrnames.cpp)
target_include_directories(render_host PRIVATE ${APP} ${CMAKE_BINARY_DIR}/include ${THIRD_PARTY}/stb)
target_compile_definitions(render_host PRIVATE TQMR_HOST_FONT="${ROBOTO}")
target_compile_options(render_host PRIVATE -Wall -Wno-unused-function)
target_link_libraries(render_host PRIVATE EGL GLESv2)
