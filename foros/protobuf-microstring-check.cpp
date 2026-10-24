#include "protobuf-microstring.pb.h"

#include <google/protobuf/arena.h>
#include <google/protobuf/reflection_visit_fields.h>

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace
{
  namespace PB = google::protobuf;

  void require(bool condition, const char* message)
  {
    if (!condition)
    {
      throw std::runtime_error(message);
    }
  }

  void check_reflection(const PB::Message& message, std::string_view block)
  {
    const auto* reflection = message.GetReflection();
    const auto* descriptor = message.GetDescriptor();
    PB::Reflection::ScratchSpace scratch;
    for (int i = 0; i < descriptor->field_count(); ++i)
    {
      const auto* field = descriptor->field(i);
      if (field->cpp_type() != PB::FieldDescriptor::CPPTYPE_STRING)
      {
        continue;
      }

      const auto actual = reflection->GetStringView(message, field, scratch);
      const auto expected = reflection->GetString(message, field);
      require(actual == expected, "GetStringView returned a wrong value");
      if (!block.empty() && !actual.empty())
      {
        const auto begin = reinterpret_cast<std::uintptr_t>(block.data());
        const auto address = reinterpret_cast<std::uintptr_t>(actual.data());
        require(address >= begin && address + actual.size() <= begin + block.size(),
          "MicroString view is outside Arena");
      }
    }

    std::vector<bool> visited(descriptor->field_count());
    PB::internal::VisitFields(message, [&](const auto& info)
    {
      using Info = std::decay_t<decltype(info)>;
      if constexpr (requires { Info::cpp_type; Info::is_cord; })
      {
        if constexpr (Info::cpp_type == PB::FieldDescriptor::CPPTYPE_STRING &&
          !Info::is_repeated && !Info::is_cord)
        {
          const auto* field = descriptor->FindFieldByNumber(info.number());
          require(info.Get() == reflection->GetString(message, field),
            "field visitor returned a wrong value");
          visited[field->index()] = true;
        }
      }
    }, PB::internal::FieldMask::kString);

    std::vector<const PB::FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (const auto* field : fields)
    {
      if (field->cpp_type() == PB::FieldDescriptor::CPPTYPE_STRING)
      {
        require(visited[field->index()], "field visitor skipped a string field");
      }
    }
  }

  void check_message(packaging::MicroMessage& message, std::string_view block, std::size_t size)
  {
    std::string input(size, 'x');
    if (size > 2)
    {
      input[size / 2] = '\0';
    }

    packaging::StringMessage reference;
    const auto compare = [&]
    {
      check_reflection(message, block);
      check_reflection(reference, {});
      require(reference.SerializeAsString() == message.SerializeAsString(),
        "wire representation differs");
    };

    compare();
    message.set_payload(input);
    message.set_text(input);
    reference.set_payload(input);
    reference.set_text(input);
    compare();

    message.set_choice_payload(input);
    reference.set_choice_payload(input);
    compare();
    message.set_choice_text(input);
    reference.set_choice_text(input);
    compare();
    message.set_other(7);
    reference.set_other(7);
    compare();

    message.Clear();
    reference.Clear();
    compare();
  }
}

int main()
{
  try
  {
    constexpr std::size_t capacity = 1024 * 1024;
    auto storage = std::make_unique<char[]>(capacity);
    for (const std::size_t size : {0, 8, 15, 16, 32, 128, 1024, 4096, 65536})
    {
      PB::Arena arena(storage.get(), capacity);
      auto* message = PB::Arena::Create<packaging::MicroMessage>(&arena);
      check_message(*message, {storage.get(), capacity}, size);
      packaging::MicroMessage heap_message;
      check_message(heap_message, {}, size);
    }

    std::cout << "MicroString Arena, reflection, field visitor and wire compatibility: PASS\n";
  }
  catch (const std::exception& error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
