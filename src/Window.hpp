#pragma once

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

class Window
{
  private:
    glm::uvec2 m_WindowSize;
    GLFWwindow* m_Window;

  public:
    Window();
    Window(Window&) = delete;
    Window(Window&&) = delete;
    ~Window();

    void create(const char* title, int winX, int winY);

    GLFWwindow* get() { return m_Window; }
    glm::uvec2 getSize() { return m_WindowSize; }

    void pollInput();
    void swapBuffes();

    bool shouldClose();

    VkSurfaceKHR createSurface(VkInstance instance);

  private:
    void initGLFW();
    void initWindow(const char* title);

    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};
