#include <flatbuffers/string.h>
#include <igasset/fb_utils.h>

#include <string>
#include <string_view>

namespace igasset {

std::string util::extract_string(const flatbuffers::String* s) {
  if (s == nullptr)
    return "";
  return s->str();
}

}  // namespace igasset