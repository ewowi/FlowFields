#pragma once

#include "FastLED.h"
#include <ArduinoJson.h>
#include <string>

#include "componentEnums.h"

inline bool displayOn = true;

typedef void (*BusParamSetterFn)(uint8_t busId, const String& paramId, float value);
inline BusParamSetterFn setBusParam = nullptr;

// Callback to read a bus parameter value by busId and param name
typedef float (*BusParamGetterFn)(uint8_t busId, const String& paramName);
inline BusParamGetterFn getBusParam = nullptr;

// EMITTER and FLOW are now FlowFieldsEngine::_emitter / ::_flow members.

// ═══════════════════════════════════════════════════════════════════
// GLOBAL PARAMETERS
// ═══════════════════════════════════════════════════════════════════

const char* const GLOBAL_PARAMS[] PROGMEM = {
   "globalSpeed", "persistence", "persistFine", "colorShift"
};

const uint8_t GLOBAL_PARAM_COUNT = 4;

// ═══════════════════════════════════════════════════════════════════
//  EMITTERS
// ═══════════════════════════════════════════════════════════════════

// Emitter names in PROGMEM
const char orbitaldots_str[] PROGMEM = "orbitaldots";
const char swarmingdots_str[] PROGMEM = "swarmingdots";
const char audiodots_str[] PROGMEM = "audiodots";
const char lissajous_str[] PROGMEM = "lissajous";
const char borderrect_str[] PROGMEM = "borderrect";
const char noisekaleido_str[] PROGMEM = "noisekaleido";
const char cube_str[] PROGMEM = "cube";
const char fluidjet_str[] PROGMEM = "fluidjet";

const char* const EMITTERS[] PROGMEM = {
      orbitaldots_str, swarmingdots_str, audiodots_str, lissajous_str, borderrect_str, noisekaleido_str, cube_str, fluidjet_str
   };

const uint8_t EMITTER_COUNTS[] = {8};

// Emitter params
const char* const ORBITALDOTS_PARAMS[] PROGMEM = {
   "numDots", "dotDiam", "orbitSpeed", "orbitDiam",
   "modOrbitSpeedRate", "modOrbitSpeedLevel", "modOrbitDiamRate", "modOrbitDiamLevel"
};
const char* const SWARMINGDOTS_PARAMS[] PROGMEM = {
   "numDots", "dotDiam", "swarmSpeed", "swarmSpread",
   "modSwarmSpeedRate", "modSwarmSpeedLevel",
   "modSwarmSpreadRate", "modSwarmSpreadLevel"
};
const char* const AUDIODOTS_PARAMS[] PROGMEM = {};
const char* const LISSAJOUS_PARAMS[] PROGMEM = {
   "lineSpeed", "lineAmp", "lineClamp",
   "modLineSpeedRate", "modLineSpeedLevel",
   "modLineAmpRate", "modLineAmpLevel"
};
const char* const BORDERRECT_PARAMS[] PROGMEM = {};
const char* const NOISEKALEIDO_PARAMS[] PROGMEM = {
   "driftSpeed", "noiseScale", "noiseBand", "kaleidoGamma"
};
const char* const CUBE_PARAMS[] PROGMEM = {
   "scale", "rotateSpeedX", "rotateSpeedY", "rotateSpeedZ",
   "modScaleRate", "modScaleLevel",
   "modRotateSpeedXRate", "modRotateSpeedXLevel",
   "modRotateSpeedYRate", "modRotateSpeedYLevel",
   "modRotateSpeedZRate", "modRotateSpeedZLevel"
};
const char* const FLUIDJET_PARAMS[] PROGMEM = {
   "jetDensity", "jetForce", "jetRadius", "jetSpread", "jetHueSpeed",
   "modJetForceRate", "modJetForceLevel",
   "modAngleRate", "modAngleLevel"
};

// Struct to hold emitter name and parameter array reference
struct EmitterParamEntry {
   const char* EmitterName;
   const char* const* params;
   uint8_t count;
};

