// Self-test for staged-asset-io.
//
// Not integrated with any test framework on purpose - the repo has no C++
// unit test infrastructure. This binary uses plain checks with a failure
// counter and exits non-zero if anything fails.
//
// Run manually after a build:
//   ./build/tools/staged-asset-io/staged-asset-io-selftest

#include <staged-asset-io/assimp-import.h>
#include <staged-asset-io/assimp-io-system.h>
#include <staged-asset-io/reference-normalize.h>
#include <staged-asset-io/staged-asset-manifest.h>
#include <staged-asset-io/staged-filesystem.h>

#include <assimp/material.h>
#include <assimp/scene.h>
#include <flatbuffers/flatbuffer_builder.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const std::string& what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::cout << "  FAIL: " << what << std::endl;
  }
}

std::optional<std::string> norm(std::string_view base, std::string_view ref) {
  return stagedio::normalize_logical_reference(base, ref);
}

bool has_record(const std::vector<stagedio::FileAccessRecord>& records,
                const std::string& path, bool allowed) {
  for (const auto& record : records) {
    if (record.allowed == allowed &&
        record.logical_path.find(path) != std::string::npos) {
      return true;
    }
  }
  return false;
}

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream fout(path, std::ios::binary);
  fout.write(content.data(), static_cast<std::streamsize>(content.size()));
}

void test_reference_normalization() {
  std::cout << "reference normalization" << std::endl;

  check(norm("", "foo.png") == std::optional<std::string>("foo.png"),
        "plain name");
  check(norm("", "./foo.png") == std::optional<std::string>("foo.png"),
        "leading ./");
  check(norm("models/", "scene.bin") ==
            std::optional<std::string>("models/scene.bin"),
        "base dir join");
  check(norm("models/", "./textures/../scene.bin") ==
            std::optional<std::string>("models/scene.bin"),
        "inner .. resolution");
  check(norm("a/b/", "c.png") == std::optional<std::string>("a/b/c.png"),
        "multi-segment base");
  check(norm("models/", "sub\\tex.png") ==
            std::optional<std::string>("models/sub/tex.png"),
        "backslash separators");
  check(norm("models/", "%73cene.bin") ==
            std::optional<std::string>("models/scene.bin"),
        "percent decoding");
  check(norm("", "file://foo.png") == std::optional<std::string>("foo.png"),
        "file:// scheme strip");

  check(!norm("", "").has_value(), "empty reference rejected");
  check(!norm("", "/abs.png").has_value(), "absolute reference rejected");
  check(!norm("", "C:\\abs.png").has_value(), "drive path rejected");
  check(norm("models/", "../escape.png") ==
            std::optional<std::string>("escape.png"),
        "single .. stays in logical namespace");
  check(!norm("models/", "../../escape.png").has_value(),
        "escaping .. rejected");
  check(!norm("", "..").has_value(), "bare .. rejected");
  check(!norm("", "%2Fetc").has_value(), "encoded absolute separator rejected");

  check(stagedio::logical_parent_dir("models/scene.gltf") == "models/",
        "parent dir with slash");
  check(stagedio::logical_parent_dir("scene.gltf") == "",
        "parent dir of bare name");
}

