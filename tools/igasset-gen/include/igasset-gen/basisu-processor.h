#ifndef IGASSET_GEN_BASISU_PROCESSOR_H
#define IGASSET_GEN_BASISU_PROCESSOR_H

#include <encoder/basisu_enc.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <igasset-gen/exec-config.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasset-gen/stb-parse.h>
#include <igasync/promise.h>
#include <igasync/task_list.h>
#include <spdlog/spdlog.h>
#include <tool-utils/filesystem.h>

#include <memory>
#include <variant>

namespace igassetgen {

class BasisuProcessor {
 public:
  BasisuProcessor(std::shared_ptr<spdlog::logger> log,
                  PlanInvocationConfig config,
                  std::shared_ptr<igasync::TaskList> io_task_list,
                  std::shared_ptr<igasync::TaskList> exec_task_list,
                  std::shared_ptr<toolutils::Filesystem> filesystem)
      : log_(log->clone("BasisuProcessor")),
        config_(config),
        io_task_list_(io_task_list),
        exec_task_list_(exec_task_list),
        filesystem_(filesystem) {}
  BasisuProcessor(const BasisuProcessor&) = delete;
  BasisuProcessor& operator=(const BasisuProcessor&) = delete;
  BasisuProcessor(BasisuProcessor&&) = default;
  BasisuProcessor& operator=(BasisuProcessor&&) = default;
  ~BasisuProcessor() = default;

 public:
  //
  // Execute an ImageToTexture2DAction
  std::shared_ptr<igasync::Promise<bool>> execute_image_to_texture2d(
      const IgAssetGen::ImageToTexture2DAction* action) const;

 private:
  enum class AltResult {
    UseCached,
    FsErr,
    InvalidOutputFormat,
    CompressionFailure,
  };

  //
  // Individual output generation functions
  std::shared_ptr<igasync::Promise<bool>> generate_single_output(
      const IgAssetGen::Output2DImage* output, const StbImageData& data) const;
  std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
               BasisuProcessor::AltResult>
  rgba8_unorm_out(const IgAssetGen::Output2DImage* output,
                  const StbImageData& data) const;
  std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
               BasisuProcessor::AltResult>
  basisu_ldr_out(const IgAssetGen::Output2DImage* output,
                 const StbImageData& data) const;

 private:
  std::shared_ptr<spdlog::logger> log_;
  PlanInvocationConfig config_;
  std::shared_ptr<igasync::TaskList> io_task_list_;
  std::shared_ptr<igasync::TaskList> exec_task_list_;
  std::shared_ptr<toolutils::Filesystem> filesystem_;
};

}  // namespace igassetgen

#endif
