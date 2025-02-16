﻿#pragma once

#include <etx/core/pimpl.hxx>
#include <etx/rt/integrators/integrator.hxx>

namespace etx {

struct CPUAtmosphere : public Integrator {
  CPUAtmosphere(Raytracing&);
  ~CPUAtmosphere() override;

  const char* name() override {
    return "Atmosphere (CPU)";
  }

  Options options() const override;

  bool have_updated_light_image() const override {
    return false;
  }

  void set_output_size(const uint2&) override;
  const float4* get_camera_image(bool force_update) override;
  const float4* get_light_image(bool force_update) override;
  const char* status() const override;

  void preview(const Options&) override;
  void run(const Options&) override;
  void update() override;
  void stop(Stop) override;
  void update_options(const Options&) override;

 private:
  ETX_DECLARE_PIMPL(CPUAtmosphere, 65536);
};

}  // namespace etx
