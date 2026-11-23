#pragma once
#include "shared/movement/movement_model.hpp"
#include <cmath>
#include <stdexcept>
namespace fixtures {
using namespace netarena;
inline MapDefinition map() { return {"arena",1,{{0,0},{20,20}},{},{{2,2},{2,3},{3,2},{3,3}}}; }
inline std::vector<PlayerProfile> players() { return {{PlayerId{"a"},"Alice"},{PlayerId{"b"},"Bob"}}; }
inline bool near(float a,float b) { return std::abs(a-b)<0.0001F; }
template<class F> bool throws(F f) { try { f(); } catch(const std::invalid_argument&) { return true; } return false; }
}
