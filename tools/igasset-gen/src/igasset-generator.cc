#include <igasset-gen/assimp-geo-processor.h>
#include <igasset-gen/basisu-processor.h>
#include <igasset-gen/exec-config.h>
#include <igasset-gen/igasset-generator.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasset-gen/wgsl-copy-processor.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/logger.h>
#include <tool-utils/filesystem.h>

#include <memory>

namespace igassetgen {

WgslCopyProcessor& IgassetGenerator::wgsl_copy_processor() const {
  if (!wgsl_copy_processor_.has_value()) {
    wgsl_copy_processor_.emplace(WgslCopyProcessor(
        log_, config_, io_task_list_, exec_task_list_, filesystem_));
  }

  return *wgsl_copy_processor_;
}

AssimpGeoProcessor& IgassetGenerator::assimp_geo_processor() const {
  if (!assimp_geo_processor_.has_value()) {
    assimp_geo_processor_.emplace(AssimpGeoProcessor(
        log_, config_, io_task_list_, exec_task_list_, filesystem_));
  }

  return *assimp_geo_processor_;
}

BasisuProcessor& IgassetGenerator::basisu_processor() const {
  if (!basisu_processor_.has_value()) {
    basisu_processor_.emplace(BasisuProcessor(log_, config_, io_task_list_,
                                              exec_task_list_, filesystem_));
  }
  return *basisu_processor_;
}

std::shared_ptr<igasync::Promise<bool>> IgassetGenerator::generate_igasset(
    const IgAssetGen::SingleIgassetGenAction* action) const {
  switch (action->action_type()) {
    case IgAssetGen::SingleIgassetGenActionData_CopyWgslSourceAction:
      return wgsl_copy_processor().generate_wgsl_igasset(
          action->action_as_CopyWgslSourceAction());
    case IgAssetGen::SingleIgassetGenActionData_AssimpToDracoAction:
      return assimp_geo_processor().draco_asset_from_assimp(
          action->action_as_AssimpToDracoAction());
    case IgAssetGen::SingleIgassetGenActionData_ImageToTexture2DAction:
      return basisu_processor().execute_image_to_texture2d(
          action->action_as_ImageToTexture2DAction());
    case IgAssetGen::SingleIgassetGenActionData_GenerateSpritesheetAction:
      return basisu_processor().execute_generate_spritesheet(
          action->action_as_GenerateSpritesheetAction());
    default:
      log_->error("Invalid ACTION_TYPE {}",
                  static_cast<int>(action->action_type()));
  }

  return igasync::Promise<bool>::Immediate(false);
}

}  // namespace igassetgen
