set(GLSL_VALIDATOR "$ENV{VULKAN_SDK}/bin/glslangValidator")

file(GLOB SHADERS "${CMAKE_SOURCE_DIR}/src/shaders/*.comp"
                  "${CMAKE_SOURCE_DIR}/src/shaders/*.vert"
                  "${CMAKE_SOURCE_DIR}/src/shaders/*.frag"
)

foreach(GLSL ${SHADERS})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
        COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
        DEPENDS ${GLSL}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(
    shaders
    DEPENDS ${SPIRV_BINARY_FILES}
)