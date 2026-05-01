# FlowFields as a PlatformIO Library

## Goal

Allow FastLED-MM (and any other project) to add FlowFields as a PlatformIO library dependency:

```ini
lib_deps = ewowi/FlowFields
```

…and wrap the engine in a `ProducerModule` subclass so all emitters, flows, and parameters are
available through the FastLED-MM / projectMM UI system without any BLE dependency.

---

## What Needs to Change

### 1. Global variables defined in headers (ODR violation)

`flowFieldsTypes.h` defines variables directly in the header (not just declares them):

```cpp
bool flowFieldsInstance = false;
uint16_t (*xyFunc)(uint8_t x, uint8_t y);
float globalSpeed = 1.0f;
float persistence = 0.05f;
// …etc
```

When a library consumer includes this header from multiple translation units the linker sees
multiple definitions of the same symbol — a hard build error.  The fix is to move all
definitions into a class so each instance owns its own copy.

### 2. Compile-time grid dimensions

`HEIGHT` and `WIDTH` come from `boardConfig.h` as compile-time `#define`s, and the grids are
declared as static 2-D arrays:

```cpp
static float gR[HEIGHT][WIDTH], gG[HEIGHT][WIDTH], gB[HEIGHT][WIDTH];
```

A library consumer's LED panel may be a different size, or the size may not be known until
`setup()`.  The grids must become dynamically allocated at runtime.

### 3. No library manifest

PlatformIO needs a `library.json` at the repo root to resolve `lib_deps = ewowi/FlowFields`.
The file tells the package manager which source files belong to the library (everything except
`main.cpp` and the BLE/hardware layer).

### 4. No lifecycle API

The engine currently exposes only free functions (`initFlowFields`, `runFlowFields`).  A library
consumer needs an object it can instantiate, configure, and destroy — matching the
`setup / loop / teardown / onSizeChanged` pattern used by FastLED-MM's `ProducerModule`.

---

## Proposed Changes

### Phase 1 — Wrap the engine in a class

Create `src/FlowFieldsEngine.h` (or promote the existing `flowFieldsEngine.hpp` to a class).
Move every namespace-level variable in `flowFieldsTypes.h` and `flowFieldsEngine.hpp` into
`FlowFieldsEngine` as member variables.

Public interface:

```cpp
class FlowFieldsEngine {
public:
    // Lifecycle
    void setup(uint8_t width, uint8_t height, uint16_t numLeds,
               uint16_t (*xyFunc)(uint8_t x, uint8_t y));
    void run(fl::CRGB* leds);   // one complete frame: prepare + emit + advect + copy
    void teardown();            // free grids

    // Selection
    void setEmitter(uint8_t idx);
    void setFlow(uint8_t idx);

    // Parameter API — set/get any parameter by its camelCase name
    void  setParam(const char* name, float value);
    float getParam(const char* name) const;

    // Direct cVar access (kept for the BLE layer in main.cpp)
    float cOrbitSpeed   = 0.35f;
    float cGlobalSpeed  = 1.0f;
    float cPersistence  = 0.05f;
    // …all other cVars as public members…
};
```

The emitter and flow functions currently read namespace globals (`t`, `dt`, `gR[y][x]`, etc.).
Introduce a single file-scope pointer:

```cpp
// in FlowFieldsEngine.cpp (or at the top of the .hpp implementation block)
static FlowFieldsEngine* g_engine = nullptr;
```

`FlowFieldsEngine::run()` sets `g_engine = this` before dispatching to any emitter/flow
function.  Because embedded targets run single-threaded and only one engine instance is active
at a time, this is safe.  All existing emitter/flow headers continue to read `g_engine->gAt(x,y)`
etc. with minimal changes.

Alternatively (cleaner but more refactor work): pass `FlowFieldsEngine&` as a parameter to
every emitter/flow function.  The dispatch table types change to
`using EmitterFn = void(*)(FlowFieldsEngine&)`.  This makes the data-flow explicit and removes
the global pointer entirely — recommended if touch-up of all headers is acceptable.

### Phase 2 — Dynamic grid allocation

Replace the static 2-D arrays with heap-allocated 1-D arrays and inline accessors:

```cpp
// In FlowFieldsEngine (private members)
float* gR = nullptr;
float* gG = nullptr;
float* gB = nullptr;
float* tR = nullptr;
float* tG = nullptr;
float* tB = nullptr;
uint8_t  _width  = 0;
uint8_t  _height = 0;
uint16_t _numLeds = 0;

// Inline accessor replacing gR[y][x]
inline float& gRAt(int x, int y) { return gR[y * _width + x]; }
```

`setup()` allocates:

```cpp
void FlowFieldsEngine::setup(uint8_t w, uint8_t h, uint16_t n,
                              uint16_t (*xy)(uint8_t, uint8_t)) {
    teardown();   // safe to call even before first setup
    _width = w; _height = h; _numLeds = n; xyFunc = xy;
    size_t sz = (size_t)w * h;
    gR = new float[sz](); gG = new float[sz](); gB = new float[sz]();
    tR = new float[sz](); tG = new float[sz](); tB = new float[sz]();
    // init noise, timers, etc.
}

void FlowFieldsEngine::teardown() {
    delete[] gR; delete[] gG; delete[] gB;
    delete[] tR; delete[] tG; delete[] tB;
    gR = gG = gB = tR = tG = tB = nullptr;
}
```

Every place that currently writes `gR[y][x]` becomes `gRAt(x, y)` (or the g_engine pointer
version: `g_engine->gRAt(x, y)`).  A find-replace across the emitter/flow headers handles
the bulk of this.

### Phase 3 — Pointer binding (zero hot-path overhead)

The goal is that `loop()` contains only `engine_.run(leds_)`.  All parameter bridging happens
via **pointer binding** set up once during `setup()`.

#### Design

Each parameter in the engine is backed by two things:
- A public `float cXxx` member — the default storage, written to directly by the BLE layer.
- A private `float* pXxx` pointer — defaults to `&cXxx`, but can be redirected to any
  caller-owned float during `setup()`.

`run()` reads only through the pointers — one dereference per parameter, no string lookups,
no copies.  Because the pointers are set during `setup()` they are effectively constants at
runtime.

```cpp
class FlowFieldsEngine {
public:
    // ── Default storage (the BLE layer writes here directly) ─────────────
    float cEmitter     = 0.0f;   // float so all bindings are uniform
    float cFlow        = 0.0f;
    float cGlobalSpeed = 1.0f;
    float cPersistence = 0.05f;
    float cOrbitSpeed  = 0.35f;
    // … all other cVars …

    // ── Redirect a pointer to a caller-owned float (call once in setup) ──
    // The string lookup happens only here, never in run().
    void bindParam(const char* name, float* externalPtr);

    // ── Lifecycle ─────────────────────────────────────────────────────────
    void setup(uint8_t w, uint8_t h, uint16_t n,
               uint16_t (*xy)(uint8_t, uint8_t));
    void run(fl::CRGB* leds);   // hot path — pointer dereferences only
    void teardown();

private:
    // Pointers default to the public cVar members above.
    float* pEmitter     = &cEmitter;
    float* pFlow        = &cFlow;
    float* pGlobalSpeed = &cGlobalSpeed;
    float* pOrbitSpeed  = &cOrbitSpeed;
    // …
};
```

`bindParam` does a one-time string look-up (fine in `setup()`, never called in `run()`):

```cpp
void FlowFieldsEngine::bindParam(const char* name, float* ptr) {
    #define X(ptrMember, paramName) \
        if (strcasecmp(name, #paramName) == 0) { ptrMember = ptr; return; }
    X(pEmitter,     "emitter")
    X(pFlow,        "flow")
    X(pGlobalSpeed, "globalSpeed")
    X(pOrbitSpeed,  "orbitSpeed")
    // …
    #undef X
}
```

Inside `run()`, reading a parameter is just:

```cpp
uint8_t emitterIdx = (uint8_t)*pEmitter;   // plain pointer dereference
float   speed      = *pGlobalSpeed;
```

#### BLE / `main.cpp` usage — no binding needed

BLE callbacks write directly to the engine's `cXxx` public members.  Because `pXxx` defaults
to `&cXxx`, `run()` sees the updated value immediately with no extra wiring:

```cpp
// BLE callback on value change:
engine.cOrbitSpeed = newValue;   // pOrbitSpeed still points here → run() picks it up
```

