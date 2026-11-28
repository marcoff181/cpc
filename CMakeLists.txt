cmake_minimum_required(VERSION 3.21)
project(hpc_cpc LANGUAGES C) 

set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # helps editors and other tools understand project 

# set by default release (flags like -o3 and -dndebug), could be moved to running explicitly with release later
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# make available to all parallel algos from here
find_package(MPI REQUIRED COMPONENTS C)
find_package(OpenMP REQUIRED COMPONENTS C)

# ordered by dependencies 
add_subdirectory(libs/xxhash)
add_subdirectory(libs/cpc)
add_subdirectory(tools)
add_subdirectory(algorithms/sequential)
add_subdirectory(algorithms/parallel)
