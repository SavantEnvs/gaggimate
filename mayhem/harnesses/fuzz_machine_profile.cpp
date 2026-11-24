// Fuzz machine-profile handling (src/display/core/MachineProfiles.cpp). A machine profile is JSON
// the LAN web UI posts with the settings form (WebUIPlugin -> validateMachineProfileRequest), or a
// reference string naming a built-in profile. The firmware deserializes it, validates it against
// the generated field schema (validateMachineProfile), hashes it (machineProfileHash) and resolves
// references (machineProfileReference). This harness runs those three on the input.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <display/core/MachineProfiles.h>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const std::string text(reinterpret_cast<const char *>(data), size);
    String ref;
    (void)machineProfileReference(String(text.c_str()), ref);

    JsonDocument doc;
    if (deserializeJson(doc, data, size))
        return 0;
    String error;
    const bool valid = validateMachineProfile(doc.as<JsonVariantConst>(), error);
    (void)isMachineProfileReference(doc.as<JsonVariantConst>());
    if (valid) {
        const String h1 = machineProfileHash(doc.as<JsonVariantConst>());
        const String h2 = machineProfileHash(doc.as<JsonVariantConst>());
        if (h1 != h2)
            __builtin_trap(); // the hash of one profile must be stable
    }
    return 0;
}
