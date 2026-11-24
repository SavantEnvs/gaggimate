// Fuzz the semantic-version parser (lib/OTA/src/semver.c). Version strings come from the OTA
// release lookup (GitHub release tag) and the controller's SystemInfo.version over the link;
// GitHubOTA/ControllerOTA parse them with semver_parse and compare with semver_compare.
// Input: two NUL-separated version strings.
#include "semver.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    char *buf = malloc(size + 2);
    memcpy(buf, data, size);
    buf[size] = 0;
    buf[size + 1] = 0;
    const char *a = buf;
    const char *b = buf + strlen(buf) + (strlen(buf) < size ? 1 : 0);
    if (b > buf + size)
        b = buf + size;

    semver_t x = {0}, y = {0};
    int ra = semver_parse(a, &x);
    int rb = semver_parse(b, &y);
    if (ra == 0 && rb == 0) {
        static const char *ops[] = {"=", ">", "<", ">=", "<=", "^", "~"};
        for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); i++)
            (void)semver_satisfies(x, y, ops[i]);
        if (semver_compare(x, x) != 0)
            abort(); // a version always equals itself
        (void)semver_compare(x, y);
        char out[4096];
        out[0] = 0;
        if (strlen(a) < 1024) {
            semver_render(&x, out);
            (void)semver_numeric(&x);
        }
    }
    (void)semver_is_valid(a);
    char *c = strdup(a);
    (void)semver_clean(c);
    free(c);
    semver_free(&x);
    semver_free(&y);
    free(buf);
    return 0;
}