#### FastLED-MM usage — bind once, loop is one line

```cpp
void setup() override {
    engine_.setup(WIDTH, HEIGHT, NUM_LEDS, myXY);
    addControl(emitter_,     "emitter",    "select", 0, EMITTER_COUNT - 1);
    addControl(globalSpeed_, "globalSpeed","slider",  0.1f, 5.0f);
    addControl(orbitSpeed_,  "orbitSpeed", "slider",  0.01f, 25.0f);
    // … register all controls …

    // Bind: engine reads the ProducerModule's floats directly, no copies in loop
    engine_.bindParam("emitter",     &emitter_);
    engine_.bindParam("globalSpeed", &globalSpeed_);
    engine_.bindParam("orbitSpeed",  &orbitSpeed_);
    // …
}

void loop() override {
    engine_.run(leds_);   // ← this is the entire hot path
}
```

projectMM updates `orbitSpeed_` when the slider moves; `run()` dereferences `pOrbitSpeed`
which now points at `orbitSpeed_` — zero latency, zero overhead.

### Phase 4 — Library manifest

Add `library.json` at the repo root:

```json
{
  "name": "FlowFields",
  "version": "1.0.0",
  "description": "Emitter + FlowField + Modulator LED visualization engine for ESP32/FastLED",
  "keywords": ["fastled", "led", "visualization", "esp32", "effects"],
  "authors": [{"name": "ewowi", "url": "https://github.com/ewowi"}],
  "repository": {"type": "git", "url": "https://github.com/ewowi/FlowFields"},
  "license": "MIT",
  "frameworks": ["arduino"],
  "platforms": ["espressif32"],
  "dependencies": [{"name": "fastled/FastLED", "version": ">=4.0.0"}],
  "build": {
    "srcDir": "src",
    "includeDir": "src",
    "srcFilter": "+<**/*> -<main.cpp> -<hosted_ble_bridge.cpp>"
  }
}
```

**Explicitly excluded by `srcFilter`** (the two `.cpp` files that would cause linker conflicts):

- `main.cpp` — the standalone firmware entry point
- `hosted_ble_bridge.cpp` — the hosted/desktop BLE simulation

**Effectively excluded** (headers that are never included by the library's own code — only pulled in via `main.cpp`):

- `bleControl.h` — BLE stack, parameter table, cVar declarations
- `boardConfig.h` — hardware pin assignments, compile-time `WIDTH`/`HEIGHT`, matrix maps
- `audio/` — all six audio headers
- `hosted_ble_bridge.h` — companion to the `.cpp`
- `profiler.h` — frame timing diagnostics
- `reference/` — hardware-specific XY lookup tables

**Included in the library** (everything else in `src/`):

- `FlowFieldsEngine.h` / `flowFieldsEngine.cpp` — engine class declaration and method implementations
- `flowFieldsTypes.h`, `modulators.h`, `componentEnums.h`, `parameterSchema.h` — types and shared parameter globals
- All six emitter headers (`emitters/`)
- All six flow headers (`flows/`)

The existing standalone firmware continues to build normally via `platformio.ini`; that build
includes `main.cpp` in `src/` as usual.

### Phase 5 — FastLED-MM ProducerModule example

Create `examples/FastLEDMM/FlowFieldsEffect.h`.  All setup work — control registration and
pointer binding — happens in `setup()`.  The `loop()` is a single call.

```cpp
#pragma once
#include <projectMM.h>
#include <FlowFieldsEngine.h>   // from lib_deps = ewowi/FlowFields

class FlowFieldsEffect : public ProducerModule {
public:
    const char* name()     const override { return "FlowFields"; }
    const char* category() const override { return "source"; }
    uint8_t preferredCore() const override { return 0; }

    uint16_t pixelWidth()  const override { return WIDTH; }
    uint16_t pixelHeight() const override { return HEIGHT; }

    void setup() override {
        declareBuffer(leds_, NUM_LEDS, sizeof(fl::CRGB));

        // 1. Register controls — projectMM owns and updates these floats.
        addControl(emitter_,     "emitter",     "select", 0.0f, (float)(EMITTER_COUNT - 1));
        addControl(flow_,        "flow",         "select", 0.0f, (float)(FLOW_COUNT - 1));
        addControl(globalSpeed_, "globalSpeed",  "slider", 0.1f, 5.0f);
        addControl(persistence_, "persistence",  "slider", 0.01f, 2.0f);
        addControl(colorShift_,  "colorShift",   "slider", 0.0f, 1.0f);
        addControl(orbitSpeed_,  "orbitSpeed",   "slider", 0.01f, 25.0f);
        addControl(numDots_,     "numDots",      "slider", 1.0f, 20.0f);
        // … one addControl per parameter …

        // 2. Init the engine for this panel size.
        engine_.setup(WIDTH, HEIGHT, NUM_LEDS, myXY);

        // 3. Bind: redirect engine pointers to the floats projectMM updates.
        //    Done once here; run() costs only a pointer dereference per param.
        engine_.bindParam("emitter",     &emitter_);
        engine_.bindParam("flow",        &flow_);
        engine_.bindParam("globalSpeed", &globalSpeed_);
        engine_.bindParam("persistence", &persistence_);
        engine_.bindParam("colorShift",  &colorShift_);
        engine_.bindParam("orbitSpeed",  &orbitSpeed_);
        engine_.bindParam("numDots",     &numDots_);
        // …
    }

    void onSizeChanged() override {
        engine_.teardown();
        engine_.setup(pixelWidth(), pixelHeight(), pixelWidth() * pixelHeight(), myXY);
        // Bindings survive teardown/setup because they point into this object's members.
    }

    void loop() override {
        engine_.run(leds_);   // ← entire hot path
    }

    void teardown() override { engine_.teardown(); }

    size_t classSize() const override { return sizeof(*this); }

private:
    fl::CRGB leds_[NUM_LEDS];
    FlowFieldsEngine engine_;

    // One float per parameter — projectMM writes here when controls change.
    float emitter_     = 0.0f;
    float flow_        = 0.0f;
    float globalSpeed_ = 1.0f;
    float persistence_ = 0.05f;
    float colorShift_  = 0.20f;
    float orbitSpeed_  = 0.35f;
    float numDots_     = 5.0f;
    // …
};

REGISTER_MODULE(FlowFieldsEffect)
```

`NUM_LEDS`, `WIDTH`, `HEIGHT`, and `myXY` come from the example's `main.cpp` (same as in the
existing standalone firmware).

### Phase 6 — Minimal changes to the existing `main.cpp`

The standalone firmware changes only at the two call sites.  No pointer binding is needed
because the BLE layer writes directly to the engine's public `cXxx` members, and the engine's
pointers default to pointing at those same members.

```cpp
// BEFORE
if (!flowFields::flowFieldsInstance) {
    flowFields::initFlowFields(myXY);
}
flowFields::runFlowFields();

// AFTER — hot loop is identical in shape
static FlowFieldsEngine engine;
if (!engineInitialized) {
    engine.setup(WIDTH, HEIGHT, NUM_LEDS, myXY);
    engineInitialized = true;
}
engine.run(leds);   // ← same single call, same hot path cost
```

BLE callbacks continue to write directly to the engine's members:

```cpp
// In bleControl.h / processNumber() — no change in behaviour:
engine.cOrbitSpeed = newValue;   // pOrbitSpeed points here by default → run() sees it
```

`sendEmitterState()` / `sendFlowState()` read from the same public members to serialise state
back to the UI — no additional API needed.

---

## Summary of Files Touched

| File | Change |
|------|--------|
| `src/FlowFieldsEngine.h` | **New** — `FlowFieldsEngine` class declaration |
| `src/flowFieldsEngine.hpp` | Convert namespace functions → class member implementations |
| `src/flowFieldsTypes.h` | Remove variable definitions; keep type/constant declarations only; add `gRAt` etc. helpers |
| `src/emitters/*.h` | Replace `gR[y][x]` → `g_engine->gRAt(x,y)`, `t` → `g_engine->t`, etc. |
| `src/flows/*.h` | Same as emitters |
| `src/main.cpp` | Replace `initFlowFields` / `runFlowFields` with `engine.setup()` / `engine.run()` |
| `library.json` | **New** — PlatformIO library manifest |
| `examples/FastLEDMM/FlowFieldsEffect.h` | **New** — FastLED-MM ProducerModule wrapper |

---

## Implementation Order

