#include "Window.hpp"

#include <GLFW/glfw3.h>
#include <spdlog/spdlog.h>

#include "VkCheck.hpp"

#include "EventHandler.hpp"
#include "Events.hpp"

#include <imgui.h>

Window::Window() { }
Window::~Window()
{
    spdlog::info("Destroying GLFW");
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

void Window::create(const char* title, int winX, int winY)
{
    initGLFW();
    initWindow(title, winX, winY);

    spdlog::info("Created GLFW instance and Window");
}

const glm::uvec2 Window::getSize()
{
    int w, h;
    glfwGetWindowSize(m_Window, &w, &h);

    return { w, h };
}

void Window::pollInput() { glfwPollEvents(); }
void Window::swapBuffers() { glfwSwapBuffers(m_Window); }
bool Window::shouldClose() { return glfwWindowShouldClose(m_Window); }
VkSurfaceKHR Window::createSurface(VkInstance instance)
{
    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(instance, m_Window, nullptr, &surface));
    return surface;
}

void Window::initGLFW()
{
    if (!glfwInit()) {
        spdlog::error("Failed to initialize GLFW");
        exit(-1);
    }
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_FALSE);
}

void Window::initWindow(const char* title, int width, int height)
{
    m_Window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!m_Window) {
        spdlog::error("Failed to create window");
        exit(-1);
    }

    glfwSwapInterval(0);

    glfwSetWindowUserPointer(m_Window, this);

    glfwSetInputMode(m_Window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);

    glfwSetKeyCallback(m_Window, Window::keyCallback);
    glfwSetCursorEnterCallback(m_Window, Window::mouseEnterCallback);
    glfwSetCursorPosCallback(m_Window, Window::mouseMoveCallback);
    glfwSetMouseButtonCallback(m_Window, Window::mouseButtonCallback);
    glfwSetScrollCallback(m_Window, Window::mouseScrollCallback);
    glfwSetWindowSizeCallback(m_Window, Window::resizeCallback);
}

void Window::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (ImGui::GetIO().WantCaptureKeyboard)
        return;

    Window* self = (Window*)glfwGetWindowUserPointer(window);
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    } else if (key == GLFW_KEY_LEFT_ALT && action == GLFW_PRESS) {
        int mode = glfwGetInputMode(window, GLFW_CURSOR);
        if (mode == GLFW_CURSOR_DISABLED) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            self->m_MouseCaptured = false;
        } else {
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

    if (self->m_FirstMouse) {
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

void Window::mouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
    MouseButton event;
    event.leftMousePressed = button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS;
    event.leftMouseReleased = button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE;

    event.rightMousePressed = button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS;
    event.rightMouseReleased = button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE;

    EventHandler::dispatchEvent(&event);
}

void Window::mouseScrollCallback(GLFWwindow* window, double xOffset, double yOffset)
{
    MouseScroll event;
    event.xOffset = xOffset;
    event.yOffset = yOffset;

    EventHandler::dispatchEvent(&event);
}

void Window::mouseEnterCallback(GLFWwindow* window, int entered)
{
    Window* self = (Window*)glfwGetWindowUserPointer(window);

    self->m_MouseContained = entered;

    if (!entered)
        self->m_FirstMouse = true;
}

void Window::resizeCallback(GLFWwindow* window, int width, int height)
{
    WindowResize wrEvent;
    wrEvent.newWidth = width;
    wrEvent.newHeight = height;

    EventHandler::dispatchEvent(&wrEvent);
}
