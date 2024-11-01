#pragma once

#include <glm/glm.hpp>

#include "Events.hpp"

class Camera : public EventReceiver
{
  public:
    Camera();
    Camera(glm::vec3 position);
    Camera(glm::vec3 position, float yaw, float pitch);

    void setWorldAxis(glm::vec3 worldUp, glm::vec3 worldForward, glm::vec3 worldRight);

    void receive(const Event* event) override;

    glm::vec4 getPosition() { return glm::vec4(m_Position, 0.f); }
    glm::vec4 getForward() { return glm::vec4(m_Forward, 0.f); }
    glm::vec4 getRight() { return glm::vec4(m_Right, 0.f); }
    glm::vec4 getUp() { return glm::vec4(m_Up, 0.f); }

  private:
    glm::vec3 m_Position;
    float m_Yaw;
    float m_Pitch;
    float m_MovementSpeed = 2.0f;
    float m_Speedup = 2.0f;

    glm::vec3 m_Forward;
    glm::vec3 m_Right;
    glm::vec3 m_Up;

    glm::vec3 m_WorldForward = glm::vec3(0.f, 0.f, 1.f);
    glm::vec3 m_WorldRight = glm::vec3(1.f, 0.f, 0.f);
    glm::vec3 m_WorldUp = glm::vec3(0.f, -1.f, 0.f);

  private:
    void updateAxis();
};
