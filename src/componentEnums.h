#pragma once
#include <stdint.h>

enum Emitter : uint8_t {
    EMITTER_ORBITALDOTS = 0,
    EMITTER_SWARMINGDOTS,
    EMITTER_AUDIODOTS,
    EMITTER_LISSAJOUS,
    EMITTER_BORDERRECT,
    EMITTER_NOISEKALEIDO,
    EMITTER_CUBE,
    EMITTER_FLUIDJET,
    EMITTER_COUNT
};

inline const char* const EMITTER_NAMES[EMITTER_COUNT] = {
    "orbitaldots", "swarmingdots", "audiodots", "lissajous",
    "borderrect",  "noisekaleido", "cube",       "fluidjet"
};

enum Flow : uint8_t {
    FLOW_NOISE = 0,
    FLOW_RADIAL,
    FLOW_DIRECTIONAL,
    FLOW_RINGS,
    FLOW_SPIRAL,
    FLOW_FLUID,
    FLOW_COUNT
};

inline const char* const FLOW_NAMES[FLOW_COUNT] = {
    "noise", "radial", "directional", "rings", "spiral", "fluid"
};
