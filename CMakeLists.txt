cmake_minimum_required(VERSION 3.27)

# Setup Project
project(VoxelEngine)

find_package(Vulkan REQUIRED)

if(UNIX)
  find_package(glfw3 REQUIRED)
endif()

# Set C++ Standard
set(CMAKE_CXX_STANDARD 20)

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

# Change the output directory
set(binaryDir "/bin")
set(outputDirectory "${PROJECT_SOURCE_DIR}${binaryDir}")

if(CMAKE_BUILD_TYPE MATCHES Debug)
  message("Debug Build")
  set(outputDirectory "${outputDirectory}/debug")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
  add_compile_definitions(DEBUG)
elseif(CMAKE_BUILD_TYPE MATCHES Release)
  message("Release Build")
  set(outputDirectory "${outputDirectory}/release")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()

add_compile_definitions(GLFW_FORCE_DEPTH_ZERO_TO_ONE GLFW_INCLUDE_VULKAN)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${outputDirectory})

# Setup build flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++2a -Wall -Wextra -Wpedantic \
-Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable")

add_subdirectory(vendor)

# Add all files
file(GLOB_RECURSE projectFiles CONFIGURE_DEPENDS "src/*.cpp" "src/*.hpp")
add_executable(${PROJECT_NAME} ${projectFiles})

target_include_directories(
  ${PROJECT_NAME} PUBLIC ${Vulkan_INCLUDE_DIR}
                         vendor/VulkanUtilityLibraries/include)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE ${linker}
  PUBLIC glfw Vulkan::Vulkan glm::glm vk-bootstrap::vk-bootstrap
         # STB
         GPUOpen::VulkanMemoryAllocator spdlog
         Imgui
)

set(InputRes "${PROJECT_SOURCE_DIR}/res")
set(OutputRes "${outputDirectory}/res")

# Enable Resource Building

add_custom_target(Resources ALL COMMENT "Copying Resources")

# Disable Resource Building

# add_custom_target(Resources COMMENT "Copying Resources")

add_custom_command(
  TARGET Resources
  PRE_LINK
  COMMAND cmake -E make_directory "${OutputRes}/shaders/"
  COMMENT "Create resource folder at ${OutputRes}")

file(GLOB RESOURCES_VERTEX ${InputRes}/shaders/*.vert.glsl)
foreach(file ${RESOURCES_VERTEX})
  get_filename_component(name ${file} NAME_WLE)
  add_custom_command(
    TARGET Resources
    PRE_LINK
    COMMAND glslc -fshader-stage=vert -o ${InputRes}/shaders/${name}.spv
            ${InputRes}/shaders/${name}.glsl
    COMMENT "Compiled ${name}")
endforeach()

# file(GLOB RESOURCES_GEOMETRY ${InputRes}/shaders/*.geo.glsl) foreach(file
# ${RESOURCES_GEOMETRY}) get_filename_component(name ${file} NAME_WLE)
# add_custom_command( TARGET Resources POST_BUILD COMMAND glslc
# -fshader-stage=geom -o ${InputRes}/shaders/${name}.spv
# ${InputRes}/shaders/${name}.glsl COMMENT "Compiled ${name}") endforeach()

file(GLOB RESOURCES_FRAGMENT ${InputRes}/shaders/*.frag.glsl)
foreach(file ${RESOURCES_FRAGMENT})
  get_filename_component(name ${file} NAME_WLE)
  add_custom_command(
    TARGET Resources
    PRE_LINK
    COMMAND glslc -fshader-stage=frag -o ${InputRes}/shaders/${name}.spv
            ${InputRes}/shaders/${name}.glsl
    COMMENT "Compiled ${name}")
endforeach()

file(GLOB RESOURCES_COMPUTE ${InputRes}/shaders/*.comp.glsl)
foreach(file ${RESOURCES_COMPUTE})
  get_filename_component(name ${file} NAME_WLE)
  add_custom_command(
    TARGET Resources
    PRE_LINK
    COMMAND glslc -fshader-stage=comp -o ${InputRes}/shaders/${name}.spv
            ${InputRes}/shaders/${name}.glsl
    COMMENT "Compiled ${name}")
endforeach()

file(GLOB RESOURCES_COMPILED_SHADERS "${InputRes}/shaders/*.spv")

add_custom_command(
  TARGET Resources
  POST_BUILD
  COMMAND cmake -E copy ${RESOURCES_COMPILED_SHADERS} "${OutputRes}/shaders"
  COMMENT "Copied Shaders
from ${InputRes}/shaders/ To ${OutputRes}/shaders/")

# add_custom_command( TARGET Resources POST_BUILD COMMAND cmake -E
# copy_directory_if_different "${InputRes}/textures" "${OutputRes}/textures"
# COMMENT "Copied Textures from ${InputRes}/textures/ To ${OutputRes}/textures/"
# )