1. `FlowFieldsEngine` class skeleton with dynamic grids (Phase 1 + 2) — verify standalone build
2. Update all emitter/flow headers to use `g_engine` accessors — verify visuals unchanged
3. Add pointer binding (Phase 3) — update BLE layer to write to `engine.cXxx` members
4. Add `library.json` (Phase 4) — verify `lib_deps = ewowi/FlowFields` installs cleanly
5. Write and test `FlowFieldsEffect.h` example (Phase 5)

---

## Effort Estimate & Sequencing

### Per-phase breakdown

| Phase | Description | Estimate |
|-------|-------------|----------|
| 1 — `FlowFieldsEngine` class | Extract ~40 namespace globals into a class; add `g_engine` pointer; convert `initFlowFields`/`runFlowFields` to `setup()`/`run()`; move drawing primitives to member functions | 3–4 h |
| 2 — Dynamic grids | Replace `static float gR[HEIGHT][WIDTH]` with `float**` (row-pointer arrays); add `allocGrid`/`freeGrid` helpers; update all 50 grid access sites to `g_engine->gR[y][x]`; refactor `flow_fluid.h`'s internal functions from `float (*x)[WIDTH]` to `float**` (that file owns 6 internal 2D arrays and 5 helper functions that all need updating) | 3–4 h |
| 3 — Pointer binding | Add `float* pXxx` members defaulting to `&cXxx`; add `bindParam()`; update `bleControl.h` call sites | 1–2 h |
| 4 — `library.json` | New file, zero risk | 30 min |
| 5 — FastLED-MM example | New file | 1–2 h |
| 6 — `main.cpp` | 4 call-site changes + set 2 callbacks | 30 min |

**Session 1** (Phase 1 + 2 + 6): ~7–9 hours. This is the structural core. Verify the standalone
BLE firmware builds and visuals are identical before proceeding.

**Session 2** (Phase 3 + 4 + 5): ~3–5 hours. Purely additive — no existing behaviour can
break once Session 1 is verified.

### Why phase-by-phase, not all at once

Session 1 contains the only real risk: two independent mechanical changes (class extraction and
grid refactor) happen simultaneously, and `flow_fluid.h` needs the most invasive edit. Keeping
Session 2 separate means a known-good checkpoint exists before the public API surface is
finalised. A broken `bindParam` call or `library.json` misconfiguration is much easier to
diagnose when the underlying engine is already verified.

---

## For the FlowFields Repo Owner — Impact & Benefits

### What changes in your repo

The table below shows every file that would be touched in a pull request.  Files marked
**unchanged** are not part of the PR at all.

| File | Change | Detail |
|------|--------|--------|
| `src/flowFieldsTypes.h` | Refactor | Variable *definitions* moved into `FlowFieldsEngine` class; the file keeps all type declarations, math helpers, noise classes, and drawing primitives exactly as they are |
| `src/flowFieldsEngine.hpp` | Refactor | Free functions `initFlowFields` / `runFlowFields` become `FlowFieldsEngine::setup` / `::run`; logic is identical |
| `src/emitters/*.h` | Minor edit | `gR[y][x]` → `g_engine->gRAt(x,y)`, `t` → `g_engine->t` — mechanical find-replace, no logic changes |
| `src/flows/*.h` | Minor edit | Same find-replace as emitters |
| `src/main.cpp` | 4 lines changed | `initFlowFields(myXY)` → `engine.setup(...)`, `runFlowFields()` → `engine.run(leds)` |
| `src/bleControl.h` | 1 line per cVar | Bare global `cOrbitSpeed` → `engine.cOrbitSpeed` — same name, same type, same value |
| `library.json` | **New file** | PlatformIO manifest; does not affect the firmware build at all |
| `examples/FastLEDMM/FlowFieldsEffect.h` | **New file** | Consumer example; not compiled by the standalone firmware |
| Everything else | **Unchanged** | `audio/`, `modulators.h`, `parameterSchema.h`, `componentEnums.h`, `index.html`, `platformio.ini`, `boardConfig.h`, … |

The BLE system, the web UI, the audio pipeline, the modulator framework, every emitter, every
flow field — none of their *logic* changes.  The PR is a structural refactor only: state that
currently lives in namespace-level globals moves into a class that owns it.

### What you get in return

**1. Listed in the PlatformIO registry**

Once `library.json` is present and the repo is public, anyone can add your effects to their
project with one line:

```ini
lib_deps = ewowi/FlowFields
```

PlatformIO will resolve, download, and wire up your library automatically.  No forking, no
copying files.

**2. Correct C++ (no ODR violation)**

The current header-defined globals (`bool flowFieldsInstance = false;` etc.) are technically
undefined behaviour when included from more than one translation unit.  The refactor fixes
this, making the code robust as projects grow and add more `.cpp` files.

**3. Runtime-configurable panel size**

The dynamic grid allocation means the engine works on any W×H panel without recompiling.
This benefits your own project too — changing panel size no longer requires a `#define` edit
and a full rebuild.

**4. Wider audience, more contributors**

FastLED-MM / projectMM is used across a growing community of LED installations.  Exposing
FlowFields as a library means those users can drop your emitters and flow fields into their
existing rigs with a single `addControl` / `bindParam` block.  Bug reports, new emitters, and
new flow fields are more likely to come back upstream when the barrier to entry is low.

**5. Your standalone firmware is unaffected**

The existing `platformio.ini`, BLE stack, web UI (`index.html`), and audio pipeline are not
part of the PR.  You flash and run the firmware exactly as you do today.  The only visible
difference in `main.cpp` is four lines.

---

## Session 1 Retrospective

Session 1 covered Phases 1, 2, and 6 from the plan — the structural core.

### Definition of Done

| Task | Status | Notes |
|------|--------|-------|
| `src/FlowFieldsEngine.h` — class declaration + `g_engine` extern | ✅ | Dynamic grids, noise members, modulator state, lifecycle API, callbacks |
| `src/flowFieldsTypes.h` — stripped to types-only | ✅ | All variable definitions removed; math helpers, noise classes, ColorF, ModConfig kept |
| `src/modulators.h` — stripped to type definitions only | ✅ | `timers` / `modulators` structs only; instances now live in `FlowFieldsEngine` |
| `src/flowFieldsEngine.hpp` — class method implementations | ✅ | `setup`/`run`/`teardown`, `calculate_modulators`, color helpers, drawing primitives, cVar bridge, dispatch tables, `g_engine` definition |
| All 6 emitter headers — `WIDTH`/`HEIGHT`/`MIN_DIMENSION` → `g_engine->` | ✅ | Logic unchanged; mechanical find-replace only |
| 5 simple flow headers — same replacement | ✅ | `flow_noise`, `flow_radial`, `flow_directional`, `flow_rings`, `flow_spiral` |
| `flow_fluid.h` — `SIM_SIZE` constexpr → runtime; `WIDTH`/`HEIGHT` → `g_engine->` | ✅ | Local `SIM_SIZE = (float)g_engine->_minDim` added in each function that uses it |
| `src/main.cpp` — `engine.setup()` / `engine.run()` | ✅ | Old `initFlowFields`/`runFlowFields` removed; callbacks wired |
| `platformio.ini` — GitHub deps instead of local `file://` paths | ✅ | FastLED and NimBLE-Arduino now resolved from GitHub |
| Build compiles cleanly | ✅ | Verified on `seeed_xiao_esp32s3` target |

### What Went Well

- **The `g_engine` pattern was invisible to callers.** All emitter and flow functions needed only a mechanical find-replace (`WIDTH` → `g_engine->_width`, etc.).  No logic changed in any of the 12 component files.
- **Dynamic `float**` grids dropped in without syntax changes.** Callers continue to write `g_engine->gR[y][x]` — the same `[y][x]` notation works for `float**` as for `float[H][W]`.
- **The cVar bridge translated directly.** `pushDefaultsToCVars` and `syncFromCVars` became class methods with no logic changes — they already referenced named structs that are now class members.
- **Separation of concerns paid off.** Because `flowFieldsTypes.h` already grouped types and `flowFieldsEngine.hpp` already grouped logic, the class extraction was a mechanical move, not a redesign.

### What Didn't Go So Well

