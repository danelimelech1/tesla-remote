cmake_minimum_required(VERSION 3.20)
project(TeslaRemote LANGUAGES CXX)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Windows definitions ───────────────────────────────────────────────────────
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        BOOST_ASIO_STANDALONE
        BOOST_ERROR_CODE_HEADER_ONLY
        BOOST_SYSTEM_NO_LIB
        BOOST_DATE_TIME_NO_LIB
        BOOST_REGEX_NO_LIB
    )
endif()

if(MSVC)
    add_compile_options(/MP /W3 /wd4996 /wd4267 /wd4244)
endif()

# ── Dependencies (vcpkg supplies these) ──────────────────────────────────────
find_package(Boost      REQUIRED)
find_package(glfw3      CONFIG   REQUIRED)
find_package(imgui      CONFIG   REQUIRED)
find_package(Stb        REQUIRED)
find_package(OpenGL     REQUIRED)

# ── Sub-projects ──────────────────────────────────────────────────────────────
add_subdirectory(common)
add_subdirectory(client)
add_subdirectory(server)
