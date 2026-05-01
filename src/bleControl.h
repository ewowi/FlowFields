#pragma once

#include <NimBLEDevice.h>
#include "parameterSchema.h"
#include "FlowFieldsEngine.h"

#if __has_include("hosted_ble_bridge.h")
    #include "hosted_ble_bridge.h"
#else
    static inline bool hostedBlePrepare() { return true; }
#endif

//#include <FS.h>
//#include "LittleFS.h"
//#define FORMAT_LITTLEFS_IF_FAILED true

ArduinoJson::JsonDocument sendDoc;
ArduinoJson::JsonDocument receivedJSON;

//*************************************************************************************
//BLE CONFIGURATION *******************************************************************

NimBLEServer* pServer = NULL;
NimBLECharacteristic* pButtonCharacteristic = NULL;
NimBLECharacteristic* pCheckboxCharacteristic = NULL;
NimBLECharacteristic* pNumberCharacteristic = NULL;
NimBLECharacteristic* pStringCharacteristic = NULL;
NimBLEAdvertising* pAdvertising = NULL;

bool deviceConnected = false;
bool wasConnected = false;

#define SERVICE_UUID                  	"19b10000-e8f2-537e-4f6c-d104768a1214"
#define BUTTON_CHARACTERISTIC_UUID     "19b10001-e8f2-537e-4f6c-d104768a1214"
#define CHECKBOX_CHARACTERISTIC_UUID   "19b10002-e8f2-537e-4f6c-d104768a1214"
#define NUMBER_CHARACTERISTIC_UUID     "19b10003-e8f2-537e-4f6c-d104768a1214"
#define STRING_CHARACTERISTIC_UUID     "19b10004-e8f2-537e-4f6c-d104768a1214"


//*************************************************************************************
// CONTROL FUNCTIONS ******************************************************************

// UI update functions ***********************************************

void sendReceiptButton(uint8_t receivedValue) {
   pButtonCharacteristic->setValue(String(receivedValue).c_str());
   pButtonCharacteristic->notify();
   if (debug) {
      Serial.print("Button value received: ");
      Serial.println(receivedValue);
   }
}

void sendReceiptCheckbox(String receivedID, bool receivedValue) {

   sendDoc.clear();
   sendDoc["id"] = receivedID;
   sendDoc["val"] = receivedValue;

   String jsonString;
   serializeJson(sendDoc, jsonString);

   pCheckboxCharacteristic->setValue(jsonString);

   pCheckboxCharacteristic->notify();

   if (debug) {
      Serial.print("Sent receipt for ");
      Serial.print(receivedID);
      Serial.print(": ");
      Serial.println(receivedValue);
   }

}

void sendReceiptNumber(String receivedID, float receivedValue) {

   sendDoc.clear();
   sendDoc["id"] = receivedID;
   sendDoc["val"] = receivedValue;

   String jsonString;
   serializeJson(sendDoc, jsonString);

   pNumberCharacteristic->setValue(jsonString);

   pNumberCharacteristic->notify();

   if (debug) {
      Serial.print("Sent receipt for ");
      Serial.print(receivedID);
      Serial.print(": ");
      Serial.println(receivedValue);
   }
}

void sendReceiptString(String receivedID, String receivedValue) {

   sendDoc.clear();
   sendDoc["id"] = receivedID;
   sendDoc["val"] = receivedValue;

   String jsonString;
   serializeJson(sendDoc, jsonString);

   pStringCharacteristic->setValue(jsonString);

   pStringCharacteristic->notify();

   if (debug) {
      Serial.print("Sent receipt for ");
      Serial.print(receivedID);
      Serial.print(": ");
      Serial.println(receivedValue);
   }
}

//*****************************************************************************
// PARAMETER/PRESET MANAGEMENT SYSTEM ("PPMS")

// Auto-generated helper functions using X-macros
void captureCurrentParameters(ArduinoJson::JsonObject& params) {
    #define X(type, parameter, def) params[#parameter] = c##parameter;
    PARAMETER_TABLE
    #undef X
}

