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
#include <igasset/schema/types.h>
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
bool BasisuProcessor::generate_single_output(
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
    return false;
  }

  auto fbb = std::make_shared<flatbuffers::FlatBufferBuilder>();

  BasisuProcessor::Image2DGenRsl gen_rsl;
  switch (output->image_encoding()) {
    case IgAsset::Image2DEncoding_RGBA8Unorm:
      gen_rsl = rgba8_unorm_out(output, fbb, *resized_img);
      break;
    case IgAsset::Image2DEncoding_ETC1S:
    case IgAsset::Image2DEncoding_UASTC_LDR_4_4:
      gen_rsl = basisu_ldr_out(output, fbb, *resized_img);
      break;
    default:
      log_->error("Unexpected Image2DEncoding value for output {}",
                  output->output_file_path()->str());
      return false;
  }

  if (std::holds_alternative<BasisuProcessor::AltResult>(gen_rsl)) {
    auto alt_rsl = std::get<BasisuProcessor::AltResult>(gen_rsl);
    switch (alt_rsl) {
      case BasisuProcessor::AltResult::UseCached:
        log_->info("Output is up to date, skipping generation");
        return true;
      case BasisuProcessor::AltResult::FsErr:
      case BasisuProcessor::AltResult::InvalidOutputFormat:
        log_->error("Error generating {}", output->output_file_path()->str());
        return false;
      case BasisuProcessor::AltResult::CompressionFailure:
        log_->error("Error compressing {}", output->output_file_path()->str());
        return false;
    }
  }

  auto fb_image2d = std::get<flatbuffers::Offset<IgAsset::Image2D>>(gen_rsl);

  auto asset = IgAsset::CreateAsset(*fbb, IgAsset::SingleAssetData_Image2D,
                                    fb_image2d.Union());
  fbb->Finish(asset);

  return config_.StagedFs->write_output(output->output_file_path()->str(),
                                        *fbb);
}

std::shared_ptr<igasync::Promise<bool>>
BasisuProcessor::execute_image_to_texture2d(
    const IgAssetGen::ImageToTexture2DAction* action) const {
  const std::string logical_input = action->input_file_path()->str();

  log_->info("Processing ImageToTexture2DAction for input file {}",
             logical_input);

  return exec_task_list_
      ->run([staged_fs = config_.StagedFs, log = log_,
             logical_input]() -> std::optional<StbImageData> {
        // TODO (kamaron): Check for cached output here and return
        auto rsl = staged_fs->read_input(logical_input);
        if (!rsl.has_value()) {
          log->error("Failed to read file {}", logical_input);
          return std::nullopt;
        }

        return StbImageData::from_memory(*rsl);
      })
      ->then_chain_consuming(
          [log = log_, logical_input, action, exec_task_list = exec_task_list_,
           this](std::optional<StbImageData> rsl)
              -> std::shared_ptr<igasync::Promise<bool>> {
            if (!rsl.has_value()) {
              return igasync::Promise<bool>::Immediate(false);
            }

            auto rsl_img = std::make_shared<StbImageData>(std::move(*rsl));

            auto combiner = igasync::PromiseCombiner::Create();

            std::vector<igasync::PromiseCombiner::PromiseKey<bool, false>>
                results;

            for (auto* output : *action->outputs()) {
              results.push_back(combiner->add(
                  exec_task_list->run([this, output, rsl_img]() -> bool {
                    return generate_single_output(output, *rsl_img);
                  }),
                  exec_task_list));
            }

            return combiner->combine(
                [log, results](igasync::PromiseCombiner::Result rsl) -> bool {
                  bool all_true = true;
                  for (auto& key : results) {
                    all_true &= rsl.get(key);
                  }

                  return all_true;
                },
                exec_task_list);
          },
          exec_task_list_);
}

