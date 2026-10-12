#include <extract-asset-metadata/detect.h>
#include <extract-asset-metadata/utf8.h>

#include <assimp/BaseImporter.h>
#include <assimp/MemoryIOWrapper.h>
#include <assimp/Importer.hpp>

#include <cctype>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace assetmeta {
namespace {

bool starts_with(const std::string& bytes, const char* sig, std::size_t len) {
  return bytes.size() >= len && std::memcmp(bytes.data(), sig, len) == 0;
}

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

std::string to_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

// ---------------------------------------------------------------------------
// glTF "uri" scanner (used for both text .gltf files and the JSON chunk of
// a GLB). Scans the raw JSON text for "uri" string values; this is a
// heuristic (no JSON parser in-repo by design) but "uri" is not a key used
// anywhere else in glTF 1/2 documents.
// ---------------------------------------------------------------------------

std::vector<RawDependency> scan_gltf_uris(std::string_view json) {
  std::vector<RawDependency> deps;
  std::size_t p = 0;
  while (true) {
    const std::size_t key = json.find("\"uri\"", p);
    if (key == std::string_view::npos) {
      break;
    }
    p = key + 5;

    std::size_t q = p;
    while (q < json.size() && (json[q] == ' ' || json[q] == '\t')) {
      ++q;
    }
    if (q >= json.size() || json[q] != ':') {
      continue;  // not a key/value after all
    }
    ++q;
    while (q < json.size() && (json[q] == ' ' || json[q] == '\t' ||
                               json[q] == '\r' || json[q] == '\n')) {
      ++q;
    }
    if (q >= json.size() || json[q] != '"') {
      continue;  // uri value is not a string (or malformed JSON)
    }
    ++q;

    std::string value;
    bool closed = false;
    while (q < json.size()) {
      const char c = json[q];
      if (c == '\\') {
        if (q + 1 >= json.size()) {
          break;
        }
        const char esc = json[q + 1];
        if (esc == '"' || esc == '\\' || esc == '/') {
          value.push_back(esc);
        } else if (esc == 'n') {
          value.push_back('\n');
        } else if (esc == 'r') {
          value.push_back('\r');
        } else if (esc == 't') {
          value.push_back('\t');
        } else {
          // \uXXXX and friends: keep raw (unusable as a file name anyway).
          value.push_back('\\');
          value.push_back(esc);
        }
        q += 2;
        continue;
      }
      if (c == '"') {
        closed = true;
        ++q;
        break;
      }
      value.push_back(c);
      ++q;
    }
    p = q;
    if (!closed) {
      break;
    }

    RawDependency dep;
    dep.source = "gltf:uri";
    if (value.rfind("data:", 0) == 0) {
      dep.reference = std::move(value);
      dep.embedded = true;
    } else {
      dep.reference = std::move(value);
    }
    deps.push_back(std::move(dep));
  }
  return deps;
}

// Extracts the JSON chunk of a GLB container and scans it for external
// uris (plan section 5.5: GLB is usually self-contained, but any external
// reference must be surfaced). Returns false when the container is too
// malformed to yield a JSON chunk.
//
// GLB 1.0 and 2.0 both place the JSON at offset 20 but frame it
// differently: v1 uses a 20-byte header (contentLength + contentFormat),
// v2 a 12-byte header followed by a chunk header (length + "JSON" type).
bool scan_glb_json_chunk(const std::string& bytes,
                         std::vector<RawDependency>* deps) {
  if (bytes.size() < 20) {
    return false;
  }
  const auto le32 = [&bytes](std::size_t off) -> std::uint32_t {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[off])) |
           (static_cast<std::uint32_t>(
                static_cast<unsigned char>(bytes[off + 1]))
            << 8) |
           (static_cast<std::uint32_t>(
                static_cast<unsigned char>(bytes[off + 2]))
            << 16) |
           (static_cast<std::uint32_t>(
                static_cast<unsigned char>(bytes[off + 3]))
            << 24);
  };

  const std::uint32_t version = le32(4);
  std::uint32_t json_len = 0;
  if (version == 1) {
    json_len = le32(12);  // contentLength; contentFormat (16) == 0 means JSON
  } else if (version == 2) {
    if (bytes.compare(16, 4, "JSON") != 0) {
      return false;
    }
    json_len = le32(12);
  } else {
    return false;  // unknown container version
  }
  if (json_len == 0 || json_len > bytes.size() - 20) {
    return false;
  }

  std::string_view json(bytes.data() + 20, json_len);
  // The JSON chunk is padded to 4-byte alignment with trailing spaces.
  while (!json.empty() && (json.back() == ' ' || json.back() == '\0')) {
    json.remove_suffix(1);
  }
  *deps = scan_gltf_uris(json);
  return true;
}

// ---------------------------------------------------------------------------
// Text heuristics (OBJ, MTL, glTF JSON, ASCII FBX). Only run for content
// that passes the plausibility gate; scans are line-oriented over the WHOLE
// file (an OBJ's mtllib can appear anywhere).
// ---------------------------------------------------------------------------