void applyCurrentParameters(const ArduinoJson::JsonObjectConst& params) {
    #define X(type, parameter, def) \
        if (!params[#parameter].isNull()) { \
            auto newValue = params[#parameter].as<type>(); \
            if (c##parameter != newValue) { \
                c##parameter = newValue; \
                sendReceiptNumber("in" #parameter, c##parameter); \
            } \
        }
    PARAMETER_TABLE
    #undef X
}

/*
// Preset file persistence functions with JSON structure
bool savePreset(int presetNumber) {
    String filename = "/preset_";
    filename += presetNumber;
    filename += ".json";

    ArduinoJson::JsonDocument preset;
    preset["programNum"] = PROGRAM;
    if (MODE_COUNTS[PROGRAM] > 0) {
      preset["modeNum"] = MODE;
    }
    ArduinoJson::JsonObject params = preset["parameters"].to<ArduinoJson::JsonObject>();
    captureCurrentParameters(params);

    File file = LittleFS.open(filename, "w");
    if (!file) {
        Serial.print("Failed to save preset: ");
        Serial.println(filename);
        return false;
    }

    serializeJson(preset, file);
    file.close();

    Serial.print("Preset saved: ");
    Serial.println(filename);
    return true;
}

bool loadPreset(int presetNumber) {
    String filename = "/preset_";
    filename += presetNumber;
    filename += ".json";

    File file = LittleFS.open(filename, "r");
    if (!file) {
        Serial.print("Failed to load preset: ");
        Serial.println(filename);
        return false;
    }

    ArduinoJson::JsonDocument preset;
    ArduinoJson::DeserializationError error = deserializeJson(preset, file);
    file.close();

    if (preset["programNum"].isNull() || preset["parameters"].isNull()) {
        Serial.print("Invalid preset format: ");
        Serial.println(filename);
        return false;
    }

    PROGRAM = (uint8_t)preset["programNum"];
    if (!preset["modeNum"].isNull()) {
      MODE = (uint8_t)preset["modeNum"];
    }
    //pauseAnimation = true;
    applyCurrentParameters(preset["parameters"]);
    //pauseAnimation = false;

    Serial.print("Preset loaded: ");
    Serial.println(filename);
    return true;
}*/

//***********************************************************************

//void sendDeviceState() {

void sendEmitterState() {
   if (debug) {
      Serial.println("Sending emitter state...");
   }

   ArduinoJson::JsonDocument stateDoc;
   stateDoc["emitter"] = flowFields::g_engine->_emitter;

   // Get parameter list for current visualizer
   const EmitterParamEntry* emitterParams = getEmitterParams(flowFields::g_engine->_emitter);

   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   if (debug) {
       Serial.print("Current emitter: ");
       Serial.println(flowFields::g_engine->_emitter);
       Serial.print("Found params: ");
       Serial.println(emitterParams != nullptr ? "YES" : "NO");
       if (emitterParams != nullptr) {
           Serial.print("Param count: ");
           Serial.println(emitterParams->count);
       }
   }

   if (emitterParams != nullptr) {
       // Loop through parameters for current emitter
       for (uint8_t i = 0; i < emitterParams->count; i++) {
           char paramName[32];
           ::strcpy(paramName, (char*)pgm_read_ptr(&emitterParams->params[i]));

           if (debug) {
               Serial.print("Processing parameter: ");
               Serial.println(paramName);
           }
       }
   }

   // Add parameter values to JSON via bleGetEngineParam
   for (uint8_t i = 0; i < emitterParams->count; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&emitterParams->params[i]));
       float val = bleGetEngineParam(paramName);
       params[paramName] = val;
       if (debug) {
           Serial.print("Added parameter ");
           Serial.print(paramName);
           Serial.print(": ");
           Serial.println(val);
       }
   }

   // Send as a single JSON doc with nested val object (avoids double-encoding
   // that would exceed BLE MTU when string-escaping the inner JSON).
   ArduinoJson::JsonDocument envelope;
   envelope["id"] = "emitterState";
   ArduinoJson::JsonObject val = envelope["val"].to<ArduinoJson::JsonObject>();
   val["emitter"] = flowFields::g_engine->_emitter;
   ArduinoJson::JsonObject valParams = val["parameters"].to<ArduinoJson::JsonObject>();
   for (auto kv : params) {
       valParams[kv.key()] = kv.value();
   }

   String json;
   serializeJson(envelope, json);

   if (debug) {
       Serial.print("emitterState payload size: ");
       Serial.println(json.length());
   }

   pStringCharacteristic->setValue(json);
   pStringCharacteristic->notify();
}

  // -----------------------------------

void sendFlowState() {
   if (debug) {
      Serial.println("Sending flow state...");
   }

   ArduinoJson::JsonDocument stateDoc;
   stateDoc["flow"] = flowFields::g_engine->_flow;

   // Get parameter list for current flow
   const FlowParamEntry* flowParams = getFlowParams(flowFields::g_engine->_flow);

   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   if (debug) {
       Serial.print("Current emitter: ");
       Serial.println(flowFields::g_engine->_flow);
       Serial.print("Found params: ");
       Serial.println(flowParams != nullptr ? "YES" : "NO");
       if (flowParams != nullptr) {
           Serial.print("Param count: ");
           Serial.println(flowParams->count);
       }
   }

   if (flowParams != nullptr) {
       // Loop through parameters for current flow
       for (uint8_t i = 0; i < flowParams->count; i++) {
           char paramName[32];
           ::strcpy(paramName, (char*)pgm_read_ptr(&flowParams->params[i]));

           if (debug) {
               Serial.print("Processing parameter: ");
               Serial.println(paramName);
           }
       }
   }

   // Add parameter values to JSON via bleGetEngineParam
   for (uint8_t i = 0; i < flowParams->count; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&flowParams->params[i]));
       float val = bleGetEngineParam(paramName);
       params[paramName] = val;
       if (debug) {
           Serial.print("Added parameter ");
           Serial.print(paramName);
           Serial.print(": ");
           Serial.println(val);
       }
   }

   // Send as a single JSON doc with nested val object (avoids double-encoding
   // that would exceed BLE MTU when string-escaping the inner JSON).
   ArduinoJson::JsonDocument envelope;
   envelope["id"] = "flowState";
   ArduinoJson::JsonObject val = envelope["val"].to<ArduinoJson::JsonObject>();
   val["flow"] = flowFields::g_engine->_flow;
   ArduinoJson::JsonObject valParams = val["parameters"].to<ArduinoJson::JsonObject>();
   for (auto kv : params) {
       valParams[kv.key()] = kv.value();
   }

   String json;
   serializeJson(envelope, json);

   if (debug) {
       Serial.print("flowState payload size: ");
       Serial.println(json.length());
   }

   pStringCharacteristic->setValue(json);
   pStringCharacteristic->notify();
}


void sendGlobalState() {
   if (debug) { Serial.println("Sending global state..."); }

   ArduinoJson::JsonDocument stateDoc;
   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   for (uint8_t i = 0; i < GLOBAL_PARAM_COUNT; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&GLOBAL_PARAMS[i]));
       params[paramName] = bleGetEngineParam(paramName);
   }

   ArduinoJson::JsonDocument envelope;
   envelope["id"] = "globalState";
   ArduinoJson::JsonObject val = envelope["val"].to<ArduinoJson::JsonObject>();
   ArduinoJson::JsonObject valParams = val["parameters"].to<ArduinoJson::JsonObject>();
   for (auto kv : params) {
       valParams[kv.key()] = kv.value();
   }

   String json;
   serializeJson(envelope, json);

   if (debug) {
       Serial.print("globalState payload size: ");
       Serial.println(json.length());
   }

   pStringCharacteristic->setValue(json);
   pStringCharacteristic->notify();
}

  // -----------------------------------

