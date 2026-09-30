#ifndef SHIPYARD_JSON_ORDERED_PROJECTION_HPP
#define SHIPYARD_JSON_ORDERED_PROJECTION_HPP
#include <nlohmann/json.hpp>
// Compiler-facing lowering helper for explicit ordered_json::object({...}).
// This is not an extra nlohmann API. Aggregate array initialization evaluates
// every member first, and emplace preserves the pinned first duplicate value.
namespace ShipyardJsonProjection {
struct OrderedMember { std::string key; nlohmann::ordered_json value; };
inline nlohmann::ordered_json ordered_object(const OrderedMember *members, size_t count) {
    nlohmann::ordered_json result = nlohmann::ordered_json::object();
    for (size_t i=0;i<count;++i) result.emplace(members[i].key,members[i].value);
    return result;
}
}
#endif