std::shared_ptr<igasync::Promise<bool>>
BasisuProcessor::execute_generate_spritesheet(
    const IgAssetGen::GenerateSpritesheetAction* action) const {
  auto fb_output_image = action->output_image();
  if (fb_output_image == nullptr) {
    log_->error(
        "Cannot execute 'GenerateSpritesheetAction' - no output_image "
        "specified");
    return igasync::Promise<bool>::Immediate(false);
  }

  std::uint8_t background_rgba[4] = {
      static_cast<std::uint8_t>(action->default_r() * 255.f),
      static_cast<std::uint8_t>(action->default_g() * 255.f),
      static_cast<std::uint8_t>(action->default_b() * 255.f),
      static_cast<std::uint8_t>(action->default_a() * 255.f),
  };

  auto fb_output_file_path = fb_output_image->output_file_path();
  if (fb_output_file_path == nullptr) {
    log_->error(
        "No output_file_path specified in "
        "'GenerateSpritesheetAction::output_image'");
    return igasync::Promise<bool>::Immediate(false);
  }

  const std::string logical_output = fb_output_file_path->str();

  auto output_image_opt = StbImageData::blank_rgba(
      fb_output_image->width(), fb_output_image->height(), background_rgba);
  if (!output_image_opt.has_value()) {
    log_->error("Could not allocate new output image for {}", logical_output);
    return igasync::Promise<bool>::Immediate(false);
  }

  auto output_image =
      std::make_shared<StbImageData>(std::move(*output_image_opt));

  auto fb_sprites = action->sprites();
  if (fb_sprites == nullptr) {
    log_->error(
        "No sprites provided for GenerateSpritesheet action {} - aborting",
        logical_output);
    return igasync::Promise<bool>::Immediate(false);
  }

  log_->info("Processing GenerateSpritesheet action (n={}) for output file {}",
             fb_sprites->size(), logical_output);

  // TODO (kamaron): Perform cache check here (OK to do on main thread, it's a
  //  quick check that skips a lot of work if it succeed)

  // All sprite reads + blits happen serially inside a single exec task.
  // There's DEFINITELY a better parallelization scheme that involves setting
  // up a dependency graph based on write-to area (e.g., a 30% x 30% sprite in
  // the top left shouldn't be blocked by a write for one of the same size in
  // the top left!) but for now this will be fine.
  // The expensive part of spritesheet generation is the image encoding anyways
  //  if any sort of texture compression is used!
  return exec_task_list_->run([this, fb_sprites, fb_output_image, output_image,
                               logical_output]() -> bool {
    for (auto* sprite : *fb_sprites) {
      auto fb_input_file_path = sprite->input_file_path();
      if (fb_input_file_path == nullptr) {
        log_->error(
            "Could not read input file path when processing "
            "GenerateSpritesheet - aborting");
        return false;
      }
      const std::string logical_sprite_input = fb_input_file_path->str();

      auto rsl = config_.StagedFs->read_input(logical_sprite_input);
      if (!rsl.has_value()) {
        // read_input logs the failure on error
        return false;
      }

      // Force RGBA (4-channel) load so the source always matches
      // the 4-channel blank_rgba canvas.
      auto sprite_opt = StbImageData::from_memory(*rsl, 4);
      if (!sprite_opt.has_value()) {
        log_->error("Failed to read sprite from {}", logical_sprite_input);
        return false;
      }

      auto fb_sprite_loc = sprite->sprite();
      if (fb_sprite_loc == nullptr) {
        log_->error(
            "Missing 'sprite' location in SpriteSubAction for "
            "input {}",
            logical_sprite_input);
        return false;
      }

      int dst_x = fb_sprite_loc->x();
      int dst_y = fb_sprite_loc->y();
      int dst_w = fb_sprite_loc->w();
      int dst_h = fb_sprite_loc->h();

      if (dst_w <= 0 || dst_h <= 0) {
        log_->error("Invalid sprite dimensions {} x {} for input {}", dst_w,
                    dst_h, logical_sprite_input);
        return false;
      }

      // Optionally crop the source before blitting.
      const StbImageData* blit_src = &*sprite_opt;
      std::optional<StbImageData> cropped_opt;
      int crop_x = sprite->crop_left();
      int crop_y = sprite->crop_top();
      bool has_right = sprite->crop_right() > -1;
      bool has_bottom = sprite->crop_bottom() > -1;
      if (crop_x != 0 || crop_y != 0 || has_right || has_bottom) {
        int crop_w = has_right ? sprite->crop_right() - crop_x
                               : sprite_opt->width - crop_x;
        int crop_h = has_bottom ? sprite->crop_bottom() - crop_y
                                : sprite_opt->height - crop_y;
        cropped_opt = sprite_opt->crop(crop_x, crop_y, crop_w, crop_h);
        if (!cropped_opt.has_value()) {
          log_->error("Failed to crop sprite from {} at [{},{}] {}x{}",
                      logical_sprite_input, crop_x, crop_y, crop_w, crop_h);
          return false;
        }
        blit_src = &*cropped_opt;
      }

      if (!output_image->blit(*blit_src, dst_x, dst_y, dst_w, dst_h)) {
        log_->error("Failed to blit sprite from {} to ({}, {})",
                    logical_sprite_input, dst_x, dst_y);
        return false;
      }
    }

    // Once all sprites have been blitted to the output image, assemble the
    //  final igasset flatbuffer and write it to file.
    auto fbb = std::make_shared<flatbuffers::FlatBufferBuilder>();

    BasisuProcessor::Image2DGenRsl gen_rsl;
    switch (fb_output_image->image_encoding()) {
      case IgAsset::Image2DEncoding_RGBA8Unorm:
        gen_rsl = rgba8_unorm_out(fb_output_image, fbb, *output_image);
        break;
      case IgAsset::Image2DEncoding_ETC1S:
      case IgAsset::Image2DEncoding_UASTC_LDR_4_4:
        gen_rsl = basisu_ldr_out(fb_output_image, fbb, *output_image);
        break;
      default:
        log_->error("Unexpected Image2DEncoding value for output {}",
                    logical_output);
        return false;
    }

    if (std::holds_alternative<BasisuProcessor::AltResult>(gen_rsl)) {
      auto alt_rsl = std::get<BasisuProcessor::AltResult>(gen_rsl);
      switch (alt_rsl) {
        case BasisuProcessor::AltResult::UseCached:
          log_->info("Output {} is up to date, skipping generation",
                     logical_output);
          return true;
        case BasisuProcessor::AltResult::FsErr:
        case BasisuProcessor::AltResult::InvalidOutputFormat:
          log_->error("Error generating {}", logical_output);
          return false;
        case BasisuProcessor::AltResult::CompressionFailure:
          log_->error("Error compressing {}", logical_output);
          return false;
      }
    }

    auto fb_image2d = std::get<flatbuffers::Offset<IgAsset::Image2D>>(gen_rsl);

    std::vector<flatbuffers::Offset<IgAsset::Sprite>> out_sprites;
    for (auto fb_sprite : *fb_sprites) {
      auto fb_in_sprite = fb_sprite->sprite();
      if (fb_in_sprite == nullptr) {
        log_->error(
            "Could not read "
            "GenerateSpritesheetAction::sprites[]::sprite in {}",
            logical_output);
        return false;
      }

      auto fb_in_sprite_name = fb_in_sprite->name();
      if (fb_in_sprite_name == nullptr) {
        log_->error(
            "Could not read "
            "GenerateSpritesheetAction::sprites[]::sprite::name in "
            "{}",
            logical_output);
        return false;
      }

      auto sprite_name = fb_in_sprite_name->str();

      auto fb_out_sprite = IgAsset::CreateSprite(
          *fbb, fbb->CreateString(sprite_name), fb_in_sprite->x(),
          fb_in_sprite->y(), fb_in_sprite->w(), fb_in_sprite->h());

      out_sprites.push_back(fb_out_sprite);
    }

    auto fb_out_sprites = fbb->CreateVector(out_sprites);

    auto fb_out_spritesheet =
        IgAsset::CreateSpritesheet(*fbb, fb_image2d, fb_out_sprites);
    auto fb_out_asset = IgAsset::CreateAsset(
        *fbb, IgAsset::SingleAssetData_Spritesheet, fb_out_spritesheet.Union());
    fbb->Finish(fb_out_asset);

    return config_.StagedFs->write_output(logical_output, *fbb);
  });
}

