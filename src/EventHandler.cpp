#include "EventHandler.hpp"

#include "Events.hpp"

std::map<EventType, std::set<EventReceiver*>> EventHandler::s_Receivers;

void EventHandler::dispatchEvent(const Event* event)
{
    EventType type = event->getType();
    auto it = s_Receivers.find(type);

    if (it == s_Receivers.end()) return;

    std::set<EventReceiver*>& receivers = it->second;
    for (EventReceiver* receiver : receivers)
    {
        receiver->receive(event);
    }
}

void EventHandler::subscribe(EventType event, EventReceiver* receiver)

{
    s_Receivers[event].insert(receiver);
}

void EventHandler::subscribe(std::initializer_list<EventType> events, EventReceiver* receiver)
{
    for (EventType event : events)
    {
        subscribe(event, receiver);
    }
}
