#!/usr/bin/env bash
# Build the gaggimate fuzz targets + the upstream native unit tests.
# The firmware is PlatformIO/Arduino for the ESP32; the parsers fuzzed here are hardware
# independent and are compiled on the host with upstream's own simulator shims (sim/platform),
# exactly as upstream's display-sim / native envs do. No network: every dependency is under
# /opt/toolchains/deps (fetched, pinned, by mayhem/Dockerfile).
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
: "${CC:=clang}" "${CXX:=clang++}"
SANITIZER_FLAGS="${SANITIZER_FLAGS-"-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g"}"
: "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${COVERAGE_FLAGS:=}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${DEPS:=/opt/toolchains/deps}"
AJSON="$DEPS/ArduinoJson-7.4.2/src/src"
NANOPB="$DEPS/nanopb-0.4.9.1/src"
UNITY="$DEPS/Unity-2.6.0/src/src"
cd "$SRC"
OUT="$SRC/out"
OBJ="$OUT/obj"
mkdir -p "$OBJ"

FUZZ_CFLAGS="$SANITIZER_FLAGS $DEBUG_FLAGS -O1 -fsanitize=fuzzer-no-link"
SIM_DEFS="-DGAGGIMATE_SIM -DARDUINO=10812 -DCORE_DEBUG_LEVEL=0"
SIM_INC="-Isim/platform -Isim/platform/arduino -Imayhem/harnesses/shim -Imayhem/harnesses -Imayhem/harnesses/gen -Ilib/NanoPbComm/src -Ilib/OTA/src -Isrc -I$NANOPB -I$AJSON"
CXXF="-std=gnu++17 $SIM_DEFS $SIM_INC $FUZZ_CFLAGS"
CF="$SIM_DEFS $SIM_INC $FUZZ_CFLAGS"

cc_c()   { "$CC"  $CF   -c "$1" -o "$OBJ/$2.o"; }
cc_cxx() { "$CXX" $CXXF -c "$1" -o "$OBJ/$2.o"; }

# --- the project code (sanitized, coverage-instrumented) ---
cc_cxx lib/NanoPbComm/src/Endpoint.cpp Endpoint
cc_cxx lib/NanoPbComm/src/uart/UartTransport.cpp UartTransport
cc_cxx lib/OTA/src/ResumableDownloader.cpp ResumableDownloader
cc_c   lib/OTA/src/semver.c semver
# nanopb runtime + generated gaggimate.pb.c (generated from lib/NanoPbComm/proto by nanopb 0.4.9.1)
cc_c "$NANOPB/pb_common.c" pb_common
cc_c "$NANOPB/pb_decode.c" pb_decode
cc_c "$NANOPB/pb_encode.c" pb_encode
cc_c mayhem/harnesses/gen/gaggimate.pb.c gaggimate_pb
# upstream's Arduino host shims
cc_cxx sim/platform/arduino/Stream.cpp Stream
cc_cxx sim/platform/arduino/Print.cpp Print
cc_cxx sim/platform/arduino/WString.cpp WString
cc_cxx sim/platform/arduino_shim.cpp arduino_shim
cc_c sim/platform/arduino/stdlib_noniso.c stdlib_noniso
cc_c sim/platform/noniso_extra.c noniso_extra
"$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS -c mayhem/lsan_off.cc -o "$OBJ/lsan_off.o"
"$CC" $SANITIZER_FLAGS $DEBUG_FLAGS -c "$STANDALONE_FUZZ_MAIN" -o "$OBJ/standalone_main.o"

ARDUINO_OBJS="$OBJ/Stream.o $OBJ/Print.o $OBJ/WString.o $OBJ/stdlib_noniso.o $OBJ/noniso_extra.o"
PB_OBJS="$OBJ/gaggimate_pb.o $OBJ/pb_common.o $OBJ/pb_decode.o $OBJ/pb_encode.o"