void test_manifest_parsing(const std::shared_ptr<spdlog::logger>& log) {
  std::cout << "manifest parsing" << std::endl;

  const std::string valid = R"({
    "entries": [
      { "logical_path": "models/scene.obj", "physical_path": "uuid-obj", "s3_uuid": "s3-1" },
      { "logical_path": "models/scene.mtl", "physical_path": "uuid-mtl" }
    ]
  })";
  auto manifest = stagedio::StagedAssetManifest::from_json(valid, log);
  check(manifest != nullptr, "valid manifest parses");
  check(manifest != nullptr && manifest->size() == 2, "valid manifest size");
  check(manifest != nullptr &&
            *manifest->physical_for("models/scene.obj") == "uuid-obj",
        "mapped lookup");
  check(manifest != nullptr && manifest->physical_for("nope.bin") == nullptr,
        "unmapped lookup is null");
  check(manifest != nullptr &&
            manifest->physical_for("models/./scene.obj") == nullptr,
        "lookups require normalized keys (callers normalize first)");

  check(stagedio::StagedAssetManifest::from_json("{ nope", log) == nullptr,
        "invalid json rejected");
  check(
      stagedio::StagedAssetManifest::from_json(
          R"({ "entries": [ { "logical_path": "/abs", "physical_path": "x" } ] })",
          log) == nullptr,
      "absolute logical path rejected");
  check(
      stagedio::StagedAssetManifest::from_json(
          R"({ "entries": [ { "logical_path": "../up", "physical_path": "x" } ] })",
          log) == nullptr,
      "escaping logical path rejected");
  check(
      stagedio::StagedAssetManifest::from_json(
          R"({ "entries": [ { "logical_path": "a/b", "physical_path": "sub/x" } ] })",
          log) == nullptr,
      "subdirectory physical name rejected");
  check(
      stagedio::StagedAssetManifest::from_json(
          R"({ "entries": [ { "logical_path": "a", "physical_path": ".." } ] })",
          log) == nullptr,
      "traversal physical name rejected");
  check(stagedio::StagedAssetManifest::from_json(
            R"({ "entries": [ { "logical_path": "a", "physical_path": "x" },
                               { "logical_path": "a", "physical_path": "y" } ] })",
            log) == nullptr,
        "duplicate logical paths rejected");
  check(stagedio::StagedAssetManifest::from_json(
            R"({ "entries": [ { "logical_path": "a" } ] })", log) == nullptr,
        "missing required physical_path rejected");
}

void test_direct_filesystem(const std::shared_ptr<spdlog::logger>& log,
                            const std::filesystem::path& tmp) {
  std::cout << "direct policy filesystem" << std::endl;

  const auto in_root = tmp / "direct-in";
  const auto out_root = tmp / "direct-out";
  write_file(in_root / "hello.txt", "hi");

  auto fs = stagedio::StagedFilesystem::direct(in_root, out_root, log);
  check(fs != nullptr, "direct construction");
  check(fs->policy() == stagedio::IoPolicy::Direct, "direct policy flag");

  check(fs->resolve_input("hello.txt") == in_root / "hello.txt",
        "direct input join");
  check(fs->resolve_input("/etc/passwd") ==
            std::optional<std::filesystem::path>("/etc/passwd"),
        "direct absolute path overrides root (historical behavior)");
  check(fs->read_input("hello.txt") == std::optional<std::string>("hi"),
        "direct read");

  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(fbb.CreateString("hello"));
  check(fs->write_output("sub/out.bin", fbb), "direct write succeeds");
  check(std::filesystem::exists(out_root / "sub" / "out.bin"),
        "direct write file exists");
}

