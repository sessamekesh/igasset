#ifndef IGASSET_WGSL_SORUCE_H
#define IGASSET_WGSL_SORUCE_H

#include <igasset/schema/igasset.h>

#include <string>

namespace igasset {

struct WgslSource {
  std::string Wgsl;
  std::string VertexEntryPoint;
  std::string FragmentEntryPoint;
  std::string ComputeEntryPoint;

  static WgslSource Unpack(const IgAsset::WgslSource* flatbuffer_data);
};

}  // namespace igasset

#endif
