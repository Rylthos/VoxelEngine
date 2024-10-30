set(GLM_QUIET
    OFF
    CACHE BOOL "" FORCE)
set(GLM_TEST_ENABLE
    OFF
    CACHE BOOL "" FORCE)
add_subdirectory(glm)

set(VK_BOOTSTRAP_DISABLE_WARNINGS
    ON
    CACHE BOOL "" FORCE)

add_subdirectory(vk-bootstrap)

add_subdirectory(VMA)
target_compile_options(
  VulkanMemoryAllocator BEFORE
  INTERFACE -Wno-nullability-extension -Wno-nullability-completeness
            -Wno-missing-field-initializers -Wno-unused-function
            -Wno-unused-private-field)

add_subdirectory(VulkanUtilityLibraries)

add_subdirectory(spdlog)

project(Imgui)
file(
  GLOB
  IMGUI_FILES
  CONFIGURE_DEPENDS
  "imgui/*.h"
  "imgui/*.cpp"
  "imgui/backends/imgui_impl_glfw.*"
  "imgui/backends/imgui_impl_vulkan.*")
add_library(Imgui STATIC ${IMGUI_FILES})
add_compile_definitions(GLFW_INCLUDE_NONE)
target_include_directories(Imgui PUBLIC "imgui/" "imgui/backends")
set_target_properties(Imgui PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(Imgui PUBLIC glfw Vulkan::Vulkan)
