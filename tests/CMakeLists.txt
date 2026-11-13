# One Qt Test executable per test file, registered with CTest.
function(ppcb_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE pocketpcb_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

ppcb_add_test(test_units)
ppcb_add_test(test_grid)
