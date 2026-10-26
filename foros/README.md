# Foros protobuf changes

The `v36.2` branch starts at upstream tag `v36.2`. Packaging uses a separate tag,
`v36.2-ssv7`, so its source archive remains independent of subsequent branch changes.

Tag `v36.2-ssv7` fixes reads of MicroString and inlined string fields in
`Reflection::GetStringView()` and the internal field visitor. That tag does not
change repeated string/bytes storage.

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

## Repeated string/bytes on the v36.2 branch

With `--cpp_out=experimental_cpp_micro_string`, repeated string/bytes fields
marked `features.(pb.cpp).string_type = VIEW` use
`RepeatedPtrField<internal::ArenaMicroString>`. Both element objects and payloads
of normally constructed Arena messages use the Arena. Heap messages retain heap
ownership. Assignments, including rvalue strings, copy into the destination
allocator; they never retain a view into caller-owned memory.

Generated indexed getters still return `absl::string_view`. Setters accept views,
strings and Cord. Container accessors return the new element type; code requiring
`RepeatedPtrField<std::string>` must be adapted. Typed legacy reflection requests
for that old container are rejected. Ordinary string reflection, views,
`RepeatedFieldRef<std::string>`, dynamic messages and field visitors are supported.

The representation is not enabled for extensions, map entries or the experimental
PROXY repeated API. Fields with string_type STRING, or code generated without
experimental_cpp_micro_string, retain their previous representation.

The parser supports fast one- and two-byte tags, the mini parser and input split
across stream buffers. Elements adopted from the heap via AddAllocated retain their
original allocator when the parser reuses them. Arena reset still invalidates views.

Build the library and compiler, then run:

```sh
CXX=/opt/rh/gcc-toolset-10/root/usr/bin/g++ \
  bash foros/check-repeated-microstring.sh /path/to/build
```

The test covers full and lite messages, wire compatibility, Arena placement,
copy/move/merge, same- and cross-Arena ownership, legacy container interoperability
through reflection, dynamic messages, text format, malformed input, UTF-8 and
allocation counts for 1000 strings of lengths 16, 64, 256 and 1024. Allocation
counts use a preallocated Arena and exclude setup. They count global operator new
calls, not all malloc calls or the cost of allocating Arena blocks.

`CHECK_DIR` selects a separate output directory. `CHECK_CXXFLAGS` can instrument
the test and generated C++ (it does not rebuild the protobuf runtime):

```sh
CXX=/opt/rh/gcc-toolset-10/root/usr/bin/g++ \
CHECK_CXXFLAGS='-fsanitize=address -fno-omit-frame-pointer' \
CHECK_DIR=/path/to/build/check-repeated-asan \
  bash foros/check-repeated-microstring.sh /path/to/build
```

The allocation counter is disabled for the ASan build to avoid interposing on its
allocator. For Valgrind, use `CHECK_CXXFLAGS=-DFOROS_DISABLE_ALLOCATION_PROBE`
and a separate CHECK_DIR, then run Memcheck on the resulting check-repeated binary.
The script also checks that VIEW fields generated without the MicroString flag
retain std::string storage and their legacy reflection API. The release RPM recipe
still pins tag v36.2-ssv7 until a new tested tag
is published.
