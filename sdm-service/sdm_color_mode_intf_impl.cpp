/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Changes from Qualcomm Innovation Center, Inc are provided under the following license:
 *
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <utils/constants.h>
#include "display_properties.h"
#include <algorithm>

#include "sdm-service/sdm_display_debugger.h"
#include "sdm-service/sdm_color_mode_intf_impl.h"

#define __CLASS__ "SDMColorModeImpl"

#define DEFAULT_RENDER_INTENT snapdragoncolor::kColorimetric
#define DEFAULT_DYNAMIC_RANGE kSdrType

namespace sdm {

#define MAX_PROP_STR_SIZE 64

static SDMColorModeIntfFactory color_mode_intf_factory_;

SDMColorModeIntfFactory *GetSDMColorModeIntfFactory() { return &color_mode_intf_factory_; }

std::unique_ptr<SDMColorModeIntf> SDMColorModeIntfFactory::GetSDMColorModeIntf(
                    DisplayType display_type, std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                    std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                    uint64_t display_handle) {
  if (display_type == kBuiltIn) {
    return std::unique_ptr<SDMColorModeIntf>(new SDMColorModeImplStc(caps_intf, settings_intf, display_handle));
  }

  if (display_type == kPluggable) {
    return std::unique_ptr<SDMColorModeIntf>(new SDMColorModeImpl(caps_intf, settings_intf, display_handle));
  }

  return std::unique_ptr<SDMColorModeIntf>();
}

SDMColorModeImpl::SDMColorModeImpl(std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                                   std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                                   uint64_t display_handle) 
    : caps_intf_(caps_intf), settings_intf_(settings_intf), display_handle_(display_handle) {}

DisplayError SDMColorModeImpl::Init() {
  PopulateColorModes();
  return kErrorNone;
}

DisplayError SDMColorModeImpl::DeInit() {
  color_mode_map_.clear();
  return kErrorNone;
}

bool SDMColorModeImpl::IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                        snapdragoncolor::RenderIntent intent) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    return false;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    return false;
  }

  if (color_mode_map_[primary][transfer].find(intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    return false;
  }

  return true;
}

DisplayError SDMColorModeImpl::GetValidColorMode(ColorPrimaries primary, GammaTransfer transfer,
                          snapdragoncolor::RenderIntent *intent, DynamicRangeType *dynamic_range) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    DLOGI("Could not find color primary: %d", primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    DLOGI("Could not find transfer %d in primary %d", transfer, primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    // Fall back to default render intent if current render intent is not supported
    DLOGW("Could not find intent %d, checking for intent %d in primary %d, transfer %d",
                                      *intent, DEFAULT_RENDER_INTENT, primary, transfer);
    *intent = DEFAULT_RENDER_INTENT;

    if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
      DLOGI("Failed to find intent %d in primary %d, transfer %d",
                                  *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }

  if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                              color_mode_map_[primary][transfer][*intent].end()) {
    // Fall back to default dynamic range if current render intent is not supported
    DLOGW("Could not find range %d, checking for range %d in intent %d, primary %d, transfer %d",
                              *dynamic_range, DEFAULT_DYNAMIC_RANGE, *intent, primary, transfer);
    *dynamic_range = DEFAULT_DYNAMIC_RANGE;

    if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                            color_mode_map_[primary][transfer][*intent].end()) {
      DLOGI("Failed to find range %d, in intent %d, primary %d, transfer %d",
                                  *dynamic_range, *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }
  return kErrorNone;
}

DisplayError SDMColorModeImpl::SetColorModeWithRenderIntent(snapdragoncolor::ColorMode color_mode,
                                                            bool hdr_present) {
  if (current_color_mode_.gamut == color_mode.gamut &&
      current_color_mode_.gamma == color_mode.gamma &&
      current_color_mode_.intent == color_mode.intent &&
      ((hdr_present && (curr_dynamic_range_ == kHdrType)) ||
      (!hdr_present && (curr_dynamic_range_ == kSdrType)))) {
      return kErrorNone;
  }

  ColorPrimaries primary = color_mode.gamut;
  GammaTransfer transfer = color_mode.gamma;
  snapdragoncolor::RenderIntent intent = color_mode.intent;
  DynamicRangeType dynamic_range = hdr_present ? kHdrType : kSdrType;

  DisplayError error = GetValidColorMode(primary, transfer, &intent, &dynamic_range);
  if (error != kErrorNone) {
    return error;
  }

  std::string mode_string = color_mode_map_[primary][transfer][intent][dynamic_range];
  DLOGI("Applying color mode: %s", mode_string.c_str());

  error = settings_intf_->SetColorMode(display_handle_, mode_string);
  if (error != kErrorNone) {
    DLOGI("Failed to apply mode: gamut = %d, gamma = %d, intent = %d, range = %d, name = %s\
      , err %d", primary, transfer, intent, dynamic_range, mode_string.c_str(), error);
    return kErrorNotSupported;
  }

  // Overwrite the intent in case default intent was applied
  color_mode.intent = intent;

  current_color_mode_ = color_mode;
  curr_dynamic_range_ = dynamic_range;
  DLOGV_IF(kTagClient, "Successfully applied mode: gamut = %d, gamma = %d, intent = %d, "
      "range = %d, name = %s", primary, transfer, intent, dynamic_range, mode_string.c_str());
  return kErrorNone;
}

void SDMColorModeImpl::PopulateColorModes() {
  DLOGW("Legacy pluggable color mode enumeration path is unavailable in this backend");
  color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][snapdragoncolor::kNative][kSdrType] =
      "hal_native_identity";
}

