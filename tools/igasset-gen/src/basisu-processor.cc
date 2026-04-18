#include <encoder/basisu_astc_ldr_encode.h>
#include <encoder/basisu_comp.h>
#include <encoder/basisu_enc.h>
#include <encoder/basisu_gpu_texture.h>
#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/vector.h>
#include <igasset-gen/basisu-processor.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasset-gen/stb-parse.h>
#include <igasset/schema/igasset.h>
#include <igasync/promise.h>
#include <igasync/promise_combiner.h>
#include <stb/stb_image.h>
#include <transcoder/basisu.h>
#include <transcoder/basisu_containers.h>
#include <transcoder/basisu_file_headers.h>
#include <transcoder/basisu_transcoder.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

constexpr const char* basisu_ec_to_string(
    basisu::basis_compressor::error_code ec) {
  switch (ec) {
    case basisu::basis_compressor::error_code::cECFailedBackend:
      return "Failed backend compression";
    case basisu::basis_compressor::error_code::cECFailedCreateBasisFile:
      return "Failed to create basis file";
    case basisu::basis_compressor::error_code::cECFailedCreateKTX2File:
      return "Failed to create KTX2 file";
    case basisu::basis_compressor::error_code::cECFailedEncodeUASTC:
      return "Failed to encode UASTC";
    case basisu::basis_compressor::error_code::cECFailedFontendExtract:
      return "Failed to extract frontend data";
    case basisu::basis_compressor::error_code::cECFailedFrontEnd:
      return "Failed frontend compression";
    case basisu::basis_compressor::error_code::cECFailedInitializing:
      return "Failed to initialize compressor";
    case basisu::basis_compressor::error_code::cECFailedInvalidParameters:
      return "Invalid compressor parameters";
    case basisu::basis_compressor::error_code::cECFailedReadingSourceImages:
      return "Failed to read source images";
    case basisu::basis_compressor::error_code::cECFailedUASTCRDOPostProcess:
      return "Failed UASTC RDO post process";
    case basisu::basis_compressor::error_code::cECFailedWritingOutput:
      return "Failed to write output file";
    case basisu::basis_compressor::error_code::cECFailedValidating:
      return "Failed to validate";
    case basisu::basis_compressor::error_code::cECSuccess:
      return "Success";
  }

  return "<< UNKNOWN >>";
}

class RaiiFree {
 public:
  RaiiFree(void* ptr) : p(ptr) {}
  RaiiFree() : p(nullptr) {}
  RaiiFree(const RaiiFree&) = delete;
  RaiiFree& operator=(const RaiiFree&) = delete;
  RaiiFree(RaiiFree&&) = delete;
  RaiiFree& operator=(RaiiFree&&) = delete;

  ~RaiiFree() {
    if (p != nullptr) {
      basisu::basis_free_data(p);
      p = nullptr;
    }
  }

 private:
  void* p;
};

}  // namespace

