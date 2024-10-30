#pragma once

#include <deque>
#include <functional>

class DeletionQueue
{
  private:
    std::deque<std::function<void()>> m_Deletors;

  public:
    DeletionQueue() {}
    ~DeletionQueue() { flush(); }
    DeletionQueue(DeletionQueue&) = delete;
    DeletionQueue(DeletionQueue&&) = delete;

    void pushFunction(std::function<void()>&& function) { m_Deletors.push_back(function); }

    void flush()
    {
        for (auto it = m_Deletors.rbegin(); it != m_Deletors.rend(); it++)
        {
            (*it)();
        }
        m_Deletors.clear();
    }
};
