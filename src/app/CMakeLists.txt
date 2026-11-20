add_executable(pocketpcb main.cpp)
target_link_libraries(pocketpcb PRIVATE pocketpcb_gui)
install(TARGETS pocketpcb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
