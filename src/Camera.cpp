#include "Camera.hpp"

#include <glm/gtx/string_cast.hpp>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

Camera::Camera() : m_Position{ 0.f, 0.f, 0.f }, m_Yaw{ 0.f }, m_Pitch{ 0.f } { updateAxis(); }

Camera::Camera(glm::vec3 position) : m_Position{ position }, m_Yaw{ 0.f }, m_Pitch{ 0.f }
{
    updateAxis();
}

Camera::Camera(glm::vec3 position, float yaw, float pitch)
    : m_Position{ position }, m_Yaw{ yaw }, m_Pitch{ pitch }
{
    updateAxis();
}

void Camera::setWorldAxis(glm::vec3 worldUp, glm::vec3 worldForward, glm::vec3 worldRight)
{
    m_WorldUp = worldUp;
    m_WorldRight = worldRight;
    m_WorldForward = worldForward;

    updateAxis();
}

void Camera::receive(const Event* event)
{
    switch (event->getType())
    {
    case EventType::KEYBOARD:
        {
            const KeyboardInput* ki = reinterpret_cast<const KeyboardInput*>(event);

            if (ki->key == GLFW_KEY_S && ki->action == GLFW_PRESS)
            {
                m_Position -= m_Forward * m_MovementSpeed;
            }
            else if (ki->key == GLFW_KEY_W && ki->action == GLFW_PRESS)
            {
                m_Position += m_Forward * m_MovementSpeed;
            }
            else if (ki->key == GLFW_KEY_A && ki->action == GLFW_PRESS)
            {
                m_Position -= m_Right * m_MovementSpeed;
            }
            else if (ki->key == GLFW_KEY_D && ki->action == GLFW_PRESS)
            {
                m_Position += m_Right * m_MovementSpeed;
            }
            else if (ki->key == GLFW_KEY_SPACE && ki->action == GLFW_PRESS)
            {
                m_Position += m_WorldUp * m_MovementSpeed;
            }
            else if (ki->key == GLFW_KEY_LEFT_CONTROL && ki->action == GLFW_PRESS)
            {
                m_Position -= m_WorldUp * m_MovementSpeed;
            }

            break;
        }
    default:
        break;
    }
}

void Camera::updateAxis()
{
    float pitch = glm::radians(m_Pitch + 90.0f);
    float yaw = glm::radians(m_Yaw + 90.0f);
    m_Forward.x = glm::sin(pitch) * glm::cos(yaw);
    m_Forward.y = glm::cos(pitch);
    m_Forward.z = glm::sin(pitch) * glm::sin(yaw);

    m_Forward = glm::normalize(m_Forward);

    m_Right = glm::normalize(glm::cross(m_Forward, m_WorldUp));
    m_Up = glm::normalize(glm::cross(m_Right, m_Forward));
}
