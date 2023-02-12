﻿#include <etx/core/core.hxx>
#include <etx/core/environment.hxx>
#include <etx/render/host/image_pool.hxx>
#include <etx/log/log.hxx>

#include "ui.hxx"

#include <sokol_app.h>
#include <sokol_gfx.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#include <cimgui.h>
#include <sokol_imgui.h>

namespace etx {

inline void decrease_exposure(ViewOptions& o) {
  o.exposure = fmaxf(1.0f / 1024.0f, 0.5f * o.exposure);
}

inline void increase_exposure(ViewOptions& o) {
  o.exposure = fmaxf(1.0f / 1024.0f, 2.0f * o.exposure);
}

void UI::MappingRepresentation::build(const std::unordered_map<std::string, uint32_t>& mapping) {
  constexpr uint64_t kMaterialNameMaxSize = 1024llu;

  indices.clear();
  indices.reserve(mapping.size());
  names.clear();
  names.reserve(mapping.size());
  data.resize(mapping.size() * kMaterialNameMaxSize);
  std::fill(data.begin(), data.end(), 0);
  char* ptr = data.data();
  int32_t pp = 0;
  for (auto& m : mapping) {
    indices.emplace_back(m.second);
    names.emplace_back(ptr + pp);
    pp += snprintf(ptr + pp, kMaterialNameMaxSize, "%s", m.first.c_str()) + 1;
  }
}

void UI::initialize() {
  simgui_desc_t imggui_desc = {};
  imggui_desc.depth_format = SG_PIXELFORMAT_NONE;
  simgui_setup(imggui_desc);
  igLoadIniSettingsFromDisk(env().file_in_data("ui.ini"));
}

void UI::cleanup() {
  igSaveIniSettingsToDisk(env().file_in_data("ui.ini"));
}

bool UI::build_options(Options& options) {
  bool changed = false;

  for (auto& option : options.values) {
    switch (option.cls) {
      case OptionalValue::Class::InfoString: {
        igTextColored({1.0f, 0.5f, 0.25f, 1.0f}, option.name.c_str());
        break;
      };

      case OptionalValue::Class::Boolean: {
        bool value = option.to_bool();
        if (igCheckbox(option.name.c_str(), &value)) {
          option.set(value);
          changed = true;
        }
        break;
      }

      case OptionalValue::Class::Float: {
        float value = option.to_float();
        igSetNextItemWidth(4.0f * igGetFontSize());
        if (igDragFloat(option.name.c_str(), &value, 0.001f, option.min_value.flt, option.max_value.flt, "%.3f", ImGuiSliderFlags_AlwaysClamp)) {
          option.set(value);
          changed = true;
        }
        break;
      }

      case OptionalValue::Class::Integer: {
        int value = option.to_integer();
        igSetNextItemWidth(4.0f * igGetFontSize());
        if (igDragInt(option.name.c_str(), &value, 1.0f, option.min_value.integer, option.max_value.integer, "%u", ImGuiSliderFlags_AlwaysClamp)) {
          option.set(uint32_t(value));
          changed = true;
        }
        break;
      }

      case OptionalValue::Class::Enum: {
        int value = option.to_integer();
        igSetNextItemWidth(4.0f * igGetFontSize());
        if (igTreeNodeEx_Str(option.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed)) {
          for (uint32_t i = 0; i <= option.max_value.integer; ++i) {
            if (igRadioButton_IntPtr(option.name_func(i).c_str(), &value, i)) {
              value = i;
            }
          }
          if (value != option.to_integer()) {
            option.set(uint32_t(value));
            changed = true;
          }
          igTreePop();
        }
        break;
      }

      default:
        ETX_FAIL("Invalid option");
    }
  }
  return changed;
}

bool igSpectrumPicker(const char* name, SpectralDistribution& spd, const Pointer<Spectrums> spectrums, bool linear) {
  float3 rgb = spectrum::xyz_to_rgb(spd.to_xyz());
  if (linear == false) {
    rgb = linear_to_gamma(rgb);
  } else {
    rgb = rgb;
  }
  uint32_t flags = linear ? ImGuiColorEditFlags_Float | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_NoPicker  //
                          : ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_PickerHueBar;                    //

  if (linear) {
    igText(name);
  }

  bool changed = false;
  char name_buffer[64] = {};
  snprintf(name_buffer, sizeof(name_buffer), "%s%s", linear ? "##" : "", name);
  if (igColorEdit3(name_buffer, &rgb.x, flags)) {
    if (linear == false) {
      rgb = gamma_to_linear(rgb);
    }
    rgb = max(rgb, float3{});
    spd = rgb::make_reflectance_spd(rgb, spectrums);
    changed = true;
  }

  if (linear) {
    if (igButton("Clear", {})) {
      spd = SpectralDistribution::from_constant(0.0f);
      changed = true;
    }
  }

  return changed;
}

void UI::build(double dt, const char* status) {
  constexpr uint32_t kWindowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
  bool has_integrator = (_current_integrator != nullptr);
  bool has_scene = (_current_scene != nullptr);

  simgui_new_frame(simgui_frame_desc_t{sapp_width(), sapp_height(), dt, sapp_dpi_scale()});

  if (igBeginMainMenuBar()) {
    if (igBeginMenu("Scene", true)) {
      if (igMenuItemEx("Open...", nullptr, "Ctrl+O", false, true)) {
        select_scene_file();
      }
      if (igMenuItemEx("Reload Scene", nullptr, "Ctrl+R", false, true)) {
        if (callbacks.reload_scene_selected) {
          callbacks.reload_scene_selected();
        }
      }
      if (igMenuItemEx("Reload Geometry and Materials", nullptr, "Ctrl+G", false, true)) {
        if (callbacks.reload_geometry_selected) {
          callbacks.reload_geometry_selected();
        }
      }
      if (igMenuItemEx("Reload Materials", nullptr, "Ctrl+M", false, false)) {
      }
      igSeparator();
      if (igMenuItemEx("Save...", nullptr, nullptr, false, true)) {
        save_scene_file();
      }
      igEndMenu();
    }

    if (igBeginMenu("Integrator", true)) {
      for (uint64_t i = 0; i < _integrators.count; ++i) {
        if (igMenuItemEx(_integrators[i]->name(), nullptr, nullptr, false, _integrators[i]->enabled())) {
          if (callbacks.integrator_selected) {
            callbacks.integrator_selected(_integrators[i]);
          }
        }
      }

      if (has_integrator) {
        igSeparator();
        if (igMenuItemEx("Reload Integrator State", nullptr, "Ctrl+A", false, true)) {
          if (callbacks.reload_integrator) {
            callbacks.reload_integrator();
          }
        }
      }

      igSeparator();
      if (igMenuItemEx("Exit", nullptr, "Ctrl+Q", false, true)) {
      }
      igEndMenu();
    }

    if (igBeginMenu("Image", true)) {
      if (igMenuItemEx("Open Reference Image...", nullptr, "Ctrl+I", false, true)) {
        load_image();
      }
      igSeparator();
      if (igMenuItemEx("Save Current Image (RGB)...", nullptr, "Ctrl+S", false, true)) {
        save_image(SaveImageMode::RGB);
      }
      if (igMenuItemEx("Save Current Image (LDR)...", nullptr, "Shift+Ctrl+S", false, true)) {
        save_image(SaveImageMode::TonemappedLDR);
      }
      if (igMenuItemEx("Save Current Image (XYZ)...", nullptr, "Alt+Ctrl+S", false, true)) {
        save_image(SaveImageMode::XYZ);
      }
      if (igMenuItemEx("Use as Reference", nullptr, "Ctrl+Shift+R", false, true)) {
        if (callbacks.use_image_as_reference) {
          callbacks.use_image_as_reference();
        }
      }
      igEndMenu();
    }

    if (igBeginMenu("View", true)) {
      char shortcut[2] = "X";
      for (uint32_t i = 0; i < uint32_t(OutputView::Count); ++i) {
        shortcut[0] = char('1' + i);
        if (igMenuItemEx(output_view_to_string(i).c_str(), nullptr, shortcut, uint32_t(_view_options.view) == i, true)) {
          _view_options.view = static_cast<OutputView>(i);
        }
      }

      igSeparator();
      if (igMenuItemEx("Increase Exposure", nullptr, "*", false, true)) {
        increase_exposure(_view_options);
      }
      if (igMenuItemEx("Decrease Exposure", nullptr, "/", false, true)) {
        increase_exposure(_view_options);
      }
      igSeparator();

      auto ui_toggle = [this](const char* label, uint32_t flag) {
        uint32_t k = 0;
        for (; (k < 8) && (flag != (1u << k)); ++k) {
        }
        char buffer[8] = {};
        snprintf(buffer, sizeof(buffer), "F%u", k + 1u);
        bool ui_integrator = (_ui_setup & flag) == flag;
        if (igMenuItemEx(label, nullptr, buffer, ui_integrator, true)) {
          _ui_setup = ui_integrator ? (_ui_setup & (~flag)) : (_ui_setup | flag);
        }
      };
      ui_toggle("Interator options", UIIntegrator);
      ui_toggle("Materials and mediums", UIMaterial);
      ui_toggle("Emitters", UIEmitters);
      ui_toggle("Camera", UICamera);
      igEndMenu();
    }

    igEndMainMenuBar();
  }

  bool scene_editable = has_integrator && has_scene &&  //
                        ((_current_integrator->state() == Integrator::State::Preview) || (_current_integrator->state() == Integrator::State::Stopped));

  ImVec2 wpadding = igGetStyle()->WindowPadding;
  ImVec2 fpadding = igGetStyle()->FramePadding;
  float text_size = igGetFontSize();
  float button_size = 32.0f;
  float input_size = 64.0f;
  if (igBeginViewportSideBar("##toolbar", igGetMainViewport(), ImGuiDir_Up, button_size + 2.0f * wpadding.y, ImGuiWindowFlags_NoDecoration)) {
    bool can_run = has_integrator && _current_integrator->can_run();
    Integrator::State state = can_run ? _current_integrator->state() : Integrator::State::Stopped;

    bool state_available[4] = {
      can_run && (state == Integrator::State::Stopped),
      can_run && ((state == Integrator::State::Stopped) || (state == Integrator::State::Preview)),
      can_run && (state == Integrator::State::Running),
      can_run && ((state != Integrator::State::Stopped)),
    };

    ImVec4 colors[4] = {
      state_available[0] ? ImVec4{0.1f, 0.1f, 0.33f, 1.0f} : ImVec4{0.25f, 0.25f, 0.25f, 1.0f},
      state_available[1] ? ImVec4{0.1f, 0.33f, 0.1f, 1.0f} : ImVec4{0.25f, 0.25f, 0.25f, 1.0f},
      state_available[2] ? ImVec4{0.33f, 0.22f, 0.11f, 1.0f} : ImVec4{0.25f, 0.25f, 0.25f, 1.0f},
      state_available[3] ? ImVec4{0.33f, 0.1f, 0.1f, 1.0f} : ImVec4{0.25f, 0.25f, 0.25f, 1.0f},
    };

    std::string labels[4] = {
      (state == Integrator::State::Preview) ? "> Preview <" : "  Preview  ",
      (state == Integrator::State::Running) ? "> Launch <" : "  Launch  ",
      (state == Integrator::State::WaitingForCompletion) ? "> Finish <" : "  Finish  ",
      " Terminate ",
    };

    igPushStyleColor_Vec4(ImGuiCol_Button, colors[0]);
    if (igButton(labels[0].c_str(), {0.0f, button_size})) {
      callbacks.preview_selected();
    }

    igSameLine(0.0f, wpadding.x);
    igPushStyleColor_Vec4(ImGuiCol_Button, colors[1]);
    if (igButton(labels[1].c_str(), {0.0f, button_size})) {
      callbacks.run_selected();
    }

    igSameLine(0.0f, wpadding.x);
    igPushStyleColor_Vec4(ImGuiCol_Button, colors[2]);
    if (igButton(labels[2].c_str(), {0.0f, button_size})) {
      callbacks.stop_selected(true);
    }

    igSameLine(0.0f, wpadding.x);
    igPushStyleColor_Vec4(ImGuiCol_Button, colors[3]);
    if (igButton(labels[3].c_str(), {0.0f, button_size})) {
      callbacks.stop_selected(false);
    }

    igPopStyleColor(4);

    igSameLine(0.0f, wpadding.x);
    igSeparatorEx(ImGuiSeparatorFlags_Vertical);
    igSameLine(0.0f, wpadding.x);

    igPushItemWidth(input_size);
    igGetStyle()->FramePadding.y = (button_size - text_size) / 2.0f;
    igText("Exposure:");
    igSameLine(0.0f, 0.0f);
    igDragFloat("##exposure", &_view_options.exposure, 1.0f / 256.0f, 1.0f / 1024.0f, 1024.0f, "%.4f", ImGuiSliderFlags_NoRoundToFormat);
    igGetStyle()->FramePadding.y = fpadding.y;
    igPopItemWidth();

    igEnd();
  }

  if (igBeginViewportSideBar("##status", igGetMainViewport(), ImGuiDir_Down, text_size + 2.0f * wpadding.y, ImGuiWindowFlags_NoDecoration)) {
    char status_buffer[2048] = {};
    uint32_t cpu_load = static_cast<uint32_t>(TimeMeasure::get_cpu_load() * 100.0f);
    snprintf(status_buffer, sizeof(status_buffer), "% 3u cpu | %.2fms | %.2ffps | %s", cpu_load, 1000.0 * dt, 1.0f / dt, status ? status : "");
    igText(status_buffer);
    igEnd();
  }

  if ((_ui_setup & UIIntegrator) && igBegin("Integrator options", nullptr, kWindowFlags)) {
    if (has_integrator) {
      igText(_current_integrator->name());
      if (build_options(_integrator_options) && callbacks.options_changed) {
        callbacks.options_changed();
      }
    } else {
      igText("No integrator selected");
    }
    igEnd();
  }

  if ((_ui_setup & UIMaterial) && igBegin("Materials and mediums", nullptr, kWindowFlags)) {
    igText("Materials");
    igListBox_Str_arr("##materials", &_selected_material, _material_mapping.names.data(), static_cast<int32_t>(_material_mapping.size()), 6);
    if (scene_editable && (_selected_material >= 0) && (_selected_material < _material_mapping.size())) {
      uint32_t material_index = _material_mapping.at(_selected_material);
      Material& material = _current_scene->materials[material_index];
      bool changed = build_material(material);
      if (changed && callbacks.material_changed) {
        callbacks.material_changed(material_index);
      }
    }

    igSeparator();

    igText("Mediums");
    igListBox_Str_arr("##mediums", &_selected_medium, _medium_mapping.names.data(), static_cast<int32_t>(_medium_mapping.size()), 6);
    if (scene_editable && (_selected_medium >= 0) && (_selected_medium < _medium_mapping.size())) {
      uint32_t medium_index = _medium_mapping.at(_selected_medium);
      Medium& m = _current_scene->mediums[medium_index];
      bool changed = build_medium(m);
      if (changed && callbacks.material_changed) {
        callbacks.medium_changed(medium_index);
      }
    }

    igEnd();
  }

  if ((_ui_setup & UIEmitters) && igBegin("Emitters", nullptr, kWindowFlags)) {
    bool has_emitters = false;
    igText("Emitters");
    if (igBeginListBox("##emitters", {})) {
      for (uint32_t index = 0; has_scene && (index < _current_scene->emitters.count); ++index) {
        auto& emitter = _current_scene->emitters[index];
        if (emitter.cls == Emitter::Class::Area)
          continue;

        char buffer[32] = {};
        switch (emitter.cls) {
          case Emitter::Class::Directional: {
            snprintf(buffer, sizeof(buffer), "%05u : directional", index);
            break;
          }
          case Emitter::Class::Environment: {
            snprintf(buffer, sizeof(buffer), "%05u : environment", index);
            break;
          }
          default:
            break;
        }
        if (igSelectable_Bool(buffer, _selected_emitter == index, ImGuiSelectableFlags_None, {})) {
          _selected_emitter = index;
        }
      }
      igEndListBox();
    }

    if (scene_editable && (_selected_emitter >= 0) && (_selected_emitter < _current_scene->emitters.count)) {
      auto& emitter = _current_scene->emitters[_selected_emitter];

      bool changed = igSpectrumPicker("Emission", emitter.emission.spectrum, _current_scene->spectrums, true);

      if (emitter.cls == Emitter::Class::Directional) {
        igText("Angular Size");
        if (igDragFloat("##angularsize", &emitter.angular_size, 0.01f, 0.0f, kHalfPi, "%.3f", ImGuiSliderFlags_NoRoundToFormat)) {
          emitter.angular_size_cosine = cosf(emitter.angular_size / 2.0f);
          emitter.equivalent_disk_size = 2.0f * std::tan(emitter.angular_size / 2.0f);
          changed = true;
        }

        igText("Direction");
        if (igDragFloat3("##direction", &emitter.direction.x, 0.1f, -256.0f, 256.0f, "%.02f", ImGuiSliderFlags_NoRoundToFormat)) {
          emitter.direction = normalize(emitter.direction);
          changed = true;
        }
      }

      if (changed && callbacks.emitter_changed) {
        callbacks.emitter_changed(_selected_emitter);
      }
    }
    igEnd();
  }

  if ((_ui_setup & UICamera) && igBegin("Camera", nullptr, kWindowFlags)) {
    if (scene_editable) {
      auto& camera = _current_scene->camera;
      bool changed = false;
      float3 pos = camera.position;
      float3 target = camera.position + camera.direction;
      float focal_len = get_camera_focal_length(camera);
      igText("Lens size");
      changed = changed || igDragFloat("##lens", &camera.lens_radius, 0.01f, 0.0f, 2.0, "%.3f", ImGuiSliderFlags_None);
      igText("Focal distance");
      changed = changed || igDragFloat("##focaldist", &camera.focal_distance, 0.1f, 0.0f, 65536.0f, "%.3f", ImGuiSliderFlags_None);
      igText("Focal length");
      changed = changed || igDragFloat("##focal", &focal_len, 0.1f, 1.0f, 90.0f, "%.3fmm", ImGuiSliderFlags_None);

      if (changed && callbacks.camera_changed) {
        camera.lens_radius = fmaxf(camera.lens_radius, 0.0f);
        camera.focal_distance = fmaxf(camera.focal_distance, 0.0f);
        update_camera(camera, pos, target, float3{0.0f, 1.0f, 0.0f}, camera.image_size, focal_length_to_fov(focal_len) * 180.0f / kPi);
        callbacks.camera_changed();
      }
    } else {
      igText("No options available");
    }
    igEnd();
  }

  if (has_integrator && (_current_integrator->debug_info_count() > 0)) {
    if (igBegin("Debug Info", nullptr, kWindowFlags)) {
      auto debug_info = _current_integrator->debug_info();
      for (uint64_t i = 0, e = _current_integrator->debug_info_count(); i < e; ++i) {
        char buffer[8] = {};
        snprintf(buffer, sizeof(buffer), "%.3f     .", debug_info[i].value);
        igLabelText(buffer, debug_info[i].title);
      }
      igEnd();
    }
  }

  simgui_render();
}

bool UI::handle_event(const sapp_event* e) {
  if (simgui_handle_event(e)) {
    return true;
  }

  if (e->type != SAPP_EVENTTYPE_KEY_DOWN) {
    return false;
  }

  auto modifiers = e->modifiers;
  bool has_alt = modifiers & SAPP_MODIFIER_ALT;
  bool has_shift = modifiers & SAPP_MODIFIER_SHIFT;

  if (modifiers & SAPP_MODIFIER_CTRL) {
    switch (e->key_code) {
      case SAPP_KEYCODE_O: {
        select_scene_file();
        break;
      }
      case SAPP_KEYCODE_I: {
        load_image();
        break;
      }
      case SAPP_KEYCODE_R: {
        if (has_shift) {
          if (callbacks.use_image_as_reference) {
            callbacks.use_image_as_reference();
          }
        } else if (callbacks.reload_scene_selected) {
          callbacks.reload_scene_selected();
        }
        break;
      }
      case SAPP_KEYCODE_G: {
        if (callbacks.reload_geometry_selected)
          callbacks.reload_geometry_selected();
        break;
      }
      case SAPP_KEYCODE_A: {
        if (callbacks.reload_integrator)
          callbacks.reload_integrator();
        break;
      }
      case SAPP_KEYCODE_S: {
        if (has_alt && (has_shift == false))
          save_image(SaveImageMode::XYZ);
        else if (has_shift && (has_alt == false)) {
          save_image(SaveImageMode::TonemappedLDR);
        } else if ((has_shift == false) && (has_alt == false)) {
          save_image(SaveImageMode::RGB);
        }
        break;
      }
      default:
        break;
    }
  }

  switch (e->key_code) {
    case SAPP_KEYCODE_F1:
    case SAPP_KEYCODE_F2:
    case SAPP_KEYCODE_F3:
    case SAPP_KEYCODE_F4:
    case SAPP_KEYCODE_F5: {
      uint32_t flag = 1u << (e->key_code - SAPP_KEYCODE_F1);
      _ui_setup = (_ui_setup & flag) ? (_ui_setup & (~flag)) : (_ui_setup | flag);
      break;
    };

    case SAPP_KEYCODE_1:
    case SAPP_KEYCODE_2:
    case SAPP_KEYCODE_3:
    case SAPP_KEYCODE_4:
    case SAPP_KEYCODE_5:
    case SAPP_KEYCODE_6: {
      _view_options.view = static_cast<OutputView>(e->key_code - SAPP_KEYCODE_1);
      break;
    }
    case SAPP_KEYCODE_KP_DIVIDE: {
      decrease_exposure(_view_options);
      break;
    }
    case SAPP_KEYCODE_KP_MULTIPLY: {
      increase_exposure(_view_options);
      break;
    }
    default:
      break;
  }

  return false;
}

ViewOptions UI::view_options() const {
  return _view_options;
}

void UI::set_current_integrator(Integrator* i) {
  _current_integrator = i;
  _integrator_options = _current_integrator ? _current_integrator->options() : Options{};
}

void UI::select_scene_file() {
  auto selected_file = open_file({"Supported formats", "*.json;*.obj"});  // TODO : add *.gltf;*.pbrt
  if ((selected_file.empty() == false) && callbacks.scene_file_selected) {
    callbacks.scene_file_selected(selected_file);
  }
}

void UI::save_scene_file() {
  auto selected_file = save_file({"Scene description", "*.json"});
  if ((selected_file.empty() == false) && callbacks.save_scene_file_selected) {
    callbacks.save_scene_file_selected(selected_file);
  }
}

void UI::save_image(SaveImageMode mode) {
  auto selected_file = save_file({
    mode == SaveImageMode::TonemappedLDR ? "PNG images" : "EXR images",
    mode == SaveImageMode::TonemappedLDR ? "*.png" : "*.exr",
  });
  if ((selected_file.empty() == false) && callbacks.save_image_selected) {
    callbacks.save_image_selected(selected_file, mode);
  }
}

void UI::load_image() {
  auto selected_file = open_file({"Supported images", "*.exr;*.png;*.hdr;*.pfm;*.jpg;*.bmp;*.tga"});
  if ((selected_file.empty() == false) && callbacks.reference_image_selected) {
    callbacks.reference_image_selected(selected_file);
  }
}

void UI::set_scene(Scene* scene, const SceneRepresentation::MaterialMapping& materials, const SceneRepresentation::MediumMapping& mediums) {
  _current_scene = scene;

  _selected_material = -1;
  _material_mapping.build(materials);

  _selected_medium = -1;
  _medium_mapping.build(mediums);
}

bool UI::build_material(Material& material) {
  int32_t material_cls = static_cast<int32_t>(material.cls);
  bool changed = igCombo_FnBoolPtr(
    "##type", &material_cls,
    [](void* data, int32_t idx, const char** out_text) -> bool {
      material_class_to_string(Material::Class(idx), out_text);
      return true;
    },
    nullptr, int32_t(Material::Class::Count), 5);
  material.cls = static_cast<Material::Class>(material_cls);
  changed |= igSliderFloat("##r_u", &material.roughness.x, 0.0f, 1.0f, "Roughness U %.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
  changed |= igSliderFloat("##r_v", &material.roughness.y, 0.0f, 1.0f, "Roughness V %.2f", ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_NoRoundToFormat);
  changed |= igSpectrumPicker("Diffuse", material.diffuse.spectrum, _current_scene->spectrums, false);
  changed |= igSpectrumPicker("Specular", material.specular.spectrum, _current_scene->spectrums, false);
  changed |= igSpectrumPicker("Transmittance", material.transmittance.spectrum, _current_scene->spectrums, false);
  changed |= igSpectrumPicker("Subsurface", material.subsurface.scattering_distance, _current_scene->spectrums, true);

  auto medium_editor = [](const char* name, uint32_t& medium, uint64_t medium_count) -> bool {
    bool has_medium = medium != kInvalidIndex;
    bool changed = igCheckbox(name, &has_medium);
    if (has_medium) {
      int32_t medium_index = static_cast<int32_t>(medium);
      if (medium_index == -1)
        medium_index = 0;
      changed |= igSliderInt("##medium_index", &medium_index, 0, int32_t(medium_count), "Index: %d", ImGuiSliderFlags_AlwaysClamp);
      medium = uint32_t(medium_index);
    } else {
      medium = kInvalidIndex;
    }
    return changed;
  };

  if (_medium_mapping.empty() == false) {
    changed |= medium_editor("Internal medium", material.int_medium, _current_scene->mediums.count);
    changed |= medium_editor("Extenral medium", material.ext_medium, _current_scene->mediums.count);
  }

  return changed;
}

bool UI::build_medium(Medium& m) {
  bool changed = false;
  changed |= igSpectrumPicker("Absorption", m.s_absorption, _current_scene->spectrums, true);
  changed |= igSpectrumPicker("Outscattering", m.s_outscattering, _current_scene->spectrums, true);
  changed |= igSliderFloat("##g", &m.phase_function_g, -0.999f, 0.999f, "Asymmetry %.2f", ImGuiSliderFlags_AlwaysClamp);
  return changed;
}

}  // namespace etx
