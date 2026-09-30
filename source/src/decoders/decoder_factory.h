#pragma once
#include "decoder_base.h"
#include <memory>

namespace libZFork {

/// Erstellt den passenden Decoder für eine Dateiendung.
/// Gibt nullptr zurück wenn Format unbekannt oder nicht kompiliert ist.
/// Alle Format-Abhängigkeiten sind in decoder_factory.cpp gekapselt.
std::unique_ptr<DecoderBase> CreateDecoderForFile(const wchar_t* path);

} // namespace libZFork
