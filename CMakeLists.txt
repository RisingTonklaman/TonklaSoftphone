cmake_minimum_required(VERSION 3.20)
project(TonklaSoftphone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Roots ----
set(PJSIP_ROOT "" CACHE PATH "Path to PJSIP install root (with include/ and lib/)")
set(OPENH264_DIR "C:/deps/openh264/src_full" CACHE PATH "OpenH264 root")

# ---- Validate ----
if(NOT IS_DIRECTORY "${PJSIP_ROOT}/include" OR NOT IS_DIRECTORY "${PJSIP_ROOT}/lib")
  message(FATAL_ERROR "PJSIP_ROOT invalid. Expect include/ and lib/ under: ${PJSIP_ROOT}")
endif()
if(NOT EXISTS "${OPENH264_DIR}/build_x64/openh264.lib")
  message(FATAL_ERROR "Missing ${OPENH264_DIR}/build_x64/openh264.lib")
endif()
if(NOT EXISTS "${OPENH264_DIR}/build_x64/openh264-8.dll")
  message(FATAL_ERROR "Missing ${OPENH264_DIR}/build_x64/openh264-8.dll")
endif()

# ---- External libs ----
file(GLOB PJSIP_LIBS "${PJSIP_ROOT}/lib/*.lib")

# SDL2 (renderer ของ pjmedia)
find_library(SDL2_LIB NAMES SDL2 PATHS "C:/vcpkg/installed/x64-windows/lib" NO_DEFAULT_PATH)
find_file(SDL2_DLL NAMES SDL2.dll PATHS "C:/vcpkg/installed/x64-windows/bin" NO_DEFAULT_PATH)
if(NOT SDL2_LIB)
  message(FATAL_ERROR "SDL2.lib not found at C:/vcpkg/installed/x64-windows/lib")
endif()

# ---- Target ----
add_executable(softphone
  src/main.cpp
)

# กัน NOMINMAX ทับ std::min/max
target_compile_definitions(softphone PRIVATE NOMINMAX)

# include dirs
target_include_directories(softphone PRIVATE
  "${PJSIP_ROOT}/include"
  "${OPENH264_DIR}/codec/api"       # มีโฟลเดอร์ย่อย 'wels'
  "C:/vcpkg/installed/x64-windows/include"  # SDL2 headers
)

# link libs
target_link_libraries(softphone PRIVATE
  ${PJSIP_LIBS}
  "${OPENH264_DIR}/build_x64/openh264.lib"
  ${SDL2_LIB}
  ws2_32 iphlpapi winmm ole32 uuid gdi32 user32 setupapi
)

# copy DLLs ข้าง ๆ exe
add_custom_command(TARGET softphone POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${OPENH264_DIR}/build_x64/openh264-8.dll"
          "$<TARGET_FILE_DIR:softphone>/"
)
if(SDL2_DLL)
  add_custom_command(TARGET softphone POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${SDL2_DLL}"
            "$<TARGET_FILE_DIR:softphone>/"
  )
endif()
