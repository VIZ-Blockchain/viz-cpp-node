
if(ENABLE_MONGO_PLUGIN)

  # Project has no direct dependencies on libmongoc but its shared libraries
  # will need to be present at runtime for the C++ libraries we use:
  # libbsoncxx & libmongocxx (both from github.com/mongodb/mongo-cxx-driver)

  # The *.cmake package files provided by mongo-cxx-driver don't give us the
  # absolute path to the libraries, which is needed whenever they are not
  # installed in system-known locations.  CMake requires the absolute paths
  # in target_link_libraries() since we are builiding an archive and the
  # link step for all executables using this archive must include the
  # mongo-cxx-driver libraries libmongocxx and libbsoncxx.

  find_package(libmongoc-1.0 1.8)

  if (libmongoc-1.0_FOUND)

    message("MongoDB found, building plugin...")

    find_package(libbsoncxx REQUIRED)
    find_package(libmongocxx REQUIRED)

    set(CURRENT_TARGET mongo_db)

    list(APPEND CURRENT_TARGET_HEADERS
      include/graphene/plugins/mongo_db/mongo_db_plugin.hpp
      include/graphene/plugins/mongo_db/mongo_db_writer.hpp
      include/graphene/plugins/mongo_db/mongo_db_operations.hpp
      include/graphene/plugins/mongo_db/mongo_db_state.hpp
      include/graphene/plugins/mongo_db/mongo_db_types.hpp
      )

    list(APPEND CURRENT_TARGET_SOURCES
      mongo_db_plugin.cpp
      mongo_db_writer.cpp
      mongo_db_operations.cpp
      mongo_db_state.cpp
      mongo_db_types.cpp
      )

    if(BUILD_SHARED_LIBRARIES)
    add_library(graphene_${CURRENT_TARGET} SHARED
        ${CURRENT_TARGET_HEADERS}
        ${CURRENT_TARGET_SOURCES}
        )
    else()
    add_library(graphene_${CURRENT_TARGET} STATIC
        ${CURRENT_TARGET_HEADERS}
        ${CURRENT_TARGET_SOURCES}
      )
    endif()

    add_library(graphene::${CURRENT_TARGET} ALIAS graphene_${CURRENT_TARGET})
    set_property(TARGET graphene_${CURRENT_TARGET} PROPERTY EXPORT_NAME ${CURRENT_TARGET})

    target_link_libraries(
      graphene_${CURRENT_TARGET}
      graphene::json_rpc
      graphene_chain
      graphene::chain_plugin
      graphene::follow
      appbase
      fc
      ${LIBBSONCXX_LIBRARIES}
      ${LIBMONGOCXX_LIBRARIES}
    )
    target_include_directories(graphene_${CURRENT_TARGET} PUBLIC "include" ${LIBMONGOCXX_INCLUDE_DIRS} ${LIBBSONCXX_INCLUDE_DIRS})

    install(TARGETS
      graphene_${CURRENT_TARGET}

      RUNTIME DESTINATION bin
      LIBRARY DESTINATION lib
      ARCHIVE DESTINATION lib
    )
    install( FILES ${HEADERS} DESTINATION "include/graphene/plugins/mongo_db" )
  else()
    message("Mongo drivers NOT found. Use proper Dockerfile")
  endif()
endif()