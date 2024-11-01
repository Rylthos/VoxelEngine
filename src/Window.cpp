#include "Window.hpp"

#include <spdlog/spdlog.h>

#include "VkCheck.hpp"

#include "EventHandler.hpp"
#include "Events.hpp"

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

    glfwSetWindowUserPointer(m_Window, this);

    glfwSetKeyCallback(m_Window, Window::keyCallback);
    glfwSetCursorEnterCallback(m_Window, Window::mouseEnterCallback);
    glfwSetCursorPosCallback(m_Window, Window::mouseMoveCallback);
}

void Window::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    Window* self = (Window*)glfwGetWindowUserPointer(window);
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, true);
    }
    else if (key == GLFW_KEY_LEFT_ALT && action == GLFW_PRESS)
    {
        int mode = glfwGetInputMode(window, GLFW_CURSOR);
        if (mode == GLFW_CURSOR_DISABLED)
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            self->m_MouseCaptured = false;
        }
        else
        {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            self->m_MouseCaptured = true;
        }
    }

    KeyboardInput input;
    input.key = key;
    input.scancode = scancode;
    input.action = action;
    input.mods = mods;

    EventHandler::dispatchEvent(&input);
}

void Window::mouseMoveCallback(GLFWwindow* window, double xPos, double yPos)
{
    Window* self = (Window*)glfwGetWindowUserPointer(window);

    static double previousX = 0.0f;
    static double previousY = 0.0f;

    if (self->m_FirstMouse)
    {
        previousX = xPos;
        previousY = yPos;
        self->m_FirstMouse = false;
    }

    double xDelta = xPos - previousX;
    double yDelta = previousY - yPos;

    previousX = xPos;
    previousY = yPos;

    MouseMove event;
    event.position = { xPos, yPos };
    event.delta = { xDelta, yDelta };

    event.captured = self->m_MouseCaptured;

    EventHandler::dispatchEvent(&event);
}

void Window::mouseEnterCallback(GLFWwindow* window, int entered)
{
    Window* self = (Window*)glfwGetWindowUserPointer(window);

    self->m_MouseContained = entered;

    if (!entered) self->m_FirstMouse = true;
}
