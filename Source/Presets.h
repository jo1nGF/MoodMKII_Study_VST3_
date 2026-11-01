#pragma once
#include <utility>
#include <vector>
#include "PluginParameters.h"

// Presets de fábrica. Valores "reais": índice para toggles/choices, 0/1 para botões, 0..1 para knobs.
// Tudo que não aparece aqui volta ao padrão ao carregar. Estado dos footswitches não é alterado.
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;
};

inline const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> v = {
        { "Init (hi-fi)", {} },
        { "Smear Pad",            { { ids::wetMode, 0.f }, { ids::wetTime, 0.78f }, { ids::wetModify, 0.95f }, { ids::mix, 0.55f } } },
        { "Multi-Tap Echoes",     { { ids::wetMode, 0.f }, { ids::wetTime, 0.42f }, { ids::wetModify, 0.f }, { ids::mix, 0.6f } } },
        { "Dusty Verb (lo-fi)",   { { ids::wetMode, 0.f }, { ids::wetTime, 0.7f }, { ids::wetModify, 0.85f }, { ids::mix, 0.6f },
                                    { ids::clock, 0.6f }, { ids::clockNoise, 0.7f }, { ids::clockDegrade, 0.2f } } },
        { "Ping-Pong Echo",       { { ids::wetMode, 1.f }, { ids::wetTime, 0.33f }, { ids::wetModify, 0.55f }, { ids::mix, 0.5f },
                                    { ids::dipSpread, 1.f }, { ids::hStereoWidth, 0.9f } } },
        { "Infinite Echo",        { { ids::wetMode, 1.f }, { ids::wetTime, 0.28f }, { ids::wetModify, 1.f }, { ids::mix, 0.6f } } },
        { "Slip Harmonizer +5th", { { ids::wetMode, 2.f }, { ids::wetTime, 0.45f }, { ids::wetModify, 0.79f }, { ids::mix, 0.5f } } },
        { "Tape Octave Loop",     { { ids::loopMode, 1.f }, { ids::loopModify, 0.8f }, { ids::loopLength, 1.f }, { ids::mix, 0.7f },
                                    { ids::routing, 1.f }, { ids::wetMode, 0.f }, { ids::wetTime, 0.6f }, { ids::wetModify, 1.f } } },
        { "Stretch Cloud",        { { ids::loopMode, 2.f }, { ids::loopModify, 0.62f }, { ids::loopLength, 0.5f }, { ids::clock, 0.8f },
                                    { ids::mix, 0.7f }, { ids::routing, 1.f }, { ids::wetMode, 0.f }, { ids::wetTime, 0.7f }, { ids::wetModify, 1.f } } },
        { "Classic Grit",         { { ids::dipClassic, 1.f }, { ids::clock, 0.4f }, { ids::clockDegrade, 0.5f }, { ids::clockNoise, 0.8f },
                                    { ids::wetMode, 0.f }, { ids::wetTime, 0.6f }, { ids::wetModify, 0.9f }, { ids::mix, 0.6f } } },
    };
    return v;
}
