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

    // Read selector bindings first — needed for emitter/flow change detection below.
    if (pEmitterSel_) _emitter = (uint8_t)(*pEmitterSel_);
    if (pFlowSel_)    _flow    = (uint8_t)(*pFlowSel_);

    unsigned long now = fl::millis();
    float rawDt = (now - lastFrameMs) * 0.001f;
    lastFrameMs = now;
    dt = rawDt * globalSpeed;
    t += dt;

    // Update emitter if changed — push struct defaults into cVars BEFORE bindings.
    if (_emitter < EMITTER_COUNT && _emitter != lastEmitter) {
        activeEmitter = (Emitter)_emitter;
        lastEmitter   = _emitter;
        pushDefaultsToCVars();
        if (onEmitterChanged) onEmitterChanged();
    }

    // Update flow field if changed — same ordering reason.
    if (_flow < FLOW_COUNT && _flow != lastFlow) {
        activeFlow = (Flow)_flow;
        lastFlow   = _flow;
        pushFlowDefaultsToCVars();
        if (onFlowChanged) onFlowChanged();
    }

    // BLE path: sync cVars → component structs.
    syncFromCVars();

    // Library path: apply external bindings LAST — overrides whatever syncFromCVars wrote.
    // Each binding points directly at the struct field the engine reads, so changes
    // from the UI host take effect this frame without cVar indirection.
    for (int i = 0; i < numParamBindings_; i++)
        *paramBindings_[i].field = *paramBindings_[i].external;

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

// ── cVar bridge ───────────────────────────────────────────────────────────────

void FlowFieldsEngine::pushFlowDefaultsToCVars() {
    switch (activeFlow) {
        case FLOW_NOISE: {
            noiseFlow = NoiseFlowParams{};
            cXSpeed = noiseFlow.xSpeed;
            cYSpeed = noiseFlow.ySpeed;
            cXAmp = noiseFlow.xAmp;
            cYAmp = noiseFlow.yAmp;
            cXFreq = noiseFlow.xFreq;
            cYFreq = noiseFlow.yFreq;
            cXShift = noiseFlow.xShift;
            cYShift = noiseFlow.yShift;
            cModAmpRate = noiseFlow.modAmp.modRate;
            cModAmpLevel = noiseFlow.modAmp.modLevel;
            cModSpeedRate = noiseFlow.modSpeed.modRate;
            cModSpeedLevel = noiseFlow.modSpeed.modLevel;
            cModShiftRate = noiseFlow.modShift.modRate;
            cModShiftLevel = noiseFlow.modShift.modLevel;
            break;
        }
        case FLOW_RADIAL: {
            radial = RadialParams{};
            cRadialStep = radial.radialStep;
            cBlendFactor = radial.blendFactor;
            cOutward = radial.outward;
            break;
        }
        case FLOW_DIRECTIONAL: {
            directional = DirectionalParams{};
            cWindStep = directional.windStep;
            cBlendFactor = directional.blendFactor;
            cRotateSpeed = directional.rotateSpeed;
            cWaveAmp = directional.waveAmp;
            cWaveFreq = directional.waveFreq;
            cWaveSpeed = directional.waveSpeed;
            break;
        }
        case FLOW_RINGS: {
            ringFlow = RingFlowParams{};
            cInnerSwirl = ringFlow.innerSwirl;
            cOuterSwirl = ringFlow.outerSwirl;
            cMidDrift = ringFlow.midDrift;
            cModBreatheRate = ringFlow.modBreathe.modRate;
            cModBreatheLevel = ringFlow.modBreathe.modLevel;
            break;
        }
        case FLOW_SPIRAL: {
            spiral = SpiralParams{};
            cAngularStep = spiral.angularStep;
            cRadialStep = spiral.radialStep;
            cBlendFactor = spiral.blendFactor;
            cOutward = spiral.outward;
            cModAngularStepRate = spiral.modAngularStep.modRate;
            cModAngularStepLevel = spiral.modAngularStep.modLevel;
            cModRadialStepRate = spiral.modRadialStep.modRate;
            cModRadialStepLevel = spiral.modRadialStep.modLevel;
            cModBlendFactorRate = spiral.modBlendFactor.modRate;
            cModBlendFactorLevel = spiral.modBlendFactor.modLevel;
            break;
        }
        case FLOW_FLUID: {
            fluid = FluidParams{};
            cViscosity = fluid.viscosity;
            cDiffusion = fluid.diffusion;
            cVelocityDissipation = fluid.velocityDissipation;
            cDyeDissipation = fluid.dyeDissipation;
            cVorticity = fluid.vorticity;
            cGravity = fluid.gravity;
            cSolverIterations = (float)fluid.solverIterations;
            cModVelDissipRate = fluid.modVelDissip.modRate;
            cModVelDissipLevel = fluid.modVelDissip.modLevel;
            cModDyeDissipRate = fluid.modDyeDissip.modRate;
            cModDyeDissipLevel = fluid.modDyeDissip.modLevel;
            break;
        }
        default: break;
    }
}