void sendAudioState() {
   if (debug) { Serial.println("Sending audio state..."); }

   ArduinoJson::JsonDocument stateDoc;
   ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();

   // Iterate AUDIO_PARAMS and match against PARAMETER_TABLE via X-macro
   for (uint8_t i = 0; i < AUDIO_PARAM_COUNT; i++) {
       char paramName[32];
       ::strcpy(paramName, (char*)pgm_read_ptr(&AUDIO_PARAMS[i]));

       #define X(type, parameter, def) \
           if (strcasecmp(paramName, #parameter) == 0) { \
               params[paramName] = c##parameter; \
           }
       PARAMETER_TABLE
       #undef X
   }

   String stateJson;
   serializeJson(stateDoc, stateJson);
   sendReceiptString("audioState", stateJson);
}

void sendBusState() {
   if (debug) { Serial.println("Sending bus state..."); }
   if (!getBusParam) { Serial.println("getBusParam not set"); return; }

   // Bus params live on Bus structs (outside X-macro system).
   // Send one message per bus to stay within BLE MTU limits.
   const char* busParamNames[] = {"threshold", "minBeatInterval", "expDecayFactor",
                                   "rampAttack", "rampDecay", "peakBase"};
   const uint8_t busParamCount = 6;

   for (uint8_t busId = 0; busId < 3; busId++) {
       ArduinoJson::JsonDocument stateDoc;
       stateDoc["bus"] = busId;
       ArduinoJson::JsonObject params = stateDoc["parameters"].to<ArduinoJson::JsonObject>();
       for (uint8_t p = 0; p < busParamCount; p++) {
           params[busParamNames[p]] = getBusParam(busId, busParamNames[p]);
       }

       String stateJson;
       serializeJson(stateDoc, stateJson);
       sendReceiptString("busState", stateJson);
   }
}


// ── BLE string-to-member mapping ─────────────────────────────────────────────
// String dispatch lives here in bleControl, not in the engine.
// Library consumers (FastLED-MM, etc.) set engine members directly.

static void bleSetEngineParam(const char* name, float value) {
    using namespace flowFields;
    FlowFieldsEngine* e = g_engine;
    // Selection (triggers reset-on-change logic in run())
    if (strcasecmp(name, "emitter")              == 0) { e->_emitter = (uint8_t)value; return; }
    if (strcasecmp(name, "flow")                 == 0) { e->_flow    = (uint8_t)value; return; }
    // Global
    if (strcasecmp(name, "globalSpeed")          == 0) { e->globalSpeed = value; return; }
    if (strcasecmp(name, "colorShift")           == 0) { e->colorShift  = value; return; }
    // persistence split: coarse int part + fine fractional part
    if (strcasecmp(name, "persistence")          == 0) { e->persistence = floorf(value) + (e->persistence - floorf(e->persistence)); return; }
    if (strcasecmp(name, "persistFine")          == 0) { e->persistence = floorf(e->persistence) + value; return; }
    // Shared across emitters
    if (strcasecmp(name, "numDots")              == 0) { e->orbitalDots.numDots = (uint8_t)value; e->swarmingDots.numDots = (uint8_t)value; return; }
    if (strcasecmp(name, "dotDiam")              == 0) { e->orbitalDots.dotDiam = value; e->swarmingDots.dotDiam = value; return; }
    // Shared across flows
    if (strcasecmp(name, "blendFactor")          == 0) { e->radial.blendFactor = value; e->directional.blendFactor = value; e->spiral.blendFactor = value; return; }
    if (strcasecmp(name, "radialStep")           == 0) { e->radial.radialStep  = value; e->spiral.radialStep = value; return; }
    // uint8_t fields (arrive as float from JSON)
    if (strcasecmp(name, "lineClamp")            == 0) { e->lissajous.lineClamp = (uint8_t)value; return; }
    if (strcasecmp(name, "solverIterations")     == 0) { e->fluid.solverIterations = (uint8_t)value; return; }
    // OrbitalDots
    if (strcasecmp(name, "orbitSpeed")           == 0) { e->orbitalDots.orbitSpeed = value; return; }
    if (strcasecmp(name, "orbitDiam")            == 0) { e->orbitalDots.orbitDiam  = value; return; }
    if (strcasecmp(name, "modOrbitSpeedRate")    == 0) { e->orbitalDots.modOrbitSpeed.modRate  = value; return; }
    if (strcasecmp(name, "modOrbitSpeedLevel")   == 0) { e->orbitalDots.modOrbitSpeed.modLevel = value; return; }
    if (strcasecmp(name, "modOrbitDiamRate")     == 0) { e->orbitalDots.modOrbitDiam.modRate   = value; return; }
    if (strcasecmp(name, "modOrbitDiamLevel")    == 0) { e->orbitalDots.modOrbitDiam.modLevel  = value; return; }
    // SwarmingDots
    if (strcasecmp(name, "swarmSpeed")           == 0) { e->swarmingDots.swarmSpeed = value; return; }
    if (strcasecmp(name, "swarmSpread")          == 0) { e->swarmingDots.swarmSpread = value; return; }
    if (strcasecmp(name, "modSwarmSpeedRate")    == 0) { e->swarmingDots.modSwarmSpeed.modRate   = value; return; }
    if (strcasecmp(name, "modSwarmSpeedLevel")   == 0) { e->swarmingDots.modSwarmSpeed.modLevel  = value; return; }
    if (strcasecmp(name, "modSwarmSpreadRate")   == 0) { e->swarmingDots.modSwarmSpread.modRate  = value; return; }
    if (strcasecmp(name, "modSwarmSpreadLevel")  == 0) { e->swarmingDots.modSwarmSpread.modLevel = value; return; }
    // Lissajous
    if (strcasecmp(name, "lineSpeed")            == 0) { e->lissajous.lineSpeed = value; return; }
    if (strcasecmp(name, "lineAmp")              == 0) { e->lissajous.lineAmp   = value; return; }
    if (strcasecmp(name, "modLineSpeedRate")     == 0) { e->lissajous.modLineSpeed.modRate  = value; return; }
    if (strcasecmp(name, "modLineSpeedLevel")    == 0) { e->lissajous.modLineSpeed.modLevel = value; return; }
    if (strcasecmp(name, "modLineAmpRate")       == 0) { e->lissajous.modLineAmp.modRate    = value; return; }
    if (strcasecmp(name, "modLineAmpLevel")      == 0) { e->lissajous.modLineAmp.modLevel   = value; return; }
    // NoiseKaleido
    if (strcasecmp(name, "driftSpeed")           == 0) { e->noiseKaleido.driftSpeed  = value; return; }
    if (strcasecmp(name, "noiseScale")           == 0) { e->noiseKaleido.noiseScale  = value; return; }
    if (strcasecmp(name, "noiseBand")            == 0) { e->noiseKaleido.noiseBand   = value; return; }
    if (strcasecmp(name, "kaleidoGamma")         == 0) { e->noiseKaleido.kaleidoGamma = value; return; }
    // Cube
    if (strcasecmp(name, "scale")                == 0) { e->cube.scale = value; return; }
    if (strcasecmp(name, "rotateSpeedX")         == 0) { e->cube.rotateSpeed[0] = value; return; }
    if (strcasecmp(name, "rotateSpeedY")         == 0) { e->cube.rotateSpeed[1] = value; return; }
    if (strcasecmp(name, "rotateSpeedZ")         == 0) { e->cube.rotateSpeed[2] = value; return; }
    if (strcasecmp(name, "modScaleRate")         == 0) { e->cube.modScale.modRate  = value; return; }
    if (strcasecmp(name, "modScaleLevel")        == 0) { e->cube.modScale.modLevel = value; return; }
    if (strcasecmp(name, "modRotateSpeedXRate")  == 0) { e->cube.modRotateSpeedX.modRate  = value; return; }
    if (strcasecmp(name, "modRotateSpeedXLevel") == 0) { e->cube.modRotateSpeedX.modLevel = value; return; }
    if (strcasecmp(name, "modRotateSpeedYRate")  == 0) { e->cube.modRotateSpeedY.modRate  = value; return; }
    if (strcasecmp(name, "modRotateSpeedYLevel") == 0) { e->cube.modRotateSpeedY.modLevel = value; return; }
    if (strcasecmp(name, "modRotateSpeedZRate")  == 0) { e->cube.modRotateSpeedZ.modRate  = value; return; }
    if (strcasecmp(name, "modRotateSpeedZLevel") == 0) { e->cube.modRotateSpeedZ.modLevel = value; return; }
    // FluidJet
    if (strcasecmp(name, "jetDensity")           == 0) { e->fluidJet.jetDensity  = value; return; }
    if (strcasecmp(name, "jetForce")             == 0) { e->fluidJet.jetForce    = value; return; }
    if (strcasecmp(name, "jetRadius")            == 0) { e->fluidJet.jetRadius   = value; return; }
    if (strcasecmp(name, "jetSpread")            == 0) { e->fluidJet.jetSpread   = value; return; }
    if (strcasecmp(name, "jetAngle")             == 0) { e->fluidJet.jetAngle    = value; return; }
    if (strcasecmp(name, "jetHueSpeed")          == 0) { e->fluidJet.jetHueSpeed = value; return; }
    if (strcasecmp(name, "modJetForceRate")      == 0) { e->fluidJet.modJetForce.modRate  = value; return; }
    if (strcasecmp(name, "modJetForceLevel")     == 0) { e->fluidJet.modJetForce.modLevel = value; return; }
    if (strcasecmp(name, "modAngleRate")         == 0) { e->fluidJet.modAngle.modRate     = value; return; }
    if (strcasecmp(name, "modAngleLevel")        == 0) { e->fluidJet.modAngle.modLevel    = value; return; }
    // NoiseFlow
    if (strcasecmp(name, "xSpeed")               == 0) { e->noiseFlow.xSpeed = value; return; }
    if (strcasecmp(name, "ySpeed")               == 0) { e->noiseFlow.ySpeed = value; return; }
    if (strcasecmp(name, "xAmp")                 == 0) { e->noiseFlow.xAmp   = value; return; }
    if (strcasecmp(name, "yAmp")                 == 0) { e->noiseFlow.yAmp   = value; return; }
    if (strcasecmp(name, "xFreq")                == 0) { e->noiseFlow.xFreq  = value; return; }
    if (strcasecmp(name, "yFreq")                == 0) { e->noiseFlow.yFreq  = value; return; }
    if (strcasecmp(name, "xShift")               == 0) { e->noiseFlow.xShift = value; return; }
    if (strcasecmp(name, "yShift")               == 0) { e->noiseFlow.yShift = value; return; }
    if (strcasecmp(name, "modAmpRate")           == 0) { e->noiseFlow.modAmp.modRate    = value; return; }
    if (strcasecmp(name, "modAmpLevel")          == 0) { e->noiseFlow.modAmp.modLevel   = value; return; }
    if (strcasecmp(name, "modSpeedRate")         == 0) { e->noiseFlow.modSpeed.modRate  = value; return; }
    if (strcasecmp(name, "modSpeedLevel")        == 0) { e->noiseFlow.modSpeed.modLevel = value; return; }
    if (strcasecmp(name, "modShiftRate")         == 0) { e->noiseFlow.modShift.modRate  = value; return; }
    if (strcasecmp(name, "modShiftLevel")        == 0) { e->noiseFlow.modShift.modLevel = value; return; }
    // Directional
    if (strcasecmp(name, "windStep")             == 0) { e->directional.windStep    = value; return; }
    if (strcasecmp(name, "rotateSpeed")          == 0) { e->directional.rotateSpeed = value; return; }
    if (strcasecmp(name, "waveAmp")              == 0) { e->directional.waveAmp     = value; return; }
    if (strcasecmp(name, "waveFreq")             == 0) { e->directional.waveFreq    = value; return; }
    if (strcasecmp(name, "waveSpeed")            == 0) { e->directional.waveSpeed   = value; return; }
    // RingFlow
    if (strcasecmp(name, "innerSwirl")           == 0) { e->ringFlow.innerSwirl = value; return; }
    if (strcasecmp(name, "outerSwirl")           == 0) { e->ringFlow.outerSwirl = value; return; }
    if (strcasecmp(name, "midDrift")             == 0) { e->ringFlow.midDrift   = value; return; }
    if (strcasecmp(name, "modBreatheRate")       == 0) { e->ringFlow.modBreathe.modRate  = value; return; }
    if (strcasecmp(name, "modBreatheLevel")      == 0) { e->ringFlow.modBreathe.modLevel = value; return; }
    // Spiral
    if (strcasecmp(name, "angularStep")          == 0) { e->spiral.angularStep = value; return; }
    if (strcasecmp(name, "modAngularStepRate")   == 0) { e->spiral.modAngularStep.modRate   = value; return; }
    if (strcasecmp(name, "modAngularStepLevel")  == 0) { e->spiral.modAngularStep.modLevel  = value; return; }
    if (strcasecmp(name, "modRadialStepRate")    == 0) { e->spiral.modRadialStep.modRate    = value; return; }
    if (strcasecmp(name, "modRadialStepLevel")   == 0) { e->spiral.modRadialStep.modLevel   = value; return; }
    if (strcasecmp(name, "modBlendFactorRate")   == 0) { e->spiral.modBlendFactor.modRate   = value; return; }
    if (strcasecmp(name, "modBlendFactorLevel")  == 0) { e->spiral.modBlendFactor.modLevel  = value; return; }
    // Fluid
    if (strcasecmp(name, "viscosity")            == 0) { e->fluid.viscosity             = value; return; }
    if (strcasecmp(name, "diffusion")            == 0) { e->fluid.diffusion             = value; return; }
    if (strcasecmp(name, "velocityDissipation")  == 0) { e->fluid.velocityDissipation   = value; return; }
    if (strcasecmp(name, "dyeDissipation")       == 0) { e->fluid.dyeDissipation        = value; return; }
    if (strcasecmp(name, "vorticity")            == 0) { e->fluid.vorticity             = value; return; }
    if (strcasecmp(name, "gravity")              == 0) { e->fluid.gravity               = value; return; }
    if (strcasecmp(name, "modVelDissipRate")     == 0) { e->fluid.modVelDissip.modRate  = value; return; }
    if (strcasecmp(name, "modVelDissipLevel")    == 0) { e->fluid.modVelDissip.modLevel = value; return; }
    if (strcasecmp(name, "modDyeDissipRate")     == 0) { e->fluid.modDyeDissip.modRate  = value; return; }
    if (strcasecmp(name, "modDyeDissipLevel")    == 0) { e->fluid.modDyeDissip.modLevel = value; return; }
}

static float bleGetEngineParam(const char* name) {
    using namespace flowFields;
    FlowFieldsEngine* e = g_engine;
    // Global
    if (strcasecmp(name, "globalSpeed")          == 0) return e->globalSpeed;
    if (strcasecmp(name, "colorShift")           == 0) return e->colorShift;
    // persistence split
    if (strcasecmp(name, "persistence")          == 0) return floorf(e->persistence);
    if (strcasecmp(name, "persistFine")          == 0) return e->persistence - floorf(e->persistence);
    // Shared params — return values from both sides (they're kept in sync by bleSetEngineParam)
    if (strcasecmp(name, "numDots")              == 0) return (float)e->orbitalDots.numDots;
    if (strcasecmp(name, "dotDiam")              == 0) return e->orbitalDots.dotDiam;
    if (strcasecmp(name, "blendFactor")          == 0) {
        switch (e->activeFlow) {
            case FLOW_DIRECTIONAL: return e->directional.blendFactor;
            case FLOW_SPIRAL:      return e->spiral.blendFactor;
            default:               return e->radial.blendFactor;
        }
    }
    if (strcasecmp(name, "radialStep")           == 0) return (e->activeFlow == FLOW_SPIRAL) ? e->spiral.radialStep : e->radial.radialStep;
    // uint8_t fields
    if (strcasecmp(name, "lineClamp")            == 0) return (float)e->lissajous.lineClamp;
    if (strcasecmp(name, "solverIterations")     == 0) return (float)e->fluid.solverIterations;
    // OrbitalDots
    if (strcasecmp(name, "orbitSpeed")           == 0) return e->orbitalDots.orbitSpeed;
    if (strcasecmp(name, "orbitDiam")            == 0) return e->orbitalDots.orbitDiam;
    if (strcasecmp(name, "modOrbitSpeedRate")    == 0) return e->orbitalDots.modOrbitSpeed.modRate;
    if (strcasecmp(name, "modOrbitSpeedLevel")   == 0) return e->orbitalDots.modOrbitSpeed.modLevel;
    if (strcasecmp(name, "modOrbitDiamRate")     == 0) return e->orbitalDots.modOrbitDiam.modRate;
    if (strcasecmp(name, "modOrbitDiamLevel")    == 0) return e->orbitalDots.modOrbitDiam.modLevel;
    // SwarmingDots
    if (strcasecmp(name, "swarmSpeed")           == 0) return e->swarmingDots.swarmSpeed;
    if (strcasecmp(name, "swarmSpread")          == 0) return e->swarmingDots.swarmSpread;
    if (strcasecmp(name, "modSwarmSpeedRate")    == 0) return e->swarmingDots.modSwarmSpeed.modRate;
    if (strcasecmp(name, "modSwarmSpeedLevel")   == 0) return e->swarmingDots.modSwarmSpeed.modLevel;
    if (strcasecmp(name, "modSwarmSpreadRate")   == 0) return e->swarmingDots.modSwarmSpread.modRate;
    if (strcasecmp(name, "modSwarmSpreadLevel")  == 0) return e->swarmingDots.modSwarmSpread.modLevel;
    // Lissajous
    if (strcasecmp(name, "lineSpeed")            == 0) return e->lissajous.lineSpeed;
    if (strcasecmp(name, "lineAmp")              == 0) return e->lissajous.lineAmp;
    if (strcasecmp(name, "modLineSpeedRate")     == 0) return e->lissajous.modLineSpeed.modRate;
    if (strcasecmp(name, "modLineSpeedLevel")    == 0) return e->lissajous.modLineSpeed.modLevel;
    if (strcasecmp(name, "modLineAmpRate")       == 0) return e->lissajous.modLineAmp.modRate;
    if (strcasecmp(name, "modLineAmpLevel")      == 0) return e->lissajous.modLineAmp.modLevel;
    // NoiseKaleido
    if (strcasecmp(name, "driftSpeed")           == 0) return e->noiseKaleido.driftSpeed;
    if (strcasecmp(name, "noiseScale")           == 0) return e->noiseKaleido.noiseScale;
    if (strcasecmp(name, "noiseBand")            == 0) return e->noiseKaleido.noiseBand;
    if (strcasecmp(name, "kaleidoGamma")         == 0) return e->noiseKaleido.kaleidoGamma;
    // Cube
    if (strcasecmp(name, "scale")                == 0) return e->cube.scale;
    if (strcasecmp(name, "rotateSpeedX")         == 0) return e->cube.rotateSpeed[0];
    if (strcasecmp(name, "rotateSpeedY")         == 0) return e->cube.rotateSpeed[1];
    if (strcasecmp(name, "rotateSpeedZ")         == 0) return e->cube.rotateSpeed[2];
    if (strcasecmp(name, "modScaleRate")         == 0) return e->cube.modScale.modRate;
    if (strcasecmp(name, "modScaleLevel")        == 0) return e->cube.modScale.modLevel;
    if (strcasecmp(name, "modRotateSpeedXRate")  == 0) return e->cube.modRotateSpeedX.modRate;
    if (strcasecmp(name, "modRotateSpeedXLevel") == 0) return e->cube.modRotateSpeedX.modLevel;
    if (strcasecmp(name, "modRotateSpeedYRate")  == 0) return e->cube.modRotateSpeedY.modRate;
    if (strcasecmp(name, "modRotateSpeedYLevel") == 0) return e->cube.modRotateSpeedY.modLevel;
    if (strcasecmp(name, "modRotateSpeedZRate")  == 0) return e->cube.modRotateSpeedZ.modRate;
    if (strcasecmp(name, "modRotateSpeedZLevel") == 0) return e->cube.modRotateSpeedZ.modLevel;
    // FluidJet
    if (strcasecmp(name, "jetDensity")           == 0) return e->fluidJet.jetDensity;
    if (strcasecmp(name, "jetForce")             == 0) return e->fluidJet.jetForce;
    if (strcasecmp(name, "jetRadius")            == 0) return e->fluidJet.jetRadius;
    if (strcasecmp(name, "jetSpread")            == 0) return e->fluidJet.jetSpread;
    if (strcasecmp(name, "jetAngle")             == 0) return e->fluidJet.jetAngle;
    if (strcasecmp(name, "jetHueSpeed")          == 0) return e->fluidJet.jetHueSpeed;
    if (strcasecmp(name, "modJetForceRate")      == 0) return e->fluidJet.modJetForce.modRate;
    if (strcasecmp(name, "modJetForceLevel")     == 0) return e->fluidJet.modJetForce.modLevel;
    if (strcasecmp(name, "modAngleRate")         == 0) return e->fluidJet.modAngle.modRate;
    if (strcasecmp(name, "modAngleLevel")        == 0) return e->fluidJet.modAngle.modLevel;
    // NoiseFlow
    if (strcasecmp(name, "xSpeed")               == 0) return e->noiseFlow.xSpeed;
    if (strcasecmp(name, "ySpeed")               == 0) return e->noiseFlow.ySpeed;
    if (strcasecmp(name, "xAmp")                 == 0) return e->noiseFlow.xAmp;
    if (strcasecmp(name, "yAmp")                 == 0) return e->noiseFlow.yAmp;
    if (strcasecmp(name, "xFreq")                == 0) return e->noiseFlow.xFreq;
    if (strcasecmp(name, "yFreq")                == 0) return e->noiseFlow.yFreq;
    if (strcasecmp(name, "xShift")               == 0) return e->noiseFlow.xShift;
    if (strcasecmp(name, "yShift")               == 0) return e->noiseFlow.yShift;
    if (strcasecmp(name, "modAmpRate")           == 0) return e->noiseFlow.modAmp.modRate;
    if (strcasecmp(name, "modAmpLevel")          == 0) return e->noiseFlow.modAmp.modLevel;
    if (strcasecmp(name, "modSpeedRate")         == 0) return e->noiseFlow.modSpeed.modRate;
    if (strcasecmp(name, "modSpeedLevel")        == 0) return e->noiseFlow.modSpeed.modLevel;
    if (strcasecmp(name, "modShiftRate")         == 0) return e->noiseFlow.modShift.modRate;
    if (strcasecmp(name, "modShiftLevel")        == 0) return e->noiseFlow.modShift.modLevel;
    // Directional
    if (strcasecmp(name, "windStep")             == 0) return e->directional.windStep;
    if (strcasecmp(name, "rotateSpeed")          == 0) return e->directional.rotateSpeed;
    if (strcasecmp(name, "waveAmp")              == 0) return e->directional.waveAmp;
    if (strcasecmp(name, "waveFreq")             == 0) return e->directional.waveFreq;
    if (strcasecmp(name, "waveSpeed")            == 0) return e->directional.waveSpeed;
    // RingFlow
    if (strcasecmp(name, "innerSwirl")           == 0) return e->ringFlow.innerSwirl;
    if (strcasecmp(name, "outerSwirl")           == 0) return e->ringFlow.outerSwirl;
    if (strcasecmp(name, "midDrift")             == 0) return e->ringFlow.midDrift;
    if (strcasecmp(name, "modBreatheRate")       == 0) return e->ringFlow.modBreathe.modRate;
    if (strcasecmp(name, "modBreatheLevel")      == 0) return e->ringFlow.modBreathe.modLevel;
    // Spiral
    if (strcasecmp(name, "angularStep")          == 0) return e->spiral.angularStep;
    if (strcasecmp(name, "modAngularStepRate")   == 0) return e->spiral.modAngularStep.modRate;
    if (strcasecmp(name, "modAngularStepLevel")  == 0) return e->spiral.modAngularStep.modLevel;
    if (strcasecmp(name, "modRadialStepRate")    == 0) return e->spiral.modRadialStep.modRate;
    if (strcasecmp(name, "modRadialStepLevel")   == 0) return e->spiral.modRadialStep.modLevel;
    if (strcasecmp(name, "modBlendFactorRate")   == 0) return e->spiral.modBlendFactor.modRate;
    if (strcasecmp(name, "modBlendFactorLevel")  == 0) return e->spiral.modBlendFactor.modLevel;
    // Fluid
    if (strcasecmp(name, "viscosity")            == 0) return e->fluid.viscosity;
    if (strcasecmp(name, "diffusion")            == 0) return e->fluid.diffusion;
    if (strcasecmp(name, "velocityDissipation")  == 0) return e->fluid.velocityDissipation;
    if (strcasecmp(name, "dyeDissipation")       == 0) return e->fluid.dyeDissipation;
    if (strcasecmp(name, "vorticity")            == 0) return e->fluid.vorticity;
    if (strcasecmp(name, "gravity")              == 0) return e->fluid.gravity;
    if (strcasecmp(name, "modVelDissipRate")     == 0) return e->fluid.modVelDissip.modRate;
    if (strcasecmp(name, "modVelDissipLevel")    == 0) return e->fluid.modVelDissip.modLevel;
    if (strcasecmp(name, "modDyeDissipRate")     == 0) return e->fluid.modDyeDissip.modRate;
    if (strcasecmp(name, "modDyeDissipLevel")    == 0) return e->fluid.modDyeDissip.modLevel;
    return 0.0f;
}

// Handle UI request functions ***********************************************

std::string convertToStdString(const String& flStr) {
   return std::string(flStr.c_str());
}

void processButton(uint8_t receivedValue) {

   sendReceiptButton(receivedValue);

   if (receivedValue < 20) { // Emitter selection
      flowFields::g_engine->_emitter = receivedValue;
      displayOn = true;
   }

   if (receivedValue >= 20 && receivedValue < 40) { // Flow selection
      flowFields::g_engine->_flow = receivedValue - 20;
      displayOn = true;
   }

   if (receivedValue == 91) { sendGlobalState(); }
   if (receivedValue == 92) { sendEmitterState(); }
   if (receivedValue == 93) { sendAudioState(); }
   if (receivedValue == 94) { sendBusState(); }
   //if (receivedValue == 95) { resetAll(); }
   if (receivedValue == 96) { sendFlowState(); }

   if (receivedValue == 98) { displayOn = true; }
   if (receivedValue == 99) { displayOn = false; }

   /*if (receivedValue >= 101 && receivedValue <= 120) {
      uint8_t savedPreset = receivedValue - 100;
      //savePreset(savedPreset);
   }*/

   /*if (receivedValue >= 121 && receivedValue <= 140) {
       uint8_t presetToLoad = receivedValue - 120;
       if (loadPreset(presetToLoad)) {
           Serial.print("Loaded preset: ");
           Serial.println(presetToLoad);
       }
   }*/

   //if (receivedValue == 160) { Trigger = true; }

}

//*****************************************************************************

void processNumber(String receivedID, float receivedValue, int8_t busId = -1) {

   sendReceiptNumber(receivedID, receivedValue);

   if (busId >= 0 && busId < 3 && setBusParam != nullptr) {
      setBusParam((uint8_t)busId, receivedID, receivedValue);
      return;
   }

   if (receivedID == "inBright") {
      cBright = receivedValue;
      BRIGHTNESS = cBright;
      FastLED.setBrightness(BRIGHTNESS);
      return;
   }

   // Route engine params (emitters, flows, globals) directly
   if (receivedID.startsWith("in")) {
      bleSetEngineParam(receivedID.c_str() + 2, receivedValue);
   }

   // Audio/misc cVars handled by X-macro table
   #define X(type, parameter, def) \
       if (receivedID == "in" #parameter) { c##parameter = receivedValue; return; }
   PARAMETER_TABLE
   #undef X

}

void processCheckbox(String receivedID, bool receivedValue ) {

   sendReceiptCheckbox(receivedID, receivedValue);

   if (receivedID == "cx5") {audioEnabled = receivedValue;};
   if (receivedID == "cx6") {avLeveler = receivedValue;};
   if (receivedID == "cx7") {autoFloor = receivedValue;};

   if (receivedID == "cx11") {mappingOverride = receivedValue;};

   if (receivedID == "cx21") { flowFields::g_engine->cube.axisFreeze[0] = receivedValue; }
   if (receivedID == "cx22") { flowFields::g_engine->cube.axisFreeze[1] = receivedValue; }
   if (receivedID == "cx23") { flowFields::g_engine->cube.axisFreeze[2] = receivedValue; }

   if (receivedID == "cx31") { flowFields::g_engine->spiral.outward = receivedValue; flowFields::g_engine->radial.outward = receivedValue; }
   if (receivedID == "cx32") { flowFields::g_engine->useRainbow = receivedValue; }

}

void processString(String receivedID, String receivedValue ) {
   sendReceiptString(receivedID, receivedValue);
}

//*******************************************************************************
// CALLBACKS ********************************************************************

   class MyServerCallbacks: public NimBLEServerCallbacks {
   void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
      deviceConnected = true;
      wasConnected = true;
      Serial.println("[ble] connected");
      if (debug) {Serial.println("Device Connected");}
   };

   void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
      deviceConnected = false;
      wasConnected = true;
      Serial.printf("[ble] disconnected reason=%d\n", reason);
   }
   };

   class ButtonCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         NimBLEAttValue value = pCharacteristic->getValue();
         if (value.size() > 0) {

            uint8_t receivedValue = value[0];

            if (debug) {
               Serial.print("Button value received: ");
               Serial.println(receivedValue);
            }

            processButton(receivedValue);

         }
      }
   };

   class CheckboxCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         String receivedBuffer = String(pCharacteristic->getValue().c_str());

         if (receivedBuffer.length() > 0) {

            if (debug) {
               Serial.print("Received buffer: ");
               Serial.println(receivedBuffer);
            }

            ArduinoJson::deserializeJson(receivedJSON, receivedBuffer);
            String receivedID = receivedJSON["id"] ;
            bool receivedValue = receivedJSON["val"];

            if (debug) {
               Serial.print(receivedID);
               Serial.print(": ");
               Serial.println(receivedValue);
            }

            processCheckbox(receivedID, receivedValue);

         }
      }
   };

   class NumberCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         String receivedBuffer = String(pCharacteristic->getValue().c_str());

         if (receivedBuffer.length() > 0) {

            if (debug) {
               Serial.print("Received buffer: ");
               Serial.println(receivedBuffer);
            }

            ArduinoJson::deserializeJson(receivedJSON, receivedBuffer);
            String receivedID = receivedJSON["id"] ;
            float receivedValue = receivedJSON["val"];
            int8_t receivedBus = -1;
            if (!receivedJSON["bus"].isNull()) {
               receivedBus = receivedJSON["bus"].as<int8_t>();
            }

            if (debug) {
               Serial.print(receivedID);
               Serial.print(": ");
               Serial.println(receivedValue);
            }

            processNumber(receivedID, receivedValue, receivedBus);
         }
      }
   };

   class StringCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
      void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo& connInfo) override {

         String receivedBuffer = String(pCharacteristic->getValue().c_str());

         if (receivedBuffer.length() > 0) {

            if (debug) {
               Serial.print("Received buffer: ");
               Serial.println(receivedBuffer);
            }

            ArduinoJson::deserializeJson(receivedJSON, receivedBuffer);
            String receivedID = receivedJSON["id"] ;
            String receivedValue = receivedJSON["val"];

            if (debug) {
               Serial.print(receivedID);
               Serial.print(": ");
               Serial.println(receivedValue);
            }

            processString(receivedID, receivedValue);
         }
      }
   };