#undef __CLASS__
#define __CLASS__ "SDMColorModeImplStc"

SDMColorModeImplStc::SDMColorModeImplStc(std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                                         std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                                         uint64_t display_handle)
    : caps_intf_(caps_intf), settings_intf_(settings_intf), display_handle_(display_handle) {}

DisplayError SDMColorModeImplStc::Init() {
  DisplayError error = kErrorNotSupported;
  stc_mode_list_.list.clear();
  DLOGW("STC color mode enumeration via display interface is unavailable in this backend");

  PopulateColorModes();
  return kErrorNone;
}

DisplayError SDMColorModeImplStc::DeInit() {
  stc_mode_list_.list.clear();
  color_mode_map_.clear();
  return kErrorNone;
}

DisplayError SDMColorModeImplStc::SetColorTransform(const float *matrix) {
  if (!matrix) {
    DLOGI("Invalid parameters : matrix %pK", matrix);
    return kErrorParameters;
  }
  double color_matrix[kColorTransformMatrixCount] = {0};
  CopyColorTransformMatrix(matrix, color_matrix);

  std::vector<float> color_transform(matrix, matrix + kColorTransformMatrixCount);
  DisplayError error = settings_intf_->SetColorTransform(display_handle_, color_transform);
  if (error != kErrorNone) {
    DLOGI("Failed to set Color Transform Matrix");
    error = kErrorNotSupported;
  }
  CopyColorTransformMatrix(matrix, color_matrix_);
  return error;
}

DisplayError SDMColorModeImplStc::RestoreColorTransform() {
  std::vector<float> color_transform(color_matrix_, color_matrix_ + kColorTransformMatrixCount);
  DisplayError error = settings_intf_->SetColorTransform(display_handle_, color_transform);
  if (error != kErrorNone) {
    DLOGI("Failed to set Color Transform");
    return kErrorParameters;
  }

  return kErrorNone;
}

void SDMColorModeImplStc::PopulateColorModes() {
  bool allow_tonemap_native = 0;
  char property[MAX_PROP_STR_SIZE] = {0};

  SdmDisplayDebugger::Get()->GetProperty(ALLOW_TONEMAP_NATIVE, property);
  if (!(std::string(property)==std::string("1"))) {
      allow_tonemap_native = true;
  }

  if (!stc_mode_list_.list.size()) {
    snapdragoncolor::ColorMode color_mode = {};
    color_mode.intent = snapdragoncolor::kNative;
    color_mode.gamut = allow_tonemap_native ? ColorPrimaries_BT709_5 : ColorPrimaries_Max;
    color_mode.gamma = allow_tonemap_native ? Transfer_sRGB : Transfer_Max;
    color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][snapdragoncolor::kNative][kSdrType] =
      color_mode;
    DLOGI("No color mode supported, add Native mode");
    return;
  }

  for (uint32_t i = 0; i < stc_mode_list_.list.size(); i++) {
    snapdragoncolor::ColorMode stc_mode = stc_mode_list_.list[i];
    if (stc_mode.intent == snapdragoncolor::kNative) {
      // Setting Max for native mode gamut and gamma
      stc_mode.gamut = allow_tonemap_native ? ColorPrimaries_BT709_5 : ColorPrimaries_Max;
      stc_mode.gamma = allow_tonemap_native ? Transfer_sRGB : Transfer_Max;
      color_mode_map_[ColorPrimaries_BT709_5][Transfer_sRGB][snapdragoncolor::kNative][kSdrType] =
        stc_mode;
      DLOGI("Color mode NATIVE supported");
    } else {
      ColorPrimaries gamut = static_cast<ColorPrimaries>(stc_mode.gamut);
      GammaTransfer gamma = static_cast<GammaTransfer>(stc_mode.gamma);
      snapdragoncolor::RenderIntent intent =
          static_cast<snapdragoncolor::RenderIntent>(stc_mode.intent);
      DynamicRangeType dynamic_range = kSdrType;
      if (std::find(stc_mode.hw_assets.begin(), stc_mode.hw_assets.end(),
                    snapdragoncolor::kPbHdrBlob) != stc_mode.hw_assets.end()) {
        dynamic_range = kHdrType;
      }

      color_mode_map_[gamut][gamma][intent][dynamic_range] = stc_mode;
      DLOGI("Add into map: gamut %d, gamma %d, render_intent %d, dynamic_range %d", gamut, gamma,
            intent, dynamic_range);
    }
  }
}

