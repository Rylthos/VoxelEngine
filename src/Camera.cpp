#include "Camera.hpp"

#include <glm/gtx/string_cast.hpp>
#include <spdlog/spdlog.h>

#include <GLFW/glfw3.h>

#include <algorithm>

#include <imgui.h>

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
    static std::map<uint32_t, bool> m_PressedKeys;

    switch (event->getType())
    {
    case EventType::KeyboardInput:
        {
            const KeyboardInput* ki = reinterpret_cast<const KeyboardInput*>(event);

            m_PressedKeys[ki->key] = (ki->action != GLFW_RELEASE);

            break;
        }
    case EventType::MouseMove:
        {
            const MouseMove* mi = reinterpret_cast<const MouseMove*>(event);
            if (!mi->captured) break;

            const float mouseSensitivity = 0.5;
            m_Yaw -= mi->delta.x * mouseSensitivity;
            m_Pitch += mi->delta.y * mouseSensitivity;
            m_Pitch = std::clamp(m_Pitch, -89.9f, 89.9f);

            updateAxis();

            break;
        }
    case EventType::GameUpdate:
        {
            const GameUpdate* gu = reinterpret_cast<const GameUpdate*>(event);

            glm::vec3 direction{ 0.f };
            float speed = m_MovementSpeed;

            glm::vec3 movementAxis = { 1.f, 1.f, 1.f };
            if (m_LockXZPlaneMovement) movementAxis.y = 0.0f;

            if (m_PressedKeys[GLFW_KEY_W]) direction += m_Forward * movementAxis;
            if (m_PressedKeys[GLFW_KEY_S]) direction -= m_Forward * movementAxis;
            if (m_PressedKeys[GLFW_KEY_A]) direction -= m_Right * movementAxis;
            if (m_PressedKeys[GLFW_KEY_D]) direction += m_Right * movementAxis;
            if (m_PressedKeys[GLFW_KEY_SPACE]) direction += m_WorldUp;
            if (m_PressedKeys[GLFW_KEY_LEFT_CONTROL]) direction -= m_WorldUp;
            if (m_PressedKeys[GLFW_KEY_LEFT_SHIFT]) speed *= m_Speedup;

            m_Position += direction * speed * gu->frameDelta;

            break;
        }
    case EventType::ImGuiRender:
        {
            if (ImGui::Begin("Camera"))
            {
                glm::vec3 camPos = getPosition();
                ImGui::Text("Camera Position");
                ImGui::Text("X: %.3f Y: %.3f Z: %.3f", camPos.x, camPos.y, camPos.z);

                glm::vec3 camForward = getForward();
                ImGui::Text("Camera Forward");
                ImGui::Text("X: %.3f Y: %.3f Z: %.3f", camForward.x, camForward.y, camForward.z);

                glm::vec3 camRight = getRight();
                ImGui::Text("Camera Right");
                ImGui::Text("X: %.3f Y: %.3f Z: %.3f", camRight.x, camRight.y, camRight.z);

                glm::vec3 camUp = getUp();
                ImGui::Text("Camera Up");
                ImGui::Text("X: %.3f Y: %.3f Z: %.3f", camUp.x, camUp.y, camUp.z);

                // ImGui::Text("Lock XZ Movement");
                ImGui::Checkbox("Lock XZ Movement", &m_LockXZPlaneMovement);

                ImGui::Text("Speedup Movement Speed");
                ImGui::SliderFloat("##MovementSpeed", &m_Speedup, 0.5f, 15.0f);
            }
            ImGui::End();
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
