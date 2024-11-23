#pragma once

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "EventHandler.hpp"

class Window
{
  public:
    Window();
    Window(Window&) = delete;
    Window(Window&&) = delete;
    ~Window();

    void create(const char* title, int winX, int winY);

    GLFWwindow* get() { return m_Window; }
    const glm::uvec2 getSize();

    void pollInput();
    void swapBuffes();

    bool shouldClose();

    VkSurfaceKHR createSurface(VkInstance instance);

  private:
    GLFWwindow* m_Window;

    bool m_MouseContained = false;
    bool m_MouseCaptured = false;
    bool m_FirstMouse = true;

  private:
    void initGLFW();
    void initWindow(const char* title, int width, int height);

    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    static void mouseEnterCallback(GLFWwindow* window, int entered);
    static void resizeCallback(GLFWwindow* window, int width, int height);
};
