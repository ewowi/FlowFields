#include "parameterSchema.h"
#include "FlowFieldsEngine.h"
#include "flows/flow_noise.h"
#include "flows/flow_radial.h"
#include "flows/flow_directional.h"
#include "flows/flow_rings.h"
#include "flows/flow_spiral.h"
#include "flows/flow_fluid.h"
#include "emitters/emitters_other.h"
#include "emitters/emitter_orbitalDots.h"
#include "emitters/emitter_swarmingDots.h"
#include "emitters/emitter_lissajousLine.h"
#include "emitters/emitter_noiseKaleido.h"
#include "emitters/emitter_cube.h"
#include "emitters/emitter_fluidJet.h"

namespace flowFields {

// ── g_engine global ──────────────────────────────────────────────────────────
FlowFieldsEngine* g_engine = nullptr;

// ── Dispatch tables ──────────────────────────────────────────────────────────

const EmitterFn EMITTER_RUN[] = {
    emitOrbitalDots,
    emitSwarmingDots,
    emitAudioDots,
    emitLissajousLine,
    emitRainbowBorder,
    emitNoiseKaleido,
    emitCube,
    emitFluidJet,
};

const FlowPrepFn FLOW_PREPARE[] = {
    noiseFlowPrepare,
    radialPrepare,
    directionalPrepare,
    ringFlowPrepare,
    spiralPrepare,
    fluidPrepare,
};

const FlowAdvectFn FLOW_ADVECT[] = {
    noiseFlowAdvect,
    radialAdvect,
    directionalAdvect,
    ringFlowAdvect,
    spiralAdvect,
    fluidAdvect,
};

// ── Memory helpers — prefer PSRAM when available ─────────────────────────────

static float* psramAlloc(size_t count) {
    size_t bytes = count * sizeof(float);
    float* p = nullptr;
#if defined(BOARD_HAS_PSRAM)
    p = (float*)ps_malloc(bytes);   // SPIRAM; falls through on failure
#endif
    if (!p) p = (float*)malloc(bytes);
    if (p) memset(p, 0, bytes);
    return p;
}

static inline void psramFree(float* p) { free(p); }

// ── Grid allocation ──────────────────────────────────────────────────────────

float** FlowFieldsEngine::allocGrid(uint16_t w, uint16_t h) {
    float** g = (float**)malloc(h * sizeof(float*));
    if (!g) return nullptr;
    for (int i = 0; i < h; i++)
        g[i] = psramAlloc(w);
    return g;
}

void FlowFieldsEngine::freeGrid(float** g, uint16_t h) {
    if (!g) return;
    for (int i = 0; i < h; i++) psramFree(g[i]);
    free(g);
}

// ── Lifecycle ────────────────────────────────────────────────────────────────

void FlowFieldsEngine::setup(uint16_t width, uint16_t height, uint32_t numLeds,
                             uint32_t (*xy)(uint16_t, uint16_t)) {
    _width   = width;
    _height  = height;
    _numLeds = numLeds;
    _minDim  = (width < height) ? width : height;
    xyFunc   = xy;

    gR = allocGrid(width, height);
    gG = allocGrid(width, height);
    gB = allocGrid(width, height);
    tR = allocGrid(width, height);
    tG = allocGrid(width, height);
    tB = allocGrid(width, height);

    xProf = psramAlloc(width);
    yProf = psramAlloc(height);

    noiseX.init(42);
    noiseY.init(1337);
    noise2X.init(42);
    noise2Y.init(1337);
    kaleidoNoise.init(7331);

    timings = timers{};
    move    = modulators{};

    lastFrameMs    = fl::millis();
    _modLastRealMs = fl::millis();
    _modVirtualMs  = 0.0f;
    lastEmitter    = 255;
    lastFlow       = 255;
}

void FlowFieldsEngine::teardown() {
    freeGrid(gR, _height); gR = nullptr;
    freeGrid(gG, _height); gG = nullptr;
    freeGrid(gB, _height); gB = nullptr;
    freeGrid(tR, _height); tR = nullptr;
    freeGrid(tG, _height); tG = nullptr;
    freeGrid(tB, _height); tB = nullptr;
    psramFree(xProf); xProf = nullptr;
    psramFree(yProf); yProf = nullptr;
}

void FlowFieldsEngine::run(fl::CRGB* leds) {
    g_engine = this;

    unsigned long now = fl::millis();
    float rawDt = (now - lastFrameMs) * 0.001f;
    lastFrameMs = now;
    dt = rawDt * globalSpeed;
    t += dt;

    // On emitter change: reset all emitter structs to defaults, then notify UI.
    if (_emitter < EMITTER_COUNT && _emitter != lastEmitter) {
        activeEmitter = (Emitter)_emitter;
        lastEmitter   = _emitter;
        orbitalDots  = OrbitalDotsParams{};
        swarmingDots = SwarmingDotsParams{};
        lissajous    = LissajousParams{};
        noiseKaleido = NoiseKaleidoParams{};
        cube         = CubeParams{};
        fluidJet     = FluidJetParams{};
        if (onEmitterChanged) onEmitterChanged();
    }

    // On flow change: reset all flow structs to defaults, then notify UI.
    if (_flow < FLOW_COUNT && _flow != lastFlow) {
        activeFlow = (Flow)_flow;
        lastFlow   = _flow;
        noiseFlow   = NoiseFlowParams{};
        radial      = RadialParams{};
        directional = DirectionalParams{};
        ringFlow    = RingFlowParams{};
        spiral      = SpiralParams{};
        fluid       = FluidParams{};
        if (onFlowChanged) onFlowChanged();
    }

    // 1. Flow field: prepare (build noise profiles, apply modulators)
    FLOW_PREPARE[activeFlow]();

    // 2. Emitter: inject color onto grid
    EMITTER_RUN[activeEmitter]();

    // 3. Flow field: advect + fade
    FLOW_ADVECT[activeFlow]();

    // 4. Copy float grid to LED array
    for (uint16_t y = 0; y < _height; y++) {
        for (uint16_t x = 0; x < _width; x++) {
            uint32_t idx = xyFunc(x, y);
            if (idx >= _numLeds) continue;
            leds[idx].r = f2u8d(gR[y][x], x, y);
            leds[idx].g = f2u8d(gG[y][x], x, y);
            leds[idx].b = f2u8d(gB[y][x], x, y);
        }
    }
}

// ── calculate_modulators ─────────────────────────────────────────────────────

void FlowFieldsEngine::calculate_modulators(uint8_t numActiveTimers) {
    unsigned long realNow = fl::millis();
    float realDeltaMs = (float)(realNow - _modLastRealMs);
    _modLastRealMs = realNow;
    _modVirtualMs += realDeltaMs * globalSpeed;

    for (int i = 0; i < numActiveTimers; i++) {
        move.linear[i] = (_modVirtualMs + timings.offset[i]) * timings.ratio[i];

        move.radial_phase[i] = fmodf(move.linear[i], CT_2PI);
        if (move.radial_phase[i] < 0.0f) move.radial_phase[i] += CT_2PI;

        move.normalized_phase[i] = move.radial_phase[i] / CT_2PI;
        move.directional_sine[i] = sinf(move.radial_phase[i]);
        move.normalized_sine[i]  = move.directional_sine[i] * 0.5f + 0.5f;

        // Perlin1D returns ~[-0.5, 0.5]; scale to [-1, 1]
        move.directional_noise[i] = noiseX.noise(move.linear[i]) * 2.0f;
        move.normalized_noise[i]  = move.directional_noise[i] * 0.5f + 0.5f;
        move.radial_noise[i]      = CT_PI * (1.0f + move.directional_noise[i]);
    }
}

// ── Color helpers ─────────────────────────────────────────────────────────────

ColorF FlowFieldsEngine::hsvSpectrum(float hue) {
    float h6 = hue * 6.0f;
    int sector = (int)h6;
    float frac = h6 - sector;
    float r, g, b;
    switch (sector % 6) {
        case 0: r = 1.0f;        g = frac;        b = 0.0f;        break;
        case 1: r = 1.0f - frac; g = 1.0f;        b = 0.0f;        break;
        case 2: r = 0.0f;        g = 1.0f;        b = frac;        break;
        case 3: r = 0.0f;        g = 1.0f - frac; b = 1.0f;        break;
        case 4: r = frac;        g = 0.0f;        b = 1.0f;        break;
        case 5: r = 1.0f;        g = 0.0f;        b = 1.0f - frac; break;
        default: r = g = b = 0.0f; break;
    }
    return ColorF{r * 255.0f, g * 255.0f, b * 255.0f};
}

// FastLED rainbow character in float precision (no uint8 banding).
ColorF FlowFieldsEngine::hsvRainbow(float hue) {
    float h8 = hue * 8.0f;
    int section = (int)h8;
    float frac = h8 - section;
    float third = frac * 85.0f;
    float twothirds = frac * 170.0f;
    float r, g, b;
    switch (section % 8) {
        case 0: r = 255.0f - third;      g = third;              b = 0.0f;                 break;
        case 1: r = 171.0f;              g = 85.0f + third;      b = 0.0f;                 break;
        case 2: r = 171.0f - twothirds;  g = 170.0f + third;     b = 0.0f;                 break;
        case 3: r = 0.0f;                g = 255.0f - third;     b = third;                break;
        case 4: r = 0.0f;                g = 171.0f - twothirds; b = 85.0f + twothirds;    break;
        case 5: r = third;               g = 0.0f;               b = 255.0f - third;       break;
        case 6: r = 85.0f + third;       g = 0.0f;               b = 171.0f - third;       break;
        case 7: r = 170.0f + third;      g = 0.0f;               b = 85.0f - third;        break;
        default: r = g = b = 0.0f; break;
    }
    return ColorF{r, g, b};
}

ColorF FlowFieldsEngine::rainbow(float t_val, float speed, float phase) const {
    float hue = fmodPos(t_val * speed + phase, 1.0f);
    return useRainbow ? hsvRainbow(hue) : hsvSpectrum(hue);
}

// ── Drawing primitives ────────────────────────────────────────────────────────

void FlowFieldsEngine::drawDot(float cx, float cy, float diam,
                               float cr, float cg, float cb) {
    float rad = diam * 0.5f;
    int x0 = max(0,                (int)fl::floorf(cx - rad - 1.0f));
    int x1 = min((int)_width  - 1, (int)fl::ceilf (cx + rad + 1.0f));
    int y0 = max(0,                (int)fl::floorf(cy - rad - 1.0f));
    int y1 = min((int)_height - 1, (int)fl::ceilf (cy + rad + 1.0f));

    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx   = (x + 0.5f) - cx;
            float dy   = (y + 0.5f) - cy;
            float dist = fl::sqrtf(dx * dx + dy * dy);
            float cov  = clampf(rad + 0.5f - dist, 0.0f, 1.0f);
            if (cov <= 0.0f) continue;
            float inv = 1.0f - cov;
            gR[y][x] = gR[y][x] * inv + cr * cov;
            gG[y][x] = gG[y][x] * inv + cg * cov;
            gB[y][x] = gB[y][x] * inv + cb * cov;
        }
    }
}

