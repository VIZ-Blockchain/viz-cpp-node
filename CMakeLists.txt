# Defines VIZ library target.
project(VIZ)
# Note: bcrypt (needed by boost::uuid / random on MinGW) is linked per-target
# in the if(WIN32) block below to avoid polluting secp256k1 and other sub-targets.

if(MINGW)
  link_libraries(bcrypt)
  list(APPEND PLATFORM_SPECIFIC_LIBS bcrypt)
endif()

cmake_minimum_required(VERSION 3.16)

set(CHAIN_NAME "VIZ")


set(GUI_CLIENT_EXECUTABLE_NAME vizd)
set(CUSTOM_URL_SCHEME "vizd")
set(INSTALLER_APP_ID "e8af6bdb-23c8-4e55-beda-2cd257393b04")

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



option(CHAINBASE_CHECK_LOCKING "Check locks in chainbase (ON or OFF)" TRUE)
message(STATUS "CHAINBASE_CHECK_LOCKING: ${CHAINBASE_CHECK_LOCKING}")
if(CHAINBASE_CHECK_LOCKING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DCHAINBASE_CHECK_LOCKING")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DCHAINBASE_CHECK_LOCKING")
endif()

option(BUILD_CONSENSUS_TESTS "Build consensus simulation test harness" OFF)
option(WITH_COVERAGE "Enable gcov coverage instrumentation for consensus tests" OFF)

if(WIN32)
    set(BOOST_ROOT $ENV{BOOST_ROOT})
    set(Boost_USE_MULTITHREADED ON)
    set(BOOST_ALL_DYN_LINK OFF) # force dynamic linking for all libraries
endif(WIN32)

# CMake removed its bundled FindBoost module in 3.30. Boost >= 1.70 ships
# BoostConfig.cmake, which still populates Boost_LIBRARIES with Boost::* targets,
# so the existing ${Boost_LIBRARIES} references keep working and the old
# double-find_package dance for coroutine collapses into one call.
list(APPEND BOOST_COMPONENTS coroutine)

# Boost.System has been header-only since 1.69. Boost 1.90 stopped shipping a
# compiled library for it, and with it the boost_system CMake config, so naming
# system in COMPONENTS hard-fails there -- while Boost 1.83 still ships and
# wants it. Probe for the component instead of assuming either way.
find_package(boost_system QUIET CONFIG)
if(boost_system_FOUND)
    list(APPEND BOOST_COMPONENTS system)
else()
    message(STATUS "Boost.System is header-only in this Boost; not requesting it as a component")
endif()

find_package(Boost 1.83 CONFIG REQUIRED COMPONENTS ${BOOST_COMPONENTS})

# Boost 1.87 dropped the deprecated Asio interface from the <boost/asio.hpp>
# umbrella header. deadline_timer (used by plugins/validator) still exists, but
# only when Asio's Boost.DateTime integration is enabled and the header is
# included directly -- see plugins/validator/validator.cpp. The integration is
# on by default; defining this keeps it on even if something else disables it,
# and it must precede every Asio header, so it lives here rather than in one.
add_compile_definitions(BOOST_ASIO_HAS_BOOST_DATE_TIME)

find_program(CCACHE_FOUND ccache)
if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif(CCACHE_FOUND)

# Precompiled headers (opt-in, default OFF). When ON, the heaviest libraries
# (chain, protocol, wallet) precompile a curated set of stable third-party
# headers (Boost.MultiIndex, FC reflection/serialization, std) once per target
# instead of re-parsing them in every translation unit. See each library's
# pch.hpp for the header list and rationale.
#
# Left OFF by default so it cannot affect existing/CI builds until a maintainer
# opts in and measures. If ccache is in use, PCH only helps when ccache is told
# not to hash the (large, stable) PCH timestamp: export
#   CCACHE_SLOPPINESS=pch_defines,time_macros
# before building, otherwise ccache conservatively disables its own cache for
# PCH-using compilations and you lose the ccache win without gaining the PCH one.
option(ENABLE_PCH "Enable precompiled headers for heavy libraries" OFF)
if(ENABLE_PCH)
    message(STATUS "ENABLE_PCH: ON (precompiling stable third-party headers for chain/protocol/wallet)")
    if(CCACHE_FOUND AND NOT "$ENV{CCACHE_SLOPPINESS}" MATCHES "pch_defines")
        message(WARNING
            "ENABLE_PCH is ON and ccache is active, but CCACHE_SLOPPINESS does not "
            "include pch_defines,time_macros. ccache will likely disable caching for "
            "PCH-using compilations. Export "
            "CCACHE_SLOPPINESS=pch_defines,time_macros before building.")
    endif()
