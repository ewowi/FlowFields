#pragma once

// ═══════════════════════════════════════════════════════════════════
//  ORBITAL DOTS - emitter_orbitalDots.h
// ═══════════════════════════════════════════════════════════════════

#include "FlowFieldsEngine.h"

namespace flowFields {

    FL_FAST_MATH_BEGIN
    FL_OPTIMIZATION_LEVEL_O3_BEGIN


    static void emitOrbitalDots() {
        static float orbitAngle = 0.0f;

        const ModConfig& speedMod = orbitalDots.modOrbitSpeed;
        const ModConfig& diamMod  = orbitalDots.modOrbitDiam;

        // -----------------------------------------------------------------
        // 1) Plumbing: assign timer rates from the parameter configs
        // -----------------------------------------------------------------
        g_engine->timings.ratio[speedMod.modTimer] = 0.00006f * speedMod.modRate;
        g_engine->timings.ratio[diamMod.modTimer]  = 0.0005f  * diamMod.modRate;

        g_engine->calculate_modulators(orbitalDots.numActiveTimers);

        // -----------------------------------------------------------------
        // 2) Signal acquisition: get normalized modulation signals
        //    directional_noise is centered bipolar noise in roughly [-1, 1]
        // -----------------------------------------------------------------
        const float speedSignal = g_engine->move.directional_noise[speedMod.modTimer];
        const float diamSignal  = g_engine->move.directional_noise[diamMod.modTimer];

        // -----------------------------------------------------------------
        // 3) Artistic application: decide what those signals mean
        // -----------------------------------------------------------------

        // Speed modulation intentionally allows reversal:
        // modLevel = 0 -> base speed only
        // modLevel = 1 -> full bipolar modulation, including negative speed
        const float currentSpeed =
            orbitalDots.orbitSpeed *
            ((1.0f - speedMod.modLevel) + speedMod.modLevel * speedSignal);

        orbitAngle += currentSpeed * g_engine->dt;

        // Diameter modulation: centered multiplicative breathing around base orbit radius
        float radiusScale = 1.0f + diamMod.modLevel * 0.85f * diamSignal;

        // Prevent collapse into center
        radiusScale = fmaxf(radiusScale, 0.35f);

        const float fNumDots = static_cast<float>(orbitalDots.numDots);
        const float ocx = g_engine->_width * 0.5f - 0.5f;
        const float ocy = g_engine->_height * 0.5f - 0.5f;

        const float minOrbit = orbitalDots.dotDiam * 1.5f;
        const float orad = fmaxf(orbitalDots.orbitDiam * radiusScale, minOrbit);

        // -----------------------------------------------------------------
        // 4) Rendering
        // -----------------------------------------------------------------
        for (int i = 0; i < orbitalDots.numDots; i++) {
            const float a = orbitAngle + i * (2.0f * CT_PI / fNumDots);
            const float cx = ocx + fl::cosf(a) * orad;
            const float cy = ocy + fl::sinf(a) * orad;

            const ColorF c = g_engine->rainbow(g_engine->t, g_engine->colorShift, i / fNumDots);
            g_engine->drawDot(cx, cy, orbitalDots.dotDiam, c.r, c.g, c.b);
        }
    }

    FL_OPTIMIZATION_LEVEL_O3_END
    FL_FAST_MATH_END
}