std::variant<flatbuffers::Offset<IgAsset::Image2D>, BasisuProcessor::AltResult>
BasisuProcessor::rgba8_unorm_out(
    const IgAssetGen::Output2DImage* output,
    std::shared_ptr<flatbuffers::FlatBufferBuilder> fbb,
    const StbImageData& img) const {
  flatbuffers::Offset<flatbuffers::Vector<uint8_t>> fb_data = 0;

  const std::size_t pixel_count = static_cast<std::size_t>(img.width) *
                                  static_cast<std::size_t>(img.height);

  if (img.num_channels == 4) {
    fb_data = fbb->CreateVector(img.data, pixel_count * 4);
  } else {
    // Expand the source channels to RGBA. Only valid channels are read:
    // grayscale (g,g,g,255), gray+alpha (g,g,g,a), RGB (r,g,b,255).
    std::vector<uint8_t> converted_data;
    converted_data.reserve(pixel_count * 4);

    for (int row = 0; row < img.height; row++) {
      for (int col = 0; col < img.width; col++) {
        const uint8_t* pixel_ptr =
            img.data + (static_cast<std::size_t>(row) * img.width + col) *
                           img.num_channels;
        uint8_t r = 0, g = 0, b = 0, a = 255;
        switch (img.num_channels) {
          case 1:
            r = g = b = pixel_ptr[0];
            a = 255;
            break;
          case 2:
            r = g = b = pixel_ptr[0];
            a = pixel_ptr[1];
            break;
          case 3:
            r = pixel_ptr[0];
            g = pixel_ptr[1];
            b = pixel_ptr[2];
            a = 255;
            break;
          default:
            break;
        }
        converted_data.push_back(r);
        converted_data.push_back(g);
        converted_data.push_back(b);
        converted_data.push_back(a);
      }
    }

    fb_data = fbb->CreateVector(converted_data.data(), pixel_count * 4);
  }

  return IgAsset::CreateImage2D(*fbb, IgAsset::Image2DEncoding_RGBA8Unorm,
                                img.width, img.height, fb_data);
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

std::variant<flatbuffers::Offset<IgAsset::Image2D>, BasisuProcessor::AltResult>
BasisuProcessor::basisu_ldr_out(
    const IgAssetGen::Output2DImage* output,
    std::shared_ptr<flatbuffers::FlatBufferBuilder> fbb,
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
      const uint8_t* pixel_ptr =
          data.data +
          (static_cast<std::size_t>(y) * data.width + x) * data.num_channels;
      uint8_t r = 0, g = 0, b = 0, a = 255;
      switch (data.num_channels) {
        case 1:
          r = g = b = pixel_ptr[0];
          a = 255;
          break;
        case 2:
          r = g = b = pixel_ptr[0];
          a = pixel_ptr[1];
          break;
        case 3:
          r = pixel_ptr[0];
          g = pixel_ptr[1];
          b = pixel_ptr[2];
          a = 255;
          break;
        default:
          r = pixel_ptr[0];
          g = pixel_ptr[1];
          b = pixel_ptr[2];
          a = pixel_ptr[3];
          break;
      }
      img(x, y).set(r, g, b, a);
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

  auto fb_data = fbb->CreateVector(reinterpret_cast<uint8_t*>(basisu_data),
                                   basisu_file_size);
  return IgAsset::CreateImage2D(*fbb, igasset_fmg, data.width, data.height,
                                fb_data);
}

}  // namespace igassetgen