const EmitterParamEntry EMITTER_PARAM_LOOKUP[] PROGMEM = {
   {"orbitaldots", ORBITALDOTS_PARAMS, 8},
   {"swarmingdots", SWARMINGDOTS_PARAMS, 8},
   {"audiodots", AUDIODOTS_PARAMS, 0},
   {"lissajous", LISSAJOUS_PARAMS, 7},
   {"borderrect", BORDERRECT_PARAMS, 0},
   {"noisekaleido", NOISEKALEIDO_PARAMS, 4},
   {"cube", CUBE_PARAMS, 12},
   {"fluidjet", FLUIDJET_PARAMS, 9},
};

static const EmitterParamEntry* getEmitterParams(uint8_t emitterIdx) {
      if (emitterIdx >= EMITTER_COUNT) return nullptr;
      return &EMITTER_PARAM_LOOKUP[emitterIdx];
}

// ═══════════════════════════════════════════════════════════════════
//  FLOWS
// ═══════════════════════════════════════════════════════════════════

// Flow names in PROGMEM
const char noise_str[] PROGMEM = "noise";
const char radial_str[] PROGMEM = "radial";
const char directional_str[] PROGMEM = "directional";
const char rings_str[] PROGMEM = "rings";
const char spiral_str[] PROGMEM = "spiral";
const char fluid_str[] PROGMEM = "fluid";

const uint8_t FLOW_COUNTS[] = {6};

const char* const FLOWS[] PROGMEM = {
      noise_str, radial_str, directional_str, rings_str, spiral_str, fluid_str
   };

// Flow field params
const char* const NOISE_PARAMS[] PROGMEM = {
   "xSpeed", "ySpeed", "xAmp", "yAmp","xFreq", "yFreq", "xShift", "yShift",
   "modAmpRate", "modAmpLevel", "modSpeedRate", "modSpeedLevel",
   "modShiftRate", "modShiftLevel"
};
const char* const RADIAL_PARAMS[] PROGMEM = {
   "radialStep", "blendFactor"
};
const char* const DIRECTIONAL_PARAMS[] PROGMEM = {
   "windStep", "blendFactor", "rotateSpeed", "waveAmp", "waveFreq", "waveSpeed"
};
const char* const RINGS_PARAMS[] PROGMEM = {
   "innerSwirl", "outerSwirl", "midDrift",
   "modBreatheRate", "modBreatheLevel"
};
const char* const SPIRAL_PARAMS[] PROGMEM = {
   "angularStep", "radialStep", "blendFactor",
   "modAngularStepRate", "modAngularStepLevel",
   "modRadialStepRate", "modRadialStepLevel",
   "modBlendFactorRate", "modBlendFactorLevel"
};
const char* const FLUID_PARAMS[] PROGMEM = {
   "viscosity", "diffusion", "velocityDissipation", "dyeDissipation",
   "vorticity", "gravity", "solverIterations",
   "modVelDissipRate", "modVelDissipLevel",
   "modDyeDissipRate", "modDyeDissipLevel"
};
// Note: spiral reuses shared cVars radialStep and blendFactor

// Struct to hold flow field name and parameter array reference
struct FlowParamEntry {
   const char* FlowName;
   const char* const* params;
   uint8_t count;
};

const FlowParamEntry FLOW_PARAM_LOOKUP[] PROGMEM = {
   {"noise", NOISE_PARAMS, 14},
   {"radial", RADIAL_PARAMS, 2},
   {"directional", DIRECTIONAL_PARAMS, 6},
   {"rings", RINGS_PARAMS, 5},
   {"spiral", SPIRAL_PARAMS, 9},
   {"fluid", FLUID_PARAMS, 11}
};

static const FlowParamEntry* getFlowParams(uint8_t flowIdx) {
      if (flowIdx >= FLOW_COUNT) return nullptr;
      return &FLOW_PARAM_LOOKUP[flowIdx];
}

// ═══════════════════════════════════════════════════════════════════
// AUDIO SETTINGS
// ═══════════════════════════════════════════════════════════════════