- **Local library dependencies blocked the build.** `lib/FastLED-e713d6f` and `libNimBLE/NimBLE-Arduino-2.5.0` are gitignored. The specific FastLED commit (e713d6f) also has a `fl::span` overload ambiguity with ESP32 Arduino 3.3.5 that the local copy had apparently been patched to fix. Switched to a pinned FastLED 4.x pre-release commit (`c83f7632`, 2026-04-11) and NimBLE-Arduino from GitHub; the FastLED version change needs on-device verification.
- **`MIN_DIMENSION` in struct defaults was not updated.** Struct member initializers like `float orbitDiam = MIN_DIMENSION * 0.3f` still reference the `boardConfig.h` compile-time macro.  These values are overwritten by `syncFromCVars()` at runtime so behavior is unchanged — but they prevent the struct from being portable to a different grid size without recompiling.
- **pyyaml missing.** PlatformIO's ESP32 Arduino 3.3.5 framework requires `pyyaml` for its build script; had to install it manually.

### Recommendations for Session 2

**Portability cleanup (recommended before Phase 4 library manifest):**

- Replace the remaining `MIN_DIMENSION` references in struct default values with hardcoded constants that match the current tuned defaults (e.g., `orbitDiam = 6.6f`, `lineAmp = 13.5f`).  These are already the effective runtime values on the 22×22 grid.
- `boardConfig.h`'s `myXY()` still uses compile-time `WIDTH`/`HEIGHT` in the bounds check and index arithmetic. For the library's `library.json` to work cleanly, this function (or a replacement) needs to be runtime-aware.  The example `FlowFieldsEffect.h` in Phase 5 should demonstrate a portable `xyFunc`.

**Phase 3 — pointer binding** is purely additive: new `float* pXxx` members defaulting to `&cXxx`, and a `bindParam()` method.  No existing code changes.  Zero risk.

**Phase 4 — `library.json`** is a new file.  Zero risk.

**Phase 5 — FastLED-MM example** is a new file.  Zero risk.

**Before Phase 4:** verify on device that the FastLED version change (pinned pre-release 4.x commit `c83f7632` instead of the old local copy) produces identical visuals.  If there are regressions, bisect to a different 4.x commit in `platformio.ini`.

---

## Session 2 Retrospective

Session 2 covered the portability cleanup, Phase 4 (library manifest), Phase 5 (illustrative example), Phase 3 (pointer binding), and two pre-use library fixes (ODR violation and missing compiled source).

### Definition of Done

| Task | Status | Notes |
|------|--------|-------|
| Portability cleanup — fix null-ptr struct defaults | ✅ | `orbitDiam = g_engine->_minDim * 0.3f` → `6.6f`; `lineAmp = (g_engine->_minDim - 4) * 0.75f` → `13.5f` |
| `library.json` — PlatformIO library manifest | ✅ | ArduinoJson `^7.4.2`; excludes `main.cpp` and `hosted_ble_bridge.cpp`; note on FastLED 4.x pre-release |
| `examples/FastLEDMM/FlowFieldsEffect.h` — integration template | ✅ | `setup()` → `addControl` + `bindParam`; `loop()` → single `engine_.run(leds_)` |
| Phase 3 — pointer binding (`bindParam` + `resolveCVar`) | ✅ | `bindParam(name, float*)` registered once in `setup()`; `run()` applies via pointer deref, zero string lookups in hot path |
| ODR fix — `parameterSchema.h` variable definitions | ✅ | `inline` added to all ~70 non-const variable definitions; safe for C++17 (ESP32 Arduino GCC 12 default) |
| Library source file — `flowFieldsEngine.hpp` → `.cpp` | ✅ | PlatformIO only compiles `.c`/`.cpp` as TUs; `.hpp` was never compiled in library builds → link errors. Renamed; `#pragma once` removed; `#include "flowFieldsEngine.hpp"` removed from `main.cpp` |
| Compile regression fix — missing `FlowFieldsEngine.h` in `main.cpp` | ✅ | Removing the `.hpp` include also removed the only path to the engine class declaration; `#include "FlowFieldsEngine.h"` added explicitly |
| Audio compile fix — `emitters_other.h` `myAudio::` guard | ✅ | All audio code wrapped in `#ifdef AUDIO_ENABLED`; no-op stub compiled otherwise. Prevents compile error when `flowFieldsEngine.cpp` is a separate TU without audio headers |

### What Went Well

- **The null-ptr bug was caught before device testing.** The `g_engine->_minDim` struct default initializers introduced in Session 1's find-replace would have crashed at startup — caught during the portability review step.
- **`library.json` is exactly 20 lines.** The `srcFilter` approach cleanly excludes the BLE/hardware entry points without touching any source file.
- **The example's hot path is a single line.** After pointer binding, `loop()` is `engine_.run(leds_)` — zero copy, zero string lookup per frame.
- **The ODR fix is non-breaking.** `inline` variables in C++17 have identical semantics to non-inline for single-TU projects; they just allow multiple TUs to each see the definition without a linker error.

### What Didn't Go So Well

- **The find-replace in Session 1 introduced the null-ptr bug.** Replacing `MIN_DIMENSION` in struct default initializers with `g_engine->_minDim` was incorrect — `g_engine` is null at static initialization time. The correct fix (hardcoded values) was applied in Session 2.
- **The `.hpp` extension was a latent link-time bomb.** `flowFieldsEngine.hpp` contained all class method implementations, but PlatformIO's library builder only compiles `.c`/`.cpp` files. The main firmware compiled correctly because `main.cpp` `#include`d the file directly, but any library consumer would have seen unresolved symbols. Not caught until pre-use review.
- **ODR violations were hidden.** `parameterSchema.h` defined ~70 mutable variables directly — no `inline`, no `extern` + single definition. The firmware compiled because it was only built as a single TU (main.cpp included everything). A library consumer linking a second TU would have hit duplicate symbol errors immediately.
- **The `.hpp` rename revealed two regressions.** (1) Removing `#include "flowFieldsEngine.hpp"` from `main.cpp` also removed the only path to the `FlowFieldsEngine` class declaration — fixed by adding `#include "FlowFieldsEngine.h"` explicitly. (2) `emitters_other.h` (compiled via `flowFieldsEngine.cpp`) referenced `myAudio::` types that were previously supplied transitively through `main.cpp`'s TU — now a separate TU, those types were invisible. Adding `audioProcessing.h` to `flowFieldsEngine.cpp` would have fixed the compile error but created link-time duplicate-symbol errors (audioTypes.h has non-inline variable definitions). Correct fix: `#ifdef AUDIO_ENABLED` guard with a no-op stub.

### Remaining Backlog

| Item | Priority | Notes |
|------|----------|-------|
| Verify visuals on device with FastLED 4.x | High | Do before merging to `main` |
| `numDots` binding gap | ✅ Done | `cNumDots` changed to `float`; cast to `(uint8_t)` at struct-write sites; added to `resolveCVar` |
| Audio integration (see below) | Medium | Step 1 done (`#ifdef AUDIO_ENABLED` guard + stub). Steps 2–3 remain: `audioTypes.h` inline fix + engine audio-data pointer |
| Typed cVar storage (see below) | Low | All cVars are currently `float`; several should be `uint8_t`, `bool`, or `uint16_t` |
| `boardConfig.h` `myXY()` — make runtime-aware | Low | Relevant when panel size changes at runtime; illustrative example already shows the pattern |

#### Audio integration

**How audio is currently used**

The audio pipeline lives entirely in `src/audio/` and is excluded from the library build. `emitters_other.h` now guards all audio code with `#ifdef AUDIO_ENABLED` and compiles a no-op stub otherwise — so the library and firmware (without `-DAUDIO_ENABLED`) build cleanly. When `AUDIO_ENABLED` is defined, the emitter references:

```cpp
if (myAudio::busC.newBeat) { … }        // beat-triggered dot spawn
cFrame = &myAudio::updateAudioFrame(b); // pull latest FFT + RMS frame
```

The `myAudio::` globals live in `audio/audioTypes.h`, which chains to FastLED's own `fl/audio/audio.h` and `fl/audio/fft/fft.h` (new in FastLED 4.x). Three frequency-band beat detectors (`busA` = kick/bass, `busB` = snare/mid, `busC` = vocals/lead) each expose `newBeat` (bool) and `avResponse` (float). A per-frame `AudioFrame` snapshot carries RMS, peak, FFT bins, vox confidence, and copies of all three bus outputs.

`emitAudioDots()` listens to `busB` and `busC` beats and spawns dots at random positions. Everything else in the engine is audio-unaware.

**The remaining problem**

