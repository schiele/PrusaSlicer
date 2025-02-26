if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

set (_openvdb_vdbprint ON)
if (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" OR NOT ${CMAKE_BUILD_TYPE} STREQUAL Release)
    # Build fails on raspberry pi due to missing link directive to latomic
    # Let's hope it will be fixed soon.
    set (_openvdb_vdbprint OFF)
endif ()

add_cmake_project(OpenVDB
    # 8.2 patched
    URL https://github.com/prusa3d/openvdb/archive/d49d53332d054e71dfa444a8cb5f965212a57a92.zip
    URL_HASH SHA256=2a5a50f3565a53ab7e7b78d6e2b345faa1f3b49891d004a3f052cb8cf01a5dfa
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON 
        -DOPENVDB_BUILD_PYTHON_MODULE=OFF
        -DUSE_BLOSC=ON
        -DOPENVDB_CORE_SHARED=${_build_shared} 
        -DOPENVDB_CORE_STATIC=${_build_static}
        -DOPENVDB_ENABLE_RPATH:BOOL=OFF
        -DTBB_STATIC=OFF
        -DOPENVDB_BUILD_VDB_PRINT=${_openvdb_vdbprint}
        -DDISABLE_DEPENDENCY_VERSION_CHECKS=ON # Centos6 has old zlib
)

set(DEP_OpenVDB_DEPENDS TBB Blosc OpenEXR Boost)
