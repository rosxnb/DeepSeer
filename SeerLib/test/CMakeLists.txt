include(GoogleTest)

function(seer_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ${SEER_LIB} GTest::gtest_main)
    deepseer_set_compiler_options(${name})
    deepseer_enable_sanitizers(${name})
    gtest_discover_tests(${name})
endfunction()

seer_add_test(TestEngine        TestEngine.cpp)
seer_add_test(TestMagikaModel   TestMagikaModel.cpp)
