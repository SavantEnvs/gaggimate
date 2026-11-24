// Fuzz the OTA firmware download state machine (lib/OTA/src/ResumableDownloader.cpp): everything
// the device does with an HTTP server's answers while fetching a firmware image — status codes,
// redirects, Content-Length, Content-Range (parseContentRange), ETag/If-Range resume, short reads,
// read errors and retries. The server (or anyone on the path) controls all of it.
// The fuzz input scripts the server: each HttpTransport call takes its answer from the input.
// DownloadEnv never sleeps (delayMs is a no-op), so retries cost no wall time.
#include "ResumableDownloader.h"
#include <cstring>
#include <fuzzer/FuzzedDataProvider.h>
#include <string>
#include <vector>

namespace {
class FuzzTransport : public HttpTransport {
  public:
    explicit FuzzTransport(FuzzedDataProvider &p) : _p(p) {}
    bool begin(const std::string &) override { return _p.ConsumeIntegralInRange<int>(0, 15) != 0; }
    void setHeader(const char *, const char *) override {}
    bool open(int &status, int &contentLength) override {
        if (_p.ConsumeIntegralInRange<int>(0, 15) == 0)
            return false;
        static const int codes[] = {200, 206, 301, 302, 307, 308, 404, 416, 500, 503};
        status = _p.ConsumeBool() ? codes[_p.ConsumeIntegralInRange<size_t>(0, 9)] : _p.ConsumeIntegral<int16_t>();
        contentLength = _p.ConsumeBool() ? -1 : _p.ConsumeIntegralInRange<int>(-2, 1 << 16);
        return true;
    }
    bool followRedirect() override { return _p.ConsumeBool(); }
    int read(uint8_t *buffer, size_t len) override {
        if (_p.remaining_bytes() == 0)
            return -1;
        const int mode = _p.ConsumeIntegralInRange<int>(0, 7);
        if (mode == 0)
            return -1;
        if (mode == 1)
            return 0;
        std::vector<uint8_t> chunk = _p.ConsumeBytes<uint8_t>(_p.ConsumeIntegralInRange<size_t>(0, len));
        if (!chunk.empty())
            memcpy(buffer, chunk.data(), chunk.size());
        return static_cast<int>(chunk.size());
    }
    bool isComplete() override { return _p.ConsumeIntegralInRange<int>(0, 3) == 0 || _p.remaining_bytes() == 0; }
    std::string header(const char *) override { return _p.ConsumeRandomLengthString(64); }
    void end() override {}

  private:
    FuzzedDataProvider &_p;
};

class FuzzEnv : public DownloadEnv {
  public:
    void delayMs(uint32_t) override {}
    bool waitForNetwork(uint32_t) override { return true; }
    uint8_t *allocBuffer(size_t size) override { return new uint8_t[size]; }
    void freeBuffer(uint8_t *b) override { delete[] b; }
};
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    FuzzedDataProvider p(data, size);
    {
        // Content-Range header values reach parseContentRange straight from the response.
        size_t start = 0, total = 0;
        ResumableDownloader::parseContentRange(p.ConsumeRandomLengthString(64), start, total);
    }
    FuzzTransport t(p);
    FuzzEnv env;
    size_t expected = 0, written = 0;
    DownloadSink sink;
    sink.prepare = [&](size_t total) {
        expected = total;
        written = 0;
        return true;
    };
    sink.write = [&](const uint8_t *, size_t len) {
        written += len;
        return true;
    };
    sink.restart = [&]() {
        written = 0;
        return true;
    };
    DownloadPolicy policy;
    policy.chunkSize = 512;
    ResumableDownloader dl(t, env, "https://example.invalid/firmware.bin", sink, nullptr, policy);
    if (dl.run() && expected > 0 && written != expected)
        __builtin_trap(); // reported success with a short/long image
    return 0;
}
