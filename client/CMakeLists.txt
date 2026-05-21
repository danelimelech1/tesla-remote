add_executable(TeslaClient main.cpp)

target_link_libraries(TeslaClient PRIVATE
    common
    Boost::headers
    glfw
    imgui::imgui
    OpenGL::GL
    gdi32
    user32
)

target_include_directories(TeslaClient PRIVATE ${Stb_INCLUDE_DIR})

if(MSVC)
    target_compile_options(TeslaClient PRIVATE /W3 /wd4996 /wd4267 /wd4244)
endif()
