add_library(xxhash INTERFACE)
target_include_directories(xxhash INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
# avoids needing importers to write #define XXH_INLINE_ALL
target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL) 