link() { # name harness-object libs...
  local name="$1"; shift
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS $LIB_FUZZING_ENGINE "$@" "$OBJ/lsan_off.o" -o "$OUT/$name"
  "$CXX" $SANITIZER_FLAGS $DEBUG_FLAGS "$@" "$OBJ/lsan_off.o" "$OBJ/standalone_main.o" -o "$OUT/$name-standalone"
}

cc_cxx mayhem/harnesses/fuzz_endpoint.cpp h_endpoint
link fuzz_endpoint "$OBJ/h_endpoint.o" "$OBJ/Endpoint.o" $ARDUINO_OBJS $PB_OBJS

cc_cxx mayhem/harnesses/fuzz_uart_transport.cpp h_uart
link fuzz_uart_transport "$OBJ/h_uart.o" "$OBJ/UartTransport.o" "$OBJ/Endpoint.o" $ARDUINO_OBJS $PB_OBJS

cc_cxx mayhem/harnesses/fuzz_profile.cpp h_profile
link fuzz_profile "$OBJ/h_profile.o" "$OBJ/arduino_shim.o" $ARDUINO_OBJS

cc_c mayhem/harnesses/fuzz_semver.c h_semver
link fuzz_semver "$OBJ/h_semver.o" "$OBJ/semver.o"

"$CXX" -std=c++17 $FUZZ_CFLAGS -DOTA_LOG_LEVEL=0 -Ilib/OTA/src -c mayhem/harnesses/fuzz_ota_download.cpp -o "$OBJ/h_ota.o"
link fuzz_ota_download "$OBJ/h_ota.o" "$OBJ/ResumableDownloader.o"

# --- display settings: machine-profile JSON + wake-up schedule strings ---
# The field schema header is generated from committed web/src/data JSON by upstream's own script
# (src/display/webassets/ is gitignored, so build.sh recreates it on every run).
python3 scripts/generate_machine_profile_schema.py
DISP_INC="-Isim/platform -Isim/platform/arduino -Isim/comms -Isim/web -Isrc -Imayhem/harnesses/gen -I$NANOPB -I$AJSON"
DXXF="-std=gnu++17 $SIM_DEFS $DISP_INC $FUZZ_CFLAGS"
for f in src/display/core/Settings.cpp src/display/core/MachineProfiles.cpp src/display/core/utils.cpp \
         src/display/util/ColorConversion.cpp sim/platform/preferences_shim.cpp; do
  "$CXX" $DXXF -c "$f" -o "$OBJ/d_$(basename "$f" .cpp).o"
done
DISP_OBJS="$OBJ/d_Settings.o $OBJ/d_MachineProfiles.o $OBJ/d_utils.o $OBJ/d_ColorConversion.o $OBJ/d_preferences_shim.o $OBJ/arduino_shim.o $ARDUINO_OBJS"
for h in machine_profile wakeup_schedules; do
  "$CXX" $DXXF -c mayhem/harnesses/fuzz_$h.cpp -o "$OBJ/h_$h.o"
  link fuzz_$h "$OBJ/h_$h.o" $DISP_OBJS
done

# --- upstream's native Unity unit tests (test/test_*), normal flags, like `pio test -e native` ---
TOBJ="$OUT/tests"
mkdir -p "$TOBJ"
"$CC" -O1 $COVERAGE_FLAGS -I"$UNITY" -c "$UNITY/unity.c" -o "$TOBJ/unity.o"
NATIVE_INC="-Isrc -Ilib/NayrodPID/src -Ilib/GaggiMateController/src -Ilib/OTA/src -Itest/ota_common -Itest/test_puckflow_latch/native_stubs -I$UNITY"
for t in autotune_simc button_handler heater_coordinator ota_download puckflow_latch shot_log_layout simple_pid_rate; do
  "$CXX" -std=c++17 -O1 $COVERAGE_FLAGS $NATIVE_INC -DOTA_LOG_LEVEL=2 -Wno-unused-variable -Wno-unused-function \
    test/test_$t/test_$t.cpp "$TOBJ/unity.o" -o "$TOBJ/test_$t"
done
echo "build.sh: done"
