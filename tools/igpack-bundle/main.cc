#include <flatbuffers/buffer.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>
#include <igasset-gen/schema/igasset.h>
#include <igasset/schema/igpack.h>
#include <igasset/schema/types.h>
#include <spdlog/logger.h>
#include <tool-utils/embedded-schema.h>
#include <tool-utils/filesystem.h>
#include <tool-utils/log-level.h>
#include <tool-utils/runtime-timer.h>

#include <flatbuffers/idl.h>
#include <igpack-bundle/exec-config.h>
#include <igpack-bundle/schema/igpack-bundle-plan.h>
#include <igpack-bundle/schema/igpack-bundle-plan-embedded.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <CLI/CLI.hpp>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

constexpr flatbuffers::Verifier::Options get_opts() {
  flatbuffers::Verifier::Options opts{};

  opts.assert = false;
  opts.check_alignment = false;
  opts.check_nested_flatbuffers = true;
  opts.max_depth = 8;
  opts.max_size = 512000000ull;

  return opts;
}

const IgAsset::Asset* get_asset(const std::string& raw,
                                const std::string& igasset_path,
                                std::shared_ptr<spdlog::logger> log) {
  flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(raw.data()),
                                 raw.size(), ::get_opts());
  if (!IgAsset::VerifyAssetBuffer(verifier)) {
    log->error("File at path {} is not a valid igasset file", igasset_path);
    return nullptr;
  }

  return IgAsset::GetAsset(raw.data());
}

bool pack_wgsl_source(
    flatbuffers::FlatBufferBuilder& fbb,
    std::vector<flatbuffers::Offset<IgAsset::SingleAsset>>& sads,
    std::shared_ptr<spdlog::logger> log, toolutils::Filesystem& filesystem,
    const std::string& asset_name, std::filesystem::path igasset_path,
    const IgAsset::WgslSource* wgsl_source) {
  auto source = fbb.CreateString(wgsl_source->source()->str());
  auto vertex_entry_point =
      wgsl_source->vertex_entry_point() != nullptr
          ? fbb.CreateString(wgsl_source->vertex_entry_point()->string_view())
          : 0;
  auto fragment_entry_point =
      wgsl_source->fragment_entry_point() != nullptr
          ? fbb.CreateString(wgsl_source->fragment_entry_point()->string_view())
          : 0;
  auto compute_entry_point =
      wgsl_source->compute_entry_point() != nullptr
          ? fbb.CreateString(wgsl_source->compute_entry_point()->string_view())
          : 0;

  auto wgsl_source_out =
      IgAsset::CreateWgslSource(fbb, source, vertex_entry_point,
                                fragment_entry_point, compute_entry_point);
  sads.push_back(IgAsset::CreateSingleAsset(fbb, fbb.CreateString(asset_name),
                                            IgAsset::SingleAssetData_WgslSource,
                                            wgsl_source_out.Union()));
  return true;
}

bool pack_image2d(flatbuffers::FlatBufferBuilder& fbb,
                  std::vector<flatbuffers::Offset<IgAsset::SingleAsset>>& sads,
                  std::shared_ptr<spdlog::logger> log,
                  toolutils::Filesystem& filesystem,
                  const std::string& asset_name,
                  std::filesystem::path igasset_path,
                  const IgAsset::Image2D* image2d) {
  auto encoding = image2d->encoding();
  auto width = image2d->width();
  auto height = image2d->height();
  auto data =
      image2d->data() != nullptr
          ? fbb.CreateVector(image2d->data()->Data(), image2d->data()->size())
          : 0;

  auto image2d_out = IgAsset::CreateImage2D(fbb, encoding, width, height, data);
  sads.push_back(IgAsset::CreateSingleAsset(fbb, fbb.CreateString(asset_name),
                                            IgAsset::SingleAssetData_Image2D,
                                            image2d_out.Union()));

  return true;
}

