// Small UTF-8 helpers shared by the metadata scanners and both report
// printers. Every string echoed into a report comes from an untrusted
// input file, so it must be sanitized (invalid UTF-8 stripped - section
// 5.6 of the pipeline plan) and dependency names are truncated to 256
// Unicode code points on UTF-8 boundaries (D7).

#ifndef EXTRACT_ASSET_METADATA_UTF8_H
#define EXTRACT_ASSET_METADATA_UTF8_H

#include <cstddef>
#include <string>
#include <string_view>

namespace assetmeta {
namespace utf8 {

// Returns the number of bytes in the sequence starting with lead byte b
// (0 = invalid lead byte).
inline std::size_t sequence_length(unsigned char b) {
  if (b < 0x80)
    return 1;
  if (b >= 0xC2 && b <= 0xDF)
    return 2;
  if (b >= 0xE0 && b <= 0xEF)
    return 3;
  if (b >= 0xF0 && b <= 0xF4)
    return 4;
  return 0;
}

inline bool is_continuation(unsigned char b) {
  return (b & 0xC0) == 0x80;
}

// True when the whole string is valid UTF-8.
inline bool is_valid(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t len = sequence_length(static_cast<unsigned char>(s[i]));
    if (len == 0 || i + len > s.size()) {
      return false;
    }
    for (std::size_t j = 1; j < len; ++j) {
      if (!is_continuation(static_cast<unsigned char>(s[i + j]))) {
        return false;
      }
    }
    // Overlong / surrogate / out-of-range lead byte checks.
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    const unsigned char b1 =
        (len >= 2) ? static_cast<unsigned char>(s[i + 1]) : 0;
    if ((b0 == 0xE0 && b1 < 0xA0) || (b0 == 0xED && b1 > 0x9F) ||
        (b0 == 0xF0 && b1 < 0x90) || (b0 == 0xF4 && b1 > 0x8F)) {
      return false;
    }
    i += len;
  }
  return true;
}

// Number of Unicode code points. Invalid bytes count as one code point
// each (they are stripped or replaced by the callers).
inline std::size_t codepoint_length(std::string_view s) {
  std::size_t count = 0;
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t len = sequence_length(static_cast<unsigned char>(s[i]));
    if (len == 0 || i + len > s.size()) {
      ++count;
      ++i;
      continue;
    }
    ++count;
    i += len;
  }
  return count;
}

// Copies only valid UTF-8 sequences, dropping invalid bytes entirely.
inline std::string sanitize(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  std::size_t i = 0;
  while (i < s.size()) {
    const std::size_t len = sequence_length(static_cast<unsigned char>(s[i]));
    bool valid = len != 0 && i + len <= s.size();
    if (valid) {
      for (std::size_t j = 1; j < len; ++j) {
        if (!is_continuation(static_cast<unsigned char>(s[i + j]))) {
          valid = false;
          break;
        }
      }
      const unsigned char b0 = static_cast<unsigned char>(s[i]);
      const unsigned char b1 =
          (len >= 2) ? static_cast<unsigned char>(s[i + 1]) : 0;
      if ((b0 == 0xE0 && b1 < 0xA0) || (b0 == 0xED && b1 > 0x9F) ||
          (b0 == 0xF0 && b1 < 0x90) || (b0 == 0xF4 && b1 > 0x8F)) {
        valid = false;
      }
    }
    if (valid) {
      out.append(s.substr(i, len));
      i += len;
    } else {
      ++i;
    }
  }
  return out;
}

// Truncates to at most max_code_points, cutting only on UTF-8 sequence
// boundaries. Sets *truncated (when non-null) if the input was actually
// cut short. Invalid bytes count as one code point each.
inline std::string truncate(std::string_view s, std::size_t max_code_points,
                            bool* truncated) {
  if (truncated != nullptr) {
    *truncated = false;
  }
  if (codepoint_length(s) <= max_code_points) {
    return std::string(s);
  }

  std::string out;
  out.reserve(max_code_points * 2);
  std::size_t count = 0;
  std::size_t i = 0;
  while (i < s.size() && count < max_code_points) {
    const std::size_t len = sequence_length(static_cast<unsigned char>(s[i]));
    const std::size_t step = (len == 0 || i + len > s.size()) ? 1 : len;
    out.append(s.substr(i, step));
    ++count;
    i += step;
  }
  if (truncated != nullptr) {
    *truncated = true;
  }
  return out;
}

// Plausibility gate for the text heuristics (plan section 5.3): no NUL in
// the first ~4 KiB and valid UTF-8 over the same prefix. The final check
// is relaxed only for a sequence cut off BY the 4 KiB boundary itself
// (a valid text file can straddle it); a file that genuinely ends mid
// sequence is not plausible text.
inline bool plausible_text(std::string_view s) {
  const std::size_t n = s.size() < 4096 ? s.size() : 4096;
  for (std::size_t i = 0; i < n; ++i) {
    if (s[i] == '\0') {
      return false;
    }
  }

  std::size_t i = 0;
  while (i < n) {
    const std::size_t len = sequence_length(static_cast<unsigned char>(s[i]));
    if (len == 0) {
      return false;
    }
    if (i + len > n) {
      // Incomplete sequence: only acceptable as an artifact of the 4 KiB
      // cut in a longer file.
      return n < s.size();
    }
    for (std::size_t j = 1; j < len; ++j) {
      if (!is_continuation(static_cast<unsigned char>(s[i + j]))) {
        return false;
      }
    }
    const unsigned char b0 = static_cast<unsigned char>(s[i]);
    const unsigned char b1 =
        (len >= 2) ? static_cast<unsigned char>(s[i + 1]) : 0;
    if ((b0 == 0xE0 && b1 < 0xA0) || (b0 == 0xED && b1 > 0x9F) ||
        (b0 == 0xF0 && b1 < 0x90) || (b0 == 0xF4 && b1 > 0x8F)) {
      return false;
    }
    i += len;
  }
  return true;
}

}  // namespace utf8
}  // namespace assetmeta

#endif