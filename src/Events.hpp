#pragma once

#include <cstdint>
#include <memory>

#include "EventHandler.hpp"

enum class EventType { UNDEFINED, KEYBOARD, MOUSE };

struct Event {
    virtual ~Event() = default;
    virtual EventType getType() const { return EventType::UNDEFINED; }
};

struct KeyboardInput : public Event {
    int32_t key;
    int32_t scancode;
    int32_t action;
    int32_t mods;

    EventType getType() const override { return EventType::KEYBOARD; }
};

struct MouseInput : public Event {
    EventType getType() const override { return EventType::MOUSE; }
};

class EventReceiver
{
  public:
    virtual ~EventReceiver();

    virtual void receive(const Event* event);
};
