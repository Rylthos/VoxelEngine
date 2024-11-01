#pragma once

#include <glm/glm.hpp>

#include <cstdint>

#include "EventHandler.hpp"

enum class EventType { UNDEFINED, KeyboardInput, MouseMove, GameUpdate, GameRender };

struct Event {
    virtual ~Event() = default;
    virtual EventType getType() const { return EventType::UNDEFINED; }
};

struct KeyboardInput : public Event {
    int32_t key;
    int32_t scancode;
    int32_t action;
    int32_t mods;

    EventType getType() const override { return EventType::KeyboardInput; }
};

struct MouseMove : public Event {
    EventType getType() const override { return EventType::MouseMove; }

    glm::vec2 position;
    glm::vec2 delta;
    bool captured;
};

struct GameUpdate : public Event {
    EventType getType() const override { return EventType::GameUpdate; }

    float frameDelta;
};

struct GameRender : public Event {
    EventType getType() const override { return EventType::GameRender; }

    float frameDelta;
};

class EventReceiver
{
  public:
    virtual ~EventReceiver();

    virtual void receive(const Event* event);
};