bool SDMColorModeImplStc::IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                        snapdragoncolor::RenderIntent intent) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    return false;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    return false;
  }

  if (color_mode_map_[primary][transfer].find(intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    return false;
  }

  return true;
}

DisplayError SDMColorModeImplStc::GetValidColorMode(ColorPrimaries primary, GammaTransfer transfer,
                          snapdragoncolor::RenderIntent *intent, DynamicRangeType *dynamic_range) {
  if (color_mode_map_.find(primary) == color_mode_map_.end()) {
    DLOGI("Could not find color primary: %d", primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary].find(transfer) == color_mode_map_[primary].end()) {
    DLOGI("Could not find transfer %d in primary %d", transfer, primary);
    return kErrorNotSupported;
  }

  if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
    // Fall back to default render intent if current render intent is not supported
    DLOGW("Could not find intent %d, checking for intent %d in primary %d, transfer %d",
                                      *intent, DEFAULT_RENDER_INTENT, primary, transfer);
    *intent = DEFAULT_RENDER_INTENT;

    if (color_mode_map_[primary][transfer].find(*intent) ==
                                      color_mode_map_[primary][transfer].end()) {
      DLOGI("Failed to find intent %d in primary %d, transfer %d",
                                  *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }

  if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                              color_mode_map_[primary][transfer][*intent].end()) {
    // Fall back to default dynamic range if current render intent is not supported
    DLOGW("Could not find range %d, checking for range %d in intent %d, primary %d, transfer %d",
                              *dynamic_range, DEFAULT_DYNAMIC_RANGE, *intent, primary, transfer);
    *dynamic_range = DEFAULT_DYNAMIC_RANGE;

    if (color_mode_map_[primary][transfer][*intent].find(*dynamic_range) ==
                            color_mode_map_[primary][transfer][*intent].end()) {
      DLOGI("Failed to find range %d, in intent %d, primary %d, transfer %d",
                                  *dynamic_range, *intent, primary, transfer);
      return kErrorNotSupported;
    }
  }
  return kErrorNone;
}

DisplayError SDMColorModeImplStc::SetColorModeWithRenderIntent(snapdragoncolor::ColorMode color_mode,
                                                                bool hdr_present) {
  if (current_color_mode_.gamut == color_mode.gamut &&
      current_color_mode_.gamma == color_mode.gamma &&
      current_color_mode_.intent == color_mode.intent &&
      ((hdr_present && (curr_dynamic_range_ == kHdrType)) ||
      (!hdr_present && (curr_dynamic_range_ == kSdrType)))) {
    return kErrorNone;
  }

  ColorPrimaries primary = color_mode.gamut;
  GammaTransfer transfer = color_mode.gamma;
  snapdragoncolor::RenderIntent intent = color_mode.intent;
  DynamicRangeType dynamic_range = hdr_present ? kHdrType : kSdrType;

  DisplayError error = GetValidColorMode(primary, transfer, &intent, &dynamic_range);
  if (error != kErrorNone) {
    return error;
  }

  snapdragoncolor::ColorMode stc_mode = color_mode_map_[primary][transfer][intent][dynamic_range];
  DLOGI("Applying Stc mode (gamut %d gamma %d intent %d hw_assets.size %d)",
      stc_mode.gamut, stc_mode.gamma, stc_mode.intent, stc_mode.hw_assets.size());

  DLOGW("SetStcColorMode is unavailable through current SDM client interfaces");
  return kErrorNotSupported;
}

} // namespace sdm
