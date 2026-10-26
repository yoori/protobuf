#!/bin/bash
set -euo pipefail

BUILD_DIR=$(realpath "${1:?Usage: check-repeated-microstring.sh BUILD_DIR}")
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SOURCE_DIR=$(dirname "$SCRIPT_DIR")
CHECK_DIR=${CHECK_DIR:-"$BUILD_DIR/check-repeated"}
CXX=${CXX:-c++}
mkdir -p "$CHECK_DIR"
"$BUILD_DIR/protoc" -I"$SOURCE_DIR/src" -I"$SCRIPT_DIR" \
  --cpp_out=experimental_cpp_micro_string:"$CHECK_DIR" \
  "$SCRIPT_DIR/protobuf-repeated-microstring.proto" \
  "$SCRIPT_DIR/protobuf-repeated-microstring-lite.proto"
"$CXX" -std=c++20 -g -O2 ${CHECK_CXXFLAGS:-} \
  -I"$SOURCE_DIR/src" -I"$CHECK_DIR" \
  "$SCRIPT_DIR/protobuf-repeated-microstring-check.cpp" \
  "$CHECK_DIR/protobuf-repeated-microstring.pb.cc" \
  "$CHECK_DIR/protobuf-repeated-microstring-lite.pb.cc" \
  -L"$BUILD_DIR" -Wl,-rpath,"$BUILD_DIR" -lprotobuf \
  $(pkg-config --libs absl_absl_check absl_absl_log absl_cord) \
  -o "$CHECK_DIR/check-repeated"
LD_LIBRARY_PATH="$BUILD_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$CHECK_DIR/check-repeated"

mkdir -p "$CHECK_DIR/legacy"
"$BUILD_DIR/protoc" -I"$SOURCE_DIR/src" -I"$SCRIPT_DIR" \
  --cpp_out="$CHECK_DIR/legacy" "$SCRIPT_DIR/protobuf-repeated-microstring.proto"
"$CXX" -std=c++20 -g -O2 ${CHECK_CXXFLAGS:-} \
  -I"$SOURCE_DIR/src" -I"$CHECK_DIR/legacy" \
  "$SCRIPT_DIR/protobuf-repeated-legacy-check.cpp" \
  "$CHECK_DIR/legacy/protobuf-repeated-microstring.pb.cc" \
  -L"$BUILD_DIR" -Wl,-rpath,"$BUILD_DIR" -lprotobuf \
  $(pkg-config --libs absl_absl_check absl_absl_log absl_cord) \
  -o "$CHECK_DIR/legacy/check-legacy"
LD_LIBRARY_PATH="$BUILD_DIR${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  "$CHECK_DIR/legacy/check-legacy"
