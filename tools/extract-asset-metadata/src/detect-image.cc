#include <extract-asset-metadata/detect.h>
#include <extract-asset-metadata/utf8.h>
#include <stb/stb_image.h>

#include <cstring>
#include <string>
#include <string_view>

namespace assetmeta {
namespace {

bool starts_with(const std::string& bytes, const char* sig, std::size_t len) {
  return bytes.size() >= len && std::memcmp(bytes.data(), sig, len) == 0;
}

std::uint32_t be32(const std::string& b, std::size_t off) {
  return (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off]))
          << 24) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1]))
          << 16) |
         (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2]))
          << 8) |
         static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3]));
}

std::uint16_t be16(const std::string& b, std::size_t off) {
  return static_cast<std::uint16_t>(
      (static_cast<std::uint16_t>(static_cast<unsigned char>(b[off])) << 8) |
      static_cast<std::uint16_t>(static_cast<unsigned char>(b[off + 1])));
}

// Header-only stb inspection (never decodes - plan D8). Returns dims via
// out-params; false when the header could not be parsed.
bool stb_header_info(const std::string& bytes, int* w, int* h, int* ch) {
  return stbi_info_from_memory(reinterpret_cast<const stbi_uc*>(bytes.data()),
                               static_cast<int>(bytes.size()), w, h, ch) != 0;
}

ImageInfo base_image_info(const std::string& bytes) {
  ImageInfo info;
  int w = 0, h = 0, ch = 0;
  if (stb_header_info(bytes, &w, &h, &ch)) {
    info.width = w;
    info.height = h;
    info.channels = ch;
    info.has_dims = true;
  }
  return info;
}

// Walks the PNG chunk table for the sRGB / gAMA ancillary chunks. Every
// step is bounds-checked (the header is untrusted); a lying length field
// just stops the walk.
PngDetails scan_png_chunks(const std::string& b) {
  PngDetails details;
  if (b.size() < 33) {
    return details;  // signature + IHDR must be present
  }

  // IHDR: length (4, must be 13), type (4), data (13), crc (4).
  if (be32(b, 8) == 13 && b.compare(12, 4, "IHDR") == 0) {
    details.bit_depth = static_cast<int>(static_cast<unsigned char>(b[24]));
    details.color_type = static_cast<int>(static_cast<unsigned char>(b[25]));
    details.interlaced = b[28] != 0;
  }

  std::size_t p = 33;
  while (p + 8 <= b.size()) {
    const std::uint32_t len = be32(b, p);
    if (len > b.size() - p - 12) {
      break;  // truncated / lying length
    }
    const std::size_t type_off = p + 4;
    if (b.compare(type_off, 4, "sRGB") == 0) {
      details.has_srgb_chunk = true;
    } else if (b.compare(type_off, 4, "gAMA") == 0) {
      details.has_gama_chunk = true;
    } else if (b.compare(type_off, 4, "IEND") == 0) {
      break;
    }
    p += 12 + len;
  }
  return details;
}

// JPEG marker walk: finds the first SOF marker (C0-CF, except C4/C8/CC)
// and reports whether it is SOF2 (progressive).
bool jpeg_scan_progressive(const std::string& b) {
  std::size_t p = 2;  // past FF D8
  while (p + 4 <= b.size()) {
    // Skip fill bytes.
    while (p < b.size() && static_cast<unsigned char>(b[p]) == 0xFF) {
      ++p;
    }
    if (p >= b.size()) {
      break;
    }
    const unsigned char marker = static_cast<unsigned char>(b[p]);
    ++p;
    if (marker == 0xD8 || marker == 0xD9 || marker == 0x01 ||
        (marker >= 0xD0 && marker <= 0xD7)) {
      continue;  // standalone markers, no length field
    }
    if (p + 2 > b.size()) {
      break;
    }
    const std::uint16_t seg_len = be16(b, p);
    if (seg_len < 2) {
      break;
    }
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 &&
        marker != 0xCC) {
      return marker == 0xC2;  // SOF2 = progressive
    }
    p += seg_len;
  }
  return false;
}

// Bounded GIF block walk counting image descriptors (0x2C). complete = the
// trailer (0x3B) was reached; anything malformed leaves it false.
GifDetails scan_gif_frames(const std::string& b) {
  GifDetails details;
  if (b.size() < 14) {
    return details;
  }
  const unsigned char packed = static_cast<unsigned char>(b[10]);
  std::size_t p = 13;
  if (packed & 0x80) {
    p += 3 * (1ull << ((packed & 0x07) + 1));  // global color table
  }

  const auto skip_sub_blocks = [&b, &p]() -> bool {
    while (true) {
      if (p >= b.size()) {
        return false;
      }
      const unsigned char n = static_cast<unsigned char>(b[p]);
      ++p;
      if (n == 0) {
        return true;
      }
      if (p + n > b.size()) {
        return false;
      }
      p += n;
    }
  };

  while (p < b.size()) {
    const unsigned char block = static_cast<unsigned char>(b[p]);
    ++p;
    if (block == 0x3B) {
      details.frame_count_complete = true;
      return details;  // trailer
    }
    if (block == 0x21) {  // extension: label byte then sub-blocks
      if (p >= b.size()) {
        return details;
      }
      ++p;
      if (!skip_sub_blocks()) {
        return details;
      }
    } else if (block == 0x2C) {  // image descriptor
      ++details.frame_count;
      if (p + 9 > b.size()) {
        return details;
      }
      const unsigned char img_packed = static_cast<unsigned char>(b[p + 8]);
      p += 9;
      if (img_packed & 0x80) {
        p += 3 * (1ull << ((img_packed & 0x07) + 1));  // local color table
      }
      if (p >= b.size()) {
        return details;
      }
      ++p;  // LZW minimum code size
      if (!skip_sub_blocks()) {
        return details;
      }
    } else {
      return details;  // unknown block byte
    }
  }
  return details;
}

