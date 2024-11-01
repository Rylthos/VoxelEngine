#pragma once

#include <memory>

#include <initializer_list>
#include <map>
#include <set>

enum class EventType;
struct Event;
class EventReceiver;

class EventHandler
{
  public:
    static void dispatchEvent(const Event* event);

    static void subscribe(EventType event, EventReceiver* receiver);
    static void subscribe(std::initializer_list<EventType> events, EventReceiver* receiver);

  private:
    EventHandler() {}

  private:
    static std::map<EventType, std::set<EventReceiver*>> s_Receivers;
};
