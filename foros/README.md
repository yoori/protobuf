# Foros protobuf changes

The `v36.2` branch starts at upstream tag `v36.2`. Packaging uses a separate tag,
`v36.2-ssv7`, so its source archive remains independent of subsequent branch changes.

This revision fixes reads of MicroString and inlined string fields in
`Reflection::GetStringView()` and the internal field visitor. It does not change
repeated string/bytes storage.

The RPM `%check` generates `protobuf-microstring.proto` with
`--cpp_out=experimental_cpp_micro_string`, then compiles and runs
`protobuf-microstring-check.cpp` against the newly built runtime. The check covers
heap and Arena messages, lengths 0 through 65536, embedded NULs, inactive and
switched oneof fields, Clear, reflection, field visits and wire compatibility
with ordinary string-backed fields. It also checks that nonempty MicroString
views in Arena messages point into the initial Arena block.

Build recipes live in the `foros-dependencies` repository (`protobuf-build.sh`
and `protobuf/protobuf.spec`). The recipes pin the GitHub tag archive by SHA-256;
a GitHub Release object is not required.
