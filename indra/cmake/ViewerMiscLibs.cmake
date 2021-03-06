# -*- cmake -*-
include(Prebuilt)


if (STANDALONE)
  set(STANDALONE OFF)
  use_prebuilt_binary(vivox)
  if(LINUX AND ${ARCH} STREQUAL "x86_64")
    use_prebuilt_binary(32bitcompatibilitylibs)
  endif(LINUX AND ${ARCH} STREQUAL "x86_64")
  set(STANDALONE ON)
else (STANDALONE)
  use_prebuilt_binary(vivox)
  use_prebuilt_binary(fontconfig)

  if(EXISTS ${CMAKE_SOURCE_DIR}/newview/res/have_artwork_bundle.marker)
    message(STATUS "We seem to have an artwork bundle in the tree - brilliant.")
  else(EXISTS ${CMAKE_SOURCE_DIR}/newview/res/have_artwork_bundle.marker)
    message(FATAL_ERROR "Didn't find an artwork bundle - this needs to be downloaded separately and unpacked into this tree.  You can probably get it from the same place you got your viewer source.  Thanks!")
  endif(EXISTS ${CMAKE_SOURCE_DIR}/newview/res/have_artwork_bundle.marker)
  if(LINUX)
    if (${ARCH} STREQUAL "x86_64")
     use_prebuilt_binary(32bitcompatibilitylibs)
    endif (${ARCH} STREQUAL "x86_64")
  endif(LINUX)
endif(STANDALONE)
