include(GoogleTest)

function(deepseer_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE DeepSeerLib GTest::gtest_main)
    deepseer_set_compiler_options(${name})
    deepseer_enable_sanitizers(${name})
    gtest_discover_tests(${name})
endfunction()

deepseer_add_test(TestEventLoop event/TestEventLoop.cpp)