void FlowFieldsEngine::blendPixelWeighted(int px, int py,
                                          float cr, float cg, float cb, float w) {
    if (px < 0 || px >= _width || py < 0 || py >= _height) return;
    w = clampf(w, 0.0f, 1.0f);
    if (w <= 0.0f) return;
    float inv = 1.0f - w;
    gR[py][px] = gR[py][px] * inv + cr * w;
    gG[py][px] = gG[py][px] * inv + cg * w;
    gB[py][px] = gB[py][px] * inv + cb * w;
}

void FlowFieldsEngine::drawAAEndpointDisc(float cx, float cy,
                                          float cr, float cg, float cb,
                                          float radius) {
    int x0 = max(0,                (int)fl::floorf(cx - radius - 1.0f));
    int x1 = min((int)_width  - 1, (int)fl::ceilf (cx + radius + 1.0f));
    int y0 = max(0,                (int)fl::floorf(cy - radius - 1.0f));
    int y1 = min((int)_height - 1, (int)fl::ceilf (cy + radius + 1.0f));
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            float dx   = (px + 0.5f) - cx;
            float dy   = (py + 0.5f) - cy;
            float dist = fl::sqrtf(dx * dx + dy * dy);
            float w    = clampf(radius + 0.5f - dist, 0.0f, 1.0f);
            blendPixelWeighted(px, py, cr, cg, cb, w);
        }
    }
}