void FlowFieldsEngine::syncFlowFromCVars() {
    // Noise flow
    noiseFlow.xSpeed = cXSpeed;
    noiseFlow.ySpeed  = cYSpeed;
    noiseFlow.xAmp = cXAmp;
    noiseFlow.yAmp = cYAmp;
    noiseFlow.xFreq = cXFreq;
    noiseFlow.yFreq = cYFreq;
    noiseFlow.xShift = cXShift;
    noiseFlow.yShift = cYShift;
    noiseFlow.modAmp.modRate = cModAmpRate;
    noiseFlow.modAmp.modLevel = cModAmpLevel;
    noiseFlow.modSpeed.modRate = cModSpeedRate;
    noiseFlow.modSpeed.modLevel = cModSpeedLevel;
    noiseFlow.modShift.modRate = cModShiftRate;
    noiseFlow.modShift.modLevel = cModShiftLevel;
    // Radial flow
    radial.radialStep = cRadialStep;
    radial.blendFactor = cBlendFactor;
    radial.outward = cOutward;
    // Directional flow
    directional.windStep = cWindStep;
    directional.blendFactor = cBlendFactor;
    directional.rotateSpeed = cRotateSpeed;
    directional.waveAmp = cWaveAmp;
    directional.waveFreq = cWaveFreq;
    directional.waveSpeed = cWaveSpeed;
    // Ring flow
    ringFlow.innerSwirl = cInnerSwirl;
    ringFlow.outerSwirl = cOuterSwirl;
    ringFlow.midDrift = cMidDrift;
    ringFlow.modBreathe.modRate = cModBreatheRate;
    ringFlow.modBreathe.modLevel = cModBreatheLevel;
    // Spiral flow
    spiral.angularStep = cAngularStep;
    spiral.radialStep = cRadialStep;
    spiral.blendFactor = cBlendFactor;
    spiral.outward = cOutward;
    spiral.modAngularStep.modRate = cModAngularStepRate;
    spiral.modAngularStep.modLevel = cModAngularStepLevel;
    spiral.modRadialStep.modRate = cModRadialStepRate;
    spiral.modRadialStep.modLevel = cModRadialStepLevel;
    spiral.modBlendFactor.modRate = cModBlendFactorRate;
    spiral.modBlendFactor.modLevel = cModBlendFactorLevel;
    // Fluid flow
    fluid.viscosity = cViscosity;
    fluid.diffusion = cDiffusion;
    fluid.velocityDissipation = cVelocityDissipation;
    fluid.dyeDissipation = cDyeDissipation;
    fluid.vorticity = cVorticity;
    fluid.gravity = cGravity;
    fluid.solverIterations = (uint8_t)cSolverIterations;
    fluid.modVelDissip.modRate = cModVelDissipRate;
    fluid.modVelDissip.modLevel = cModVelDissipLevel;
    fluid.modDyeDissip.modRate = cModDyeDissipRate;
    fluid.modDyeDissip.modLevel = cModDyeDissipLevel;
}