bool pack_spritesheet(
    flatbuffers::FlatBufferBuilder& fbb,
    std::vector<flatbuffers::Offset<IgAsset::SingleAsset>>& sads,
    std::shared_ptr<spdlog::logger> log, toolutils::Filesystem& filesystem,
    const std::string& asset_name, std::filesystem::path igasset_path,
    const IgAsset::Spritesheet* spritesheet) {
  auto fb_in_image = spritesheet->image();
  if (fb_in_image == nullptr) {
    log->error("Spritesheet {} - expected 'image' missing in input.",
               asset_name);
    return false;
  }

  auto fb_in_sprites = spritesheet->sprites();
  if (fb_in_sprites == nullptr) {
    log->error("Spritesheet {} - expected 'sprites' missing in input.",
               asset_name);
    return false;
  }

  std::vector<flatbuffers::Offset<IgAsset::Sprite>> out_sprites;
  for (std::size_t i = 0; i < fb_in_sprites->size(); i++) {
    auto* fb_in_sprite = fb_in_sprites->Get(i);
    auto fb_in_name = fb_in_sprite->name();
    if (fb_in_name == nullptr) {
      log->error(
          "Spritesheet {} - expected 'sprites[{}].name' missing in input.",
          asset_name, i);
      return false;
    }

    if (fb_in_sprite->x() < 0 || fb_in_sprite->x() >= fb_in_image->width() ||
        fb_in_sprite->y() < 0 || fb_in_sprite->y() >= fb_in_image->height() ||
        fb_in_sprite->w() < 0 || fb_in_sprite->h() < 0 ||
        (fb_in_sprite->x() + fb_in_sprite->w()) > fb_in_image->width() ||
        (fb_in_sprite->y() + fb_in_sprite->h()) > fb_in_image->height()) {
      log->error(
          "Spritesheet {} sprite #{} - location [{}, {}] and size {} x {} is "
          "incompatible with underlying image size {} x {}",
          asset_name, i, fb_in_sprite->x(), fb_in_sprite->y(),
          fb_in_sprite->w(), fb_in_sprite->h(), fb_in_image->width(),
          fb_in_image->height());
      return false;
    }

    auto fb_out_sprite = IgAsset::CreateSprite(
        fbb, fbb.CreateString(fb_in_name->str()), fb_in_sprite->x(),
        fb_in_sprite->y(), fb_in_sprite->w(), fb_in_sprite->h());
    out_sprites.push_back(fb_out_sprite);
  }

  auto encoding = fb_in_image->encoding();
  auto width = fb_in_image->width();
  auto height = fb_in_image->height();
  auto data = fb_in_image->data() != nullptr
                  ? fbb.CreateVector(fb_in_image->data()->Data(),
                                     fb_in_image->data()->size())
                  : 0;

  if (width <= 0 || height <= 0 || data.IsNull()) {
    log->error(
        "Failed to output spritesheet {} with dimensions {}x{} and data size "
        "{}",
        asset_name, width, height, data.IsNull() ? "0" : "(valid)");
    return false;
  }

  auto fb_spritesheet_out = IgAsset::CreateSpritesheet(
      fbb, IgAsset::CreateImage2D(fbb, encoding, width, height, data),
      fbb.CreateVector(out_sprites));

  sads.push_back(IgAsset::CreateSingleAsset(
      fbb, fbb.CreateString(asset_name), IgAsset::SingleAssetData_Spritesheet,
      fb_spritesheet_out.Union()));
  return true;
}

