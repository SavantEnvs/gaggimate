// Deterministic clock for the protocol harnesses: millis()/micros() advance only when the harness
// says so, so link timeouts, retransmits and RTT depend on the input, never on host speed.
#pragma once
#include <cstdint>
#include <Arduino.h>

static unsigned long g_fuzz_now_ms = 0;
unsigned long millis(void) { return g_fuzz_now_ms; }
unsigned long micros(void) { return g_fuzz_now_ms * 1000UL; }
void delay(uint32_t ms) { g_fuzz_now_ms += ms; }
void delayMicroseconds(uint32_t) {}
void yield(void) {}
