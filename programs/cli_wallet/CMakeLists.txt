add_executable(cli_wallet main.cpp)
if(UNIX AND NOT APPLE)
    set(rt_library rt)
endif()
if(APPLE)
    list(APPEND PLATFORM_SPECIFIC_LIBS readline)
endif()
find_package(Gperftools QUIET)
if(GPERFTOOLS_FOUND)
    message(STATUS "Found gperftools; compiling cli_wallet with TCMalloc")
    list(APPEND PLATFORM_SPECIFIC_LIBS tcmalloc)
endif()
FIND_PACKAGE(Boost REQUIRED COMPONENTS regex)

target_link_libraries(
        cli_wallet
        PRIVATE
        graphene_network
        graphene_chain
        graphene_protocol
        graphene_utilities
        graphene_wallet
        graphene::database_api
        graphene::account_history
        graphene::network_broadcast_api
        graphene::validator_api
        fc
        ${READLINE_LIBRARIES}
        ${CMAKE_DL_LIBS}
        ${Boost_LIBRARIES}
        ${PLATFORM_SPECIFIC_LIBS}
)

if(MINGW)
    target_link_libraries(cli_wallet PRIVATE
        -Wl,--push-state,--start-group
        bcrypt
<<<<<<< HEAD
<<<<<<< HEAD
<<<<<<< HEAD
        -Wl,--pop-state
=======
        -Wl,--pop-state,--end-group
>>>>>>> 8086b055 (build: update CMake configuration for MinGW UCRT64 and add Windows CI workflow)
=======
        -Wl,--pop-state
>>>>>>> 281f9a1f (build: fix Windows CI workflow branch name and update cli_wallet build steps)
=======
        -Wl,--pop-state
>>>>>>> 09eb8f13b00e6685bb818b2233f2eef8900925db
    )
endif()

if(MSVC)
    set_source_files_properties(main.cpp PROPERTIES COMPILE_FLAGS "/bigobj")
endif(MSVC)

install(TARGETS cli_wallet
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)