struct TextScan {
  bool has_v = false;
  bool has_f = false;
  bool has_usemtl = false;
  bool has_mtllib = false;
  bool has_newmtl = false;
  std::vector<RawDependency> deps;
};

std::vector<std::string_view> split_tokens(std::string_view line) {
  std::vector<std::string_view> tokens;
  std::size_t p = 0;
  while (p < line.size()) {
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) {
      ++p;
    }
    if (p >= line.size()) {
      break;
    }
    const std::size_t start = p;
    while (p < line.size() && line[p] != ' ' && line[p] != '\t') {
      ++p;
    }
    tokens.push_back(line.substr(start, p - start));
  }
  return tokens;
}

// Scans OBJ / MTL lines: first token per line decides the interpretation
// (a JSON document's lines can never start with these bare keywords, so
// the scan is robust against glTF text false positives).
TextScan scan_obj_mtl_text(std::string_view text) {
  TextScan out;

  std::size_t pos = 0;
  bool first_line = true;
  while (pos <= text.size()) {
    const std::size_t eol = text.find('\n', pos);
    std::string_view line = (eol == std::string_view::npos)
                                ? text.substr(pos)
                                : text.substr(pos, eol - pos);
    pos = (eol == std::string_view::npos) ? text.size() + 1 : eol + 1;

    if (first_line) {
      first_line = false;
      if (line.size() >= 3 && line.substr(0, 3) == "\xEF\xBB\xBF") {
        line.remove_prefix(3);  // UTF-8 BOM
      }
    }
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }

    // Leading whitespace tolerance.
    std::size_t ws = 0;
    while (ws < line.size() && (line[ws] == ' ' || line[ws] == '\t')) {
      ++ws;
    }
    if (ws >= line.size()) {
      continue;
    }
    const std::string_view rest = line.substr(ws);

    char first = rest[0];
    if (first == '#') {
      continue;  // comment
    }

    const auto tokens = split_tokens(rest);
    if (tokens.empty()) {
      continue;
    }
    const std::string_view keyword = tokens[0];

    if (keyword == "v") {
      out.has_v = true;
    } else if (keyword == "f") {
      out.has_f = true;
    } else if (keyword == "usemtl") {
      out.has_usemtl = true;
    } else if (keyword == "mtllib") {
      out.has_mtllib = true;
      // Every token after the keyword is a material library reference
      // (multiple per line, multiple lines).
      for (std::size_t i = 1; i < tokens.size(); ++i) {
        RawDependency dep;
        dep.reference = std::string(tokens[i]);
        dep.source = "obj:mtllib";
        out.deps.push_back(std::move(dep));
      }
    } else if (keyword == "newmtl") {
      out.has_newmtl = true;
    } else if (keyword == "map_Kd" || keyword == "map_Ka" ||
               keyword == "map_Ks" || keyword == "map_d" ||
               keyword == "map_bump" || keyword == "bump") {
      // map_* [-options [values]...] filename - the file name is the
      // last token that is not a dash-option.
      for (std::size_t i = tokens.size(); i > 1; --i) {
        const std::string_view tok = tokens[i - 1];
        if (!tok.empty() && tok[0] != '-') {
          RawDependency dep;
          dep.reference = std::string(tok);
          dep.source = "mtl:map";
          out.deps.push_back(std::move(dep));
          break;
        }
      }
    }
  }
  return out;
}

// glTF text detection: JSON that starts (after optional BOM/whitespace)
// with '{' and contains the required "asset" key plus a "scene(s)" key.
bool looks_like_gltf_json(std::string_view text) {
  std::size_t p = 0;
  if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") {
    p = 3;  // UTF-8 BOM
  }
  while (p < text.size() && (text[p] == ' ' || text[p] == '\t' ||
                             text[p] == '\r' || text[p] == '\n')) {
    ++p;
  }
  if (p >= text.size() || text[p] != '{') {
    return false;
  }
  return contains(text, "\"asset\"") &&
         (contains(text, "\"scenes\"") || contains(text, "\"scene\""));
}

// ASCII FBX detection: the standard comment header ("; FBX ...") or the
// FBXHeaderExtension section near the top of the file.
bool looks_like_ascii_fbx(std::string_view text) {
  std::string_view head =
      text.substr(0, text.size() < 1024 ? text.size() : 1024);
  std::size_t p = 0;
  while (p < head.size() && (head[p] == ' ' || head[p] == '\t' ||
                             head[p] == '\r' || head[p] == '\n')) {
    ++p;
  }
  if (head.substr(p, 5) == "; FBX") {
    return true;
  }
  return contains(head, "FBXHeaderExtension");
}

