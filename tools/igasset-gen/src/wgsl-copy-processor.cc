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

namespace {
enum class LoadWgslAltResult {
  UseCached,
  FsErr,
  Invalid,
};
}

namespace igassetgen {

std::shared_ptr<igasync::Promise<bool>>
WgslCopyProcessor::generate_wgsl_igasset(
    const IgAssetGen::CopyWgslSourceAction* action) const {
  auto input_file_path =
      config_.InputAssetPathRoot / action->input_file_path()->str();

  log_->info("Copying WGSL file {} to IgAsset {}", input_file_path.string(),
             action->output_file_path()->str());

  return io_task_list_
      ->run([filesystem = filesystem_, config = config_, log = log_,
             input_file_path,
             action]() -> std::variant<std::string, LoadWgslAltResult> {
        std::string wgsl_text = "";

        std::vector<std::filesystem::path> input_paths;
        input_paths.push_back(input_file_path);
        auto output_file_path =
            config.IgassetPathRoot / action->output_file_path()->str();
        if (skip_rebuild(config, std::move(input_paths), output_file_path,
                         log)) {
          return LoadWgslAltResult::UseCached;
        }

        auto rsl = filesystem->read_bin(input_file_path);
        if (!rsl.has_value()) {
          return LoadWgslAltResult::FsErr;
        }
        return *rsl;
      })
      ->then_consuming(
          [action, log = log_](
              std::variant<std::string, LoadWgslAltResult> wgsl_source_var)
              -> std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
                              LoadWgslAltResult> {
            if (std::holds_alternative<LoadWgslAltResult>(wgsl_source_var)) {
              return std::get<LoadWgslAltResult>(wgsl_source_var);
            }

            std::string wgsl_source = std::get<std::string>(wgsl_source_var);
            if (wgsl_source == "") {
              log->error("Empty WGSL, aborting");
              return LoadWgslAltResult::Invalid;
            }

            auto fbb = std::make_shared<flatbuffers::FlatBufferBuilder>();
            auto source = fbb->CreateString(wgsl_source);
            auto vertex_entry_point =
                action->vertex_entry_point() != nullptr
                    ? fbb->CreateString(
                          action->vertex_entry_point()->string_view())
                    : 0;
            auto fragment_entry_point =
                action->fragment_entry_point() != nullptr
                    ? fbb->CreateString(
                          action->fragment_entry_point()->string_view())
                    : 0;
            auto compute_entry_point =
                action->compute_entry_point() != nullptr
                    ? fbb->CreateString(
                          action->compute_entry_point()->string_view())
                    : 0;
            auto wgsl_source_offset = IgAsset::CreateWgslSource(
                *fbb, source, vertex_entry_point, fragment_entry_point,
                compute_entry_point);
            auto igasset =
                IgAsset::CreateAsset(*fbb, IgAsset::SingleAssetData_WgslSource,
                                     wgsl_source_offset.Union());

            fbb->Finish(igasset);
            return fbb;
          },
          exec_task_list_)
      ->then_consuming(
          [config = config_, log = log_, action, filesystem = filesystem_](
              std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
                           LoadWgslAltResult>
                  wgsl_igasset_bin_var) -> bool {
            if (std::holds_alternative<LoadWgslAltResult>(
                    wgsl_igasset_bin_var)) {
              switch (std::get<LoadWgslAltResult>(wgsl_igasset_bin_var)) {
                case LoadWgslAltResult::FsErr:
                case LoadWgslAltResult::Invalid:
                  return false;
                case LoadWgslAltResult::UseCached:
                  return true;
                default:
                  log->error("Unknown LoadWgslAltResult, failing");
                  return false;
              }
            }

            std::shared_ptr<flatbuffers::FlatBufferBuilder> wgsl_igasset_bin =
                std::get<std::shared_ptr<flatbuffers::FlatBufferBuilder>>(
                    std::move(wgsl_igasset_bin_var));

            return filesystem->write_bin(
                config.IgassetPathRoot / action->output_file_path()->str(),
                *wgsl_igasset_bin);
          },
          io_task_list_);
}

}  // namespace igassetgen