//*******************************************************************************
// BLE SETUP FUNCTION ***********************************************************

void bleSetup() {

      if (!hostedBlePrepare()) {
         Serial.println("[ble] Hosted BLE not ready; BLE setup aborted.");
         return;
      }

      NimBLEDevice::init("Flow Fields");
      NimBLEDevice::setMTU(517);  // Request max MTU for larger JSON payloads

      pServer = NimBLEDevice::createServer();
      pServer->setCallbacks(new MyServerCallbacks());

      NimBLEService *pService = pServer->createService(SERVICE_UUID);

      pButtonCharacteristic = pService->createCharacteristic(
                        BUTTON_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pButtonCharacteristic->setCallbacks(new ButtonCharacteristicCallbacks());

      pCheckboxCharacteristic = pService->createCharacteristic(
                        CHECKBOX_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pCheckboxCharacteristic->setCallbacks(new CheckboxCharacteristicCallbacks());

      pNumberCharacteristic = pService->createCharacteristic(
                        NUMBER_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pNumberCharacteristic->setCallbacks(new NumberCharacteristicCallbacks());

      pStringCharacteristic = pService->createCharacteristic(
                        STRING_CHARACTERISTIC_UUID,
                        NIMBLE_PROPERTY::WRITE |
                        NIMBLE_PROPERTY::READ |
                        NIMBLE_PROPERTY::NOTIFY
                     );
      pStringCharacteristic->setCallbacks(new StringCharacteristicCallbacks());


      //**********************************************************

      pAdvertising = NimBLEDevice::getAdvertising();
      pAdvertising->addServiceUUID(SERVICE_UUID);

      // Set up advertisement data with device name for Web Bluetooth compatibility
      NimBLEAdvertisementData advertisementData;
      advertisementData.setName("Flow Fields");
      advertisementData.setCompleteServices(NimBLEUUID(SERVICE_UUID));
      pAdvertising->setAdvertisementData(advertisementData);

      // Set up scan response data
      NimBLEAdvertisementData scanResponseData;
      scanResponseData.setName("Flow Fields");
      pAdvertising->setScanResponseData(scanResponseData);

      pAdvertising->start();
      if (debug) {Serial.println("Waiting a client connection to notify...");}
}
