#ifndef EXTRACT_ASSET_METADATA_REPORT_H
#define EXTRACT_ASSET_METADATA_REPORT_H

#include <extract-asset-metadata/metadata.h>

#include <ostream>

namespace assetmeta {

// Human-readable report (default output), styled after the enumerate-*
// tools ("------- Asset Metadata -------").
void render_text(const AssetMetadata& metadata, std::ostream& out);

// Machine-readable single-object JSON report (--json). Hand-rolled
// printer (D10): strict escaping of '"', '\' and control characters;
// invalid UTF-8 is stripped from every echoed string (section 5.6). The
// JSON schema is the union of both report modes - every run reports its
// mode, and per-kind sections appear only when applicable.
void render_json(const AssetMetadata& metadata, std::ostream& out);

}  // namespace assetmeta

#endif