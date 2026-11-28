add_library(cpc STATIC
  src/fm85.c
  src/fm85Compression.c
  src/fm85Merging.c
  src/fm85Util.c
  src/u32Table.c
  src/icon.c
)

target_include_directories(cpc
  PUBLIC  include
  PRIVATE include/cpc
)

target_link_libraries(cpc PRIVATE m)
