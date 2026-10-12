#include <extract-asset-metadata/metadata.h>
#include <extract-asset-metadata/report.h>
#include <extract-asset-metadata/utf8.h>

#include <cstdio>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace assetmeta {
namespace {

// Escapes a string for inclusion in JSON output. Every value echoed into
// the report originates in an untrusted input file, so invalid UTF-8 is
// stripped (plan section 5.6) and '"', '\' and all control characters are
// escaped.
void write_json_string(std::ostream& out, std::string_view value) {
  out << '"';
  const std::string clean = utf8::sanitize(value);
  for (const char c : clean) {
    switch (c) {
      case '"':
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
        break;
      case '\b':
        out << "\\b";
        break;
      case '\f':
        out << "\\f";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        static_cast<unsigned>(static_cast<unsigned char>(c)));
          out << buf;
        } else {
          out << c;
        }
        break;
    }
  }
  out << '"';
}

void write_string_array(std::ostream& out, const std::string& indent,
                        const std::vector<std::string>& items) {
  if (items.empty()) {
    out << "[]";
    return;
  }
  out << "[\n";
  for (std::size_t i = 0; i < items.size(); ++i) {
    out << indent << "  ";
    write_json_string(out, items[i]);
    out << ((i + 1 < items.size()) ? ",\n" : "\n");
  }
  out << indent << "]";
}

void write_actions(std::ostream& out,
                   const std::vector<ActionAdvice>& actions) {
  if (actions.empty()) {
    out << "[]";
    return;
  }
  out << "[\n";
  for (std::size_t i = 0; i < actions.size(); ++i) {
    out << "    {\n      \"action\": ";
    write_json_string(out, actions[i].action);
    out << ",\n      \"notes\": ";
    write_string_array(out, "      ", actions[i].notes);
    out << "\n    }" << ((i + 1 < actions.size()) ? "," : "") << "\n";
  }
  out << "  ]";
}

void write_dependencies(std::ostream& out,
                        const std::vector<Dependency>& deps) {
  if (deps.empty()) {
    out << "[]";
    return;
  }
  out << "[\n";
  for (std::size_t i = 0; i < deps.size(); ++i) {
    out << "    {\n      \"name\": ";
    write_json_string(out, deps[i].name);
    out << ",\n      \"source\": ";
    write_json_string(out, deps[i].source);
    out << ",\n      \"status\": \"" << to_string(deps[i].status)
        << "\",\n      \"truncated\": "
        << (deps[i].truncated ? "true" : "false") << "\n    }"
        << ((i + 1 < deps.size()) ? "," : "") << "\n";
  }
  out << "  ]";
}

void write_image(std::ostream& out, const ImageInfo& image) {
  out << "{\n    \"has_dims\": " << (image.has_dims ? "true" : "false");
  if (image.has_dims) {
    out << ",\n    \"width\": " << image.width
        << ",\n    \"height\": " << image.height
        << ",\n    \"channels\": " << image.channels;
  } else {
    out << ",\n    \"width\": null,\n    \"height\": null,\n    \"channels\": "
           "null";
  }
  if (image.png.has_value()) {
    out << ",\n    \"png\": {\n      \"bit_depth\": " << image.png->bit_depth
        << ",\n      \"color_type\": " << image.png->color_type
        << ",\n      \"interlaced\": "
        << (image.png->interlaced ? "true" : "false")
        << ",\n      \"has_srgb_chunk\": "
        << (image.png->has_srgb_chunk ? "true" : "false")
        << ",\n      \"has_gama_chunk\": "
        << (image.png->has_gama_chunk ? "true" : "false") << "\n    }";
  }
  if (image.jpeg.has_value()) {
    out << ",\n    \"jpeg\": {\n      \"progressive\": "
        << (image.jpeg->progressive ? "true" : "false") << "\n    }";
  }
  if (image.gif.has_value()) {
    out << ",\n    \"gif\": {\n      \"frame_count\": "
        << image.gif->frame_count << ",\n      \"frame_count_complete\": "
        << (image.gif->frame_count_complete ? "true" : "false") << "\n    }";
  }
  if (image.pnm.has_value()) {
    out << ",\n    \"pnm\": {\n      \"variant\": " << image.pnm->variant
        << "\n    }";
  }
  out << "\n  }";
}

