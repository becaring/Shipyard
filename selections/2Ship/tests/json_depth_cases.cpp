#include <nlohmann/json.hpp>
#include <sstream>
#include <iostream>
#include <string>

int main() {
    typedef nlohmann::json Json;
    const char* cases[] = {
        "{\"finalSeed\":123,\"settings\":{\"a\":[1,{\"b\":2}]},\"items\":[1,2],\"empty\":{},\"duplicate\":{\"a\":1},\"duplicate\":5}",
        "[0,[1,{\"k\":2}],{\"z\":3},[],{}]", "5", "null", "\"string\"",
        "{\"bad\":[1,]}", "{\"bad\":{\"key\" 1}}", "{\"bad\":[1e1000]}",
        "[[]] trailing", "{\"x\":1,\"x\":{\"y\":2}}"
    };
    for (size_t depth = 0; depth < 4; ++depth) {
        for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
            std::istringstream stream(cases[i]);
            try {
#ifdef REFERENCE_JSON
                Json result = Json::parse(stream, [depth](int current, Json::parse_event_t, Json&) {
                    return current <= static_cast<int>(depth);
                });
#else
                Json result = Json::parse_depth_limited(stream, depth);
#endif
                std::cout << result.dump() << '\n';
            } catch (const Json::exception& e) {
                std::cout << e.id << ':' << e.what() << '\n';
            }
        }
    }
    // The default parser path shares the same state machine.
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        try { std::cout << Json::parse(cases[i]).dump() << '\n'; }
        catch (const Json::exception& e) { std::cout << e.id << ':' << e.what() << '\n'; }
    }
}
