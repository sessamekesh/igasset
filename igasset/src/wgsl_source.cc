#include <igasset/fb_utils.h>
#include <igasset/schema/igasset.h>
#include <igasset/wgsl_source.h>

namespace igasset {

WgslSource WgslSource::Unpack(const IgAsset::WgslSource* flatbuffer_data) {
  if (flatbuffer_data == nullptr) {
    return WgslSource{};
  }

  return WgslSource{
      .Wgsl = util::extract_string(flatbuffer_data->source()),
      .VertexEntryPoint =
          util::extract_string(flatbuffer_data->vertex_entry_point()),
      .FragmentEntryPoint =
          util::extract_string(flatbuffer_data->fragment_entry_point()),
      .ComputeEntryPoint =
          util::extract_string(flatbuffer_data->compute_entry_point()),
  };
}

}  // namespace igasset
