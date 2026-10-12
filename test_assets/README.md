# Test fixtures

Hand-authored fixtures (`triangle.obj` + `triangle.mtl`, `scene.gltf` +
`scene.bin`, `tex.png`, `entry-points.wgsl`) were written for this
repository's test suites and are free to use.

`box.fbx` is copied from the Assimp library's vendored test suite
(`test/models/FBX/box.fbx` at assimp v6.0.5, fetched via CMake
FetchContent into `build/_deps/assimp-src`), because this environment
has no Blender and this build has Assimp exporters disabled, so no
smaller binary FBX could be produced locally. It ships with Assimp
(BSD-3-Clause) as test data.
