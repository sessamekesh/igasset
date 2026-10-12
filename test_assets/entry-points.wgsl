// igasset test fixture: WGSL with detectable entry points.

struct VsOut {
    @builtin(position) pos: vec4f,
}

@vertex
fn vs_main(@location(0) in_pos: vec3f) -> VsOut {
    var out: VsOut;
    out.pos = vec4f(in_pos, 1.0);
    return out;
}

@fragment
fn fs_main(@location(0) in_color: vec4f) -> @location(0) vec4f {
    return in_color;
}
