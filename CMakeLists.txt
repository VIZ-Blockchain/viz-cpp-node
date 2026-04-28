# Defines VIZ library target.
project(VIZ)
cmake_minimum_required(VERSION 3.16)

set(CHAIN_NAME "VIZ")

set(GUI_CLIENT_EXECUTABLE_NAME VIZ)
set(CUSTOM_URL_SCHEME "gcs")
set(INSTALLER_APP_ID "68ad7005-8eee-49c9-95ce-9eed97e5b347")

# http://stackoverflow.com/a/18369825
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
        message(FATAL_ERROR "GCC version must DB at least 4.8!")
    endif()
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 3.3)
        message(FATAL_ERROR "Clang version must be at least 3.3!")
    endif()
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules")

set(CMAKE_EXPORT_COMPILE_COMMANDS "ON")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fc/CMakeModules")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fc/GitVersionGen")
include(GetGitRevisionDescription)
get_git_head_revision(GIT_REFSPEC GIT_SHA2)

set(BOOST_COMPONENTS)
list(APPEND BOOST_COMPONENTS thread
        date_time
        system
        filesystem
        program_options
        serialization
        chrono
        unit_test_framework
        context
        locale
)

option(Boost_USE_STATIC_LIBS "Build with Boost static libraries usage" TRUE)

option(BUILD_SHARED_LIBRARIES "Build shared libraries" FALSE)

option(BUILD_TESTNET "Build source for test network (ON OR OFF)" FALSE)
message(STATUS "BUILD_TESTNET: ${BUILD_TESTNET}")
if(BUILD_TESTNET)
    message(STATUS "     ")
    message(STATUS "             CONFIGURING FOR TEST NETWORK             ")
    message(STATUS "     ")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBUILD_TESTNET")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DBUILD_TESTNET")
endif()

option(LOW_MEMORY_NODE "Build source for low memory node (ON OR OFF)" FALSE)
message(STATUS "LOW_MEMORY_NODE: ${LOW_MEMORY_NODE}")
if(LOW_MEMORY_NODE)
    message(STATUS "     ")
    message(STATUS "             CONFIGURING FOR LOW MEMORY NODE      ")
    message(STATUS "     ")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DIS_LOW_MEM")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DIS_LOW_MEM")
endif()

option(CHAINBASE_CHECK_LOCKING "Check locks in chainbase (ON or OFF)" TRUE)
message(STATUS "CHAINBASE_CHECK_LOCKING: ${CHAINBASE_CHECK_LOCKING}")
if(CHAINBASE_CHECK_LOCKING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCHAINBASE_CHECK_LOCKING")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCHAINBASE_CHECK_LOCKING")
endif()

option(ENABLE_MONGO_PLUGIN "Build with mongodb plugin" FALSE)
if(ENABLE_MONGO_PLUGIN)
  set(MONGO_LIB graphene::mongo_db)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DMONGODB_PLUGIN_BUILT")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DMONGODB_PLUGIN_BUILT")
endif()

if(WIN32)
    set(BOOST_ROOT $ENV{BOOST_ROOT})
    set(Boost_USE_MULTITHREADED ON)
    set(BOOST_ALL_DYN_LINK OFF) # force dynamic linking for all libraries
endif(WIN32)

find_package(Boost 1.71 REQUIRED COMPONENTS ${BOOST_COMPONENTS})
set(BOOST_LIBRARIES_TEMP ${Boost_LIBRARIES})
find_package(Boost 1.71 REQUIRED COMPONENTS coroutine)
list(APPEND BOOST_COMPONENTS coroutine)
set(Boost_LIBRARIES ${BOOST_LIBRARIES_TEMP} ${Boost_LIBRARIES})

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif(CCACHE_FOUND)

