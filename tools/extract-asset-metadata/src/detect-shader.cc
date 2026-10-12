#include <extract-asset-metadata/detect.h>
#include <extract-asset-metadata/utf8.h>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace assetmeta {
namespace {

bool is_ident_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

// Reads the entry-point name that follows an @stage attribute chain, e.g.
//   @fragment fn fs_main(...)  or  @compute @workgroup_size(8,8) fn simulate
// Returns false when no fn follows within the (bounded) lookahead.
bool parse_entry_point_after_attribute(std::string_view text,
                                       std::size_t at_pos, std::string* name) {
  std::size_t p = at_pos;
  std::size_t guard = 0;
  while (p < text.size() && guard < 8) {
    ++guard;
    while (p < text.size() && (text[p] == ' ' || text[p] == '\t' ||
                               text[p] == '\r' || text[p] == '\n')) {
      ++p;
    }
    if (p >= text.size()) {
      return false;
    }

    if (text[p] == '@') {
      // Another attribute in the chain: skip it (including any parenthesized
      // argument group).
      ++p;
      while (p < text.size() && is_ident_char(text[p])) {
        ++p;
      }
      while (p < text.size() && (text[p] == ' ' || text[p] == '\t' ||
                                 text[p] == '\r' || text[p] == '\n')) {
        ++p;
      }
      if (p < text.size() && text[p] == '(') {
        int depth = 0;
        while (p < text.size()) {
          if (text[p] == '(') {
            ++depth;
          } else if (text[p] == ')') {
            --depth;
            ++p;
            if (depth == 0) {
              break;
            }
            continue;
          }
          ++p;
        }
      }
      continue;
    }

    if (text.compare(p, 2, "fn") == 0 &&
        (p + 2 >= text.size() || !is_ident_char(text[p + 2]))) {
      // "fn" keyword found; the entry-point name follows.
      p += 2;
      while (p < text.size() && (text[p] == ' ' || text[p] == '\t' ||
                                 text[p] == '\r' || text[p] == '\n')) {
        ++p;
      }
      const std::size_t start = p;
      while (p < text.size() && is_ident_char(text[p])) {
        ++p;
      }
      if (p == start) {
        return false;
      }
      *name = std::string(text.substr(start, p - start));
      return true;
    }
    return false;  // whatever follows is not an entry point
  }
  return false;
}

}  // namespace

// WGSL entry-point scan (plan D13: heuristics only - there is no WGSL
// parser in this repo, and the pipeline only ever copies WGSL verbatim).
std::optional<ShaderInfo> detect_shader(const std::string& bytes) {
  if (!utf8::plausible_text(bytes)) {
    return std::nullopt;
  }
  const std::string_view text(bytes);

  struct StagePattern {
    const char* attribute;
    const char* stage;
  };
  static constexpr StagePattern kStages[] = {{"@vertex", "vertex"},
                                             {"@fragment", "fragment"},
                                             {"@compute", "compute"}};

  ShaderInfo info;
  for (const auto& stage : kStages) {
    std::size_t p = 0;
    while (true) {
      const std::size_t at = text.find(stage.attribute, p);
      if (at == std::string_view::npos) {
        break;
      }
      p = at + 1;

      std::string name;
      if (!parse_entry_point_after_attribute(
              text, at + std::strlen(stage.attribute), &name)) {
        continue;
      }

      // Deduplicate repeated declarations of the same stage+name pair.
      bool dup = false;
      for (const auto& existing : info.entry_points) {
        if (existing.stage == stage.stage && existing.name == name) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        info.entry_points.push_back(
            WgslEntryPoint{stage.stage, std::move(name)});
      }
    }
  }

  if (info.entry_points.empty()) {
    return std::nullopt;
  }
  return info;
}

}  // namespace assetmeta