void FlowFieldsEngine::pushDefaultsToCVars() {
    // Universal
    cGlobalSpeed = globalSpeed;
    cPersistence = floorf(persistence);
    cPersistFine = persistence - cPersistence;
    cColorShift = colorShift;
    // Emitter: orbitalDots
    cNumDots = orbitalDots.numDots;
    cOrbitSpeed = orbitalDots.orbitSpeed;
    cDotDiam = orbitalDots.dotDiam;
    cOrbitDiam = orbitalDots.orbitDiam;
    cModOrbitSpeedRate = orbitalDots.modOrbitSpeed.modRate;
    cModOrbitSpeedLevel = orbitalDots.modOrbitSpeed.modLevel;
    cModOrbitDiamRate = orbitalDots.modOrbitDiam.modRate;
    cModOrbitDiamLevel = orbitalDots.modOrbitDiam.modLevel;
    // Emitter: swarmingDots
    cSwarmSpeed = swarmingDots.swarmSpeed;
    cSwarmSpread = swarmingDots.swarmSpread;
    cModSwarmSpeedRate = swarmingDots.modSwarmSpeed.modRate;
    cModSwarmSpeedLevel = swarmingDots.modSwarmSpeed.modLevel;
    cModSwarmSpreadRate = swarmingDots.modSwarmSpread.modRate;
    cModSwarmSpreadLevel = swarmingDots.modSwarmSpread.modLevel;
    // Emitter: lissajous / borderRect
    cLineSpeed = lissajous.lineSpeed;
    cLineAmp = lissajous.lineAmp;
    cLineClamp = lissajous.lineClamp;
    cModLineSpeedRate = lissajous.modLineSpeed.modRate;
    cModLineSpeedLevel = lissajous.modLineSpeed.modLevel;
    cModLineAmpRate = lissajous.modLineAmp.modRate;
    cModLineAmpLevel = lissajous.modLineAmp.modLevel;
    // Emitter: noiseKaleido
    cDriftSpeed = noiseKaleido.driftSpeed;
    cNoiseScale = noiseKaleido.noiseScale;
    cNoiseBand = noiseKaleido.noiseBand;
    cKaleidoGamma = noiseKaleido.kaleidoGamma;
    // Emitter: cube
    cScale = cube.scale;
    cRotateSpeedX = cube.rotateSpeed[0];
    cRotateSpeedY = cube.rotateSpeed[1];
    cRotateSpeedZ = cube.rotateSpeed[2];
    cAxisFreezeX = cube.axisFreeze[0];
    cAxisFreezeY = cube.axisFreeze[1];
    cAxisFreezeZ = cube.axisFreeze[2];
    cModScaleRate = cube.modScale.modRate;
    cModScaleLevel = cube.modScale.modLevel;
    cModRotateSpeedXRate = cube.modRotateSpeedX.modRate;
    cModRotateSpeedXLevel = cube.modRotateSpeedX.modLevel;
    cModRotateSpeedYRate = cube.modRotateSpeedY.modRate;
    cModRotateSpeedYLevel = cube.modRotateSpeedY.modLevel;
    cModRotateSpeedZRate = cube.modRotateSpeedZ.modRate;
    cModRotateSpeedZLevel = cube.modRotateSpeedZ.modLevel;
    // Emitter: fluidJet
    cJetDensity = fluidJet.jetDensity;
    cJetForce = fluidJet.jetForce;
    cJetRadius = fluidJet.jetRadius;
    cJetSpread = fluidJet.jetSpread;
    cJetAngle = fluidJet.jetAngle;
    cJetHueSpeed = fluidJet.jetHueSpeed;
    cModJetForceRate = fluidJet.modJetForce.modRate;
    cModJetForceLevel = fluidJet.modJetForce.modLevel;
    cModAngleRate = fluidJet.modAngle.modRate;
    cModAngleLevel = fluidJet.modAngle.modLevel;
}

