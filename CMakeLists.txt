cmake_minimum_required(VERSION 3.15)
project(MCE_by_IV LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable warnings
if(MSVC)
    add_compile_options(/W4 /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Collect sources
file(GLOB SRC_FILES src/*.cpp)
file(GLOB HEADER_FILES include/*.hpp)

# Create library from our code (optional)
add_library(mce_lib ${SRC_FILES} ${HEADER_FILES})
target_include_directories(mce_lib PUBLIC include)

# Create final executable
add_executable(MCE_by_IV src/main.cpp)
target_link_libraries(MCE_by_IV PRIVATE mce_lib)

# Windows: avoid secure CRT warnings
if(WIN32)
    target_compile_definitions(MCE_by_IV PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
