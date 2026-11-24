// Fuzz the brew-profile JSON parser (src/display/models/profile.h: parseProfile/writeProfile).
// Profiles arrive from the LAN web UI (WebSocket req:profiles:save, profile import) and are
// stored as /p/<id>.json; ProfileManager parses them with exactly this deserialize + parseProfile.
// Accepted profiles are serialized back (writeProfile) and re-parsed: the round trip must succeed.
#include <Arduino.h>
#include <ArduinoJson.h>
#include <display/models/profile.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    JsonDocument doc;
    if (deserializeJson(doc, data, size))
        return 0;
    Profile p;
    if (!parseProfile(doc.as<JsonObject>(), p))
        return 0;
    JsonDocument out;
    JsonObject o = out.to<JsonObject>();
    writeProfile(o, p);
    String text;
    serializeJson(out, text);
    JsonDocument again;
    if (deserializeJson(again, text.c_str(), text.length()))
        __builtin_trap();
    Profile p2;
    if (!parseProfile(again.as<JsonObject>(), p2))
        __builtin_trap(); // a profile we wrote must parse again
    return 0;
}
