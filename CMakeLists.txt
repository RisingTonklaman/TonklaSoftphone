cmake_minimum_required(VERSION 3.20)
project(TonklaSoftphone LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Roots ----
# Defaults to local, override if you have system installs
set(PJSIP_ROOT "${CMAKE_SOURCE_DIR}/pjsip/pjproject/install" CACHE PATH "Path to PJSIP install root (with include/ and lib/)")
set(OPENH264_DIR "${CMAKE_SOURCE_DIR}/third_party/openh264" CACHE PATH "OpenH264 root")
set(VCPKG_DIR "${CMAKE_SOURCE_DIR}/third_party/vcpkg" CACHE PATH "vcpkg root (for SDL2)")

# ---- Validate ----
if(NOT IS_DIRECTORY "${PJSIP_ROOT}/include" OR NOT IS_DIRECTORY "${PJSIP_ROOT}/lib")
  message(FATAL_ERROR "PJSIP_ROOT invalid. Expect include/ and lib/ under: ${PJSIP_ROOT}")
endif()
if(NOT EXISTS "${OPENH264_DIR}/build_x64/openh264.lib")
  message(FATAL_ERROR "Missing ${OPENH264_DIR}/build_x64/openh264.lib (run scripts/setup-windows.ps1)")
endif()
if(NOT EXISTS "${OPENH264_DIR}/build_x64/openh264-8.dll")
  message(FATAL_ERROR "Missing ${OPENH264_DIR}/build_x64/openh264-8.dll (run scripts/setup-windows.ps1)")
endif()

# ---- External libs ----
file(GLOB PJSIP_LIBS "${PJSIP_ROOT}/lib/*.lib")

# SDL2 (renderer ของ pjmedia)
set(_VCPKG_INC "${VCPKG_DIR}/installed/x64-windows/include")
set(_VCPKG_LIB "${VCPKG_DIR}/installed/x64-windows/lib")
set(_VCPKG_BIN "${VCPKG_DIR}/installed/x64-windows/bin")
find_library(SDL2_LIB NAMES SDL2 PATHS "${_VCPKG_LIB}" NO_DEFAULT_PATH)
find_file(SDL2_DLL NAMES SDL2.dll PATHS "${_VCPKG_BIN}" NO_DEFAULT_PATH)
if(NOT SDL2_LIB)
  message(FATAL_ERROR "SDL2.lib not found. Expected at ${_VCPKG_LIB}. Run scripts/setup-windows.ps1")
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
  "${_VCPKG_INC}"                    # SDL2 headers
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
