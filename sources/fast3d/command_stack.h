#pragma once

#include <stack>

namespace Shipyard { namespace Fast3D {

template<class T> class CommandStack : public std::stack<T> {
    typedef std::stack<T> Base;
    // Preserve deque slot references while avoiding XDK's iterator-heavy top().
    T* current_;

    void Refresh() { current_ = Base::empty() ? 0 : &Base::top(); }

public:
    CommandStack() : current_(0) {}
    CommandStack(const CommandStack& other) : Base(other), current_(0) { Refresh(); }
    CommandStack& operator=(const CommandStack& other) {
        if (this != &other) { CommandStack copy(other); swap(copy); }
        return *this;
    }
    __forceinline T& top() { return *current_; }
    __forceinline const T& top() const { return *current_; }
    void push(const T& value) { Base::push(value); Refresh(); }
    void pop() { Base::pop(); Refresh(); }
    void swap(CommandStack& other) {
        this->c.swap(other.c);
        Refresh();
        other.Refresh();
    }
};

} }
