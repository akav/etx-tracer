﻿#pragma once

#include <etx/render/shared/spectrum.hxx>
#include <etx/render/shared/camera.hxx>
#include <etx/render/shared/image.hxx>
#include <etx/render/shared/medium.hxx>
#include <etx/render/shared/material.hxx>
#include <etx/render/shared/emitter.hxx>
#include <etx/render/shared/sampler.hxx>
#include <etx/render/shared/bsdf.hxx>

namespace etx {

struct alignas(16) EnvironmentEmitters {
  constexpr static const uint32_t kMaxCount = 7;
  uint32_t emitters[kMaxCount];
  uint32_t count;
};

struct alignas(16) Scene {
  Camera camera;
  ArrayView<Vertex> vertices ETX_EMPTY_INIT;
  ArrayView<Triangle> triangles ETX_EMPTY_INIT;
  ArrayView<Material> materials ETX_EMPTY_INIT;
  ArrayView<Emitter> emitters ETX_EMPTY_INIT;
  ArrayView<Image> images ETX_EMPTY_INIT;
  ArrayView<Medium> mediums ETX_EMPTY_INIT;
  Distribution emitters_distribution ETX_EMPTY_INIT;
  EnvironmentEmitters environment_emitters ETX_EMPTY_INIT;
  Spectrums* spectrums ETX_EMPTY_INIT;
  float3 bounding_sphere_center ETX_EMPTY_INIT;
  float bounding_sphere_radius ETX_EMPTY_INIT;
  uint64_t acceleration_structure ETX_EMPTY_INIT;
  uint32_t camera_medium_index ETX_INIT_WITH(kInvalidIndex);
  uint32_t camera_lens_shape_image_index ETX_INIT_WITH(kInvalidIndex);

  ETX_GPU_CODE bool valid() const {
    return (vertices.count > 0) && (triangles.count > 0) && (materials.count > 0) && (emitters.count > 0) && (bounding_sphere_radius > 0.0f);
  }
};

ETX_GPU_CODE float3 lerp_pos(const ArrayView<Vertex>& vertices, const Triangle& t, const float3& bc) {
  return vertices[t.i[0]].pos * bc.x +  //
         vertices[t.i[1]].pos * bc.y +  //
         vertices[t.i[2]].pos * bc.z;   //
}

ETX_GPU_CODE float3 lerp_normal(const ArrayView<Vertex>& vertices, const Triangle& t, const float3& bc) {
  return normalize(vertices[t.i[0]].nrm * bc.x +  //
                   vertices[t.i[1]].nrm * bc.y +  //
                   vertices[t.i[2]].nrm * bc.z);  //
}

ETX_GPU_CODE float2 lerp_uv(const ArrayView<Vertex>& vertices, const Triangle& t, const float3& b) {
  return vertices[t.i[0]].tex * b.x +  //
         vertices[t.i[1]].tex * b.y +  //
         vertices[t.i[2]].tex * b.z;   //
}

ETX_GPU_CODE Vertex lerp_vertex(const ArrayView<Vertex>& vertices, const Triangle& t, const float3& bc) {
  const auto& v0 = vertices[t.i[0]];
  const auto& v1 = vertices[t.i[1]];
  const auto& v2 = vertices[t.i[2]];
  return {
    /*     */ v0.pos * bc.x + v1.pos * bc.y + v2.pos * bc.z,
    normalize(v0.nrm * bc.x + v1.nrm * bc.y + v2.nrm * bc.z),
    normalize(v0.tan * bc.x + v1.tan * bc.y + v2.tan * bc.z),
    normalize(v0.btn * bc.x + v1.btn * bc.y + v2.btn * bc.z),
    /*     */ v0.tex * bc.x + v1.tex * bc.y + v2.tex * bc.z,
  };
}

ETX_GPU_CODE float3 shading_pos_project(const float3& position, const float3& origin, const float3& normal) {
  return position - dot(position - origin, normal) * normal;
}

ETX_GPU_CODE float3 shading_pos(const ArrayView<Vertex>& vertices, const Triangle& t, const float3& bc, const float3& w_o) {
  float3 geo_pos = lerp_pos(vertices, t, bc);
  float3 sh_normal = lerp_normal(vertices, t, bc);
  float direction = (dot(sh_normal, w_o) >= 0.0f) ? +1.0f : -1.0f;
  float3 p0 = shading_pos_project(geo_pos, vertices[t.i[0]].pos, direction * vertices[t.i[0]].nrm);
  float3 p1 = shading_pos_project(geo_pos, vertices[t.i[1]].pos, direction * vertices[t.i[1]].nrm);
  float3 p2 = shading_pos_project(geo_pos, vertices[t.i[2]].pos, direction * vertices[t.i[2]].nrm);
  float3 sh_pos = p0 * bc.x + p1 * bc.y + p2 * bc.z;
  bool convex = dot(sh_pos - geo_pos, sh_normal) * direction > 0.0f;
  return offset_ray(convex ? sh_pos : geo_pos, t.geo_n * direction);
}

ETX_GPU_CODE bool apply_rr(float eta_scale, float rnd, SpectralResponse& throughput) {
  float max_t = throughput.maximum() * (eta_scale * eta_scale);
  if (valid_value(max_t) == false) {
    return false;
  }

  float q = min(0.95f, max_t);
  if ((q > 0.0f) && (rnd < q)) {
    throughput *= (1.0f / q);
    return true;
  }

  return false;
}

}  // namespace etx

#include <etx/render/shared/scene_bsdf.hxx>
#include <etx/render/shared/scene_camera.hxx>
#include <etx/render/shared/scene_emitters.hxx>