if(WIN32)

    message(STATUS "Configuring VIZ on WIN32")
    set(DB_VERSION 60)
    set(BDB_STATIC_LIBS 1)

    set(ZLIB_LIBRARIES "")
    set(DEFAULT_EXECUTABLE_INSTALL_DIR bin/)

    set(CRYPTO_LIB)

    if(MSVC)
        add_compile_options(/wd4503 /wd4267 /wd4244)
        #looks like this flag can have different default on some machines.
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /SAFESEH:NO")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")

        # Probably cmake has a bug and vcxproj generated for executable in Debug conf. has disabled debug info
        set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")

        # On windows tcl should be installed to the directory pointed by setenv.bat script
        set(TCL_INCLUDE_PATH $ENV{TCL_ROOT}/include)
        message(STATUS "tcl INCLUDE PATH: ${TCL_INCLUDE_PATH}")

        find_package(TCL)
        message(STATUS "tcl_library: ${TCL_LIBRARY}")

        set(TCL_LIBS "optimized;${TCL_LIBRARY};debug;")
        get_filename_component(TCL_LIB_PATH "${TCL_LIBRARY}" PATH)
        get_filename_component(TCL_LIB_NAME "${TCL_LIBRARY}" NAME_WE)
        get_filename_component(TCL_LIB_EXT "${TCL_LIBRARY}" EXT)

        set(TCL_LIBS "${TCL_LIBS}${TCL_LIB_PATH}/${TCL_LIB_NAME}g${TCL_LIB_EXT}")
        set(TCL_LIBRARY ${TCL_LIBS})

    elseif(MINGW)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fpermissive -msse4.2 -Wa,-mbig-obj")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3")
        # Optimization flag apparently needed to get rid of "File too big" assembler errors when compiling in Debug mode
        # See: https://stackoverflow.com/questions/14125007/gcc-string-table-overflow-error-during-compilation/14601779#29479701
        set(CMAKE_CXX_FLAGS_DEBUG "-O2")
        if(FULL_STATIC_BUILD)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
        endif(FULL_STATIC_BUILD)
    endif(MSVC)

else(WIN32) # Apple AND Linux

    find_library(READLINE_LIBRARIES NAMES readline)
    find_path(READLINE_INCLUDE_DIR readline/readline.h)
    #if(NOT READLINE_INCLUDE_DIR OR NOT READLINE_LIBRARIES)
    #  MESSAGE(FATAL_ERROR "Could not find lib readline.")
    #endif()

    if(APPLE)
        # Apple Specific Options Here
        message(STATUS "Configuring VIZ on OS X")
        set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14 -stdlib=libc++ -Wall -Wno-conversion -Wno-deprecated-declarations")
    else(APPLE)
        # Linux Specific Options Here
        message(STATUS "Configuring VIZ on Linux")
        set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -std=c++14 -Wall -DBOOST_BIND_GLOBAL_PLACEHOLDERS")
        set(rt_library rt)
        set(pthread_library pthread)
        if(NOT DEFINED crypto_library)
            # I'm not sure why this is here, I guess someone has openssl and can't detect it with find_package()?
            # if you have a normal install, you can define crypto_library to the empty string to avoid a build error
            set(crypto_library crypto)
        endif()
        if(FULL_STATIC_BUILD)
            set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
        endif(FULL_STATIC_BUILD)
    endif(APPLE)

    if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-builtin-memcmp")
    endif()

    if("${CMAKE_GENERATOR}" STREQUAL "Ninja")
        if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
            set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")
        endif()
    endif()

    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

    # based on http://www.delorie.com/gnu/docs/gdb/gdb_70.html
    # uncomment this line to tell GDB about macros (slows compile times)
    # set( CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -gdwarf-2 -g3" )

endif(WIN32)

set(ENABLE_COVERAGE_TESTING FALSE CACHE BOOL "Build VIZ for code coverage analysis")

if(ENABLE_COVERAGE_TESTING)
    set(CMAKE_CXX_FLAGS "--coverage ${CMAKE_CXX_FLAGS}")
endif()

add_subdirectory(thirdparty)
add_subdirectory(libraries)
add_subdirectory(plugins)
add_subdirectory(programs)

if(BUILD_TESTNET)
    message(STATUS "\n\n             CONFIGURED FOR TEST NETWORK             \n\n")
else()
    message(STATUS "\n\n             CONFIGURED FOR VIZ NETWORK             \n\n")
endif()

if(LOW_MEMORY_NODE)
    message(STATUS "\n\n             CONFIGURED FOR LOW MEMORY NODE          \n\n")
else()
    message(STATUS "\n\n             CONFIGURED FOR FULL NODE                \n\n")
endif()