`audioTypes.h` defines its bus and config objects as non-`inline` variables at namespace scope. Including it from two TUs (e.g. `flowFieldsEngine.cpp` and `main.cpp`) causes linker "multiple definition" errors. This is why step 1 uses a compile-time guard rather than simply including the audio headers in `flowFieldsEngine.cpp`.

**What still needs to happen**

1. ~~**Guard the audio references**~~ ✅ Done — `#ifdef AUDIO_ENABLED` guard with no-op stub in `emitters_other.h`.

2. **Expose an audio data pointer on the engine** — add an optional `const myAudio::AudioFrame* audioFrame = nullptr;` field (or a generic equivalent) to `FlowFieldsEngine`. Library consumers set it before each `run()` call. Emitters read from it instead of calling `myAudio::updateAudioFrame()` directly.

3. **FastLED-MM integration** — a FastLED-MM consumer would pull an `AudioFrame` from FastLED's audio API each loop and pass it to the engine:
   ```cpp
   void loop() override {
       engine_.audioFrame = myAudio::getLatestFrame();  // or equivalent FA API
       engine_.run(leds_);
   }
   ```

This approach keeps the engine itself audio-agnostic — it holds a pointer that is either null (no audio, emitter is a no-op) or valid (audio data flows through).

#### Typed cVar storage

**Why everything is currently `float`**

The BLE transport sends all parameter values as JSON floating-point numbers (`{id: "inNumDots", val: 3.0}`). `processNumber()` receives a `float` and assigns it directly to the cVar. The `resolveCVar()` / `bindParam()` binding mechanism also returns `float*`, so any non-float cVar is invisible to library consumers.

The PARAMETER_TABLE X-macro *does* carry the intended type — `X(uint8_t, NumDots, 3)`, `X(bool, AxisFreezeX, false)`, `X(uint16_t, NoiseGateOpen, 70)` — and uses it for preset serialization. But the live cVar storage was never made to match, because the single-TU BLE firmware was the only consumer and implicit float→uint8_t narrowing silently worked.

`cNumDots` was changed to `float` during the Session 2 binding-gap fix as the path of least resistance. Several others should also be their real types:

| cVar | Current type | Natural type |
|------|-------------|--------------|
| `cNumDots` | `float` (was `uint8_t`) | `uint8_t` |
| `cBright` | `uint8_t` | `uint8_t` ✓ (not bound) |
| `cUseRainbow`, `cAxisFreezeX/Y/Z`, `cOutward` | `bool` | `bool` (not bound) |
| `cNoiseGateOpen`, `cNoiseGateClose` | `uint16_t` | `uint16_t` (not bound) |
| `maxBins`, `autoFloor`, `avLeveler` | `bool` | `bool` (not bound) |

**What needs to happen**

`resolveCVar` and `bindParam` would need typed overloads (or a tagged-union / `std::variant` slot) so that `uint8_t`, `bool`, and `uint16_t` cVars can participate in binding without being widened to `float`. The BLE `processNumber()` already has the type available via the X-macro and could cast on assignment instead of relying on implicit conversion.

---

## Session 3 Retrospective

Session 3 addressed four areas: a latent bug in the binding mechanism (controls not updating), encapsulation of two remaining global variables, PSRAM-aware memory allocation, and dimension-type widening for large-panel support.

### Definition of Done

| Task | Status | Notes |
|------|--------|-------|
| `EMITTER`/`FLOW` global encapsulation | ✅ | Moved into `FlowFieldsEngine` as `_emitter`/`_flow` members; `bleControl.h` updated to read/write via `g_engine->_emitter`/`_flow`; extern declarations removed from `parameterSchema.h` |
| PSRAM-aware grid allocation | ✅ | `ps_malloc()` used for all 8 float arrays (gR/gG/gB, tR/tG/tB, xProf, yProf) on `BOARD_HAS_PSRAM` devices; `malloc()` fallback otherwise; both freed with `free()` |
| Binding fix — controls not propagating in FastLED-MM | ✅ | Root cause found and fixed; `resolveField` replaces `resolveCVar`; bindings now target struct fields directly and are applied after `syncFromCVars()` |
| Dimension type widening | ✅ | `_width`/`_height`/`_minDim`: `uint8_t` → `uint16_t`; `_numLeds`: `uint16_t` → `uint32_t`; `xyFunc` return: `uint16_t` → `uint32_t`; supports up to 1024×1024 (1M pixels) for PC targets |
| `EMITTER_NAMES` / `FLOW_NAMES` arrays | ✅ | Added as `inline const char*` arrays to `componentEnums.h`, parallel to the enum definitions; available to any consumer without pulling in BLE infrastructure |
| `FlowFieldsEffect.h` — select option population | ✅ | `addSelectOption` loops added for emitter and flow controls using the new name arrays |

### Change Details

#### 1. `EMITTER` / `FLOW` global encapsulation

`EMITTER` and `FLOW` were `uint8_t` globals defined in `main.cpp` and declared `extern` in `parameterSchema.h`. Any library consumer had to define them manually in their own code — a leaking implementation detail that caused linker errors in FastLED-MM.

Both are now `uint8_t _emitter` / `uint8_t _flow` members of `FlowFieldsEngine`. The BLE layer writes through `g_engine->_emitter` and `g_engine->_flow`; library consumers control them via `bindParam("emitter", ...)` / `bindParam("flow", ...)` exactly like any other parameter. **The standalone firmware is unaffected** — BLE callbacks work identically, just through the engine pointer that was already required.

#### 2. PSRAM-aware grid allocation

The eight float arrays that hold the pixel colour grid (six `float**` colour channels plus `xProf`/`yProf` noise profiles) are the largest allocations in the engine. On PSRAM-equipped boards (S3, S3 mini, etc.) they previously landed in the small internal DRAM heap, limiting usable panel size and leaving SPIRAM idle.

A `psramAlloc(count)` helper now calls `ps_malloc()` when `BOARD_HAS_PSRAM` is defined, with a `malloc()` fallback. `free()` is used for both (valid on ESP32). The change is gated by the standard build flag — boards without PSRAM compile and run identically to before. **This is a direct improvement for the standalone firmware** on any PSRAM-equipped ESP32.

#### 3. Binding fix — controls not propagating (`resolveField`)

This was the most important fix in Session 3. In FastLED-MM, moving a slider had no visible effect on the engine even though `bindParam` appeared to work at startup.

**Root cause — execution order in `run()`:**

The old sequence was:
1. Copy external floats → cVars (binding step)
2. On emitter/flow change: `pushDefaultsToCVars()` — **overwrites cVars with struct defaults**
3. `syncFromCVars()` — structs get the defaults, not the external values

`pushDefaultsToCVars()` fires on the first frame (because `lastEmitter` starts at 255) and on every emitter or flow switch, silently discarding whatever the binding had written. A secondary bug: `persistence` was split into `cPersistence + cPersistFine`; the binding wrote only to `cPersistence`, so `cPersistFine` accumulated as a permanent offset.

**Fix — two changes working together:**

**(a)** `resolveCVar` renamed to `resolveField` and changed to return **struct field pointers** (`&orbitalDots.orbitSpeed`, `&noiseFlow.xSpeed`, `&globalSpeed`, `&persistence`, …) instead of cVar global pointers. Special cases that need cVar fan-out (shared params like `numDots`, `dotDiam`, `blendFactor`; and uint8_t-target params like `lineClamp`, `solverIterations`) keep cVar pointers — these are documented in the function with a comment explaining the one-frame lag.

**(b)** Binding application **moved to after `syncFromCVars()`** in `run()`. The new sequence is:

```
1. pushDefaultsToCVars()  — if emitter/flow changed: defaults → cVars
2. syncFromCVars()        — BLE path:  cVars → struct fields
3. binding loop           — library path: external floats → same struct fields (overrides step 2)
```

Because bindings write to the struct fields that the emitters and flows read directly — and they run last — external values always win, regardless of what `pushDefaultsToCVars` or `syncFromCVars` wrote. The `persistence` split bug disappears because "persistence" now resolves to `&persistence` (the engine member), which `syncFromCVars` writes and the binding then overrides with the raw external value.

**The BLE firmware path is unchanged.** BLE writes to cVars; `syncFromCVars` copies to structs; no bindings are registered so the binding loop is a no-op. Existing behaviour is bit-for-bit identical.

#### 4. Dimension type widening

