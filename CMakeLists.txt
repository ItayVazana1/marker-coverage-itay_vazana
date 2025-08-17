cmake_minimum_required(VERSION 3.15)
project(MCE_CPP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
  add_compile_options(/W4 /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---- OpenCV ----
# Hint for MSYS2 UCRT64 on Windows; on Linux (e.g., Docker) it'll be found normally.
if (WIN32 AND DEFINED ENV{MSYSTEM} AND "$ENV{MSYSTEM}" STREQUAL "UCRT64")
  set(OpenCV_DIR "C:/msys64/ucrt64/lib/cmake/opencv4" CACHE PATH "" FORCE)
endif()
# detect_and_compute משתמש ב-core,imgproc,imgcodecs בלבד
find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)

# ---- Core lib ----
add_library(mce_core
  src/detect_and_compute.cpp    # ← החדש
  src/log.cpp
)
target_include_directories(mce_core PUBLIC include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(mce_core PUBLIC ${OpenCV_LIBS})

# ---- TUI executable ----
add_executable(MCE_by_IV
  src/main.cpp
  src/app.cpp
  src/ui.cpp
  src/progress.cpp
)
target_include_directories(MCE_by_IV PRIVATE include ${OpenCV_INCLUDE_DIRS})
target_link_libraries(MCE_by_IV PRIVATE mce_core)
if (WIN32)
  target_compile_definitions(MCE_by_IV PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Put binaries in build/ for single-config generators (Ninja/Make)
if (NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()