const char* const AUDIO_PARAMS[] PROGMEM = {
"maxBins", "audioFloor", "audioGain",
"avLevelerTarget", "autoFloorAlpha", "autoFloorMin", "autoFloorMax",
"noiseGateOpen", "noiseGateClose",
"threshold", "minBeatInterval",
"rampAttack", "rampDecay", "peakBase", "expDecayFactor"
};

const uint8_t AUDIO_PARAM_COUNT = 15;

// ═══════════════════════════════════════════════════════════════════
//  MISCELLANEOUS CONTROLS
// ═══════════════════════════════════════════════════════════════════

inline uint8_t cBright = 35;
inline uint8_t cMapping = 0;
inline uint8_t cOverrideMapping = 0;

/*fl::EaseType getEaseType(uint8_t value) {
    switch (value) {
        case 0: return fl::EASE_NONE;
        case 1: return fl::EASE_IN_QUAD;
        case 2: return fl::EASE_OUT_QUAD;
        case 3: return fl::EASE_IN_OUT_QUAD;
        case 4: return fl::EASE_IN_CUBIC;
        case 5: return fl::EASE_OUT_CUBIC;
        case 6: return fl::EASE_IN_OUT_CUBIC;
        case 7: return fl::EASE_IN_SINE;
        case 8: return fl::EASE_OUT_SINE;
        case 9: return fl::EASE_IN_OUT_SINE;
    }
    FL_ASSERT(false, "Invalid ease type");
    return fl::EASE_NONE;
}*/

inline uint8_t cEaseSat = 0;
inline uint8_t cEaseLum = 0;

// ═══════════════════════════════════════════════════════════════════
//  PARAMETER DECLARATIONS
// ═══════════════════════════════════════════════════════════════════

// Engine params (emitters, flows, globals) are members of FlowFieldsEngine.
// BLE accesses them via bleSetEngineParam()/bleGetEngineParam() in bleControl.h.
// Library consumers set members directly: engine.orbitalDots.orbitSpeed = x.

// AUDIO -----------------------
inline bool maxBins = false;
inline uint16_t cNoiseGateOpen = 70;
inline uint16_t cNoiseGateClose = 50;
inline float cAudioGain = 1.0f;
inline float cAudioFloor = 0.0f;
inline bool autoFloor = false;
inline float cAutoFloorAlpha = 0.01f;
inline float cAutoFloorMin = 0.0f;
inline float cAutoFloorMax = 0.5f;
inline bool avLeveler = true;
inline float cAvLevelerTarget = 0.5f;
inline float cThreshold = 0.40f;
inline float cMinBeatInterval = 75.f;
inline float cRampAttack = 0.f;
inline float cRampDecay = 100.f;
inline float cPeakBase = 1.0f;
inline float cExpDecayFactor = 0.9f;

// ═══════════════════════════════════════════════════════════════════
//  X-MACRO PARAMETER TABLE
// ═══════════════════════════════════════════════════════════════════

// Engine params are set/read directly by bleSetEngineParam()/bleGetEngineParam() in bleControl.h.
// This table covers only audio + misc cVars that bleControl owns directly.
#define PARAMETER_TABLE \
   X(uint8_t, OverrideMapping, 0) \
   X(float, AudioGain, 1.0f) \
   X(float, AvLevelerTarget, 0.5f) \
   X(float, AudioFloor, 0.05f) \
   X(float, AutoFloorAlpha, 0.05f) \
   X(float, AutoFloorMin, 0.0f) \
   X(float, AutoFloorMax, 0.05f) \
   X(uint16_t, NoiseGateOpen, 70) \
   X(uint16_t, NoiseGateClose, 50) \
   X(float, Threshold, 0.25f) \
   X(float, MinBeatInterval, 75.0f) \
   X(float, RampAttack, 0.f) \
   X(float, RampDecay, 150.f) \
   X(float, PeakBase, 1.0f) \
   X(float, ExpDecayFactor, 1.0f)
