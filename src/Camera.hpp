#pragma once

#include <glm/glm.hpp>

#include "Events.hpp"

class Camera : public EventReceiver {
  public:
    Camera();
    Camera(glm::vec3 position);
    Camera(glm::vec3 position, float yaw, float pitch);

    void setWorldAxis(glm::vec3 worldUp, glm::vec3 worldForward, glm::vec3 worldRight);

    void receive(const Event* event) override;

    glm::vec3 getPosition() { return m_Position; }
    glm::vec3 getForward() { return m_Forward; }
    glm::vec3 getRight() { return m_Right; }
    glm::vec3 getUp() { return m_Up; }

  private:
    glm::vec3 m_Position;
    float m_Yaw;
    float m_Pitch;
    float m_MovementSpeed = 10.0f;
    float m_MovementMultiplier = 5.0f;

    glm::vec3 m_Forward;
    glm::vec3 m_Right;
    glm::vec3 m_Up;

    glm::vec3 m_WorldForward = glm::vec3(0.f, 0.f, 1.f);
    glm::vec3 m_WorldRight = glm::vec3(1.f, 0.f, 0.f);
    glm::vec3 m_WorldUp = glm::vec3(0.f, -1.f, 0.f);

    bool m_LockXZPlaneMovement = true;

  private:
    void updateAxis();
};
