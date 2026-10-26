#include "protobuf-repeated-microstring.pb.h"

#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(std::is_same_v<
  decltype(std::declval<packaging::MicroRepeated>().mutable_payload()),
  google::protobuf::RepeatedPtrField<std::string>*>);

int main()
{
  packaging::MicroRepeated message;
  const std::string input(1024, 'x');
  message.add_payload(std::string_view(input));
  message.set_payload(0, std::string_view(input));
  const auto* field = message.GetDescriptor()->FindFieldByName("payload");
  const auto& legacy = message.GetReflection()->GetRepeatedPtrField<std::string>(message, field);
  packaging::MicroRepeated parsed;
  if (legacy.Get(0) != input || !parsed.ParseFromString(message.SerializeAsString()) ||
    parsed.payload(0) != input)
  {
    std::cerr << "Legacy VIEW field behavior changed\n";
    return 1;
  }
  std::cout << "VIEW fields without MicroString mode retain std::string storage: PASS\n";
}
