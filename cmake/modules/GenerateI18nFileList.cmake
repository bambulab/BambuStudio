# Writes the list of source files to scan for translatable strings.
# Run in script mode by the gettext_make_pot target, so new sources are found without re-configure:
#   cmake -DSOURCE_ROOT=<repo root> -DOUTPUT=<list file> -P GenerateI18nFileList.cmake
# Paths are relative to SOURCE_ROOT and sorted, so the generated .pot is stable across machines.

cmake_policy(SET CMP0009 NEW)

if (NOT SOURCE_ROOT OR NOT OUTPUT)
    message(FATAL_ERROR "GenerateI18nFileList.cmake needs -DSOURCE_ROOT and -DOUTPUT")
endif ()

set(_exts cpp hpp h c mm)
set(_globs)
foreach (_dir src/libslic3r src/slic3r)
    foreach (_ext ${_exts})
        list(APPEND _globs "${SOURCE_ROOT}/${_dir}/*.${_ext}")
    endforeach ()
endforeach ()

file(GLOB_RECURSE _files RELATIVE "${SOURCE_ROOT}" ${_globs})
list(APPEND _files src/BambuStudio.cpp)
list(SORT _files)
list(JOIN _files "\n" _content)
file(WRITE "${OUTPUT}" "${_content}\n")