void FlowFieldsEngine::syncFromCVars() {
    globalSpeed = cGlobalSpeed;
    persistence = cPersistence + cPersistFine;
    colorShift = cColorShift;
    orbitalDots.numDots = (uint8_t)cNumDots;
    orbitalDots.orbitSpeed = cOrbitSpeed;
    orbitalDots.dotDiam  = cDotDiam;
    orbitalDots.orbitDiam = cOrbitDiam;
    orbitalDots.modOrbitSpeed.modRate = cModOrbitSpeedRate;
    orbitalDots.modOrbitSpeed.modLevel = cModOrbitSpeedLevel;
    orbitalDots.modOrbitDiam.modRate = cModOrbitDiamRate;
    orbitalDots.modOrbitDiam.modLevel = cModOrbitDiamLevel;
    swarmingDots.numDots = (uint8_t)cNumDots;
    swarmingDots.swarmSpeed = cSwarmSpeed;
    swarmingDots.swarmSpread = cSwarmSpread;
    swarmingDots.modSwarmSpeed.modRate = cModSwarmSpeedRate;
    swarmingDots.modSwarmSpeed.modLevel = cModSwarmSpeedLevel;
    swarmingDots.modSwarmSpread.modRate = cModSwarmSpreadRate;
    swarmingDots.modSwarmSpread.modLevel = cModSwarmSpreadLevel;
    swarmingDots.dotDiam = cDotDiam;
    lissajous.lineSpeed = cLineSpeed;
    lissajous.lineAmp = cLineAmp;
    lissajous.lineClamp = (uint8_t)cLineClamp;
    lissajous.modLineSpeed.modRate = cModLineSpeedRate;
    lissajous.modLineSpeed.modLevel = cModLineSpeedLevel;
    lissajous.modLineAmp.modRate = cModLineAmpRate;
    lissajous.modLineAmp.modLevel = cModLineAmpLevel;
    noiseKaleido.driftSpeed = cDriftSpeed;
    noiseKaleido.noiseScale = cNoiseScale;
    noiseKaleido.noiseBand = cNoiseBand;
    noiseKaleido.kaleidoGamma = cKaleidoGamma;
    cube.scale = cScale;
    cube.rotateSpeed[0] = cRotateSpeedX;
    cube.rotateSpeed[1] = cRotateSpeedY;
    cube.rotateSpeed[2] = cRotateSpeedZ;
    cube.axisFreeze[0] = cAxisFreezeX;
    cube.axisFreeze[1] = cAxisFreezeY;
    cube.axisFreeze[2] = cAxisFreezeZ;
    cube.modScale.modRate = cModScaleRate;
    cube.modScale.modLevel = cModScaleLevel;
    cube.modRotateSpeedX.modRate = cModRotateSpeedXRate;
    cube.modRotateSpeedX.modLevel = cModRotateSpeedXLevel;
    cube.modRotateSpeedY.modRate = cModRotateSpeedYRate;
    cube.modRotateSpeedY.modLevel = cModRotateSpeedYLevel;
    cube.modRotateSpeedZ.modRate = cModRotateSpeedZRate;
    cube.modRotateSpeedZ.modLevel = cModRotateSpeedZLevel;
    // fluidJet
    fluidJet.jetDensity = cJetDensity;
    fluidJet.jetForce = cJetForce;
    fluidJet.jetRadius = cJetRadius;
    fluidJet.jetSpread = cJetSpread;
    fluidJet.jetAngle = cJetAngle;
    fluidJet.jetHueSpeed = cJetHueSpeed;
    fluidJet.modJetForce.modRate = cModJetForceRate;
    fluidJet.modJetForce.modLevel = cModJetForceLevel;
    fluidJet.modAngle.modRate = cModAngleRate;
    fluidJet.modAngle.modLevel = cModAngleLevel;
    // Flow field + modulator
    syncFlowFromCVars();
}

