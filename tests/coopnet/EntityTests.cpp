#include "../../src/CoopNet/EntityRegistry.h"
#include <iostream>
using namespace coopnet;
void check(bool value, int line) {
    if (!value) { std::cerr << "Entity test failed at " << line << '\n'; std::exit(1); }
}
#define require(value) check((value), __LINE__)
int main() {
    EntityRegistry registry(3);
    auto first = registry.create(), second = registry.create();
    require(registry.bind(first, {10,0}) && registry.find_engine({10,0}) == first);
    require(!registry.bind(second, {10,0}));
    require(registry.bind(second, {11,0})); // native IDs can repeat across level incarnations
    require(registry.matches(first, 1, 10) && !registry.matches(first, 1, 11));
    require(!registry.unbind(first, 2) && !registry.erase(first));
    registry.unload(10);
    require(!registry.matches(first, 1, 10) && registry.matches(second, 1, 11));
    require(registry.bind(first, {11,7}) && registry.matches(first, 2, 11));
    require(!registry.unbind(first, 1)); // delayed destroy from old incarnation
    require(registry.unbind(first, 2) && registry.erase(first));
    auto third = registry.create(); require(third > second);
    require(!registry.bind(third, {11,0xffff}) && !registry.bind(third, {0,0}));
    auto fourth = registry.create(); require(fourth > third);
    bool full = false;
    try { registry.create(); } catch (const std::length_error&) { full = true; }
    require(full);
    std::cout << "CoopNet entity mapping, lifecycle generation and level isolation tests passed\n";
}
