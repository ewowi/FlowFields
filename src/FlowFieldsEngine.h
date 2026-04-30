#pragma once

#include "componentEnums.h"
#include "flowFieldsTypes.h"   // Perlin1D/2D, ValueNoise2D, ColorF, ModConfig, math helpers
#include "modulators.h"        // timers, modulators structs

namespace flowFields {

class FlowFieldsEngine {
public:
    // ── Dimensions ────────────────────────────────────────────────────────
    uint16_t _width   = 0;
    uint16_t _height  = 0;
    uint32_t _numLeds = 0;
    uint16_t _minDim  = 0;

    // ── Float grids — float**[height][width] allocated in setup() ─────────
    float** gR = nullptr;
    float** gG = nullptr;
    float** gB = nullptr;
    float** tR = nullptr;
    float** tG = nullptr;
    float** tB = nullptr;

    // ── Noise generators ──────────────────────────────────────────────────
    Perlin1D     noiseX, noiseY;
    Perlin1D     ampVarX, ampVarY;
    Perlin2D     noise2X, noise2Y;
    ValueNoise2D kaleidoNoise;

    // ── Noise profiles — float*[_width] / float*[_height] ────────────────
    float* xProf = nullptr;
    float* yProf = nullptr;

    // ── Frame timing ──────────────────────────────────────────────────────
    unsigned long lastFrameMs = 0;
    float t  = 0.0f;
    float dt = 0.0f;

    // ── Config ────────────────────────────────────────────────────────────
    float globalSpeed = 1.0f;
    float persistence = 0.05f;
    float colorShift  = 0.20f;
    bool  useRainbow  = false;

    // ── Selection state ───────────────────────────────────────────────────
    uint8_t _emitter    = 0;     // replaces global EMITTER — set by BLE or bindParam
    uint8_t _flow       = 0;     // replaces global FLOW
    uint8_t lastEmitter = 255;
    uint8_t lastFlow    = 255;
    Emitter activeEmitter = EMITTER_ORBITALDOTS;
    Flow    activeFlow    = FLOW_NOISE;

    // ── XY mapping ────────────────────────────────────────────────────────
    uint32_t (*xyFunc)(uint16_t x, uint16_t y) = nullptr;

    // ── Modulator state ───────────────────────────────────────────────────
    timers    timings;
    modulators move;
    unsigned long _modLastRealMs = 0;
    float         _modVirtualMs  = 0.0f;

    // ── Callbacks (null-safe; set by caller during setup) ─────────────────
    void (*onEmitterChanged)() = nullptr;
    void (*onFlowChanged)()    = nullptr;

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void setup(uint16_t width, uint16_t height, uint32_t numLeds,
               uint32_t (*xy)(uint16_t, uint16_t));
    void run(fl::CRGB* leds);
    void teardown();

    // ── Modulator calculation — called by emitters/flows as g_engine->calculate_modulators(N)
    void calculate_modulators(uint8_t numActiveTimers);

    // ── Color helpers ─────────────────────────────────────────────────────
    static ColorF hsvSpectrum(float hue);
    static ColorF hsvRainbow(float hue);
    ColorF rainbow(float t_val, float speed, float phase) const;

    // ── Drawing primitives — called by emitters/flows as g_engine->drawXxx(...)
    void drawDot(float cx, float cy, float diam, float cr, float cg, float cb);
    void blendPixelWeighted(int px, int py, float cr, float cg, float cb, float w);
    void drawAAEndpointDisc(float cx, float cy, float cr, float cg, float cb,
                            float radius = 0.85f);
    void drawAASubpixelLine(float x0, float y0, float x1, float y1);

    // ── cVar bridge (implemented in flowFieldsEngine.cpp; reads/writes parameterSchema.h cVars) ──
    void pushDefaultsToCVars();
    void syncFromCVars();
    void pushFlowDefaultsToCVars();
    void syncFlowFromCVars();

    // ── Parameter binding (Phase 3) ───────────────────────────────────────────
    // Call once during setup() to redirect a named cVar to an external float.
    // run() copies *externalPtr → cVar before syncFromCVars() — zero per-frame
    // string lookups, just one pointer dereference per bound parameter.
    // The BLE firmware ignores this; bindings are only set by library consumers.
    void bindParam(const char* name, float* externalPtr);

private:
    static float** allocGrid(uint16_t w, uint16_t h);
    static void    freeGrid(float** g, uint16_t h);
    float*         resolveField(const char* name);

    struct ParamBinding { float* external; float* field; };
    static constexpr int MAX_PARAM_BINDINGS = 80;
    ParamBinding paramBindings_[MAX_PARAM_BINDINGS] = {};
    int          numParamBindings_ = 0;
    // Emitter/flow selects are uint8_t globals, handled separately.
    float*       pEmitterSel_ = nullptr;
    float*       pFlowSel_    = nullptr;
};

// Set by FlowFieldsEngine::run() before dispatching to any emitter/flow function.
// Safe for single-threaded embedded use (one active engine at a time).
extern FlowFieldsEngine* g_engine;

} // namespace flowFields
