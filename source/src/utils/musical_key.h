#pragma once
#include <libzfork/libzfork.h>
#include <cstddef>

namespace libZFork {

// Shared helpers used by tags.cpp and the public ParseMusicalKey API.
bool ExtractKeyHint(const char* text, char* outKey, std::size_t outCap);

} // namespace libZFork