// Anti-aliased sub-pixel line with rainbow color varying along its length.
// Uses this->t and this->colorShift directly (dropped from call signature).
void FlowFieldsEngine::drawAASubpixelLine(float x0, float y0, float x1, float y1) {
    float dx = x1 - x0;
    float dy = y1 - y0;
    float maxd = fl::fabsf(dx) > fl::fabsf(dy) ? fl::fabsf(dx) : fl::fabsf(dy);
    int steps = max(1, (int)(maxd * 3.0f));
    for (int i = 0; i <= steps; i++) {
        float u  = (float)i / (float)steps;
        float x  = x0 + dx * u;
        float y  = y0 + dy * u;
        int   xi = (int)fl::floorf(x);
        int   yi = (int)fl::floorf(y);
        float fx = x - xi;
        float fy = y - yi;
        ColorF c = rainbow(t, colorShift, u);
        blendPixelWeighted(xi,     yi,     c.r, c.g, c.b, (1.0f - fx) * (1.0f - fy));
        blendPixelWeighted(xi + 1, yi,     c.r, c.g, c.b, fx * (1.0f - fy));
        blendPixelWeighted(xi,     yi + 1, c.r, c.g, c.b, (1.0f - fx) * fy);
        blendPixelWeighted(xi + 1, yi + 1, c.r, c.g, c.b, fx * fy);
    }
}

} // namespace flowFields
