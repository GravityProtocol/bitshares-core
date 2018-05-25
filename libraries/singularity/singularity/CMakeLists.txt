file(GLOB HEADERS "include/graphene/singularity/*.hpp")

set(SOURCES ncd_aware_rank.cpp
            activity_index_calculator.cpp
            scan.cpp
            utils.cpp
            emission.cpp
            gravity_index_calculator.cpp
   )

add_library( graphene_singularity ${SOURCES} ${HEADERS} )

target_link_libraries( graphene_singularity 
  PUBLIC fc graphene_db )
target_include_directories( graphene_singularity 
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_SOURCE_DIR}/libraries/chain/include"
)

if(MSVC)
  set_source_files_properties( node.cpp PROPERTIES COMPILE_FLAGS "/bigobj" )
endif(MSVC)

if (USE_PCH)
  set_target_properties(graphene_singularity PROPERTIES COTIRE_ADD_UNITY_BUILD FALSE)
  cotire(graphene_singularity)
endif(USE_PCH)

install( TARGETS
   graphene_singularity

   RUNTIME DESTINATION bin
   LIBRARY DESTINATION lib
   ARCHIVE DESTINATION lib
)
install( FILES ${HEADERS} DESTINATION "include/graphene/singularity" )