namespace igassetgen {

//
// SINGLE OUTPUT GENERATION ROOT
//
std::shared_ptr<igasync::Promise<bool>> BasisuProcessor::generate_single_output(
    const IgAssetGen::Output2DImage* output,
    const StbImageData& input_img) const {
  int out_width = output->width();
  int out_height = output->height();
  if (out_height == 0) {
    out_height = out_width;
  }
  if (out_width == 0) {
    out_width = input_img.width;
    out_height = input_img.height;
  }

  auto resized_img = input_img.resized(out_width, out_height);
  if (!resized_img.has_value()) {
    log_->error("Failed to resize image for output {}",
                output->output_file_path()->str());
    return igasync::Promise<bool>::Immediate(false);
  }

  std::shared_ptr<igasync::Promise<
      std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
                   BasisuProcessor::AltResult>>>
      gen_rsl = nullptr;

  switch (output->image_encoding()) {
    case IgAsset::Image2DEncoding_RGBA8Unorm:
      gen_rsl = igasync::Promise<
          std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
                       BasisuProcessor::AltResult>>::
          Immediate(rgba8_unorm_out(output, *resized_img));
      break;
    case IgAsset::Image2DEncoding_ETC1S:
    case IgAsset::Image2DEncoding_UASTC_LDR_4_4:
      gen_rsl = igasync::Promise<std::variant<
          std::shared_ptr<flatbuffers::FlatBufferBuilder>,
          BasisuProcessor::AltResult>>::Immediate(basisu_ldr_out(output,
                                                                 *resized_img));
      break;
  }

  if (gen_rsl == nullptr) {
    log_->error("Unsupported output type for {}",
                output->output_file_path()->str());
    return igasync::Promise<bool>::Immediate(false);
  }

  return gen_rsl->then(
      [this, output](
          const std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
                             BasisuProcessor::AltResult>& rsl) -> bool {
        if (std::holds_alternative<BasisuProcessor::AltResult>(rsl)) {
          auto alt_rsl = std::get<BasisuProcessor::AltResult>(rsl);
          switch (alt_rsl) {
            case BasisuProcessor::AltResult::UseCached:
              log_->info("Output is up to date, skipping generation");
              return true;
            case BasisuProcessor::AltResult::FsErr:
            case BasisuProcessor::AltResult::InvalidOutputFormat:
              log_->error("Error generating {}",
                          output->output_file_path()->str());
              return false;
            case BasisuProcessor::AltResult::CompressionFailure:
              log_->error("Error compressing {}",
                          output->output_file_path()->str());
              return false;
          }
        }

        return filesystem_->write_bin(
            config_.IgassetPathRoot / output->output_file_path()->str(),
            *std::get<std::shared_ptr<flatbuffers::FlatBufferBuilder>>(rsl));
      },
      io_task_list_);
}

std::shared_ptr<igasync::Promise<bool>>
BasisuProcessor::execute_image_to_texture2d(
    const IgAssetGen::ImageToTexture2DAction* action) const {
  auto input_file_path =
      config_.InputAssetPathRoot / action->input_file_path()->str();

  log_->info("Processing ImageToTexture2DAction for input file {}",
             input_file_path.string());

  return io_task_list_
      ->run([filesystem = filesystem_, config = config_, log = log_,
             input_file_path, action]() -> std::optional<std::string> {
        // TODO (kamaron): Check for cached output here and return
        return filesystem->read_bin(input_file_path);
      })
      ->then_consuming(
          [log = log_, input_file_path](
              std::optional<std::string> rsl) -> std::optional<StbImageData> {
            if (!rsl.has_value()) {
              log->error("Failed to read file {}", input_file_path.string());
              return std::nullopt;
            }

            return StbImageData::from_memory(*rsl);
          },
          exec_task_list_)
      ->then_chain_consuming(
          [log = log_, input_file_path, action,
           exec_task_list = exec_task_list_,
           this](std::optional<StbImageData> rsl)
              -> std::shared_ptr<igasync::Promise<bool>> {
            auto combiner = igasync::PromiseCombiner::Create();

            std::vector<igasync::PromiseCombiner::PromiseKey<bool, false>>
                results;

            for (auto* output : *action->outputs()) {
              results.push_back(combiner->add(
                  generate_single_output(output, *rsl), exec_task_list));
            }

            return combiner->combine(
                [log, results](igasync::PromiseCombiner::Result rsl) -> bool {
                  bool all_true = true;
                  for (auto& key : results) {
                    all_true |= rsl.get(key);
                  }

                  return all_true;
                },
                exec_task_list);
          },
          exec_task_list_);
}

std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
             BasisuProcessor::AltResult>
BasisuProcessor::rgba8_unorm_out(const IgAssetGen::Output2DImage* output,
                                 const StbImageData& img) const {
  flatbuffers::Offset<flatbuffers::Vector<uint8_t>> fb_data = 0;

  auto fbb = std::make_shared<flatbuffers::FlatBufferBuilder>();

  if (img.num_channels == 4) {
    fb_data =
        fbb->CreateVector(img.data, img.width * img.height * img.num_channels);
  } else {
    std::vector<uint8_t> converted_data;
    converted_data.reserve(img.width * img.height * 4);

    for (uint16_t row = 0; row < img.height; row++) {
      for (uint16_t col = 0; col < img.width; col++) {
        uint8_t* pixel_ptr =
            img.data + (row * img.width + col) * img.num_channels;
        converted_data.push_back(pixel_ptr[0]);
        converted_data.push_back(pixel_ptr[1]);
        converted_data.push_back(pixel_ptr[2]);
        if (img.num_channels == 4) {
          converted_data.push_back(pixel_ptr[3]);
        } else {
          converted_data.push_back(255);
        }
      }
    }

    fb_data =
        fbb->CreateVector(converted_data.data(), img.width * img.height * 4);
  }

  auto fb_img =
      IgAsset::CreateImage2D(*fbb, IgAsset::Image2DEncoding_RGBA8Unorm,
                             img.width, img.height, fb_data);

  auto asset = IgAsset::CreateAsset(*fbb, IgAsset::SingleAssetData_Image2D,
                                    fb_img.Union());

  fbb->Finish(asset);
  return fbb;
}

static basist::basis_tex_format basis_format(
    IgAsset::Image2DEncoding encoding) {
  switch (encoding) {
    case IgAsset::Image2DEncoding::Image2DEncoding_UASTC_LDR_4_4:
      return basist::basis_tex_format::cUASTC_LDR_4x4;
    case IgAsset::Image2DEncoding_ETC1S:
    default:
      return basist::basis_tex_format::cETC1S;
  }
}

static IgAsset::Image2DEncoding from_igpackgen_fmt(
    IgAsset::Image2DEncoding encoding) {
  switch (encoding) {
    case IgAsset::Image2DEncoding::Image2DEncoding_UASTC_LDR_4_4:
      return IgAsset::Image2DEncoding::Image2DEncoding_UASTC_LDR_4_4;
    case IgAsset::Image2DEncoding_ETC1S:
      return IgAsset::Image2DEncoding::Image2DEncoding_ETC1S;
    case IgAsset::Image2DEncoding::Image2DEncoding_RGBA8Unorm:
    default:
      return IgAsset::Image2DEncoding::Image2DEncoding_RGBA8Unorm;
  }
}

static constexpr const char* for_log(IgAsset::Image2DEncoding encoding) {
  switch (encoding) {
    case IgAsset::Image2DEncoding::Image2DEncoding_UASTC_LDR_4_4:
      return "UASTC LDR 4x4";
    case IgAsset::Image2DEncoding_ETC1S:
      return "ETC1S";
    case IgAsset::Image2DEncoding::Image2DEncoding_RGBA8Unorm:
      return "RGBA8Unorm";
    default:
      return "<< UNKNOWN >>";
  }
}

std::variant<std::shared_ptr<flatbuffers::FlatBufferBuilder>,
             BasisuProcessor::AltResult>
BasisuProcessor::basisu_ldr_out(const IgAssetGen::Output2DImage* output,
                                const StbImageData& data) const {
  log_->debug("Processing {}x{} {} output file {}", output->width(),
              output->height(), for_log(output->image_encoding()),
              output->output_file_path()->str());
  // A couple reference files that helped in writing this:
  // Also: Notice, using formats defined in schema file, details:
  // https://github.com/BinomialLLC/basis_universal?tab=readme-ov-file
  // Also, see this for a Basis code example:
  // https://github.com/BinomialLLC/basis_universal/blob/41a758e7d17428f6d9d45704dcb0c3bb0547ae77/example/example.cpp#L115-L121

  basisu::image img(data.width, data.height);
  for (uint32_t x = 0; x < data.width; x++) {
    for (uint32_t y = 0; y < data.height; y++) {
      uint8_t* pixel_ptr = data.data + (y * data.width + x) * data.num_channels;
      uint8_t a = data.num_channels == 4 ? pixel_ptr[3] : 255;
      img(x, y).set(pixel_ptr[0], pixel_ptr[1], pixel_ptr[2], a);
    }
  }
  basisu::vector<basisu::image> images;
  images.push_back(std::move(img));

  uint32_t flags_and_quality = output->quality() & 0xFF;
  flags_and_quality |= basisu::cFlagUseOpenCL | basisu::cFlagThreaded;
  if (output->gen_mips()) {
    flags_and_quality |= basisu::cFlagGenMipsClamp;
  }
  if (output->use_srgb()) {
    flags_and_quality |= basisu::cFlagSRGB;
  }

  size_t basisu_file_size = 0;
  auto basis_fmt = basis_format(output->image_encoding());
  auto igasset_fmg = from_igpackgen_fmt(output->image_encoding());
  void* basisu_data = basisu::basis_compress(
      basis_fmt, images, flags_and_quality, 0.f, &basisu_file_size, nullptr);
  RaiiFree rf(basisu_data);

  if (!basisu_data) {
    log_->error("Basis compressor failure");
    return BasisuProcessor::AltResult::CompressionFailure;
  }

  auto fbb = std::make_shared<flatbuffers::FlatBufferBuilder>();

  auto fb_data = fbb->CreateVector(reinterpret_cast<uint8_t*>(basisu_data),
                                   basisu_file_size);
  auto fb_img = IgAsset::CreateImage2D(*fbb, igasset_fmg, data.width,
                                       data.height, fb_data);

  auto asset = IgAsset::CreateAsset(*fbb, IgAsset::SingleAssetData_Image2D,
                                    fb_img.Union());

  fbb->Finish(asset);
  return fbb;
}

}  // namespace igassetgen
