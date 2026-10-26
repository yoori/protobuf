#include "protobuf-repeated-microstring.pb.h"
#include "protobuf-repeated-microstring-lite.pb.h"

#include <google/protobuf/arena.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/reflection.h>
#include <google/protobuf/reflection_visit_fields.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>

#include <cstdint>
#include <cstdlib>
#include <new>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#if !defined(__SANITIZE_ADDRESS__) && !defined(FOROS_DISABLE_ALLOCATION_PROBE)
namespace allocation_probe
{
  thread_local bool enabled = false;
  thread_local std::size_t count = 0;
}

void* operator new(std::size_t size)
{
  if (allocation_probe::enabled) ++allocation_probe::count;
  if (void* pointer = std::malloc(size ? size : 1)) return pointer;
  throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
#endif

namespace
{
  namespace PB = google::protobuf;
  using Micro = packaging::MicroRepeated;
  using Ordinary = packaging::OrdinaryRepeated;

  void require(bool condition, const char* message)
  {
    if (!condition) throw std::runtime_error(message);
  }

  void check_placement(const Micro& message, const char* storage, std::size_t capacity)
  {
    const auto begin = reinterpret_cast<std::uintptr_t>(storage);
    const auto* reflection = message.GetReflection();
    PB::Reflection::ScratchSpace scratch;
    for (const auto* field : {Micro::descriptor()->FindFieldByName("payload"),
      Micro::descriptor()->FindFieldByName("text"),
      Micro::descriptor()->FindFieldByName("distant")})
    {
      for (int i = 0; i < reflection->FieldSize(message, field); ++i)
      {
        const auto value = reflection->GetRepeatedStringView(message, field, i, scratch);
        if (!value.empty())
        {
          const auto address = reinterpret_cast<std::uintptr_t>(value.data());
          require(address >= begin && address + value.size() <= begin + capacity,
            "repeated string payload is outside Arena");
        }
      }
    }
  }

  void check_values(const Micro& message, const Ordinary& reference)
  {
    require(message.SerializeAsString() == reference.SerializeAsString(), "wire mismatch");
    const auto* reflection = message.GetReflection();
    PB::Reflection::ScratchSpace scratch;
    std::string copy;
    for (int f = 0; f < Micro::descriptor()->field_count(); ++f)
    {
      const auto* field = Micro::descriptor()->field(f);
      const auto* ref_field = Ordinary::descriptor()->field(f);
      const int size = reflection->FieldSize(message, field);
      require(size == reference.GetReflection()->FieldSize(reference, ref_field), "size mismatch");
      auto values = reflection->GetRepeatedFieldRef<std::string>(message, field);
      require(values.size() == size, "repeated ref size mismatch");
      for (int i = 0; i < size; ++i)
      {
        const auto expected = reference.GetReflection()->GetRepeatedString(reference, ref_field, i);
        require(reflection->GetRepeatedString(message, field, i) == expected,
          "copy getter mismatch");
        require(reflection->GetRepeatedStringReference(message, field, i, &copy) == expected,
          "reference getter mismatch");
        require(reflection->GetRepeatedStringView(message, field, i, scratch) == expected,
          "view getter mismatch");
        require(values.Get(i) == expected, "repeated ref getter mismatch");
      }
    }
    int visited = 0;
    PB::internal::VisitFields(message, [&](const auto& info)
    {
      using Info = std::decay_t<decltype(info)>;
      if constexpr (requires { Info::cpp_type; })
      {
        if constexpr (Info::cpp_type == PB::FieldDescriptor::CPPTYPE_STRING && Info::is_repeated)
        {
          for (const auto& value : info.Get())
          {
            (void)value.size();
            ++visited;
          }
        }
      }
    }, PB::internal::FieldMask::kString);
    require(visited == message.payload_size() + message.text_size() + message.distant_size(),
      "visitor skipped strings");
    require(message.SpaceUsedLong() >= sizeof(message), "invalid space accounting");
  }


#if !defined(__SANITIZE_ADDRESS__) && !defined(FOROS_DISABLE_ALLOCATION_PROBE)
  void check_allocations()
  {
    constexpr int count = 1000;
    for (const std::size_t length : {16, 64, 256, 1024})
    {
      auto storage = std::make_unique<char[]>(8 * 1024 * 1024);
      PB::Arena arena(storage.get(), 8 * 1024 * 1024);
      auto* micro = PB::Arena::Create<Micro>(&arena);
      auto* ordinary = PB::Arena::Create<Ordinary>(&arena);
      micro->mutable_payload()->Reserve(count);
      ordinary->mutable_payload()->Reserve(count);
      std::string input(length, 'x');
      allocation_probe::count = 0;
      allocation_probe::enabled = true;
      for (int i = 0; i < count; ++i) micro->add_payload(input);
      allocation_probe::enabled = false;
      const auto micro_allocations = allocation_probe::count;
      allocation_probe::count = 0;
      allocation_probe::enabled = true;
      for (int i = 0; i < count; ++i) ordinary->add_payload(input);
      allocation_probe::enabled = false;
      const auto ordinary_allocations = allocation_probe::count;
      require(micro_allocations == 0, "MicroString setter made separate heap allocations");
      require(ordinary_allocations >= count, "allocation probe did not observe string buffers");
      const auto wire = ordinary->SerializeAsString();
      auto* parsed = PB::Arena::Create<Micro>(&arena);
      allocation_probe::count = 0;
      allocation_probe::enabled = true;
      const bool parsed_ok = parsed->ParseFromString(wire);
      allocation_probe::enabled = false;
      require(parsed_ok, "allocation probe parse failed");
      require(allocation_probe::count == 0, "MicroString parser made separate heap allocations");
      require(parsed->SerializeAsString() == wire, "allocation probe parse changed data");
      std::cout << "length=" << length << " count=" << count
                << " ordinary_new=" << ordinary_allocations << " micro_new=" << micro_allocations
                << " micro_parse_new=0\n";
    }
  }


#endif
  std::string invalid_utf8_wire(unsigned field_number)
  {
    std::string wire;
    unsigned tag = (field_number << 3) | 2;
    while (tag >= 128)
    {
      wire.push_back(static_cast<char>((tag & 127) | 128));
      tag >>= 7;
    }
    wire.push_back(static_cast<char>(tag));
    wire.push_back(1);
    wire.push_back(static_cast<char>(255));
    return wire;
  }

  void check_parse_paths()
  {
    packaging::MicroParsePaths source;
    source.add_narrow(std::string(1024, 'a'));
    source.add_medium(std::string(1024, 'b'));
    source.add_far(std::string(1024, 'c'));
    source.add_far_bytes(std::string(1024, '\0'));
    const auto wire = source.SerializeAsString();
    PB::Arena arena;
    auto* parsed = PB::Arena::Create<packaging::MicroParsePaths>(&arena);
    require(parsed->ParseFromString(wire), "mini parser failed");
    require(parsed->SerializeAsString() == wire, "mini parser changed data");
    for (const unsigned field : {1, 1000, 10000})
    {
      require(!parsed->ParseFromString(invalid_utf8_wire(field)), "invalid UTF-8 accepted");
    }
    require(parsed->ParseFromString(invalid_utf8_wire(10001)), "bytes subjected to UTF-8 check");
    require(parsed->far_bytes(0).size() == 1, "mini bytes parser lost content");
  }

  void check()
  {
    constexpr std::size_t capacity = 8 * 1024 * 1024;
    auto storage = std::make_unique<char[]>(capacity);
    PB::Arena arena(storage.get(), capacity);
    auto* message = PB::Arena::Create<Micro>(&arena);
    Ordinary reference;
    for (const std::size_t size : {0, 1, 7, 8, 15, 16, 32, 255, 256, 4096, 65536})
    {
      std::string text(size, 'x');
      message->add_text(text);
      reference.add_text(text);
      if (size > 1) text[size / 2] = '\0';
      message->add_payload(text);
      reference.add_payload(text);
      message->add_distant(text);
      reference.add_distant(text);
      text.assign(text.size(), 'z');
    }
    check_values(*message, reference);
    check_placement(*message, storage.get(), capacity);
    absl::Cord cord(std::string(4096, 'a'));
    cord.Append(std::string(4096, 'b'));
    message->add_payload(cord);
    reference.add_payload(cord);
    check_values(*message, reference);
    check_placement(*message, storage.get(), capacity);
    const auto wire = message->SerializeAsString();

    Micro heap(*message);
    check_values(heap, reference);
    Micro moved(std::move(heap));
    check_values(moved, reference);
    heap = moved;
    check_values(heap, reference);

    auto* parsed = PB::Arena::Create<Micro>(&arena);
    require(parsed->ParseFromString(wire), "parse failed");
    check_values(*parsed, reference);
    check_placement(*parsed, storage.get(), capacity);
    parsed->Clear();
    PB::io::ArrayInputStream chunks(wire.data(), static_cast<int>(wire.size()), 3);
    require(parsed->ParseFromZeroCopyStream(&chunks), "chunked parse failed");
    check_values(*parsed, reference);
    check_placement(*parsed, storage.get(), capacity);
    parsed->MergeFrom(*message);
    Ordinary doubled(reference);
    doubled.MergeFrom(reference);
    check_values(*parsed, doubled);
    require(!parsed->ParseFromString(wire.substr(0, wire.size() - 1)), "truncated wire accepted");

    auto* lite = PB::Arena::Create<packaging::MicroRepeatedLite>(&arena);
    require(lite->ParseFromString(wire), "lite parse failed");
    require(lite->SerializeAsString() == wire, "lite wire mismatch");
    packaging::MicroRepeatedLite lite_copy(*lite);
    require(lite_copy.SerializeAsString() == wire, "lite copy mismatch");
    lite->Clear();
    lite->MergeFrom(lite_copy);
    require(lite->SerializeAsString() == wire, "lite merge mismatch");

    {
      PB::Arena adopted_arena;
      auto* adopted = PB::Arena::Create<Micro>(&adopted_arena);
      auto* external = new PB::internal::ArenaMicroString;
      external->assign(absl::string_view("heap element"));
      adopted->mutable_payload()->AddAllocated(external);
      adopted->Clear();
      require(adopted->ParseFromString(wire), "parse into adopted heap element failed");
      check_values(*adopted, reference);
      std::unique_ptr<PB::internal::ArenaMicroString> released(
        adopted->mutable_payload()->ReleaseLast());
      require(released->GetArena() == nullptr, "ReleaseLast did not produce a heap element");
    }

    PB::DynamicMessageFactory factory;
    auto dynamic = std::unique_ptr<PB::Message>(factory.GetPrototype(Micro::descriptor())->New());
    require(dynamic->ParseFromString(wire), "dynamic parse failed");
    Micro from_dynamic;
    from_dynamic.CopyFrom(*dynamic);
    check_values(from_dynamic, reference);
    dynamic->CopyFrom(from_dynamic);
    require(dynamic->SerializeAsString() == wire, "dynamic copy mismatch");
    require(PB::util::MessageDifferencer::Equals(from_dynamic, *dynamic), "differencer mismatch");
    std::string text;
    require(PB::TextFormat::PrintToString(*message, &text), "text format print failed");
    Micro from_text;
    require(PB::TextFormat::ParseFromString(text, &from_text), "text format parse failed");
    check_values(from_text, reference);

    const auto* field = Micro::descriptor()->FindFieldByName("payload");
    const auto* reflection = message->GetReflection();
    const auto* ref_field = Ordinary::descriptor()->FindFieldByName("payload");
    reflection->SetRepeatedString(message, field, 0, std::string(1024, 'a'));
    reference.set_payload(0, std::string(1024, 'a'));
    reflection->AddString(message, field, std::string(1024, 'b'));
    reference.add_payload(std::string(1024, 'b'));
    reflection->SwapElements(message, field, 0, 1);
    reference.mutable_payload()->SwapElements(0, 1);
    reflection->RemoveLast(message, field);
    reference.mutable_payload()->RemoveLast();
    check_values(*message, reference);
    check_placement(*message, storage.get(), capacity);

    auto mutable_ref = reflection->GetMutableRepeatedFieldRef<std::string>(message, field);
    mutable_ref.Add(std::string(1024, 'c'));
    reference.add_payload(std::string(1024, 'c'));
    mutable_ref.Set(0, std::string(2048, 'd'));
    reference.set_payload(0, std::string(2048, 'd'));
    check_values(*message, reference);
    check_placement(*message, storage.get(), capacity);

    Ordinary other;
    other.add_payload("one");
    auto old_ref =
      other.GetReflection()->GetMutableRepeatedFieldRef<std::string>(&other, ref_field);
    const auto saved_payload = reference.payload();
    mutable_ref.Swap(old_ref);
    require(message->payload_size() == 1 && message->payload(0) == "one", "cross-type swap failed");
    require(other.payload_size() == saved_payload.size(), "cross-type swap lost elements");
    old_ref.Swap(mutable_ref);
    check_values(*message, reference);

    Micro survivor;
    {
      PB::Arena temporary;
      auto* source = PB::Arena::Create<Micro>(&temporary);
      source->CopyFrom(*message);
      survivor.Swap(source);
    }
    check_values(survivor, reference);
    auto storage2 = std::make_unique<char[]>(capacity);
    PB::Arena arena2(storage2.get(), capacity);
    auto* target = PB::Arena::Create<Micro>(&arena2);
    target->add_payload("other arena");
    message->Swap(target);
    check_values(*target, reference);
    check_placement(*target, storage2.get(), capacity);
    reflection->ClearField(target, field);
    require(target->payload_size() == 0, "reflection ClearField failed");
    target->add_payload(std::string(1024, 'r'));
    check_placement(*target, storage2.get(), capacity);
  }
}

int main()
{
  try
  {
    check();
    check_parse_paths();
#if !defined(__SANITIZE_ADDRESS__) && !defined(FOROS_DISABLE_ALLOCATION_PROBE)
    check_allocations();
#endif
    PB::ShutdownProtobufLibrary();
    std::cout << "Repeated MicroString Arena, parsing, reflection, copy and swap: PASS\n";
  }
  catch (const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
