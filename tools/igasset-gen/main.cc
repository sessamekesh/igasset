#if IGASSET_ENABLE_BASISU_SUPPORT
#include <encoder/basisu_enc.h>
#endif
#include <tool-utils/embedded-schema.h>
#include <tool-utils/filesystem.h>
#include <tool-utils/log-level.h>
#include <tool-utils/runtime-timer.h>

#include <flatbuffers/idl.h>
#include <igasset-gen/igasset-generator.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
#include <igasset-gen/schema/igasset-gen-plan-embedded.h>
#include <igasync/promise_combiner.h>
#include <igasync/task_list.h>
#include <igasync/thread_pool.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <CLI/App.hpp>
#include <CLI/CLI.hpp>

#include <igasset-gen/exec-config.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>

#if IGASSET_ENABLE_BASISU_SUPPORT
class BasisuRaii {
 public:
  BasisuRaii() {
    basisu::basisu_encoder_init();
  }
  ~BasisuRaii() {
    basisu::basisu_encoder_deinit();
  }
};
#endif

int main(int argc, char** argv) {
  CLI::App app{"igasset-gen - a tool for generating IgAsset files",
               "igasset-gen"};

  //
  // Input asset path root (for reading input asset files)
  std::string input_asset_path_root;
  app.add_option("-i,--input_asset_path_root", input_asset_path_root,
                 "Root directory for resolving input asset file paths")
      ->required(false)
      ->check(CLI::ExistingDirectory)
      ->default_val(std::filesystem::current_path().string());

  //
  // Output path root (for generated *.igasset files)
  std::string igasset_path_root;
  app.add_option("-o,--output_path_root", igasset_path_root,
                 "Root directory used when saving generated *.igasset files")
      ->required(false)
      ->default_val(std::filesystem::current_path().string());

  //
  // Input plan file path (JSON formatted)
  std::string input_plan_file_path;
  app.add_option(
         "input_plan_file", input_plan_file_path,
         "Path of the Indigo asset pack plan file (*.igasset-plan.json) "
         "that should be processed by this tool")
      ->required(true)
      ->check(CLI::ExistingFile);

  //
  // "Clean" flag - if set, all assets will be re-generated with no caching
  bool clean_build = false;
  app.add_flag("-c,--clean", clean_build,
               "If set, all igassets will be re-generated from "
               "scratch (no caching for outputs from unmodified inputs)")
      ->required(false)
      ->default_val(false);

  //
  // Single threaded flag - if set, everything runs on the main thread.
  //  If not set (default), two thread pools will be created - one for I/O
  //  and another for processing (encoding/decoding/transforming resources)
  bool single_threaded = false;
  app.add_flag("--single-threaded", single_threaded,
               "If set, IgassetGen will run on a single thread (useful for "
               "debugging)")
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
  toolutils::RuntimeTimer timer(log, "IgassetGen");
#if IGASSET_ENABLE_BASISU_SUPPORT
  BasisuRaii basisu_raii;
#endif

  log->set_level(toolutils::to_spdlog_level(log_level));

  //
  // Validate args...
  std::filesystem::path input_path = input_asset_path_root;
  if (!std::filesystem::is_directory(igasset_path_root) ||
      !std::filesystem::exists(igasset_path_root)) {
    if (!std::filesystem::create_directories(igasset_path_root)) {
      log->error("Failed to create igasset output directory {}",
                 igasset_path_root);
      return EXIT_FAILURE;
    }
  }

  std::filesystem::path igasset_output_path = igasset_path_root;

  //
  // Read plan...
  flatbuffers::Parser parser;
  if (!toolutils::ParseEmbeddedSchema(
          parser, log, igassetgen::embedded_schema::kRootSchemaFilename,
          igassetgen::embedded_schema::kEmbeddedSchemaFiles)) {
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
  const IgAssetGen::IgAssetGenPlan* igasset_gen_plan =
      IgAssetGen::GetIgAssetGenPlan(parser.builder_.GetBufferPointer());

  igassetgen::PlanInvocationConfig config{};
  config.PlanFileLastModified = plan_file_last_modified;
  config.InputAssetPathRoot = input_path;
  config.IgassetPathRoot = igasset_output_path;
  config.CleanBuild = clean_build;

  //
  // Set up thread pools for I/O and processing
  igasync::TaskList::Desc io_task_list_desc{};
  io_task_list_desc.QueueSizeHint = 16;
  io_task_list_desc.EnqueueListenerSizeHint = 1;

  igasync::TaskList::Desc exec_task_list_desc{};
  exec_task_list_desc.QueueSizeHint = 32;
  exec_task_list_desc.EnqueueListenerSizeHint = 1;

  auto io_task_list = igasync::TaskList::Create(io_task_list_desc);
  auto exec_task_list = igasync::TaskList::Create(exec_task_list_desc);

  // Kept here for scope!
  std::shared_ptr<igasync::ThreadPool> io_thread_pool = nullptr;
  std::shared_ptr<igasync::ThreadPool> exec_thread_pool = nullptr;
  if (!single_threaded) {
    igasync::ThreadPool::Desc io_thread_pool_desc{};
    io_thread_pool_desc.UseHardwareConcurrency = false;
    io_thread_pool_desc.AdditionalThreads = 2;

    igasync::ThreadPool::Desc exec_thread_pool_desc{};
    exec_thread_pool_desc.UseHardwareConcurrency = true;
    exec_thread_pool_desc.AdditionalThreads = 0;

    io_thread_pool = igasync::ThreadPool::Create(io_thread_pool_desc);
    exec_thread_pool = igasync::ThreadPool::Create(exec_thread_pool_desc);
    io_thread_pool->add_task_list(io_task_list);
    exec_thread_pool->add_task_list(exec_task_list);
  }

  //
  // Kick off igasset generation
  log->info("Starting IgAsset generation for {} assets...",
            igasset_gen_plan->actions()->size());
  auto filesystem =
      std::make_shared<toolutils::Filesystem>(log, io_task_list);
  igassetgen::IgassetGenerator generator(log, config, io_task_list,
                                         exec_task_list, filesystem);

  auto igasset_combiner = igasync::PromiseCombiner::Create();
  bool igassets_finished = false;
  bool all_igassets_success = false;

  std::vector<igasync::PromiseCombiner::PromiseKey<bool, false>>
      igasset_pc_keys;
  for (const auto* action : *igasset_gen_plan->actions()) {
    igasset_pc_keys.push_back(igasset_combiner->add(
        generator.generate_igasset(action), exec_task_list));
  }
  auto igassets_finished_promise =
      igasset_combiner
          ->combine(
              [log, &igasset_pc_keys, igassets_finished, &all_igassets_success](
                  igasync::PromiseCombiner::Result rsl) -> void {
                log->info("IgAssets finished generating!");
                all_igassets_success = true;
                for (const auto& igasset_rsl_key : igasset_pc_keys) {
                  all_igassets_success =
                      all_igassets_success && rsl.get(igasset_rsl_key);
                }
              },
              exec_task_list)
          ->then([&igassets_finished]() { igassets_finished = true; },
                 exec_task_list);

  //
  // Spin on exec_task_list from the main thread until all tasks are finished
  while (!igassets_finished) {
    if (single_threaded) {
      while (io_task_list->execute_next());
    }

    exec_task_list->execute_next();
  }

  if (!all_igassets_success) {
    log->error("IgAssetGen errors detected - aborting!");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