void write_model(std::ostream& out, const ModelInfo& model) {
  out << "{\n    \"format\": ";
  write_json_string(out, model.format);
  if (!model.importer_name.empty()) {
    out << ",\n    \"importer_name\": ";
    write_json_string(out, model.importer_name);
  }
  out << ",\n    \"light_scan_limited\": "
      << (model.light_scan_limited ? "true" : "false")
      << ",\n    \"deep_scanned\": " << (model.deep_scanned ? "true" : "false");

  out << ",\n    \"meshes\": ";
  if (model.meshes.empty()) {
    out << "[]";
  } else {
    out << "[\n";
    for (std::size_t i = 0; i < model.meshes.size(); ++i) {
      out << "      {\n        \"name\": ";
      write_json_string(out, model.meshes[i].name);
      out << ",\n        \"vertices\": " << model.meshes[i].vertices
          << ",\n        \"faces\": " << model.meshes[i].faces
          << ",\n        \"has_texcoords\": "
          << (model.meshes[i].has_texcoords ? "true" : "false")
          << ",\n        \"has_bones\": "
          << (model.meshes[i].has_bones ? "true" : "false") << "\n      }"
          << ((i + 1 < model.meshes.size()) ? "," : "") << "\n";
    }
    out << "    ]";
  }

  out << ",\n    \"selection_groups\": ";
  if (model.selection_groups.empty()) {
    out << "[]";
  } else {
    out << "[\n";
    for (std::size_t i = 0; i < model.selection_groups.size(); ++i) {
      out << "      {\n        \"include_texcoords\": "
          << (model.selection_groups[i].include_texcoords ? "true" : "false")
          << ",\n        \"include_bones\": "
          << (model.selection_groups[i].include_bones ? "true" : "false")
          << ",\n        \"mesh_names\": ";
      write_string_array(out, "        ", model.selection_groups[i].mesh_names);
      out << "\n      }" << ((i + 1 < model.selection_groups.size()) ? "," : "")
          << "\n";
    }
    out << "    ]";
  }

  out << ",\n    \"animation_count\": " << model.animation_count;

  out << ",\n    \"materials\": ";
  if (model.materials.empty()) {
    out << "[]";
  } else {
    out << "[\n";
    for (std::size_t i = 0; i < model.materials.size(); ++i) {
      out << "      {\n        \"name\": ";
      write_json_string(out, model.materials[i].name);
      out << ",\n        \"texture_paths\": ";
      write_string_array(out, "        ", model.materials[i].texture_paths);
      out << "\n      }" << ((i + 1 < model.materials.size()) ? "," : "")
          << "\n";
    }
    out << "    ]";
  }
  out << "\n  }";
}

void write_shader(std::ostream& out, const ShaderInfo& shader) {
  out << "{\n    \"entry_points\": ";
  if (shader.entry_points.empty()) {
    out << "[]";
  } else {
    out << "[\n";
    for (std::size_t i = 0; i < shader.entry_points.size(); ++i) {
      out << "      {\n        \"stage\": ";
      write_json_string(out, shader.entry_points[i].stage);
      out << ",\n        \"name\": ";
      write_json_string(out, shader.entry_points[i].name);
      out << "\n      }" << ((i + 1 < shader.entry_points.size()) ? "," : "")
          << "\n";
    }
    out << "    ]";
  }
  out << "\n  }";
}

}  // namespace

void render_json(const AssetMetadata& meta, std::ostream& out) {
  out << "{\n  \"mode\": \"" << (meta.staged_mode ? "staged" : "direct")
      << "\",\n  \"kind\": \"" << to_string(meta.kind)
      << "\",\n  \"category\": \"" << to_string(meta.category) << "\"";
  out << ",\n  \"physical_path\": ";
  write_json_string(out, meta.physical_path.string());
  out << ",\n  \"logical_name\": ";
  write_json_string(out, meta.logical_name);
  out << ",\n  \"file_size\": " << meta.file_size << ",\n  \"sha256\": ";
  write_json_string(out, meta.sha256);

  if (meta.image.has_value()) {
    out << ",\n  \"image\": ";
    write_image(out, *meta.image);
  }
  if (meta.model.has_value()) {
    out << ",\n  \"model\": ";
    write_model(out, *meta.model);
  }
  if (meta.shader.has_value()) {
    out << ",\n  \"shader\": ";
    write_shader(out, *meta.shader);
  }
  if (meta.generated.has_value() && !meta.generated->inspect_with.empty()) {
    out << ",\n  \"generated\": {\n    \"inspect_with\": ";
    write_json_string(out, meta.generated->inspect_with);
    out << "\n  }";
  }

  out << ",\n  \"actions\": ";
  write_actions(out, meta.actions);
  out << ",\n  \"dependencies\": ";
  write_dependencies(out, meta.dependencies);
  out << ",\n  \"notes\": ";
  write_string_array(out, "  ", meta.notes);
  out << ",\n  \"warnings\": ";
  write_string_array(out, "  ", meta.warnings);
  out << "\n}\n";
}

}  // namespace assetmeta