bool pack_draco_geo(
    flatbuffers::FlatBufferBuilder& fbb,
    std::vector<flatbuffers::Offset<IgAsset::SingleAsset>>& sads,
    std::shared_ptr<spdlog::logger> log, toolutils::Filesystem& filesystem,
    const std::string& asset_name, std::filesystem::path igasset_path,
    const IgAsset::DracoGeometry* geo) {
  auto draco_bin =
      geo->draco_bin() != nullptr
          ? fbb.CreateVector(geo->draco_bin()->Data(), geo->draco_bin()->size())
          : 0;

  flatbuffers::Offset<
      flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>>
      ozz_bone_names = 0;
  if (geo->ozz_bone_names() != nullptr) {
    std::vector<std::string> names;
    names.reserve(geo->ozz_bone_names()->size());
    for (const auto* name : *geo->ozz_bone_names()) {
      names.push_back(name->str());
    }
    ozz_bone_names = fbb.CreateVectorOfStrings(names);
  }

  flatbuffers::Offset<flatbuffers::Vector<flatbuffers::Offset<IgAsset::Mat4>>>
      ozz_inv_bind_poses = 0;
  if (geo->ozz_inv_bind_poses() != nullptr) {
    std::vector<flatbuffers::Offset<IgAsset::Mat4>> poses;
    poses.reserve(geo->ozz_inv_bind_poses()->size());
    for (const auto* pose : *geo->ozz_inv_bind_poses()) {
      auto values = fbb.CreateVector(
          reinterpret_cast<const float*>(pose->values()->Data()),
          pose->values()->size() / sizeof(float));
      poses.push_back(IgAsset::CreateMat4(fbb, values));
    }
    ozz_inv_bind_poses = fbb.CreateVector(poses);
  }

  auto geo_out = IgAsset::CreateDracoGeometry(
      fbb, geo->pos_attrib(), geo->normal_attrib(), geo->tangent_attrib(),
      geo->bitangent_attrib(), geo->texcoord_attrib(), geo->bone_idx_attrib(),
      geo->bone_weight_attrib(), draco_bin, geo->index_format(), ozz_bone_names,
      ozz_inv_bind_poses);
  sads.push_back(IgAsset::CreateSingleAsset(
      fbb, fbb.CreateString(asset_name), IgAsset::SingleAssetData_DracoGeometry,
      geo_out.Union()));
  return true;
}

