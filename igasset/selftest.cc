#include <flatbuffers/flatbuffer_builder.h>
#include <igasset/image2d.h>
#include <igasset/schema/igasset.h>

#if IGASSET_ENABLE_BASISU_SUPPORT
#include <encoder/basisu_comp.h>
#include <encoder/basisu_enc.h>
#include <transcoder/basisu.h>
#include <transcoder/basisu_transcoder.h>
#endif

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* what) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
  }
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
}

const IgAsset::Image2D* as_image2d(const flatbuffers::FlatBufferBuilder& fbb) {
  return IgAsset::GetAsset(fbb.GetBufferPointer())->asset_as_Image2D();
}

// Builds an uncompressed Image2D asset with recognizable per-pixel bytes.
bool make_rgba8_asset(int w, int h, flatbuffers::FlatBufferBuilder& fbb) {
  std::vector<std::uint8_t> pixels;
  pixels.reserve(static_cast<std::size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      pixels.push_back(static_cast<std::uint8_t>(x));
      pixels.push_back(static_cast<std::uint8_t>(y));
      pixels.push_back(0x80);
      pixels.push_back(0xFF);
    }
  }

  auto data = fbb.CreateVector(pixels);
  auto image = IgAsset::CreateImage2D(fbb, IgAsset::Image2DEncoding_RGBA8Unorm,
                                      w, h, data);
  auto asset = IgAsset::CreateAsset(fbb, IgAsset::SingleAssetData_Image2D,
                                    image.Union());
  fbb.Finish(asset);
  return true;
}

#if IGASSET_ENABLE_BASISU_SUPPORT

// Builds an ETC1S (basisu supercompressed) Image2D asset from a tiny
// generated image, exercising the decoder's transcode path.
bool make_etc1s_asset(int w, int h, flatbuffers::FlatBufferBuilder& fbb) {
  basisu::image img(w, h);
  for (int x = 0; x < w; ++x) {
    for (int y = 0; y < h; ++y) {
      img(x, y).set(static_cast<std::uint8_t>(x * 40), 0x80,
                    static_cast<std::uint8_t>(y * 40), 0xFF);
    }
  }
  basisu::vector<basisu::image> images;
  images.push_back(std::move(img));

  std::size_t basisu_file_size = 0;
  void* basisu_data =
      basisu::basis_compress(basist::basis_tex_format::cETC1S, images, 0, 0.f,
                             &basisu_file_size, nullptr);
  if (basisu_data == nullptr) {
    return false;
  }

  auto data =
      fbb.CreateVector(reinterpret_cast<const std::uint8_t*>(basisu_data),
                       static_cast<flatbuffers::uoffset_t>(basisu_file_size));
  basisu::basis_free_data(basisu_data);
  auto image =
      IgAsset::CreateImage2D(fbb, IgAsset::Image2DEncoding_ETC1S, w, h, data);
  auto asset = IgAsset::CreateAsset(fbb, IgAsset::SingleAssetData_Image2D,
                                    image.Union());
  fbb.Finish(asset);
  return true;
}

#endif  // IGASSET_ENABLE_BASISU_SUPPORT

void test_unpack_uncompressed() {
  std::printf("RGBA8Unorm-stored image (issue #18)\n");
  flatbuffers::FlatBufferBuilder fbb;
  make_rgba8_asset(4, 4, fbb);
  const IgAsset::Image2D* fb_image = as_image2d(fbb);

  auto as_rgba =
      igasset::Image2D::Unpack(fb_image, igasset::Image2DFormat::RGBA8Unorm);
  check(as_rgba.has_value() &&
            as_rgba->format == igasset::Image2DFormat::RGBA8Unorm &&
            as_rgba->width == 4 && as_rgba->height == 4 &&
            as_rgba->data.size() == 4 * 4 * 4,
        "RGBA8Unorm request succeeds with raw data");

  // Uncompressed data has no compressed representation to transcode: any
  // format request must return the raw RGBA8Unorm data (issue #18: this
  // used to always fail).
  auto as_bc7 = igasset::Image2D::Unpack(fb_image, igasset::Image2DFormat::BC7);
  check(as_bc7.has_value() &&
            as_bc7->format == igasset::Image2DFormat::RGBA8Unorm,
        "BC7 request returns RGBA8Unorm data (actual format reported)");
  check(as_rgba.has_value() && as_bc7.has_value() &&
            as_rgba->data == as_bc7->data,
        "BC7 request returns identical bytes");

  auto as_astc =
      igasset::Image2D::Unpack(fb_image, igasset::Image2DFormat::ASTC);
  check(as_astc.has_value() &&
            as_astc->format == igasset::Image2DFormat::RGBA8Unorm &&
            as_astc->data.size() == 4 * 4 * 4,
        "ASTC request returns RGBA8Unorm data");
}

#if IGASSET_ENABLE_BASISU_SUPPORT

void test_unpack_compressed() {
  std::printf("ETC1S-stored image (transcode control path)\n");
  flatbuffers::FlatBufferBuilder fbb;
  if (!make_etc1s_asset(4, 4, fbb)) {
    check(false, "in-test basisu compression");
    return;
  }
  const IgAsset::Image2D* fb_image = as_image2d(fbb);

  auto as_rgba =
      igasset::Image2D::Unpack(fb_image, igasset::Image2DFormat::RGBA8Unorm);
  check(as_rgba.has_value() &&
            as_rgba->format == igasset::Image2DFormat::RGBA8Unorm &&
            as_rgba->width == 4 && as_rgba->height == 4 &&
            !as_rgba->data.empty(),
        "RGBA8Unorm request transcodes");

  auto as_bc7 = igasset::Image2D::Unpack(fb_image, igasset::Image2DFormat::BC7);
  check(as_bc7.has_value() && as_bc7->format == igasset::Image2DFormat::BC7,
        "BC7 request transcodes to BC7");

  auto as_astc =
      igasset::Image2D::Unpack(fb_image, igasset::Image2DFormat::ASTC);
  check(as_astc.has_value() && as_astc->format == igasset::Image2DFormat::ASTC,
        "ASTC request transcodes to ASTC");
}

#endif  // IGASSET_ENABLE_BASISU_SUPPORT

}  // namespace

int main() {
  test_unpack_uncompressed();
#if IGASSET_ENABLE_BASISU_SUPPORT
  basisu::basisu_encoder_init();
  test_unpack_compressed();
  basisu::basisu_encoder_deinit();
#endif

  std::printf(g_failures == 0 ? "ALL PASS (%d checks)\n" : "FAILURES (%d)\n",
              g_failures == 0 ? g_checks : g_failures);
  return g_failures == 0 ? 0 : 1;
}