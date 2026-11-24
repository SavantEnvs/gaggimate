// Fuzz the auto-wakeup schedule parser (src/display/core/Settings.cpp parseAutoWakeupSchedules).
// The string arrives as the `autowakeupSchedules` argument of the web UI settings POST
// (WebUIPlugin.cpp) and is also read back from NVS. Parsed schedules are serialized again
// (serializeAutoWakeupSchedules) and re-parsed: the round trip must be a fixed point.
#include <Arduino.h>
#include <display/core/Settings.h>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    const std::string text(reinterpret_cast<const char *>(data), size);
    const auto schedules = parseAutoWakeupSchedules(String(text.c_str()));
    const String once = serializeAutoWakeupSchedules(schedules);
    const String twice = serializeAutoWakeupSchedules(parseAutoWakeupSchedules(once));
    if (once != twice)
        __builtin_trap();
    return 0;
}
