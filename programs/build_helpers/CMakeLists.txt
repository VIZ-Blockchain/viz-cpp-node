# cat-parts concatenates hardfork.d/*.hf into hardfork.hpp. It is only invoked
# on MSVC/MinGW (see libraries/chain/CMakeLists.txt); every other platform uses
# cat_parts.py instead. Only build the binary where it is actually used so Linux
# and macOS builds don't compile and link a tool they never run.
if(MSVC OR MINGW)
    add_executable(cat-parts cat-parts.cpp)
    if(UNIX AND NOT APPLE)
        set(rt_library rt)
    endif()

    # we only actually need Boost, but link against FC for now so we don't duplicate it.
    target_link_libraries(cat-parts PRIVATE fc ${CMAKE_DL_LIBS} ${PLATFORM_SPECIFIC_LIBS})
endif()
