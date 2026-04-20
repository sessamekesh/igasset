#include <igasset-gen/filesystem.h>
#if IGASSET_ENABLE_BASISU_SUPPORT
#include <encoder/basisu_enc.h>
#endif

#include <flatbuffers/idl.h>
#include <igasset-gen/igasset-generator.h>
#include <igasset-gen/schema/igasset-gen-plan.h>
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

enum class LogLevel : int {
  Error,
  Warn,
  Debug,
  Info,
  Trace,
};

class RuntimeTimer {
 public:
  RuntimeTimer(std::shared_ptr<spdlog::logger> log, std::string task_name)
      : log_(log),
        task_name_(task_name),
        start_(std::chrono::high_resolution_clock::now()) {}
  ~RuntimeTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start_);

    log_->info("Executed {} in {}ms", task_name_,
               static_cast<float>(elapsed.count()) / 1000.f);
  }

 private:
  std::shared_ptr<spdlog::logger> log_;
  std::string task_name_;
  std::chrono::high_resolution_clock::time_point start_;
};

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
  app.add_option("-w,--input_asset_path_root", input_asset_path_root,
                 "Root directory for resolving input asset file paths")
      ->required(false)
      ->default_str(std::filesystem::current_path().string())
      ->check(CLI::ExistingDirectory);

  //
  // Output path root (for generated *.igasset files)
  std::string igasset_path_root;
  app.add_option("-o,--output_path_root", igasset_path_root,
                 "Root directory used when saving generated *.igasset files")
      ->required(false)
      ->default_str(std::filesystem::current_path().string());

  //
  // Input plan file path (JSON formatted)
  std::string input_plan_file_path;
  app.add_option(
         "-i,--input_plan_file", input_plan_file_path,
         "Path of the Indigo asset pack plan file (*.igasset-plan.json) "
         "that should be processed by this tool")
      ->required(true)
      ->check(CLI::ExistingFile);

  //
  // igasset-gen-plan.fbs schema location
  std::string schema_path;
  app.add_option(
         "-s,--schema", schema_path,
         "Path of the schema file igasset-gen-plan.fbs that defines how the "
         "JSON parser should read in igasset-gen-plan files. Assumed to be in "
         "PATH by default, set this value to give a specific location.")
      ->required(false)
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
  LogLevel log_level{LogLevel::Warn};
  std::map<std::string, LogLevel> log_level_map{
      {"ERROR", LogLevel::Error}, {"WARN", LogLevel::Warn},
      {"DEBUG", LogLevel::Debug}, {"INFO", LogLevel::Info},
      {"TRACE", LogLevel::Trace},
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
  RuntimeTimer timer(log, "IgassetGen");
#if IGASSET_ENABLE_BASISU_SUPPORT
  BasisuRaii basisu_raii;
#endif

  switch (log_level) {
    case LogLevel::Error:
      log->set_level(spdlog::level::err);
      break;
    case LogLevel::Warn:
      log->set_level(spdlog::level::warn);
      break;
    case LogLevel::Debug:
      log->set_level(spdlog::level::debug);
      break;
    case LogLevel::Info:
      log->set_level(spdlog::level::info);
      break;
    case LogLevel::Trace:
      log->set_level(spdlog::level::trace);
      break;
  }

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
  {
    // Copied per CMake file instruction - must be in the same directory as
    // binary
    if (schema_path.empty()) {
      schema_path = "igasset-gen-plan.fbs";
    }

    std::filesystem::path const schema_file_path =
        std::filesystem::absolute(schema_path);
    std::filesystem::path const schema_include_root = schema_file_path.parent_path();
    std::string const schema_include_root_posix =
        schema_include_root.generic_string();
    std::string const schema_file_posix = schema_file_path.generic_string();

    std::ifstream fin(schema_file_path);
    if (!fin) {
      log->error("Failed to read igasset-gen-plan.fbs schema file");
      return EXIT_FAILURE;
    }

    fin.seekg(0, std::ios::end);
    std::string schema_text(fin.tellg(), '\0');
    fin.seekg(0, std::ios::beg);
    schema_text.assign(std::istreambuf_iterator<char>(fin),
                       std::istreambuf_iterator<char>());

    // Resolves include "types.fbs" / include "igasset.fbs" (same directory as
    // igasset-gen-plan.fbs). Paths must use POSIX separators per FlatBuffers.
    const char* schema_include_paths[] = {schema_include_root_posix.c_str(),
                                          nullptr};
    if (!parser.Parse(schema_text.c_str(), schema_include_paths,
                      schema_file_posix.c_str())) {
      log->error("Failed to parse igasset-gen-plan.fbs schema file: {}",
                 parser.error_);
      return EXIT_FAILURE;
    }
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
      std::make_shared<igassetgen::Filesystem>(log, io_task_list, config);
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
