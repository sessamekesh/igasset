#ifndef IGASSET_FB_UTILS_H
#define IGASSET_FB_UTILS_H

#include <flatbuffers/string.h>

#include <string>

namespace igasset::util {

std::string extract_string(const flatbuffers::String* s);

}  // namespace igasset::util

#endif
