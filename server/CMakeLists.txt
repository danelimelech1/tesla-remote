add_executable(TeslaServer main.cpp)

target_link_libraries(TeslaServer PRIVATE
    common
    Boost::headers
    glfw
    imgui::imgui
    OpenGL::GL
    user32
)

target_include_directories(TeslaServer PRIVATE ${Stb_INCLUDE_DIR})

if(MSVC)
    target_compile_options(TeslaServer PRIVATE /W3 /wd4996 /wd4267 /wd4244)
endif()
