#add_executable( exchange exchange.cpp )
#target_link_libraries( exchange hulk-core hulk-fix pthread )

add_executable( match match.cpp )
target_link_libraries( match hulk-core hulk-fix pthread )
