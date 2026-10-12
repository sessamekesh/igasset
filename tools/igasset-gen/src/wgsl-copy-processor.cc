#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <igasset-gen/exec-config.h>
#include <igasset-gen/inc-build.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasset-gen/schema/igasset.h>
#include <igasset-gen/wgsl-copy-processor.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/logger.h>

#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace igassetgen {

std::shared_ptr<igasync::Promise<bool>>
WgslCopyProcessor::generate_wgsl_igasset(
    const IgAssetGen::CopyWgslSourceAction* action) const {
  auto input_file_path =
      config_.InputAssetPathRoot / action->input_file_path()->str();

  log_->info("Copying WGSL file {} to IgAsset {}", input_file_path.string(),
             action->output_file_path()->str());

  return exec_task_list_->run([filesystem = filesystem_, config = config_,
                               log = log_, input_file_path, action]() -> bool {
    std::vector<std::filesystem::path> input_paths;
    input_paths.push_back(input_file_path);
    auto output_file_path =
        config.IgassetPathRoot / action->output_file_path()->str();
    if (skip_rebuild(config, std::move(input_paths), output_file_path, log)) {
      return true;
    }

    auto rsl = filesystem->read_bin(input_file_path);
    if (!rsl.has_value()) {
      // read_bin logs the failure on error
      return false;
    }
    if (rsl->empty()) {
      log->error("Empty WGSL, aborting");
      return false;
    }

    auto fbb = std::make_shared<flatbuffers::FlatBufferBuilder>();
    auto source = fbb->CreateString(*rsl);
    auto vertex_entry_point =
        action->vertex_entry_point() != nullptr
            ? fbb->CreateString(action->vertex_entry_point()->string_view())
            : 0;
    auto fragment_entry_point =
        action->fragment_entry_point() != nullptr
            ? fbb->CreateString(action->fragment_entry_point()->string_view())
            : 0;
    auto compute_entry_point =
        action->compute_entry_point() != nullptr
            ? fbb->CreateString(action->compute_entry_point()->string_view())
            : 0;
    auto wgsl_source_offset =
        IgAsset::CreateWgslSource(*fbb, source, vertex_entry_point,
                                  fragment_entry_point, compute_entry_point);
    auto igasset = IgAsset::CreateAsset(
        *fbb, IgAsset::SingleAssetData_WgslSource, wgsl_source_offset.Union());

    fbb->Finish(igasset);

    return filesystem->write_bin(output_file_path, *fbb);
  });
}

}  // namespace igassetgen
