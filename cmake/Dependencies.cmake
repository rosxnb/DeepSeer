include(FetchContent)

# --- System dependencies ---
find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)


# --- FetchContent dependencies ---

# GoogleTest - testing
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.16.0
    GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