// ── Parameter binding (Phase 3) ───────────────────────────────────────────────

void FlowFieldsEngine::bindParam(const char* name, float* externalPtr) {
    // Special-case the two uint8_t selection globals.
    if (strcasecmp(name, "emitter") == 0) { pEmitterSel_ = externalPtr; return; }
    if (strcasecmp(name, "flow")    == 0) { pFlowSel_    = externalPtr; return; }

    if (numParamBindings_ >= MAX_PARAM_BINDINGS) return;
    float* field = resolveField(name);
    if (!field) return;
    paramBindings_[numParamBindings_++] = { externalPtr, field };
}

// One-time lookup called from bindParam() during setup().
// Returns the struct field pointer the engine actually reads each frame.
// Bindings are applied AFTER syncFromCVars() in run(), so they override the BLE path.
// Special cases (uint8_t targets or multi-struct fan-out) return the cVar pointer;
// syncFromCVars fans those out to structs on the NEXT frame (one-frame lag, not visible).
float* FlowFieldsEngine::resolveField(const char* name) {
    // Engine members — no cVar indirection, persistence bypasses the cPersistFine split
    if (strcasecmp(name, "globalSpeed")          == 0) return &globalSpeed;
    if (strcasecmp(name, "persistence")          == 0) return &persistence;
    if (strcasecmp(name, "colorShift")           == 0) return &colorShift;
    // Shared dot params: uint8_t structs or multi-struct fan-out — keep cVar, one-frame lag
    if (strcasecmp(name, "numDots")              == 0) return &cNumDots;
    if (strcasecmp(name, "dotDiam")              == 0) return &cDotDiam;
    // OrbitalDots
    if (strcasecmp(name, "orbitSpeed")           == 0) return &orbitalDots.orbitSpeed;
    if (strcasecmp(name, "orbitDiam")            == 0) return &orbitalDots.orbitDiam;
    if (strcasecmp(name, "modOrbitSpeedRate")    == 0) return &orbitalDots.modOrbitSpeed.modRate;
    if (strcasecmp(name, "modOrbitSpeedLevel")   == 0) return &orbitalDots.modOrbitSpeed.modLevel;
    if (strcasecmp(name, "modOrbitDiamRate")     == 0) return &orbitalDots.modOrbitDiam.modRate;
    if (strcasecmp(name, "modOrbitDiamLevel")    == 0) return &orbitalDots.modOrbitDiam.modLevel;
    // SwarmingDots
    if (strcasecmp(name, "swarmSpeed")           == 0) return &swarmingDots.swarmSpeed;
    if (strcasecmp(name, "swarmSpread")          == 0) return &swarmingDots.swarmSpread;
    if (strcasecmp(name, "modSwarmSpeedRate")    == 0) return &swarmingDots.modSwarmSpeed.modRate;
    if (strcasecmp(name, "modSwarmSpeedLevel")   == 0) return &swarmingDots.modSwarmSpeed.modLevel;
    if (strcasecmp(name, "modSwarmSpreadRate")   == 0) return &swarmingDots.modSwarmSpread.modRate;
    if (strcasecmp(name, "modSwarmSpreadLevel")  == 0) return &swarmingDots.modSwarmSpread.modLevel;
    // LissajousLine
    if (strcasecmp(name, "lineSpeed")            == 0) return &lissajous.lineSpeed;
    if (strcasecmp(name, "lineAmp")              == 0) return &lissajous.lineAmp;
    if (strcasecmp(name, "lineClamp")            == 0) return &cLineClamp;  // uint8_t cast in syncFromCVars
    if (strcasecmp(name, "modLineSpeedRate")     == 0) return &lissajous.modLineSpeed.modRate;
    if (strcasecmp(name, "modLineSpeedLevel")    == 0) return &lissajous.modLineSpeed.modLevel;
    if (strcasecmp(name, "modLineAmpRate")       == 0) return &lissajous.modLineAmp.modRate;
    if (strcasecmp(name, "modLineAmpLevel")      == 0) return &lissajous.modLineAmp.modLevel;
    // NoiseKaleido
    if (strcasecmp(name, "driftSpeed")           == 0) return &noiseKaleido.driftSpeed;
    if (strcasecmp(name, "noiseScale")           == 0) return &noiseKaleido.noiseScale;
    if (strcasecmp(name, "noiseBand")            == 0) return &noiseKaleido.noiseBand;
    if (strcasecmp(name, "kaleidoGamma")         == 0) return &noiseKaleido.kaleidoGamma;
    // Cube
    if (strcasecmp(name, "scale")                == 0) return &cube.scale;
    if (strcasecmp(name, "rotateSpeedX")         == 0) return &cube.rotateSpeed[0];
    if (strcasecmp(name, "rotateSpeedY")         == 0) return &cube.rotateSpeed[1];
    if (strcasecmp(name, "rotateSpeedZ")         == 0) return &cube.rotateSpeed[2];
    if (strcasecmp(name, "modScaleRate")         == 0) return &cube.modScale.modRate;
    if (strcasecmp(name, "modScaleLevel")        == 0) return &cube.modScale.modLevel;
    if (strcasecmp(name, "modRotateSpeedXRate")  == 0) return &cube.modRotateSpeedX.modRate;
    if (strcasecmp(name, "modRotateSpeedXLevel") == 0) return &cube.modRotateSpeedX.modLevel;
    if (strcasecmp(name, "modRotateSpeedYRate")  == 0) return &cube.modRotateSpeedY.modRate;
    if (strcasecmp(name, "modRotateSpeedYLevel") == 0) return &cube.modRotateSpeedY.modLevel;
    if (strcasecmp(name, "modRotateSpeedZRate")  == 0) return &cube.modRotateSpeedZ.modRate;
    if (strcasecmp(name, "modRotateSpeedZLevel") == 0) return &cube.modRotateSpeedZ.modLevel;
    // FluidJet
    if (strcasecmp(name, "jetDensity")           == 0) return &fluidJet.jetDensity;
    if (strcasecmp(name, "jetForce")             == 0) return &fluidJet.jetForce;
    if (strcasecmp(name, "jetRadius")            == 0) return &fluidJet.jetRadius;
    if (strcasecmp(name, "jetSpread")            == 0) return &fluidJet.jetSpread;
    if (strcasecmp(name, "jetAngle")             == 0) return &fluidJet.jetAngle;
    if (strcasecmp(name, "jetHueSpeed")          == 0) return &fluidJet.jetHueSpeed;
    if (strcasecmp(name, "modJetForceRate")      == 0) return &fluidJet.modJetForce.modRate;
    if (strcasecmp(name, "modJetForceLevel")     == 0) return &fluidJet.modJetForce.modLevel;
    if (strcasecmp(name, "modAngleRate")         == 0) return &fluidJet.modAngle.modRate;
    if (strcasecmp(name, "modAngleLevel")        == 0) return &fluidJet.modAngle.modLevel;
    // Flows shared: blendFactor used by multiple flows — keep cVar, syncFromCVars fans out
    if (strcasecmp(name, "blendFactor")          == 0) return &cBlendFactor;
    // NoiseFlow
    if (strcasecmp(name, "xSpeed")               == 0) return &noiseFlow.xSpeed;
    if (strcasecmp(name, "ySpeed")               == 0) return &noiseFlow.ySpeed;
    if (strcasecmp(name, "xAmp")                 == 0) return &noiseFlow.xAmp;
    if (strcasecmp(name, "yAmp")                 == 0) return &noiseFlow.yAmp;
    if (strcasecmp(name, "xFreq")                == 0) return &noiseFlow.xFreq;
    if (strcasecmp(name, "yFreq")                == 0) return &noiseFlow.yFreq;
    if (strcasecmp(name, "xShift")               == 0) return &noiseFlow.xShift;
    if (strcasecmp(name, "yShift")               == 0) return &noiseFlow.yShift;
    if (strcasecmp(name, "modAmpRate")           == 0) return &noiseFlow.modAmp.modRate;
    if (strcasecmp(name, "modAmpLevel")          == 0) return &noiseFlow.modAmp.modLevel;
    if (strcasecmp(name, "modSpeedRate")         == 0) return &noiseFlow.modSpeed.modRate;
    if (strcasecmp(name, "modSpeedLevel")        == 0) return &noiseFlow.modSpeed.modLevel;
    if (strcasecmp(name, "modShiftRate")         == 0) return &noiseFlow.modShift.modRate;
    if (strcasecmp(name, "modShiftLevel")        == 0) return &noiseFlow.modShift.modLevel;
    // RadialFlow
    if (strcasecmp(name, "radialStep")           == 0) return &radial.radialStep;
    // DirectionalFlow
    if (strcasecmp(name, "windStep")             == 0) return &directional.windStep;
    if (strcasecmp(name, "rotateSpeed")          == 0) return &directional.rotateSpeed;
    if (strcasecmp(name, "waveAmp")              == 0) return &directional.waveAmp;
    if (strcasecmp(name, "waveFreq")             == 0) return &directional.waveFreq;
    if (strcasecmp(name, "waveSpeed")            == 0) return &directional.waveSpeed;
    // RingFlow
    if (strcasecmp(name, "innerSwirl")           == 0) return &ringFlow.innerSwirl;
    if (strcasecmp(name, "outerSwirl")           == 0) return &ringFlow.outerSwirl;
    if (strcasecmp(name, "midDrift")             == 0) return &ringFlow.midDrift;
    if (strcasecmp(name, "modBreatheRate")       == 0) return &ringFlow.modBreathe.modRate;
    if (strcasecmp(name, "modBreatheLevel")      == 0) return &ringFlow.modBreathe.modLevel;
    // Spiral
    if (strcasecmp(name, "angularStep")          == 0) return &spiral.angularStep;
    if (strcasecmp(name, "modAngularStepRate")   == 0) return &spiral.modAngularStep.modRate;
    if (strcasecmp(name, "modAngularStepLevel")  == 0) return &spiral.modAngularStep.modLevel;
    if (strcasecmp(name, "modRadialStepRate")    == 0) return &spiral.modRadialStep.modRate;
    if (strcasecmp(name, "modRadialStepLevel")   == 0) return &spiral.modRadialStep.modLevel;
    if (strcasecmp(name, "modBlendFactorRate")   == 0) return &spiral.modBlendFactor.modRate;
    if (strcasecmp(name, "modBlendFactorLevel")  == 0) return &spiral.modBlendFactor.modLevel;
    // Fluid
    if (strcasecmp(name, "viscosity")            == 0) return &fluid.viscosity;
    if (strcasecmp(name, "diffusion")            == 0) return &fluid.diffusion;
    if (strcasecmp(name, "velocityDissipation")  == 0) return &fluid.velocityDissipation;
    if (strcasecmp(name, "dyeDissipation")       == 0) return &fluid.dyeDissipation;
    if (strcasecmp(name, "vorticity")            == 0) return &fluid.vorticity;
    if (strcasecmp(name, "gravity")              == 0) return &fluid.gravity;
    if (strcasecmp(name, "solverIterations")     == 0) return &cSolverIterations;  // uint8_t cast in syncFromCVars
    if (strcasecmp(name, "modVelDissipRate")     == 0) return &fluid.modVelDissip.modRate;
    if (strcasecmp(name, "modVelDissipLevel")    == 0) return &fluid.modVelDissip.modLevel;
    if (strcasecmp(name, "modDyeDissipRate")     == 0) return &fluid.modDyeDissip.modRate;
    if (strcasecmp(name, "modDyeDissipLevel")    == 0) return &fluid.modDyeDissip.modLevel;
    return nullptr;
}

} // namespace flowFields
