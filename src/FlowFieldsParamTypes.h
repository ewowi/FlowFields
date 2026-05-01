#pragma once

// ═══════════════════════════════════════════════════════════════════
//  FlowFieldsParamTypes.h — param struct TYPE definitions only
//
//  Included by FlowFieldsEngine.h so all param structs are public
//  members of FlowFieldsEngine.  Must NOT include FlowFieldsEngine.h
//  (would create a circular dependency).
// ═══════════════════════════════════════════════════════════════════

#include "flowFieldsTypes.h"   // ModConfig, math helpers

namespace flowFields {

// ── Emitter params ────────────────────────────────────────────────

struct OrbitalDotsParams {
    uint8_t numDots         = 3;
    float   orbitSpeed      = 2.0f;
    ModConfig modOrbitSpeed = {0, 1.0f, 1.0f};
    float   dotDiam         = 1.5f;
    float   orbitDiam       = 6.6f;
    ModConfig modOrbitDiam  = {1, 1.0f, 1.0f};
    uint8_t numActiveTimers = 2;
};

struct SwarmingDotsParams {
    uint8_t   numDots          = 3;
    float     swarmSpeed       = 0.5f;
    float     swarmSpread      = 0.5f;
    ModConfig modSwarmSpread   = {10, 1.0f, 1.0f};
    ModConfig modSwarmSpeed    = {11, 1.0f, 0.0f};
    float     dotDiam          = 1.5f;
    uint8_t   numActiveTimers  = 12;
};

struct AudioDotsParams {
    float dotDiam = 1.0f;
};

struct LissajousParams {
    float     lineSpeed    = 0.35f;
    float     lineAmp      = 13.5f;
    uint8_t   lineClamp    = 0;      // 0=free(wrap), 1=clamp, 2=tether
    ModConfig modLineSpeed = {0, 1.0f, 0.0f};
    ModConfig modLineAmp   = {1, 0.5f, 0.0f};
};

struct NoiseKaleidoParams {
    float driftSpeed   = 0.35f;
    float noiseScale   = 0.0375f;
    float noiseBand    = 0.1f;
    float kaleidoGamma = 0.65f;
};

struct CubeParams {
    float     scale          = 1.0f;
    float     rotateSpeed[3] = {0.6f, 0.9f, 0.3f};
    bool      axisFreeze[3]  = {false, false, false};
    ModConfig modScale         = {0, 0.5f, 0.0f};
    ModConfig modRotateSpeedX  = {1, 0.5f, 0.0f};
    ModConfig modRotateSpeedY  = {2, 0.5f, 0.0f};
    ModConfig modRotateSpeedZ  = {3, 0.5f, 0.0f};
};

struct FluidJetParams {
    float     jetDensity  = 50.0f;
    float     jetForce    = 0.25f;
    float     jetRadius   = 2.0f;
    float     jetSpread   = 1.0f;
    float     jetAngle    = 0.0f;
    float     jetHueSpeed = 0.7f;
    ModConfig modJetForce = {0, 0.3f, 0.1f};
    ModConfig modAngle    = {1, 0.3f, 2.0f};
};

// ── Flow params ───────────────────────────────────────────────────

struct NoiseFlowParams {
    float     xSpeed          = 0.15f;
    float     ySpeed          = 0.15f;
    float     xAmp            = 1.00f;
    float     yAmp            = 1.00f;
    float     xFreq           = 0.33f;
    float     yFreq           = 0.32f;
    float     xShift          = 1.5f;
    float     yShift          = 1.5f;
    float     noiseFreq       = 0.2f;
    uint8_t   numActiveTimers = 2;
    ModConfig modAmp          = {0, 0.5f, 0.5f};
    ModConfig modSpeed        = {2, 0.1f, 0.1f};
    ModConfig modShift        = {4, 0.5f, 0.5f};
};

struct RadialParams {
    float radialStep  = 0.18f;
    float blendFactor = 0.45f;
    bool  outward     = false;
};

struct DirectionalParams {
    float windStep    = 0.95f;
    float blendFactor = 0.86f;
    float rotateSpeed = 0.25f;
    float waveAmp     = 0.0f;
    float waveFreq    = 0.20f;
    float waveSpeed   = 1.20f;
};

struct RingFlowParams {
    float     innerSwirl  = -0.2f;
    float     outerSwirl  =  0.2f;
    float     midDrift    =  0.3f;
    ModConfig modBreathe  = {0, 1.0f, 1.0f};
};

struct SpiralParams {
    float     angularStep     = 0.28f;
    float     radialStep      = 0.18f;
    float     blendFactor     = 0.45f;
    bool      outward         = false;
    ModConfig modAngularStep  = {0, 0.5f, 0.5f};
    ModConfig modRadialStep   = {1, 0.5f, 0.5f};
    ModConfig modBlendFactor  = {2, 0.5f, 0.5f};
};

struct FluidParams {
    float     viscosity            = 0.0005f;
    float     diffusion            = 0.0005f;
    float     velocityDissipation  = 0.75f;
    float     dyeDissipation       = 0.25f;
    float     vorticity            = 7.0f;
    float     gravity              = 0.3f;
    uint8_t   solverIterations     = 5;
    ModConfig modVelDissip         = {0, 0.5f, 0.0f};
    ModConfig modDyeDissip         = {1, 0.5f, 0.0f};
};

} // namespace flowFields
