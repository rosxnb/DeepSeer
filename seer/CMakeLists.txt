# Seer — AI inference library for DeepSeer
# Produces a static library with zero dependency on the proxy core.

set(SEER_LIB Seer)

add_library(${SEER_LIB} STATIC
    src/Engine.cpp
    src/Magika/MagikaEngine.cpp
    src/Magika/MagikaModel.cpp
)

target_include_directories(${SEER_LIB}
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(${SEER_LIB}
    PUBLIC Threads::Threads
)

deepseer_set_compiler_options(${SEER_LIB})
deepseer_enable_sanitizers(${SEER_LIB})

# --- Tests ---
if(DEEPSEER_BUILD_TESTS)
    include(GoogleTest)
    add_executable(TestEngine test/TestEngine.cpp)
    target_link_libraries(TestEngine PRIVATE ${SEER_LIB} GTest::gtest_main)
    deepseer_set_compiler_options(TestEngine)
    deepseer_enable_sanitizers(TestEngine)
    gtest_discover_tests(TestEngine)

    add_executable(TestMagikaModel test/TestMagikaModel.cpp)
    target_link_libraries(TestMagikaModel PRIVATE ${SEER_LIB} GTest::gtest_main)
    deepseer_set_compiler_options(TestMagikaModel)
    deepseer_enable_sanitizers(TestMagikaModel)
    gtest_discover_tests(TestMagikaModel WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
