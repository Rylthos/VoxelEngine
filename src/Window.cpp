#include "Window.hpp"

#include <spdlog/spdlog.h>

#include "VkCheck.hpp"

Window::Window() {}
Window::~Window()
{
    spdlog::info("Destroying GLFW");
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Window::create(const char* title, int winX, int winY)
{
    m_WindowSize = { winX, winY };
    initGLFW();
    initWindow(title);

    spdlog::info("Created GLFW instance and Window");
}

void Window::pollInput() { glfwPollEvents(); }
void Window::swapBuffes() { glfwSwapBuffers(m_Window); }
bool Window::shouldClose() { return glfwWindowShouldClose(m_Window); }
VkSurfaceKHR Window::createSurface(VkInstance instance)
{
    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(instance, m_Window, nullptr, &surface));
    return surface;
}

void Window::initGLFW()
{
    if (!glfwInit())
    {
        spdlog::error("Failed to initialize GLFW");
        exit(-1);
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    glfwWindowHintString(GLFW_X11_CLASS_NAME, "GLFW");
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "GLFW");
}

void Window::initWindow(const char* title)
{
    m_Window = glfwCreateWindow(m_WindowSize.x, m_WindowSize.y, title, nullptr, nullptr);
    if (!m_Window)
    {
        spdlog::error("Failed to create window");
        exit(-1);
    }

    glfwSwapInterval(0);

    glfwSetKeyCallback(m_Window, Window::keyCallback);
}

void Window::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }
}