std::optional<ModelScan> detect_model_text(const std::string& bytes) {
  if (!utf8::plausible_text(bytes)) {
    return std::nullopt;
  }
  const std::string_view text(bytes);

  // OBJ (plan section 5.3 order: OBJ, MTL, glTF, FBX-ASCII; the WGSL scan
  // runs after all of these because it is the most generic heuristic).
  const TextScan scan = scan_obj_mtl_text(text);
  if (scan.has_newmtl) {
    ModelScan out;
    out.kind = Kind::Mtl;
    out.format = "mtl";
    out.dependencies = std::move(scan.deps);
    return out;
  }
  if (scan.has_v && (scan.has_f || scan.has_usemtl || scan.has_mtllib)) {
    ModelScan out;
    out.kind = Kind::Obj;
    out.format = "obj";
    out.dependencies = std::move(scan.deps);
    return out;
  }
  if (looks_like_gltf_json(text)) {
    ModelScan out;
    out.kind = Kind::Gltf;
    out.format = "gltf";
    out.dependencies = scan_gltf_uris(text);
    return out;
  }
  if (looks_like_ascii_fbx(text)) {
    ModelScan out;
    out.kind = Kind::Fbx;
    out.format = "fbx";
    // FBX dependency extraction is --deep only (plan section 5.5): the
    // light scan of either FBX flavor cannot see references reliably.
    out.light_scan_limited = true;
    return out;
  }
  return std::nullopt;
}

// Binary FBX files carry the Kaydara magic; the light scan cannot extract
// anything from the compressed node graph (deep is authoritative).
bool is_binary_fbx(const std::string& bytes) {
  return starts_with(bytes, "Kaydara FBX Binary", 17);
}

std::optional<Kind> kind_for_extensions(const std::set<std::string>& exts) {
  if (exts.count("fbx") != 0) {
    return Kind::Fbx;
  }
  if (exts.count("obj") != 0) {
    return Kind::Obj;
  }
  if (exts.count("gltf") != 0 || exts.count("glb") != 0) {
    return Kind::Gltf;
  }
  return std::nullopt;
}

// The FBX importer's CanRead only requires the substring "fbx" in the
// first 200 bytes - far too weak to act on (a README mentioning fbx would
// be claimed). Only trust an FBX sniff when a deterministic FBX marker is
// present.
bool has_fbx_marker(const std::string& bytes) {
  const std::string head = to_lower(std::string_view(bytes).substr(
      0, bytes.size() < 256 ? bytes.size() : 256));
  return contains(head, "kaydara fbx binary") ||
         contains(head, "fbxheaderextension");
}

}  // namespace

std::optional<ModelScan> detect_model(const std::string& bytes) {
  if (is_binary_fbx(bytes)) {
    ModelScan out;
    out.kind = Kind::Fbx;
    out.format = "fbx";
    out.light_scan_limited = true;
    return out;
  }

  if (starts_with(bytes, "glTF", 4)) {
    ModelScan out;
    out.kind = Kind::Glb;
    out.format = "glb";
    // A GLB usually embeds everything; surface any external uri the JSON
    // chunk references. A missing/unparseable chunk only limits the light
    // scan (deep remains authoritative).
    out.light_scan_limited = !scan_glb_json_chunk(bytes, &out.dependencies);
    return out;
  }

  return detect_model_text(bytes);
}

std::optional<ModelScan> sniff_assimp(const std::string& bytes) {
  if (bytes.empty()) {
    return std::nullopt;
  }

  // Content-sniff exactly the way ReadFile auto-detection does for a file
  // without a known extension: iterate the compiled importers (only 3 in
  // this build) and take the first CanRead claim, reading the bytes from
  // memory (never disk). The kind is derived from the claimed importer's
  // extension list - never from string-matching its display name.
  Assimp::Importer importer;
  Assimp::MemoryIOSystem memio(
      reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(),
      nullptr);

  for (std::size_t i = 0; i < importer.GetImporterCount(); ++i) {
    auto* imp = importer.GetImporter(i);
    if (imp == nullptr) {
      continue;
    }
    if (!imp->CanRead(AI_MEMORYIO_MAGIC_FILENAME, &memio, true)) {
      continue;
    }

    std::set<std::string> exts;
    imp->GetExtensionList(exts);
    const auto kind = kind_for_extensions(exts);
    if (!kind.has_value()) {
      continue;
    }
    if (*kind == Kind::Fbx && !has_fbx_marker(bytes)) {
      continue;  // weak "fbx" token claim; see has_fbx_marker
    }

    ModelScan out;
    out.kind = *kind;
    out.format = (*kind == Kind::Fbx)   ? "fbx"
                 : (*kind == Kind::Obj) ? "obj"
                                        : "gltf";
    out.light_scan_limited = true;  // the sniff cannot extract anything
    const auto* info = imp->GetInfo();
    if (info != nullptr && info->mName != nullptr) {
      out.importer_name = info->mName;
    }
    return out;
  }
  return std::nullopt;
}

}  // namespace assetmeta