`_width` and `_height` were `uint8_t` (max 255), `_numLeds` was `uint16_t` (max 65 535). PC targets running the engine as a library can address panels of 1024×1024 = ~1 M pixels — both limits are far exceeded.

Changed to `uint16_t` for the spatial dimensions and `uint32_t` for the LED count, propagated consistently through `setup()`, `allocGrid()`, `freeGrid()`, the LED-copy loop, `xyFunc` pointer type, `myXY` in `boardConfig.h` and the example, `ledNum` in `main.cpp`, and the index arithmetic (explicit `(uint32_t)y * width + x` cast to prevent 16-bit overflow). ESP32 builds are unaffected — the wider types have identical runtime cost on a 32-bit MCU.

#### 5. `EMITTER_NAMES` / `FLOW_NAMES` in `componentEnums.h`

The emitter and flow name strings existed only in `parameterSchema.h` — a BLE-infrastructure header not appropriate to include from a library consumer. `componentEnums.h` is the natural home for them: they are the string counterpart of the enum values defined in the same file.

Two `inline const char*` arrays were added:

```cpp
inline const char* const EMITTER_NAMES[EMITTER_COUNT] = {
    "orbitaldots", "swarmingdots", "audiodots", "lissajous",
    "borderrect",  "noisekaleido", "cube",       "fluidjet"
};

inline const char* const FLOW_NAMES[FLOW_COUNT] = {
    "noise", "radial", "directional", "rings", "spiral", "fluid"
};
```

Any consumer that includes `FlowFieldsEngine.h` already gets `componentEnums.h` transitively. No PROGMEM — that is a firmware-specific concern handled separately in `parameterSchema.h`.

#### 6. Select option population in `FlowFieldsEffect.h`

The example was registering emitter and flow controls as `"select"` type but never populating the option list, leaving the UI with a numeric range but no labels. Two `addSelectOption` loops now follow each `addControl` call:

```cpp
addControl(emitterIdx_, "emitter", "select", 0.0f, (float)(EMITTER_COUNT - 1), 0.0f);
for (int i = 0; i < EMITTER_COUNT; i++)
    addSelectOption(emitterIdx_, EMITTER_NAMES[i]);
```

The loop is indexing the same `EMITTER_NAMES` array whose order matches the `Emitter` enum — no possibility of label/value mismatch.

### What Went Well

- **The binding bug had a clean fix.** Once the execution order in `run()` was understood, the solution was a targeted reorder plus a rename (`resolveCVar` → `resolveField`) with updated return values. No architecture changes outside `flowFieldsEngine.cpp`.
- **PSRAM allocation is fully backwards compatible.** The `#if defined(BOARD_HAS_PSRAM)` guard means boards without PSRAM see exactly the old behaviour. Adding the flag to `platformio.ini` is the only step needed to enable PSRAM on a new board.
- **Type widening required no logic changes.** Widening integer types on a 32-bit MCU is safe by construction — no overflow, no silent truncation. The only required care was the `xyFunc` index computation, which got an explicit cast.
- **`EMITTER_NAMES`/`FLOW_NAMES` close a gap in the library's API surface.** Before this change, a consumer had no way to get human-readable names for the enum values without depending on the BLE layer.

### What Didn't Go So Well

- **The binding bug was not caught in Session 2.** The `bindParam` mechanism was designed and tested conceptually but not exercised in FastLED-MM against a running UI. The ordering problem (`pushDefaultsToCVars` silently overwriting binding results) only became visible when a real consumer moved a slider and saw no response.
- **`resolveField` has two tiers of correctness.** Most params resolve directly to struct fields (immediate effect). A few shared or type-cast params (`numDots`, `dotDiam`, `blendFactor`, `lineClamp`, `solverIterations`) still go through cVars and take effect one frame later. This is documented and invisible in practice, but it is an inconsistency that could surprise someone adding a new parameter.

### Remaining Backlog (updated)

| Item | Priority | Notes |
|------|----------|-------|
| Verify visuals on device with FastLED 4.x | High | Do before merging to `main` |
| Audio integration steps 2–3 | Medium | Engine audio-data pointer + FastLED-MM loop wiring |
| Typed cVar storage | ✅ Resolved in Session 4 | Engine members now use their natural types; string dispatch in BLE casts explicitly |
| `resolveField` two-tier consistency | ✅ Resolved in Session 4 | `resolveField` and cVar bridge removed entirely |

---

## Session 4 Retrospective

Session 4 completed a structural simplification that Sessions 2 and 3 had been building toward: the elimination of all cVar globals, all sync functions, all pointer binding, and the engine's string-based parameter API. The result is a cleaner architecture with one, obvious mechanism for setting parameters.

### Motivation — The Two-Mechanism Problem

After Session 3 the engine had two parallel paths for changing a parameter value:

| Consumer | Write path | Read path |
|----------|-----------|-----------|
| BLE firmware | `c##parameter` global → `syncFromCVars()` → struct field | `sendEmitterState()` reads `c##parameter` via X-macro |
| Library consumer (FastLED-MM) | `bindParam(name, &myFloat)` → binding loop in `run()` → struct field | — |

Both paths wrote to the same struct fields that emitters and flows actually read. The cVar globals were purely an intermediate layer that the BLE path couldn't avoid (they were the canonical store) and the library path had to fight around (bindings had to overwrite what `syncFromCVars` just wrote, causing the ordering bug fixed in Session 3).

The presence of two mechanisms had concrete costs:
- `syncFromCVars()` and `pushDefaultsToCVars()` — ~250 lines of boilerplate that duplicated every parameter assignment, needed to be kept in sync with struct definitions, and was the source of the Session 3 binding bug.
- The `bindParam` / `resolveField` machinery — string dispatch in `setup()` plus a per-frame binding loop in `run()`.
- All parameters forced to `float` — `bool`, `uint8_t`, `uint16_t` members had to be shadowed by float cVars, and shared params (e.g. `blendFactor` used by three flow structs) required fan-out logic in `syncFromCVars`.
- One-frame lag for shared/cast params — the Session 3 retrospective documented this as a known inconsistency in `resolveField`.

### What Changed

#### 1. All param struct types extracted to `FlowFieldsParamTypes.h`

**The problem:** Param structs were defined inside the emitter/flow headers (e.g. `OrbitalDotsParams` in `emitter_orbitalDots.h`). For `FlowFieldsEngine` to own instances of them as members, `FlowFieldsEngine.h` would need to include those headers. But those headers include `FlowFieldsEngine.h` — a circular dependency the preprocessor cannot resolve.

**The fix:** A new `src/FlowFieldsParamTypes.h` contains only the 13 struct type definitions and includes nothing except `flowFieldsTypes.h`. The engine header includes `FlowFieldsParamTypes.h`; the emitter/flow headers include `FlowFieldsEngine.h` as before. The cycle is broken.

This is a standard C++ pattern: types that appear in a class declaration live in a header that has no reverse dependency on that class. The struct definitions themselves are unchanged — the move is purely mechanical.

#### 2. All struct instances moved into `FlowFieldsEngine` as public members

```cpp
// FlowFieldsEngine.h (public section)
OrbitalDotsParams  orbitalDots;
SwarmingDotsParams swarmingDots;
AudioDotsParams    audioDots;
LissajousParams    lissajous;
NoiseKaleidoParams noiseKaleido;
CubeParams         cube;
FluidJetParams     fluidJet;

NoiseFlowParams    noiseFlow;
RadialParams       radial;
DirectionalParams  directional;
RingFlowParams     ringFlow;
SpiralParams       spiral;
FluidParams        fluid;
```

The namespace-level singleton instances that existed in each emitter/flow header are removed. Emitter and flow functions already accessed everything through `g_engine` — changing `orbitalDots.orbitSpeed` to `g_engine->orbitalDots.orbitSpeed` throughout those headers was the only mechanical change required.

On emitter or flow change, `run()` resets all structs for that type to their default-constructed values (`orbitalDots = OrbitalDotsParams{};` etc.), replacing `pushDefaultsToCVars()` with direct, zero-overhead initialization.

#### 3. `setParam` / `getParam` / `resolveField` removed from the engine

These three methods are deleted from `FlowFieldsEngine` entirely. The engine no longer has any string-based API. Its public surface is:

- Lifecycle: `setup()`, `run()`, `teardown()`
- State: all param struct members, `_emitter`, `_flow`, `globalSpeed`, `persistence`, `colorShift`, `useRainbow`
- Modulators: `calculate_modulators()`
- Drawing: `drawDot()`, `drawAASubpixelLine()`, etc.

