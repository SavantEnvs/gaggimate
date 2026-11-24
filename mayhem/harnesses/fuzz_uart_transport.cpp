// Fuzz the controller<->display UART link (lib/NanoPbComm/src/uart/UartTransport.cpp): the raw
// byte stream a peer (or line noise) writes to the serial port. UartTransport reassembles
// 0x00-delimited COBS frames, checks the CRC-16, tracks the keepalive handshake and demuxes
// channels; protocol-channel datagrams go on into an Endpoint (nanopb Frame decode, ACK/dedup),
// the same chain the firmware builds in GaggiMateServer/GaggiMateClient.
//
// Input: the bytes the serial port delivers. They are fed in slices whose sizes come from the
// input itself (a 0x00 never splits; each loop() drains what is "available"), and the fake clock
// advances 20 ms per slice so link timeouts and keepalives fire deterministically.
// A fresh transport + Endpoint is built per input.
#include "Endpoint.h"
#include "fake_clock.h"
#include "uart/UartTransport.h"
#include <Stream.h>
#include <cstddef>
#include <cstdint>

namespace {
class FuzzStream : public Stream {
  public:
    const uint8_t *data = nullptr;
    size_t size = 0, pos = 0, limit = 0;
    size_t written = 0;
    int available() override { return static_cast<int>(limit - pos); }
    int read() override { return pos < limit ? data[pos++] : -1; }
    int peek() override { return pos < limit ? data[pos] : -1; }
    size_t write(uint8_t) override {
        written++;
        return 1;
    }
    size_t write(const uint8_t *, size_t n) override {
        written += n;
        return n;
    }
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    g_fuzz_now_ms = 0;
    FuzzStream s;
    s.data = data;
    s.size = size;
    UartTransport *t = new UartTransport(s);
    Endpoint *ep = new Endpoint(*t);
    t->onChannelData(UartTransport::CHANNEL_OTA, [](const uint8_t *, size_t) {});
    t->begin();
    ep->begin();
    for (pb_size_t tag = 0; tag < 32; tag++)
        ep->on(tag, [](const gm::Payload &) {});

    // Slice size = 64 bytes, or until just past the next 0x00 delimiter, whichever comes first.
    while (s.pos < size) {
        size_t end = s.pos;
        while (end < size && end - s.pos < 64 && data[end++] != 0x00) {
        }
        s.limit = end;
        t->loop();
        ep->loop();
        g_fuzz_now_ms += 20;
    }
    delete ep;
    delete t;
    return 0;
}