else()
    message(STATUS "ENABLE_PCH: OFF")
endif()

if(WIN32)

    message(STATUS "Configuring VIZ on WIN32")
    set(PLATFORM_SPECIFIC_LIBS) # Păstrăm inițializarea pentru alte biblioteci
    set(DB_VERSION 60)
    set(BDB_STATIC_LIBS 1)

    # Target Windows 10 (0x0A00). XP (0x0501) is too old for
    # boost::asio extended Winsock2 APIs and modern async I/O.
    # Set these directly in CMAKE_C_FLAGS and CMAKE_CXX_FLAGS to ensure they are
    # applied globally and consistently across all compilation units.
    add_compile_definitions(_WIN32_WINNT=0x0601  WINVER=0x0601 )

    set(ZLIB_LIBRARIES "")
    set(DEFAULT_EXECUTABLE_INSTALL_DIR bin/)

    set(CRYPTO_LIB)

    if(MSVC)
        add_compile_options(/wd4503 /wd4267 /wd4244 /EHa)
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
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14 -fpermissive -Wa,-mbig-obj")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    set(CMAKE_CXX_FLAGS_DEBUG "-O2")
    if(FULL_STATIC_BUILD)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
    endif(FULL_STATIC_BUILD)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    # bcrypt trebuie sa fie dupa toate librariile Boost statice
    list(APPEND PLATFORM_SPECIFIC_LIBS bcrypt)
        # We remove the explicit addition of bcrypt to PLATFORM_SPECIFIC_LIBS, as it is now linked globally
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

if(BUILD_CONSENSUS_TESTS)
    enable_testing()
    add_subdirectory(tests/consensus_sim)
    add_subdirectory(tests/pm)
endif()

if(ENABLE_INSTALLER)

    set(VERSION_MAJOR 0)
    set(VERSION_MINOR 1)
    set(VERSION_PATCH 0)


    include(InstallRequiredSystemLibraries)

    set(CPACK_OUTPUT_FILE_PREFIX ${CMAKE_BINARY_DIR}/packages)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install)

    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_INSTALL_PREFIX}")
    set(CPACK_PACKAGE_NAME "viz")
    set(CPACK_PACKAGE_VENDOR "VIZ-Blockchain")
    set(CPACK_PACKAGE_VERSION_MAJOR "${VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${VERSION_PATCH}")
    set(CPACK_PACKAGE_VERSION "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")
    set(CPACK_PACKAGE_DESCRIPTION "A client for the VIZ network")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "A client for the VIZ network")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "VIZ ${CPACK_PACKAGE_VERSION}")

    if(WIN32)
        set(CPACK_GENERATOR "ZIP;NSIS")
        set(CPACK_PACKAGE_NAME "VIZ") # override above
        set(CPACK_NSIS_EXECUTABLES_DIRECTORY .)
        set(CPACK_NSIS_PACKAGE_NAME "VIZ v${CPACK_PACKAGE_VERSION}")
        set(CPACK_NSIS_DISPLAY_NAME "${CPACK_NSIS_PACKAGE_NAME}")
        set(CPACK_NSIS_DEFINES "  !define MUI_STARTMENUPAGE_DEFAULTFOLDER \\\"VIZ\\\"")
        # it seems like windows zip files usually don't have a single directory inside them, unix tgz frequently do
        set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)

    endif(WIN32)

    if(APPLE)
        set(CPACK_GENERATOR "DragNDrop")
    endif()

    if(LINUX)
        # Linux gets a .tgz
        set(CPACK_GENERATOR "TGZ")
        set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 1)
    endif(LINUX)

    include(CPack)
endif(ENABLE_INSTALLER)

if(BUILD_TESTNET)
    message(STATUS "\n\n             CONFIGURED FOR TEST NETWORK             \n\n")
else()
    message(STATUS "\n\n             CONFIGURED FOR VIZ NETWORK             \n\n")
endif()