// Manual PNM header parse covering ALL variants (P1-P6); stb only consumes
// the binary ones (P5/P6). Comments ('#' to end of line) are permitted in
// headers. Channels are determined by the variant.
bool parse_pnm_header(const std::string& b, int* variant, int* w, int* h,
                      int* ch) {
  if (b.size() < 2 || b[0] != 'P' || b[1] < '1' || b[1] > '6') {
    return false;
  }
  *variant = b[1] - '0';
  *ch = (*variant == 3 || *variant == 6) ? 3 : 1;

  std::size_t p = 2;
  int field = 0;  // 0 = width, 1 = height
  int values[2] = {0, 0};
  while (field < 2) {
    // Skip whitespace and comments.
    while (p < b.size()) {
      const char c = b[p];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        ++p;
      } else if (c == '#') {
        while (p < b.size() && b[p] != '\n') {
          ++p;
        }
      } else {
        break;
      }
    }
    if (p >= b.size() || b[p] < '0' || b[p] > '9') {
      return false;
    }
    int value = 0;
    while (p < b.size() && b[p] >= '0' && b[p] <= '9') {
      value = value * 10 + (b[p] - '0');
      if (value > 0x40000000) {
        return false;  // absurd dimension
      }
      ++p;
    }
    values[field] = value;
    ++field;
  }
  *w = values[0];
  *h = values[1];
  return *w > 0 && *h > 0;
}

bool is_tga_footer(const std::string& b) {
  const std::string_view sig = "TRUEVISION-XFILE.";
  if (b.size() < 18) {
    return false;
  }
  return std::memcmp(b.data() + b.size() - 18, sig.data(), 17) == 0 &&
         b[b.size() - 1] == '\0';
}

}  // namespace

std::optional<ImageInfo> detect_image(const std::string& bytes, Kind* kind) {
  *kind = Kind::Unknown;

  // Content-first detection by magic bytes (plan section 5.3); extensions
  // are meaningless for UUID-named staged files.
  if (starts_with(bytes, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8)) {
    *kind = Kind::Png;
    ImageInfo info = base_image_info(bytes);
    info.png = scan_png_chunks(bytes);
    return info;
  }
  if (starts_with(bytes, "\xFF\xD8\xFF", 3)) {
    *kind = Kind::Jpeg;
    ImageInfo info = base_image_info(bytes);
    info.jpeg = JpegDetails{jpeg_scan_progressive(bytes)};
    return info;
  }
  if (starts_with(bytes, "GIF87a", 6) || starts_with(bytes, "GIF89a", 6)) {
    *kind = Kind::Gif;
    ImageInfo info = base_image_info(bytes);
    info.gif = scan_gif_frames(bytes);
    return info;
  }
  if (starts_with(bytes, "BM", 2)) {
    *kind = Kind::Bmp;
    return base_image_info(bytes);
  }
  if (starts_with(bytes, "8BPS", 4)) {
    *kind = Kind::Psd;
    return base_image_info(bytes);
  }
  if (starts_with(bytes, "#?RADIANCE\n", 11) ||
      starts_with(bytes, "#?RGBE\n", 7)) {
    *kind = Kind::Hdr;
    return base_image_info(bytes);
  }
  if (bytes.size() >= 4 && static_cast<unsigned char>(bytes[0]) == 0x53 &&
      static_cast<unsigned char>(bytes[1]) == 0x80 &&
      static_cast<unsigned char>(bytes[2]) == 0xF6 &&
      static_cast<unsigned char>(bytes[3]) == 0x34) {
    *kind = Kind::Pic;
    return base_image_info(bytes);
  }

  // PNM: "P1".."P6" followed by whitespace. All variants are detected
  // (stb only consumes P5/P6); the header is parsed manually so ASCII
  // variants report dimensions too.
  if (bytes.size() >= 3 && bytes[0] == 'P' && bytes[1] >= '1' &&
      bytes[1] <= '6') {
    const char c2 = bytes[2];
    if (c2 == ' ' || c2 == '\t' || c2 == '\r' || c2 == '\n') {
      *kind = Kind::Pnm;
      ImageInfo info;
      int variant = 0, w = 0, h = 0, ch = 0;
      if (parse_pnm_header(bytes, &variant, &w, &h, &ch)) {
        info.width = w;
        info.height = h;
        info.channels = ch;
        info.has_dims = true;
        info.pnm = PnmDetails{variant};
      } else {
        info.pnm = PnmDetails{bytes[1] - '0'};
      }
      return info;
    }
  }

  // TGA has no leading magic: accept the TRUEVISION-XFILE footer, then
  // fall back to stb content heuristics (every other stb format has a
  // magic checked above, so a stb header parse at this point is TGA).
  if (is_tga_footer(bytes)) {
    *kind = Kind::Tga;
    return base_image_info(bytes);
  }
  {
    int w = 0, h = 0, ch = 0;
    if (stb_header_info(bytes, &w, &h, &ch)) {
      *kind = Kind::Tga;
      ImageInfo info;
      info.width = w;
      info.height = h;
      info.channels = ch;
      info.has_dims = true;
      return info;
    }
  }

  return std::nullopt;
}

}  // namespace assetmeta
