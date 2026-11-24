# Keep the historical macOS 10.15 deployment target on SDKs that still accept it.
# Xcode 27's libc++ warns for anything older than macOS 11, and dependencies that
# compile with -Werror (Boost.Nowide) turn that warning into a hard failure.
# Probe once per compiler and requested target, and raise only when the probe fails.

function(bambu_ensure_osx_deployment_target)
    if(NOT APPLE)
        return()
    endif()

    set(_osx_target "${CMAKE_OSX_DEPLOYMENT_TARGET}")
    if(NOT _osx_target)
        set(_osx_target "10.15")
    endif()

    if(_osx_target VERSION_LESS "11.0")
        set(_probe_key "LIBCXX_ACCEPTS_OSX_${_osx_target}_${CMAKE_CXX_COMPILER_ID}_${CMAKE_CXX_COMPILER_VERSION}")
        string(MAKE_C_IDENTIFIER "${_probe_key}" _probe_key)
        if(NOT DEFINED ${_probe_key})
            include(CheckCXXSourceCompiles)
            set(CMAKE_REQUIRED_FLAGS "-Werror")
            set(CMAKE_OSX_DEPLOYMENT_TARGET "${_osx_target}")
            # The libc++ warning lives in a standard header, so an empty main
            # never sees it. Including <version> is enough to pull it in.
            check_cxx_source_compiles("#include <version>\nint main(){return 0;}" ${_probe_key})
            unset(CMAKE_REQUIRED_FLAGS)
        endif()
        if(NOT ${_probe_key})
            message(STATUS "macOS deployment target ${_osx_target} is rejected by this SDK's libc++; using 11.0")
            set(_osx_target "11.0")
        endif()
    endif()

    set(CMAKE_OSX_DEPLOYMENT_TARGET "${_osx_target}" CACHE STRING "Minimum OS X deployment version" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "${_osx_target}" PARENT_SCOPE)
endfunction()