void test_staged_filesystem(const std::shared_ptr<spdlog::logger>& log,
                            const std::filesystem::path& tmp) {
  std::cout << "staged policy filesystem" << std::endl;

  const auto staging_root = tmp / "staged";
  const auto out_root = tmp / "staged-out";
  write_file(staging_root / "uuid-obj", "obj-bytes");
  write_file(staging_root / "uuid-mtl", "mtl-bytes");

  const std::string manifest_json = R"({
    "entries": [
      { "logical_path": "scene.obj", "physical_path": "uuid-obj" },
      { "logical_path": "scene.mtl", "physical_path": "uuid-mtl" }
    ]
  })";
  auto manifest = stagedio::StagedAssetManifest::from_json(manifest_json, log);
  check(manifest != nullptr, "staged manifest loads");

  auto fs =
      stagedio::StagedFilesystem::staged(staging_root, out_root, manifest, log);
  check(fs != nullptr, "staged construction");
  check(fs->policy() == stagedio::IoPolicy::Staged, "staged policy flag");
  check(stagedio::StagedFilesystem::staged(staging_root, out_root, nullptr,
                                           log) == nullptr,
        "staged construction without manifest fails");

  check(fs->read_input("scene.obj") == std::optional<std::string>("obj-bytes"),
        "staged mapped read");
  check(fs->resolve_input("scene.obj") == staging_root / "uuid-obj",
        "staged resolve maps to physical file");

  check(!fs->read_input("missing.bin").has_value(),
        "staged unmapped read denied");
  check(!fs->read_input("../uuid-obj").has_value(),
        "staged traversal read denied");
  check(!fs->read_input("/etc/passwd").has_value(),
        "staged absolute read denied");
  check(!fs->resolve_input("..\\uuid-obj").has_value(),
        "staged backslash traversal denied");

  const auto denied_records = fs->journal()->snapshot();
  check(has_record(denied_records, "missing.bin", false),
        "journal records unmapped denial");
  check(has_record(denied_records, "../uuid-obj", false),
        "journal records traversal denial");
  check(has_record(denied_records, "scene.obj", true),
        "journal records allowed read");

  flatbuffers::FlatBufferBuilder fbb;
  fbb.Finish(fbb.CreateString("hello"));
  check(fs->write_output("models/out.bin", fbb), "staged contained write");
  check(std::filesystem::exists(out_root / "models" / "out.bin"),
        "staged write landed under output root");
  check(!fs->write_output("../escape.bin", fbb),
        "staged escaping write denied");
  check(!fs->write_output("/abs.bin", fbb), "staged absolute write denied");
}

