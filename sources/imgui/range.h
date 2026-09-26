#ifndef SHIPYARD_IMGUI_RANGE_H
#define SHIPYARD_IMGUI_RANGE_H

#include <stddef.h>

template <class Sequence>
inline typename Sequence::iterator ShipyardImGuiBegin(Sequence& value) {
    return value.begin();
}
template <class Sequence>
inline typename Sequence::iterator ShipyardImGuiEnd(Sequence& value) {
    return value.end();
}
template <class Sequence>
inline typename Sequence::const_iterator ShipyardImGuiBegin(const Sequence& value) {
    return value.begin();
}
template <class Sequence>
inline typename Sequence::const_iterator ShipyardImGuiEnd(const Sequence& value) {
    return value.end();
}
template <class T, size_t Count>
inline T* ShipyardImGuiBegin(T (&value)[Count]) {
    return value;
}
template <class T, size_t Count>
inline T* ShipyardImGuiEnd(T (&value)[Count]) {
    return value + Count;
}

#endif