int main(int argc, char** argv) {
  CLI::App app(
      "igpack-bundle - a tool for bundling IgAsset files into IgPack bundles",
      "igpack-bundle");

  //
  // Input igasset path root (for reading input IgAsset files)
  std::string input_igasset_path_root;
  app.add_option("-i,--input_igasset_path_root", input_igasset_path_root,
                 "Root directory for resolving input igasset file paths")
      ->required(false)
      ->check(CLI::ExistingDirectory)
      ->default_val(std::filesystem::current_path().string());

  //
  // Output path root (for generated *.igpack files)
  std::string igpack_path_root;
  app.add_option("-o,--output_path_root", igpack_path_root,
                 "Root directory used when saving generated *.igasset files")
      ->required(false)
      ->default_val(std::filesystem::current_path().string());

  //
  // Input plan file path (JSON formatted)
  std::string input_plan_file_path;
  app.add_option(
         "input_plan_file", input_plan_file_path,
         "Path of the Indigo asset bundle plan file (*.igpack-plan.json) "
         "that should be processed by this tool")
      ->required(true)
      ->check(CLI::ExistingFile);

  //
  // "Clean" flag - if set, all bundles will be re-generated with no caching.
  bool clean_build = false;
  app.add_flag("-c,--clean", clean_build,
               "If set, all igpacks will be re-generated from scratch (no "
               "caching for outputs from unmodified inputs)")
      ->required(false)
      ->default_val(false);

  //
  // Log level - corresponds to SPDLog levels
  toolutils::LogLevel log_level{toolutils::LogLevel::Warn};
  std::map<std::string, toolutils::LogLevel> log_level_map{
      {"ERROR", toolutils::LogLevel::Error},
      {"WARN", toolutils::LogLevel::Warn},
      {"DEBUG", toolutils::LogLevel::Debug},
      {"INFO", toolutils::LogLevel::Info},
      {"TRACE", toolutils::LogLevel::Trace},
  };
  app.add_option("-l,--log_level", log_level,
                 "Level of verbosity in logs - 'ERROR', 'WARN', 'DEBUG', "
                 "'INFO', 'TRACE'")
      ->required(false)
      ->transform(CLI::CheckedTransformer(log_level_map, CLI::ignore_case))
      ->default_val("WARN");

  CLI11_PARSE(app, argc, argv);

  auto log = spdlog::stdout_color_mt("console");
  spdlog::set_default_logger(log);

  //
  // RAII lifetime concerns for the app...
  toolutils::RuntimeTimer timer(log, "IgpackBundle");

  log->set_level(toolutils::to_spdlog_level(log_level));

  std::filesystem::path input_path = input_igasset_path_root;
  if (!std::filesystem::exists(igpack_path_root) &&
      !std::filesystem::create_directories(igpack_path_root)) {
    log->error("Failed to create igpack output directory {}", igpack_path_root);
    return EXIT_FAILURE;
  }
  if (!std::filesystem::is_directory(igpack_path_root)) {
    log->error("Output path root {} exists but is not a directory",
               igpack_path_root);
    return EXIT_FAILURE;
  }

  std::filesystem::path igpack_output_path = igpack_path_root;

  //
  // Read plan...
  flatbuffers::Parser parser;
  if (!toolutils::ParseEmbeddedSchema(
          parser, log, igpackbundle::embedded_schema::kRootSchemaFilename,
          igpackbundle::embedded_schema::kEmbeddedSchemaFiles)) {
    return EXIT_FAILURE;
  }

  {
    std::ifstream fin(input_plan_file_path);
    if (!fin) {
      log->error("Failed to open {} for reading", input_plan_file_path);
      return EXIT_FAILURE;
    }

    fin.seekg(0, std::ios::end);
    std::string plan_text(fin.tellg(), '\0');
    fin.seekg(0, std::ios::beg);
    plan_text.assign(std::istreambuf_iterator<char>(fin),
                     std::istreambuf_iterator<char>());

    if (!parser.Parse(plan_text.c_str())) {
      log->error("Failed to parse input plan {} - {}", input_plan_file_path,
                 parser.error_);
      return EXIT_FAILURE;
    }
  }

  //
  // Configuration for generators + plan formalization...
  auto plan_file_last_modified =
      std::filesystem::last_write_time(input_plan_file_path);
  const IgPackGen::IgpackGenPlan* igpack_bundle_plan =
      IgPackGen::GetIgpackGenPlan(parser.builder_.GetBufferPointer());

  igpackbundle::PlanInvocationConfig config{};
  config.PlanFileLastModified = plan_file_last_modified;
  config.IgassetPathRoot = input_path;
  config.IgpackPathRoot = igpack_output_path;
  config.CleanBuild = clean_build;

  //
  // Kick off igpack bundling
  log->info("Starting IgPack bundling for {} bundles...",
            igpack_bundle_plan->igpack_gen_actions()->size());
  auto filesystem =
      std::make_shared<toolutils::Filesystem>(log, /* io_task_list= */ nullptr);

  for (const auto* igpack_gen_action :
       *igpack_bundle_plan->igpack_gen_actions()) {
    auto output_path =
        config.IgpackPathRoot / igpack_gen_action->output_path()->str();
    log->info("Bundling {}...", output_path.string());

    //
    // Before allocating memory + performing ops: Check all assets exist, do not
    //  have duplicate names, and perform dirty check (skip if no sources
    //  modified since)
    {
      std::set<std::string> igasset_names;
      std::optional<std::filesystem::file_time_type>
          most_recent_modified_igasset;
      std::optional<std::filesystem::file_time_type> igpack_out_time =
          std::filesystem::exists(output_path)
              ? std::optional(std::filesystem::last_write_time(output_path))
              : std::nullopt;
      for (const auto* igasset_source : *igpack_gen_action->igasset_sources()) {
        auto igasset_path =
            config.IgassetPathRoot / igasset_source->file_path()->str();
        std::string igasset_name = igasset_source->igasset_name() != nullptr
                                       ? igasset_source->igasset_name()->str()
                                       : "";

        if (igasset_name == "") {
          log->error("Igpack for output {} has igasset with no name specified",
                     output_path.string());
          return EXIT_FAILURE;
        }

        if (igasset_names.contains(igasset_name)) {
          log->error(
              "Cannot encode two assets with the same name {}, aborting!",
              igasset_name);
          return EXIT_FAILURE;
        }
        igasset_names.insert(igasset_name);

        if (!std::filesystem::exists(igasset_path)) {
          log->error("No igasset found at {}, aborting!",
                     igasset_path.string());
          return EXIT_FAILURE;
        }

        auto last_modified_time =
            std::filesystem::last_write_time(igasset_path);
        if (!most_recent_modified_igasset.has_value() ||
            last_modified_time > *most_recent_modified_igasset) {
          most_recent_modified_igasset = last_modified_time;
        }
      }

      bool force_write =
          config.CleanBuild || !std::filesystem::exists(output_path);
      bool is_igasset_dirty = most_recent_modified_igasset.has_value() &&
                              igpack_out_time.has_value() &&
                              *most_recent_modified_igasset > *igpack_out_time;
      bool plan_dirty = igpack_out_time.has_value() &&
                        std::filesystem::last_write_time(input_plan_file_path) >
                            *igpack_out_time;

      if (!force_write && !is_igasset_dirty && !plan_dirty) {
        log->info("Skipping up-to-date igpack {}", output_path.string());
        continue;
      }
    }

    flatbuffers::FlatBufferBuilder fbb{};
    std::vector<flatbuffers::Offset<IgAsset::SingleAsset>> sads{};

    for (const auto* igasset_source : *igpack_gen_action->igasset_sources()) {
      auto igasset_path =
          config.IgassetPathRoot / igasset_source->file_path()->str();
      std::string igasset_name = igasset_source->igasset_name()->str();
      log->info("> Packing {} ({})...", igasset_name, igasset_path.string());

      auto raw_igasset = filesystem->read_bin(igasset_path);
      if (!raw_igasset.has_value()) {
        log->error("Could not read igasset at path {}", igasset_path.string());
        return EXIT_FAILURE;
      }

      auto* igasset = ::get_asset(*raw_igasset, igasset_path.string(), log);
      if (igasset == nullptr) {
        log->error("Could not igasset parse file {}", igasset_path.string());
        return EXIT_FAILURE;
      }

      switch (igasset->asset_type()) {
        case IgAsset::SingleAssetData_WgslSource:
          if (!::pack_wgsl_source(fbb, sads, log, *filesystem, igasset_name,
                                  igasset_path,
                                  igasset->asset_as_WgslSource())) {
            return EXIT_FAILURE;
          }
          break;
        case IgAsset::SingleAssetData_Image2D:
          if (!::pack_image2d(fbb, sads, log, *filesystem, igasset_name,
                              igasset_path, igasset->asset_as_Image2D())) {
            return EXIT_FAILURE;
          }
          break;
        case IgAsset::SingleAssetData_DracoGeometry:
          if (!::pack_draco_geo(fbb, sads, log, *filesystem, igasset_name,
                                igasset_path,
                                igasset->asset_as_DracoGeometry())) {
            return EXIT_FAILURE;
          }
          break;
        case IgAsset::SingleAssetData_Spritesheet:
          if (!::pack_spritesheet(fbb, sads, log, *filesystem, igasset_name,
                                  igasset_path,
                                  igasset->asset_as_Spritesheet())) {
            return EXIT_FAILURE;
          }
          break;
        default:
          log->error("Could not process {}: unsupported IgAsset source type",
                     igasset_path.string());
          return EXIT_FAILURE;
      }
    }

    auto assets = fbb.CreateVector(sads);
    auto pack = IgAsset::CreateAssetPack(fbb, assets);
    fbb.Finish(pack);
    if (!filesystem->write_bin(output_path, fbb)) {
      log->error("Failed to write {}!", output_path.string());
      return EXIT_FAILURE;
    }
  }

  return EXIT_SUCCESS;
}