void test_staged_assimp_import(const std::shared_ptr<spdlog::logger>& log,
                               const std::filesystem::path& tmp) {
  std::cout << "staged assimp import" << std::endl;

  const auto staging_root = tmp / "assimp-staged";
  const std::string obj_content =
      "mtllib scene.mtl\n"
      "v 0 0 0\n"
      "v 1 0 0\n"
      "v 0 1 0\n"
      "vt 0 0\n"
      "usemtl red\n"
      "f 1/1 2/1 3/1\n";
  const std::string mtl_content = "newmtl red\nKd 1 0 0\n";
  write_file(staging_root / "uuid-obj", obj_content);
  write_file(staging_root / "uuid-mtl", mtl_content);

  const std::string manifest_json = R"({
    "entries": [
      { "logical_path": "scene.obj", "physical_path": "uuid-obj" },
      { "logical_path": "scene.mtl", "physical_path": "uuid-mtl" }
    ]
  })";
  auto manifest = stagedio::StagedAssetManifest::from_json(manifest_json, log);
  check(manifest != nullptr, "assimp staged manifest loads");

  auto journal = std::make_shared<stagedio::FileAccessJournal>();
  {
    auto* io = new stagedio::AssimpStagedIOSystem(staging_root, manifest,
                                                  journal, log);
    auto scene = stagedio::load_scene(std::filesystem::path("scene.obj"),
                                      "scene.obj", log, io);
    check(scene != nullptr, "staged import succeeds via logical name");
    check(scene != nullptr && scene->scene != nullptr &&
              scene->scene->mNumMeshes == 1,
          "staged import produced 1 mesh");
    check(scene != nullptr && scene->scene != nullptr &&
              scene->scene->mNumMaterials == 1 &&
              std::string(scene->scene->mMaterials[0]->GetName().C_Str())
                      .find("red") != std::string::npos,
          "staged import parsed referenced mtl (material 'red')");
    {
      aiColor4D diffuse{};
      check(scene != nullptr && scene->scene != nullptr &&
                aiGetMaterialColor(scene->scene->mMaterials[0],
                                   AI_MATKEY_COLOR_DIFFUSE,
                                   &diffuse) == aiReturn_SUCCESS &&
                diffuse.r > 0.99f && diffuse.g < 0.01f && diffuse.b < 0.01f,
            "staged mtl content reached the importer (Kd 1 0 0)");
    }
  }  // Importer (and its IOSystem) destroyed here.

  const auto records = journal->snapshot();
  check(has_record(records, "scene.obj", true), "journal records main file");
  check(has_record(records, "scene.mtl", true),
        "journal records referenced mtl (outlives IOSystem)");

  // Denial: manifest without the mtl mapping. The OBJ importer tolerates a
  // missing mtl file (imports geometry without materials), but the denied
  // access must be recorded either way - and never read from disk.
  const std::string manifest_no_mtl_json = R"({
    "entries": [
      { "logical_path": "scene.obj", "physical_path": "uuid-obj" }
    ]
  })";
  auto manifest_no_mtl =
      stagedio::StagedAssetManifest::from_json(manifest_no_mtl_json, log);
  check(manifest_no_mtl != nullptr, "no-mtl manifest loads");

  auto journal2 = std::make_shared<stagedio::FileAccessJournal>();
  {
    auto* io = new stagedio::AssimpStagedIOSystem(staging_root, manifest_no_mtl,
                                                  journal2, log);
    auto scene = stagedio::load_scene(std::filesystem::path("scene.obj"),
                                      "scene.obj", log, io);
    check(has_record(journal2->snapshot(), "scene.mtl", false),
          "denied mtl access recorded");
    check(scene != nullptr,
          "OBJ import tolerates denied mtl (no crash / no read)");
    {
      // The OBJ importer creates a placeholder material named "red" when
      // the mtl cannot be read - but it keeps the default diffuse color,
      // proving no mtl content leaked through.
      aiColor4D diffuse{};
      check(scene != nullptr && scene->scene != nullptr &&
                scene->scene->mNumMaterials >= 1 &&
                std::string(scene->scene->mMaterials[0]->GetName().C_Str())
                        .find("red") != std::string::npos &&
                aiGetMaterialColor(scene->scene->mMaterials[0],
                                   AI_MATKEY_COLOR_DIFFUSE,
                                   &diffuse) == aiReturn_SUCCESS &&
                diffuse.r > 0.5f && diffuse.g > 0.5f && diffuse.b > 0.5f,
            "denied mtl yields placeholder material (default diffuse)");
    }
  }

  // Direct policy recording: identical import, real paths, recorded opens.
  const auto direct_dir = tmp / "assimp-direct";
  write_file(direct_dir / "scene.obj", obj_content);
  write_file(direct_dir / "scene.mtl", mtl_content);

  auto journal3 = std::make_shared<stagedio::FileAccessJournal>();
  {
    auto* io = new stagedio::AssimpRecordingIOSystem(journal3);
    auto scene =
        stagedio::load_scene(direct_dir / "scene.obj", "scene.obj", log, io);
    check(scene != nullptr, "direct import succeeds");
    const auto direct_records = journal3->snapshot();
    check(has_record(direct_records, "scene.obj", true),
          "direct journal records main file");
    check(has_record(direct_records, "scene.mtl", true),
          "direct journal records referenced mtl");
  }

  // Nested logical names: the model lives in a logical subdirectory and
  // references its mtl with a RAW reference (the OBJ importer does not
  // qualify the mtllib name; the glTF importers prepend the model's own
  // directory). The IOSystem must resolve the request relative to the
  // model's base_dir - and still accept the fully-qualified main-file
  // open.
  const std::string manifest_nested_json = R"({
    "entries": [
      { "logical_path": "sub/scene.obj", "physical_path": "uuid-obj" },
      { "logical_path": "sub/scene.mtl", "physical_path": "uuid-mtl" }
    ]
  })";
  auto manifest_nested =
      stagedio::StagedAssetManifest::from_json(manifest_nested_json, log);
  check(manifest_nested != nullptr, "nested manifest loads");

  auto journal4 = std::make_shared<stagedio::FileAccessJournal>();
  {
    auto* io = new stagedio::AssimpStagedIOSystem(staging_root, manifest_nested,
                                                  journal4, log, "sub/");
    auto scene = stagedio::load_scene(std::filesystem::path("sub/scene.obj"),
                                      "sub/scene.obj", log, io);
    check(scene != nullptr, "nested staged import succeeds");
    {
      aiColor4D diffuse{};
      check(scene != nullptr && scene->scene != nullptr &&
                scene->scene->mNumMaterials >= 1 &&
                aiGetMaterialColor(scene->scene->mMaterials[0],
                                   AI_MATKEY_COLOR_DIFFUSE,
                                   &diffuse) == aiReturn_SUCCESS &&
                diffuse.r > 0.99f && diffuse.g < 0.01f && diffuse.b < 0.01f,
            "raw mtllib reference resolved via base_dir (Kd 1 0 0)");
    }
    const auto nested_records = journal4->snapshot();
    check(has_record(nested_records, "sub/scene.obj", true),
          "nested journal records main file (qualified request)");
    check(has_record(nested_records, "sub/scene.mtl", true),
          "nested journal records mtl (base_dir-resolved request)");
  }

  // Nested denial: the manifest maps only the model; the raw mtllib
  // reference must be denied and recorded under its model-relative name.
  const std::string manifest_nested_no_mtl_json = R"({
    "entries": [
      { "logical_path": "sub/scene.obj", "physical_path": "uuid-obj" }
    ]
  })";
  auto manifest_nested_no_mtl = stagedio::StagedAssetManifest::from_json(
      manifest_nested_no_mtl_json, log);
  check(manifest_nested_no_mtl != nullptr, "nested no-mtl manifest loads");

  auto journal5 = std::make_shared<stagedio::FileAccessJournal>();
  {
    auto* io = new stagedio::AssimpStagedIOSystem(
        staging_root, manifest_nested_no_mtl, journal5, log, "sub/");
    auto scene = stagedio::load_scene(std::filesystem::path("sub/scene.obj"),
                                      "sub/scene.obj", log, io);
    check(scene != nullptr, "nested import tolerates denied mtl");
    check(has_record(journal5->snapshot(), "sub/scene.mtl", false),
          "nested denied mtl recorded under model-relative name");
  }
}

