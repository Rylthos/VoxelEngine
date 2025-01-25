#pragma once

#include <concepts>
#include <optional>
#include <set>

template <typename T>
concept IntervalType = std::is_integral<T>::value;

template <IntervalType T> class IntervalList {
    typedef std::pair<T, T> Interval;

  public:
    IntervalList() { }
    ~IntervalList() { }

    void addInterval(T element) { addInterval(element, element); }
    void addInterval(Interval v) { addInterval(v.first, v.second); }
    void addInterval(T v1, T v2)
    {
        T lower = std::min(v1, v2);
        T higher = std::max(v1, v2);

        std::set<Interval> toBeRemoved;
        for (const auto& p : m_Intervals) {
            if (doIntersect({ lower - 1, higher + 1 }, p)) {
                lower = std::min(lower, p.first);
                higher = std::max(higher, p.second);
                toBeRemoved.insert(p);
            }
        }

        for (const auto& p : toBeRemoved) {
            m_Intervals.erase(p);
        }

        m_Intervals.insert(std::make_pair(lower, higher));
    }

    void removeInterval(T element) { removeInterval(element, element); }
    void removeInterval(Interval v) { removeInterval(v.first, v.second); }
    void removeInterval(T v1, T v2)
    {
        T lower = std::min(v1, v2);
        T higher = std::max(v1, v2);

        std::set<Interval> toRemove;
        std::set<Interval> toInsert;
        for (const auto& p : m_Intervals) {
            if (doIntersect({ lower, higher }, p)) {

                if (p.first < lower) {
                    Interval lowerInterval = std::make_pair(p.first, lower - 1);
                    toInsert.insert(lowerInterval);
                }
                if (p.second > higher) {
                    Interval higherInterval = std::make_pair(higher + 1, p.second);
                    toInsert.insert(higherInterval);
                }

                toRemove.insert(p);
            }
        }

        for (const auto& p : toRemove) {
            m_Intervals.erase(p);
        }

        for (const auto& p : toInsert) {
            addInterval(p);
        }
    }

    std::set<Interval> getIntervals() { return m_Intervals; }

    std::optional<Interval> getFirstGreater(T size)
    {
        for (const auto& p : m_Intervals) {
            if (sizeOfInterval(p) >= size) {
                return p;
            }
        }
        return {};
    }

    void clearIntervals() { m_Intervals.clear(); }
    T totalFree()
    {
        T size = 0;
        for (const auto& p : m_Intervals) {
            size += sizeOfInterval(p);
        }
        return size;
    }

    T sizeOfInterval(Interval a) { return a.second - a.first + 1; }

  private:
    std::set<Interval> m_Intervals;

  private:
    bool doIntersect(const Interval& i, const Interval& j)
    {
        return ((i.first >= j.first && i.first <= j.second)
            || (i.second >= j.first && i.second <= j.second)
            || (i.first <= j.first && i.second >= j.second));
    }
};
