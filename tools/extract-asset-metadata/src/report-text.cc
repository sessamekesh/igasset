#include <extract-asset-metadata/metadata.h>
#include <extract-asset-metadata/report.h>
#include <extract-asset-metadata/utf8.h>

#include <ostream>
#include <string>

namespace assetmeta {

const char* to_string(Kind kind) {
  switch (kind) {
    case Kind::Png:
      return "png";
    case Kind::Jpeg:
      return "jpeg";
    case Kind::Tga:
      return "tga";
    case Kind::Bmp:
      return "bmp";
    case Kind::Psd:
      return "psd";
    case Kind::Gif:
      return "gif";
    case Kind::Hdr:
      return "hdr";
    case Kind::Pic:
      return "pic";
    case Kind::Pnm:
      return "pnm";
    case Kind::Fbx:
      return "fbx";
    case Kind::Gltf:
      return "gltf";
    case Kind::Glb:
      return "glb";
    case Kind::Obj:
      return "obj";
    case Kind::Mtl:
      return "mtl";
    case Kind::Wgsl:
      return "wgsl";
    case Kind::Igasset:
      return "igasset";
    case Kind::Igpack:
      return "igpack";
    case Kind::Unknown:
      return "unknown";
  }
  return "unknown";
}

const char* to_string(Category category) {
  switch (category) {
    case Category::Image:
      return "image";
    case Category::Model:
      return "model";
    case Category::Shader:
      return "shader";
    case Category::Generated:
      return "generated";
    case Category::Unknown:
      return "unknown";
  }
  return "unknown";
}

const char* to_string(DepStatus status) {
  switch (status) {
    case DepStatus::InManifest:
      return "in-manifest";
    case DepStatus::NotInManifest:
      return "not-in-manifest";
    case DepStatus::Resolved:
      return "resolved";
    case DepStatus::Unmapped:
      return "unmapped";
    case DepStatus::Missing:
      return "missing";
    case DepStatus::Embedded:
      return "embedded";
  }
  return "missing";
}

namespace {

void write_line(std::ostream& out, const std::string& label,
                const std::string& value) {
  out << label << ": " << utf8::sanitize(value) << "\n";
}

void render_image(const ImageInfo& image, std::ostream& out) {
  if (image.has_dims) {
    out << "  Width: " << image.width << "\n";
    out << "  Height: " << image.height << "\n";
    out << "  Channels: " << image.channels << "\n";
  } else {
    out << "  Dimensions: unknown (header could not be parsed)\n";
  }
  if (image.png.has_value()) {
    out << "  PNG:\n";
    out << "    Bit depth: " << image.png->bit_depth << "\n";
    out << "    Color type: " << image.png->color_type << "\n";
    out << "    Interlaced: " << (image.png->interlaced ? "yes" : "no") << "\n";
    out << "    sRGB chunk: " << (image.png->has_srgb_chunk ? "yes" : "no")
        << "\n";
    out << "    gAMA chunk: " << (image.png->has_gama_chunk ? "yes" : "no")
        << "\n";
  }
  if (image.jpeg.has_value()) {
    out << "  Progressive JPEG: " << (image.jpeg->progressive ? "yes" : "no")
        << "\n";
  }
  if (image.gif.has_value()) {
    out << "  GIF frames: " << image.gif->frame_count
        << (image.gif->frame_count_complete ? "" : " (walk incomplete)")
        << "\n";
  }
  if (image.pnm.has_value()) {
    out << "  PNM variant: P" << image.pnm->variant << "\n";
  }
}

void render_model(const ModelInfo& model, std::ostream& out) {
  write_line(out, "  Format", model.format);
  if (!model.importer_name.empty()) {
    write_line(out, "  Importer", model.importer_name);
  }
  out << "  Light scan limited: " << (model.light_scan_limited ? "yes" : "no")
      << "\n";
  out << "  Deep scanned: " << (model.deep_scanned ? "yes" : "no") << "\n";
  if (!model.meshes.empty()) {
    out << "  Meshes:\n";
    for (const auto& mesh : model.meshes) {
      out << "    - " << utf8::sanitize(mesh.name) << " (" << mesh.vertices
          << " vertices, " << mesh.faces
          << " faces, texcoords: " << (mesh.has_texcoords ? "yes" : "no")
          << ", bones: " << (mesh.has_bones ? "yes" : "no") << ")\n";
    }
  }
  if (!model.selection_groups.empty()) {
    out << "  Mesh selection groups (texcoords/bones must be uniform):\n";
    for (const auto& group : model.selection_groups) {
      out << "    - include_texcoords="
          << (group.include_texcoords ? "yes" : "no")
          << ", include_bones=" << (group.include_bones ? "yes" : "no") << ": ";
      for (std::size_t i = 0; i < group.mesh_names.size(); ++i) {
        out << (i == 0 ? "" : ", ") << utf8::sanitize(group.mesh_names[i]);
      }
      out << "\n";
    }
  }
  out << "  Animations: " << model.animation_count << "\n";
  if (!model.materials.empty()) {
    out << "  Materials:\n";
    for (const auto& material : model.materials) {
      out << "    - " << utf8::sanitize(material.name);
      if (!material.texture_paths.empty()) {
        out << " (textures: ";
        for (std::size_t i = 0; i < material.texture_paths.size(); ++i) {
          out << (i == 0 ? "" : ", ")
              << utf8::sanitize(material.texture_paths[i]);
        }
        out << ")";
      }
      out << "\n";
    }
  }
}

void render_shader(const ShaderInfo& shader, std::ostream& out) {
  out << "  Entry points:\n";
  for (const auto& entry_point : shader.entry_points) {
    out << "    - " << entry_point.stage << ": "
        << utf8::sanitize(entry_point.name) << "\n";
  }
}

void render_string_list(std::ostream& out, const char* bullet,
                        const std::vector<std::string>& items) {
  for (const auto& item : items) {
    out << bullet << utf8::sanitize(item) << "\n";
  }
}

}  // namespace

void render_text(const AssetMetadata& meta, std::ostream& out) {
  out << "------- Asset Metadata -------\n";
  write_line(out, "File", meta.physical_path.string());
  write_line(out, "Logical name", meta.logical_name);
  write_line(out, "Mode", meta.staged_mode ? "staged" : "direct");
  write_line(out, "Kind", to_string(meta.kind));
  write_line(out, "Category", to_string(meta.category));
  out << "File size: " << meta.file_size << " bytes\n";
  write_line(out, "SHA256", meta.sha256);

  if (meta.image.has_value()) {
    out << "\nImage:\n";
    render_image(*meta.image, out);
  }
  if (meta.model.has_value()) {
    out << "\nModel:\n";
    render_model(*meta.model, out);
  }
  if (meta.shader.has_value()) {
    out << "\nShader:\n";
    render_shader(*meta.shader, out);
  }
  if (meta.generated.has_value() && !meta.generated->inspect_with.empty()) {
    out << "\nGenerated:\n";
    write_line(out, "  Inspect with", meta.generated->inspect_with);
  }

  if (!meta.actions.empty()) {
    out << "\nActions:\n";
    for (const auto& action : meta.actions) {
      out << "  - " << action.action << "\n";
      render_string_list(out, "      ", action.notes);
    }
  }

  if (!meta.dependencies.empty()) {
    out << "\nDependencies:\n";
    for (const auto& dep : meta.dependencies) {
      out << "  - " << utf8::sanitize(dep.name) << " [" << to_string(dep.status)
          << "]" << (dep.truncated ? " [truncated]" : "")
          << " (source: " << dep.source << ")\n";
    }
  }

  if (!meta.notes.empty()) {
    out << "\nNotes:\n";
    render_string_list(out, "  - ", meta.notes);
  }

  if (!meta.warnings.empty()) {
    out << "\nWarnings:\n";
    render_string_list(out, "  - ", meta.warnings);
  }
}

}  // namespace assetmeta
