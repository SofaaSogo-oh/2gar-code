cmake_minimum_required(VERSION 3.10)
message(STATUS "Using Cmake version: ${CMAKE_VERSION}")
project(NGAR VERSION 0.0)

add_executable(NGAR main.cc)

target_compile_options(NGAR PRIVATE -fopenmp=libomp -O3)
target_link_options(NGAR PRIVATE -fopenmp=libomp)
target_compile_features(NGAR PRIVATE cxx_std_23)

find_package(Boost  COMPONENTS program_options REQUIRED)
target_link_libraries(NGAR PRIVATE Boost::program_options)