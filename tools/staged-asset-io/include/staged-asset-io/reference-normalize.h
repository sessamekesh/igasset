#ifndef STAGED_ASSET_IO_REFERENCE_NORMALIZE_H
#define STAGED_ASSET_IO_REFERENCE_NORMALIZE_H

#include <optional>
#include <string>
#include <string_view>

namespace stagedio {

// Returns the logical directory prefix of a logical path:
// "models/scene.gltf" -> "models/"; "scene.gltf" -> "".
std::string_view logical_parent_dir(std::string_view logical_path);

// Normalizes a logical file reference (e.g. a plan input_file_path, an OBJ
// "mtllib" file name, a glTF buffer uri) into canonical logical-path form:
//
// - a "file://" scheme prefix is stripped
// - backslashes are converted to forward slashes
// - percent-escapes ("%20" etc.) are decoded
// - "." segments are dropped; trailing/duplicate separators collapse
// - ".." segments are resolved lexically against base_dir
// - relative references are resolved against base_dir (the logical directory
//   of the referencing file - see logical_parent_dir)
//
// Returns std::nullopt when the reference cannot be resolved within the
// logical namespace: empty references, absolute references (leading "/" or a
// Windows drive prefix), or ".." segments that would escape base_dir.
//
// NOTE: this is a pure string operation - it never touches the filesystem.
std::optional<std::string> normalize_logical_reference(
    std::string_view base_dir, std::string_view reference);

}  // namespace stagedio

#endif