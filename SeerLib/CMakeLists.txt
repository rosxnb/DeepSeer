# SeerLib — AI inference library for DeepSeer
# Produces a static library with zero dependency on the proxy core.

set(SEER_LIB SeerLib)

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

if(DEEPSEER_BUILD_TESTS)
    add_subdirectory(test)
endif()