void test_manifest_extra_entry(const std::shared_ptr<spdlog::logger>& log) {
  std::cout << "manifest extra entries" << std::endl;

  const std::string base_json = R"({
    "entries": [
      { "logical_path": "scene.obj", "physical_path": "uuid-obj" }
    ]
  })";
  auto manifest = stagedio::StagedAssetManifest::from_json(base_json, log);
  check(manifest != nullptr, "base manifest loads");

  auto extended = manifest->with_extra_entry("new/file.png", "uuid-png", log);
  check(extended != nullptr, "with_extra_entry adds entry");
  check(extended != nullptr &&
            extended->physical_for("new/file.png") != nullptr &&
            *extended->physical_for("new/file.png") == "uuid-png",
        "extra entry maps");
  check(extended != nullptr && extended->physical_for("scene.obj") != nullptr,
        "existing entries preserved");
  check(extended != nullptr && extended->size() == 2, "extra entry size");

  auto identical = manifest->with_extra_entry("scene.obj", "uuid-obj", log);
  check(identical != nullptr, "identical duplicate tolerated");

  auto conflict = manifest->with_extra_entry("scene.obj", "other-uuid", log);
  check(conflict == nullptr, "conflicting duplicate rejected");

  check(manifest->with_extra_entry("/abs.png", "uuid", log) == nullptr,
        "invalid extra logical path rejected");
  check(manifest->with_extra_entry("ok.png", "../escape", log) == nullptr,
        "invalid extra physical name rejected");

  check(manifest->size() == 1, "original manifest unchanged");
}

}  // namespace

int main() {
  auto log = spdlog::stdout_color_mt("staged-asset-io-selftest");
  log->set_level(spdlog::level::err);

  const auto tmp =
      std::filesystem::temp_directory_path() /
      ("staged-asset-io-selftest-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(tmp);

  test_reference_normalization();
  test_manifest_parsing(log);
  test_manifest_extra_entry(log);
  test_direct_filesystem(log, tmp);
  test_staged_filesystem(log, tmp);
  test_staged_assimp_import(log, tmp);

  std::error_code ec;
  std::filesystem::remove_all(tmp, ec);

  std::cout << (g_failures == 0 ? "ALL PASS" : "FAILURES") << " (" << g_checks
            << " checks)" << std::endl;
  return g_failures == 0 ? 0 : 1;
}