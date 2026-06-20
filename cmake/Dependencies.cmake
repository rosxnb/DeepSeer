include(FetchContent)

# --- System dependencies ---
find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)


# --- FetchContent dependencies ---

# llhttp - Node.js HTTP parser
FetchContent_Declare(llhttp
    GIT_REPOSITORY https://github.com/nodejs/llhttp.git
    GIT_TAG        release/v9.2.1
    GIT_SHALLOW    TRUE
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(llhttp)

# GoogleTest - testing
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
