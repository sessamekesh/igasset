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
#include <staged-asset-io/staged-filesystem.h>

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
  const std::string logical_input = action->input_file_path()->str();
  const std::string logical_output = action->output_file_path()->str();

  log_->info("Copying WGSL file {} to IgAsset {}", logical_input,
             logical_output);

  return exec_task_list_->run([config = config_, log = log_, logical_input,
                               logical_output, action]() -> bool {
    const auto& staged_fs = config.StagedFs;

    auto input_physical = staged_fs->resolve_input(logical_input);
    if (!input_physical.has_value()) {
      // resolve_input logs the denial on error
      return false;
    }
    auto output_physical = staged_fs->resolve_output(logical_output);
    if (!output_physical.has_value()) {
      // resolve_output logs the denial on error
      return false;
    }

    std::vector<std::filesystem::path> input_paths;
    input_paths.push_back(*input_physical);
    if (skip_rebuild(config, std::move(input_paths), *output_physical, log)) {
      return true;
    }

    auto rsl = staged_fs->read_input(logical_input);
    if (!rsl.has_value()) {
      // read_input logs the failure on error
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

    return staged_fs->write_output(logical_output, *fbb);
  });
}

}  // namespace igassetgen