#### 4. All cVar globals removed (except audio)

`parameterSchema.h` previously declared ~70 `inline float c##Xxx` globals covering every engine parameter. These are removed. The file now contains only:

- `cBright`, `cMapping`, `cOverrideMapping`, `cEaseSat`, `cEaseLum` — hardware/display globals owned by `main.cpp`, not the engine
- All audio cVars (`cAudioGain`, `cNoiseGateOpen`, etc.) — see note below
- The `PARAMETER_TABLE` X-macro, now reduced to 15 audio/misc entries

`syncFromCVars()` and `pushDefaultsToCVars()` (and the two corresponding flow variants) are deleted with the cVars they served.

**Why audio cVars were not removed:** Audio params (`cAudioGain`, `cNoiseGateOpen`, `cThreshold`, etc.) are not engine parameters — they control the separate audio pipeline (`src/audio/`) which the `FlowFieldsEngine` class has no knowledge of. There are no corresponding `AudioParams` struct members on the engine to move them into. The audio subsystem is deliberately kept outside the library boundary (excluded by `library.json`'s `srcFilter`). Cleaning up the audio cVars is a separate task that belongs to the audio integration work (steps 2–3 of the audio backlog item).

#### 5. String dispatch moved to `bleControl.h` — where it belongs

BLE receives parameters as JSON strings (`{"id":"inOrbitSpeed","val":2.5}`). Some translation between string and struct field is unavoidable. The question is whose responsibility it is.

Previously the engine owned this via `setParam`/`getParam`. Session 4 moves it into `bleControl.h` as two `static` functions: `bleSetEngineParam(name, value)` and `bleGetEngineParam(name)`. These functions contain the same if-chain that `resolveField` did, now writing and reading `g_engine->structName.field` directly:

```cpp
static void bleSetEngineParam(const char* name, float value) {
    FlowFieldsEngine* e = g_engine;
    if (strcasecmp(name, "orbitSpeed") == 0) { e->orbitalDots.orbitSpeed = value; return; }
    if (strcasecmp(name, "blendFactor") == 0) {
        e->radial.blendFactor = value;
        e->directional.blendFactor = value;
        e->spiral.blendFactor = value;
        return;
    }
    // …
}
```

The BLE caller is now explicit about what it is doing: translating a wire-format string into a direct struct-field write. The engine doesn't need to know about strings at all.

**`processNumber()`** strips the "in" prefix and calls `bleSetEngineParam`, then falls through to the PARAMETER_TABLE loop for audio cVars:

```cpp
if (receivedID.startsWith("in"))
    bleSetEngineParam(receivedID.c_str() + 2, receivedValue);
// PARAMETER_TABLE handles audio/misc cVars
```

**`sendEmitterState()` / `sendFlowState()` / `sendGlobalState()`** call `bleGetEngineParam(paramName)` for each parameter in the lookup table instead of reading cVars via X-macro.

**`processCheckbox()`** sets bool engine members directly:

```cpp
if (receivedID == "cx21") { g_engine->cube.axisFreeze[0] = receivedValue; }
if (receivedID == "cx31") { g_engine->spiral.outward = receivedValue; g_engine->radial.outward = receivedValue; }
if (receivedID == "cx32") { g_engine->useRainbow = receivedValue; }
```

This also fixes the typed-cVar issue from the Session 3 backlog: `bool outward`, `bool axisFreeze[]`, and `bool useRainbow` are now set as actual bools — no float cast required.

#### 6. `FlowFieldsEffect.h` — `onUpdate()` + single-line `loop()`

The example uses `onUpdate(name)` — a projectMM callback fired only when a control actually changes value — to push new values into the engine. `loop()` is reduced to a single call:

```cpp
void onUpdate(const char* /*name*/) override {
    engine_._emitter    = emitterIdx_;      // uint8_t — no cast needed
    engine_._flow       = flowIdx_;         // uint8_t — no cast needed
    engine_.globalSpeed = globalSpeed_;
    engine_.orbitalDots.numDots  = numDots_;  // uint8_t — no cast needed
    engine_.swarmingDots.numDots = numDots_;
    engine_.orbitalDots.orbitSpeed = orbitSpeed_;
    engine_.noiseFlow.xSpeed      = xSpeed_;
    // … all other struct-field assignments …
}

void loop() override { engine_.run(leds); }
```

Because `onUpdate` is called outside the render hot path (only on user interaction or preset load, not 60 × per second), there is no per-frame overhead for parameter writes. The private members `emitterIdx_`, `flowIdx_`, and `numDots_` are declared as `uint8_t` rather than `float` — they match the struct field types exactly so no cast is needed when assigning into the engine.

The `bindParam` mechanism from Sessions 2–3 is entirely removed from the example.

### Definition of Done

| Task | Status | Notes |
|------|--------|-------|
| `FlowFieldsParamTypes.h` — 13 struct type definitions | ✅ | No engine dependency; breaks circular include |
| Struct instances added to `FlowFieldsEngine` as public members | ✅ | Replaces namespace-level singletons in emitter/flow headers |
| All 13 emitter/flow headers updated to `g_engine->struct.field` | ✅ | Mechanical find-replace; logic unchanged |
| `setParam` / `getParam` / `resolveField` removed from engine | ✅ | Engine has no string-based API |
| All engine cVar globals removed from `parameterSchema.h` | ✅ | ~70 `inline float c##Xxx` declarations deleted |
| `PARAMETER_TABLE` reduced to audio/misc (15 entries) | ✅ | Engine params no longer go through X-macro |
| `syncFromCVars` / `pushDefaultsToCVars` implementations deleted | ✅ | ~250 lines of boilerplate removed from `flowFieldsEngine.cpp` |
| `bleSetEngineParam` / `bleGetEngineParam` added to `bleControl.h` | ✅ | Full string→field mapping owned by BLE, not the engine |
| `processNumber` updated to call `bleSetEngineParam` | ✅ | Engine params routed directly; audio/misc still via PARAMETER_TABLE |
| `sendEmitterState` / `sendFlowState` / `sendGlobalState` updated | ✅ | Read via `bleGetEngineParam`; no X-macro cVar reads |
| `processCheckbox` updated to set bool engine members directly | ✅ | `axisFreeze[]`, `outward`, `useRainbow` set as bools |
| `FlowFieldsEffect.h` `onUpdate()` — direct member writes on control change | ✅ | Assignments out of hot path; `loop()` is one line; `uint8_t` members require no cast |

### Benefits

**For the engine's public API:**

The engine is now a plain C++ class with typed public members. Any consumer — BLE, FastLED-MM, a test harness, a future MIDI controller — reads and writes it the same way:

```cpp
engine.orbitalDots.orbitSpeed = 2.5f;   // obvious, immediate, no overhead
engine._emitter = EMITTER_SWARMINGDOTS;
```

No methods to learn, no string names to remember, no float restriction on parameter types.

**For the BLE firmware (unchanged behaviour, simpler code):**

The BLE path was always: receive string → find parameter → write value. That logic now lives in `bleControl.h` where it belongs, expressed as direct struct-field writes. The behaviour is identical. The complexity is now local to the layer that introduced it.

**For library consumers:**

The `bindParam` pattern from Sessions 2–3 is gone. There is nothing to bind and no setup-time overhead. A FastLED-MM module pushes control values into engine struct fields in `onUpdate()` — called only when a slider actually moves, not every frame. `loop()` is a single line. That is the same direct-write pattern the BLE firmware uses — one mechanism for everyone.

**For maintainability:**

Adding a new parameter now requires two things: add a field to the relevant param struct in `FlowFieldsParamTypes.h`, and add two lines (one set, one get) to the string tables in `bleControl.h`. No cVar declaration, no X-macro entry, no `syncFromCVars` line, no `pushDefaultsToCVars` line. The old process was a 5-file change; the new process is a 2-file change.

**Type correctness:**

Boolean and integer parameters (`outward`, `axisFreeze[]`, `lineClamp`, `solverIterations`) are now their natural types throughout. The `float`-everywhere constraint was an artefact of the `resolveCVar`/`bindParam` returning `float*`. `bleSetEngineParam` can cast explicitly (`(uint8_t)value`, `(bool)value`) at the BLE boundary where the type conversion is intentional, rather than silently throughout the engine.

### Trade-offs and Honest Assessment

**The string dispatch table still exists** — it moved from the engine to `bleControl.h`. This is the right location for it: it is BLE's responsibility to translate wire-format strings into typed C++ writes. The engine should not carry this dependency. .

**`FlowFieldsParamTypes.h` is an added file.** The 13 struct definitions are no longer co-located with their emitter/flow functions. This is the minimum cost of having the engine own typed instances of each struct — the circular dependency that makes this necessary is explained in the [session 4 motivation section above](#session-4-retrospective). The structs themselves are unchanged; only their location in the source tree moved.

**The example's `loop()` is a single line.** The `onUpdate()` callback handles all struct-field assignments when controls change — not on every frame. `loop()` is just `engine_.run(leds)`. This is cleaner than the Session 2 bindParam approach (which still required 40 `bindParam` calls in `setup()` and had a hidden ordering hazard) and cleaner than writing all assignments in `loop()` every frame. Param writes happen exactly as often as the user moves a slider.

### Remaining Backlog (updated)

| Item | Priority | Notes |
|------|----------|-------|
| Verify visuals on device with FastLED 4.x | High | Do before merging to `main` |
| Audio integration steps 2–3 | Medium | Engine audio-data pointer + FastLED-MM loop wiring |
| `FlowFieldsEffect.h` `onUpdate()` refactor | ✅ Done | Implemented in Session 4; `loop()` is one line |

---

## Possible Session 5: Dynamic Parameter Visibility

### Problem Statement

The engine supports 8 emitters × 6 flows = 48 distinct combinations. Each combination has a different relevant parameter set. The current `FlowFieldsEffect.h` registers all 35 controls unconditionally in `setup()` — but at any active combination only 8–15 of those controls are meaningful. The other 20–27 are visible but irrelevant, creating UI noise and making the effect harder to use.

### Parameter Relevance

**Global — always relevant (5 controls):**
emitter (select), flow (select), globalSpeed, persistence, colorShift

**Emitter-specific:**

| Emitter | Relevant params |
|---------|----------------|
| orbitaldots | numDots, dotDiam, orbitSpeed, orbitDiam |
| swarmingdots | numDots, dotDiam, swarmSpeed, swarmSpread |
| audiodots | *(none currently exposed)* |
| lissajous | lineSpeed, lineAmp |
| borderrect | *(none)* |
| noisekaleido | driftSpeed, noiseScale, noiseBand |
| cube | scale, rotateSpeedX, rotateSpeedY, rotateSpeedZ |
| fluidjet | jetForce, jetAngle |

**Flow-specific:**

| Flow | Relevant params |
|------|----------------|
| noise | xSpeed, ySpeed, xShift, yShift, xFreq, yFreq |
| radial | blendFactor |
| directional | blendFactor |
| rings | innerSwirl, outerSwirl, midDrift |
| spiral | angularStep, blendFactor |
| fluid | viscosity, vorticity, gravity |

Note that `numDots`/`dotDiam` are shared between two emitters, and `blendFactor` is shared across three flows. The groupings are already annotated as comments in `FlowFieldsEffect.h`.

### Design Options

**Option A — Dynamic control registration** *(recommended if projectMM adds the API)*

Detect emitter/flow change in `onUpdate()`, call `clearControls()`, then re-register only the relevant subset. Control values are not lost — they live in the private members, not in projectMM.

```cpp
// In onUpdate():
if (emitterIdx_ != lastEmitterIdx_ || flowIdx_ != lastFlowIdx_) {
    registerControls();   // clearControls() + targeted addControl() calls
    lastEmitterIdx_ = emitterIdx_;
    lastFlowIdx_    = flowIdx_;
}
```

The `registerControls()` pseudocode for this is already in `FlowFieldsEffect.h`. Requires projectMM to expose `clearControls()` (or equivalent). Pro: minimal UI, exactly the right controls shown. Con: API not yet available; emitter/flow selectors must always survive the clear.

**Option B — Visibility toggling** *(if projectMM adds `setControlVisible`)*

Register all controls once in `setup()`. Call `setControlVisible(name, bool)` from `onUpdate()` whenever emitter or flow changes. Pro: simpler than A, no value loss risk. Con: same API requirement; all control memory still allocated.

**Option C — One effect class per emitter** *(works today, no new API needed)*

Eight separate classes (`OrbitalDotsEffect`, `SwarmingDotsEffect`, …), each hard-coding its emitter and registering only its emitter-specific params. All share one `FlowFieldsEngine` instance (static or via pointer). A flow selector inside each class handles flow-param visibility dynamically (or via a second level of Option A/B).

- Pro: works without any new projectMM API; each class is small and focused.
- Con: 8 classes; the user switches effects to switch emitters; flow-param problem remains per class.
- Taking this to its limit — one class per emitter/flow combo — gives 48 effects. That eliminates all selectors and makes each effect fully self-describing, but is impractical to register and maintain.

**Option D — Composite modules** *(future projectMM architecture)*

An `EmitterModule` owns the emitter selector and emitter-specific params. A `FlowModule` owns the flow selector and flow-specific params. Both share one `FlowFieldsEngine` instance. The framework composes their outputs. Pro: maximum modularity, minimal per-module surface area. Con: requires projectMM to support module composition and shared engine state.

### Consequences for the Engine API

Session 5 is **purely additive to the consumer layer** — the engine API (`FlowFieldsEngine` public struct members) does not change. All options write into the same struct fields; only `FlowFieldsEffect.h` changes shape. This is a direct benefit of the Session 4 cleanup: the single direct-write mechanism makes any of the above options straightforward to implement without touching the engine.

### BLE Already Solves This — at the UI Layer

The BLE firmware has implemented dynamic parameter visibility since before Session 1. `sendEmitterState()` calls `getEmitterParams(g_engine->_emitter)`, which looks up `EMITTER_PARAM_LOOKUP[]` for the currently active emitter and returns only that emitter's parameter names. It then reads their values via `bleGetEngineParam()` and sends a compact JSON document to the web UI containing only the relevant parameters. `sendFlowState()` does the same via `FLOW_PARAM_LOOKUP[]`.

The web UI reconstructs its slider panel from that JSON — so switching emitter or flow causes the UI to show *exactly* the relevant sliders, with no extras.

This works easily in the BLE case because the "UI" is a browser page: showing and hiding DOM elements is free. projectMM/FastLED-MM has the harder version of the problem because controls are registered C++ objects with allocated state, not DOM nodes.

The key takeaway for Session 5: the same two lookup tables that drive `sendEmitterState()` and `sendFlowState()` could also drive `registerControls()` in `FlowFieldsEffect.h`, making the FastLED-MM version data-driven rather than the hardcoded switch/case shown in the current pseudocode:

```cpp
// Data-driven alternative to the switch/case pseudocode:
// void registerControls() {
//     clearControls();
//     // ... globals and selectors ...
//     const EmitterParamEntry* ep = getEmitterParams(emitterIdx_);
//     if (ep) for (uint8_t i = 0; i < ep->count; i++)
//         addControlByName(ep->params[i]);   // hypothetical projectMM API
//     const FlowParamEntry* fp = getFlowParams(flowIdx_);
//     if (fp) for (uint8_t i = 0; i < fp->count; i++)
//         addControlByName(fp->params[i]);
// }
```

This would make `registerControls()` a ~15-line function regardless of how many emitters or flows are added in the future.

### What Already Exists

- `FlowFieldsEffect.h` `setup()` — controls annotated with emitter/flow groupings as inline comments
- `FlowFieldsEffect.h` — `registerControls()` pseudocode (complete switch/case skeleton) ready to activate when projectMM adds `clearControls()`
- `parameterSchema.h` — `EMITTER_PARAM_LOOKUP[]` and `FLOW_PARAM_LOOKUP[]` — the same tables the BLE send functions use; they are the canonical data source for per-emitter/flow parameter sets
- `bleControl.h` — `sendEmitterState()` / `sendFlowState()` — working reference implementation of the dynamic-visibility pattern, already in production on the BLE side

### Recommendation

Wait for projectMM to expose `clearControls()` or `setControlVisible()`, then activate Option A using the existing pseudocode in `FlowFieldsEffect.h` — ideally rewritten as the data-driven loop above rather than the switch/case, so it stays in sync with `EMITTER_PARAM_LOOKUP[]` / `FLOW_PARAM_LOOKUP[]` automatically. If faster resolution is needed, Option C (one class per emitter) works today and is the natural stepping stone toward the composable Option D architecture.
