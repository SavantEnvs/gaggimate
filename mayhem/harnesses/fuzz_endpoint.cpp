// Fuzz the NanoPbComm protocol session (lib/NanoPbComm/src/Endpoint.cpp): the code both firmware
// units run on every datagram received over BLE or UART from the other unit.
//
// Input: a sequence of records [op][len][len bytes]. op bit 0..1 select what happens before the
// datagram is delivered: advance the clock (drives ACK timeouts/retransmits), toggle the link,
// or nothing. Each datagram is handed to Endpoint::handleData via Transport::emitData, exactly as
// BleServerTransport / UartTransport do. Payloads the peer sends are queued back with
// Endpoint::send (what the application handlers do when they answer), which exercises the
// coalescing queue (Protocol.h keys/priorities) and the frame encoder.
// A fresh Endpoint is built per input, so no state survives between inputs.
#include "Endpoint.h"
#include "fake_clock.h"
#include <cstddef>
#include <cstdint>
#include <pb_decode.h>

namespace {
class FuzzTransport : public Transport {
  public:
    bool connected = false;
    size_t sent = 0;
    bool send(const uint8_t *data, size_t length) override {
        // A sent frame must decode as a Frame: the encoder and decoder agree on the schema.
        gm::Frame f = gaggimate_Frame_init_zero;
        pb_istream_t is = pb_istream_from_buffer(data, length);
        if (!pb_decode(&is, &gaggimate_Frame_msg, &f))
            __builtin_trap();
        sent += length;
        return true;
    }
    bool isConnected() const override { return connected; }
    size_t maxDatagram() const override { return 244; } // BLE MTU payload used by the BLE transports
    void deliver(const uint8_t *d, size_t n) { emitData(d, n); }
    void link(bool up) {
        connected = up;
        emitConnection(up);
    }
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    g_fuzz_now_ms = 0;
    FuzzTransport t;
    Endpoint *ep = new Endpoint(t);
    ep->begin();
    for (pb_size_t tag = 0; tag < 32; tag++)
        ep->on(tag, [](const gm::Payload &) {});
    t.link(true);

    size_t i = 0;
    while (i + 2 <= size) {
        const uint8_t op = data[i];
        size_t len = data[i + 1];
        i += 2;
        if (len > size - i)
            len = size - i;
        switch (op & 3) {
        case 1:
            g_fuzz_now_ms += static_cast<unsigned long>(op >> 2) * 50;
            break;
        case 2:
            t.link(!t.connected);
            break;
        default:
            break;
        }
        t.deliver(data + i, len);
        if (op & 0x80) {
            gm::Frame f = gaggimate_Frame_init_zero;
            pb_istream_t is = pb_istream_from_buffer(data + i, len);
            if (pb_decode(&is, &gaggimate_Frame_msg, &f)) {
                if (op & 0x40)
                    ep->sendBatch(f.payloads, f.payloads_count);
                else
                    for (pb_size_t k = 0; k < f.payloads_count; k++)
                        ep->send(f.payloads[k]);
            }
        }
        ep->loop();
        i += len;
    }
    delete ep;
    return 0;
}
