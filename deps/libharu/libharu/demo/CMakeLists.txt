# demo/CMakeLists.txt
#
# Copyright (C) 2008  Werner Smekal
#
# create demo executables

  # =======================================================================
  # source file names and c flags
  # =======================================================================
set(
  demos_NAMES
    attach
    character_map
    encoding_list
    encryption
    ext_gstate_demo
    font_demo
    jpeg_demo
    jpfont_demo
    image_demo
    line_demo
    link_annotation
    make_rawimage
    outline_demo
    #outline_demo_jp
    pdf_a_conformance
    permission
    png_demo
    slide_show_demo
    text_annotation
    ttfont_demo
    ttfont_demo_jp
)

set(
  demos_with_grid_NAMES
    arc_demo
    chfont_demo
    raw_image_demo
    text_demo
    text_demo2
)

# we need to add some definitions for win 32
set(DEMO_C_FLAGS)
if(WIN32 AND NOT CYGWIN)
  set(DEMO_C_FLAGS "-D__WIN32__")
endif(WIN32 AND NOT CYGWIN)

# =======================================================================
# create demos
# =======================================================================
foreach(demo ${demos_NAMES})
  add_executable(${demo} ${demo}.c)
  target_link_libraries(${demo} PUBLIC hpdf)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${demo} PUBLIC m)
  endif()
  if(DEMO_C_FLAGS)
    set_target_properties(${demo} PROPERTIES COMPILE_FLAGS ${DEMO_C_FLAGS})
  endif(DEMO_C_FLAGS)
endforeach()

# some demos need grid_sheet.c compiled in
foreach(demo ${demos_with_grid_NAMES})
  add_executable(${demo} ${demo}.c grid_sheet.c)
  target_link_libraries(${demo} PUBLIC hpdf)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${demo} PUBLIC m)
  endif()
  if(DEMO_C_FLAGS)
    set_target_properties(${demo} PROPERTIES COMPILE_FLAGS ${DEMO_C_FLAGS})
  endif(DEMO_C_FLAGS)
endforeach()

# the grid_sheet demo needs extra defines
add_executable(grid_sheet grid_sheet.c)
target_link_libraries(grid_sheet PUBLIC hpdf)
  if(UNIX AND NOT APPLE)
    target_link_libraries(grid_sheet PUBLIC m)
  endif()
set_target_properties(grid_sheet PROPERTIES COMPILE_FLAGS "${DEMO_C_FLAGS} -DSTAND_ALONE")

# =======================================================================
# install demo source, demo executables and other files
# =======================================================================
install(
  TARGETS ${demos_NAMES} ${demos_with_grid_NAMES} grid_sheet
  DESTINATION demo
)
foreach(demo ${demos_NAMES} ${demos_with_grid_NAMES} grid_sheet)
  install(FILES ${demo}.c DESTINATION demo)
endforeach()

# install demo files
install(
  DIRECTORY images mbtext pdf_a pngsuite rawimage ttfont type1
  DESTINATION demo
)
