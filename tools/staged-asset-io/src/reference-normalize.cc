#include <staged-asset-io/reference-normalize.h>

#include <vector>

namespace stagedio {

namespace {

bool is_hex_digit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}

int hex_digit_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  return c - 'A' + 10;
}

bool has_windows_drive_prefix(std::string_view path) {
  if (path.size() < 2) {
    return false;
  }
  const char c0 = path[0];
  if (!((c0 >= 'a' && c0 <= 'z') || (c0 >= 'A' && c0 <= 'Z'))) {
    return false;
  }
  return path[1] == ':' || path[1] == '|';
}

}  // namespace

std::string_view logical_parent_dir(std::string_view logical_path) {
  const std::size_t last_sep = logical_path.find_last_of('/');
  if (last_sep == std::string_view::npos) {
    return {};
  }
  return logical_path.substr(0, last_sep + 1);
}

std::optional<std::string> normalize_logical_reference(
    std::string_view base_dir, std::string_view reference) {
  std::string_view ref = reference;
  constexpr std::string_view kFileScheme = "file://";
  if (ref.substr(0, kFileScheme.size()) == kFileScheme) {
    ref.remove_prefix(kFileScheme.size());
  }

  if (ref.empty()) {
    return std::nullopt;
  }

  // Percent-decode into a forward-slash-separated path. Decoding can also
  // produce separators / drive letters, so absolute-path checks happen again
  // on the decoded result.
  std::string decoded;
  decoded.reserve(ref.size());
  for (std::size_t i = 0; i < ref.size(); ++i) {
    if (ref[i] == '%' && i + 2 < ref.size() && is_hex_digit(ref[i + 1]) &&
        is_hex_digit(ref[i + 2])) {
      decoded.push_back(static_cast<char>((hex_digit_value(ref[i + 1]) << 4) |
                                          hex_digit_value(ref[i + 2])));
      i += 2;
    } else if (ref[i] == '\\') {
      decoded.push_back('/');
    } else {
      decoded.push_back(ref[i]);
    }
  }

  if (decoded.empty() || decoded.front() == '/' ||
      has_windows_drive_prefix(decoded)) {
    return std::nullopt;
  }

  // Lexically resolve base_dir followed by the reference, segment by
  // segment. ".." pops a segment; popping past the start of base_dir is a
  // containment violation.
  std::vector<std::string_view> segments;

  auto resolve_segment = [&segments](std::string_view seg) -> bool {
    if (seg.empty() || seg == ".") {
      return true;
    }
    if (seg == "..") {
      if (segments.empty()) {
        return false;
      }
      segments.pop_back();
      return true;
    }
    segments.push_back(seg);
    return true;
  };

  const std::string_view parts[2] = {base_dir, std::string_view(decoded)};
  for (const auto& part : parts) {
    std::size_t start = 0;
    while (start < part.size()) {
      const std::size_t sep = part.find('/', start);
      const std::size_t stop =
          (sep == std::string_view::npos) ? part.size() : sep;
      if (!resolve_segment(part.substr(start, stop - start))) {
        return std::nullopt;
      }
      start = stop + 1;
    }
  }

  if (segments.empty()) {
    return std::nullopt;
  }

  std::string out;
  for (std::size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) {
      out.push_back('/');
    }
    out.append(segments[i]);
  }
  return out;
}

}  // namespace stagedio