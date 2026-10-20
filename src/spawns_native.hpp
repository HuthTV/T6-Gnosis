#pragma once
#include "spawns_native_decode.hpp"

namespace spawns::native {
struct Capture {State state=State::invalid;const char* reason="Native spawn reader disabled";std::shared_ptr<const Snapshot> data;};
bool startup();
void clear();
Capture collect(std::shared_ptr<const nav::Graph> map);
}
