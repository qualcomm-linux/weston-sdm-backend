/*
* Copyright (c) 2017, 2021 The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
 *
 * Copyright (c) 2023-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#include <assert.h>
#include <stdarg.h>
#include <linux/limits.h>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <ostream>
#include <fstream>
#include <utility>
#include <bitset>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/resource.h>

#include <gbm.h>

#include "sdm-service/sdm_display.h"
#include "sdm-service/uevent.h"

#include "sdm-internal.h"
#include "pixel-formats.h"
#include <ISnapMapper.h>
#include <ISnapAlloc.h>
#include <display-intf/common/PixelFormat.h>

#define __CLASS__ "SdmDisplay"
extern "C" void NotifyOnRefresh(struct drm_output *);

vblank_cb_t vblank_cb_;

namespace sdm {

using vendor::qti::hardware::display::snapalloc::ISnapAlloc;
using vendor::qti::hardware::display::snapalloc::ISnapMapper;
using vendor::qti::hardware::display::snapalloc::BufferDescriptor;
using vendor::qti::hardware::display::snapalloc::AllocationResult;

#define GET_GPU_TARGET_SLOT(max_layers) ((max_layers) - 1)
/* Cursor is fixed in (gpu_target_index-1) slot in SDM */
#define GET_CURSOR_SLOT(max_layers) ((max_layers) - 2)
#define LEN_LOCAL 2048

#define SDM_DISPLAY_DUMP_LAYER_STACK 0

#define SDM_DEAFULT_NULL_DISPLAY_WIDTH 1920
#define SDM_DEAFULT_NULL_DISPLAY_HEIGHT 1080
#define SDM_DEAFULT_NULL_DISPLAY_FPS 60
#define SDM_DEAFULT_NULL_DISPLAY_X_DPI 25.4
#define SDM_DEAFULT_NULL_DISPLAY_Y_DPI 25.4
#define SDM_DEAFULT_NULL_DISPLAY_IS_YUV false

#define MAX_PROP_STR_SIZE 64
#define SDM_NULL_DISPLAY_RESOLUTON_PROP_NAME "weston.sdm.default.resolution"
#define SDM_DISABLE_HDR_HANDLING "vendor.display.disable_hdr"

SdmDisplay::SdmDisplay(DisplayType type,
                       std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf,
                       std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf,
                       std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf,
                       std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                       std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                       std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf,
                       std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf,
                       SdmDisplayBufferAllocator *buffer_allocator) {
    // Map Weston DisplayType to SDMDisplayType
    switch (type) {
      case kPrimary:
        display_type_ = kBuiltIn;
        break;
      case kHDMI:
        display_type_ = kPluggable;
        break;
      case kVirtual:
        display_type_ = kVirtual;
        break;
      default:
        display_type_ = kBuiltIn;
        break;
    }

    lifecycle_intf_ = lifecycle_intf;
    drawcycle_intf_ = drawcycle_intf;
    layer_builder_intf_ = layer_builder_intf;
    caps_intf_ = caps_intf;
    settings_intf_ = settings_intf;
    sideband_intf_ = sideband_intf;
    aiqe_intf_ = aiqe_intf;
    buffer_allocator_ = buffer_allocator;
    drm_output_   = NULL;
    vblank_cb_    = NULL;
    
    // Create SDM 2.0 compositor callbacks
    compositor_callbacks_ = std::make_shared<SDMCompositorCallbacks>();
    DLOGI("SDMCompositorCallbacks created");
}

SdmDisplay::~SdmDisplay() {
    DestroyLayersForFrame();
}

const char * SdmDisplay::FourccToString(uint32_t fourcc)
{
    static __thread char s[4];
    uint32_t fmt = htole32(fourcc);

    memcpy(s, &fmt, 4);

    return s;
}

DisplayError SdmDisplay::CreateDisplay(uint32_t display_id) {
    DisplayError error = kErrorNone;
    struct DisplayHdrInfo display_hdr_info = {};

    DLOGI(">>> SdmDisplay::CreateDisplay START - display_id=%d, display_type_=%d", display_id, display_type_);

    char property[MAX_PROP_STR_SIZE] = {0};
    SdmDisplayDebugger::Get()->GetProperty(SDM_DISABLE_HDR_HANDLING, property);
    if (!(std::string(property)==std::string("1"))) {
        disable_hdr_handling_ = 0;
    }

    // SDM 2.0: Primary display is created during lifecycle_intf_->Init()
    // For now, just use display_id as handle and skip CreateDisplay call
    // TODO: Get proper display handle from lifecycle interface
    DLOGI("SDM 2.0: Skipping CreateDisplay call, using display_id=%d as handle", display_id);
    display_handle_ = display_id;
    display_id_ = display_id;
    
    DLOGI("SDM 2.0: Set display_handle_=%llu, display_id_=%d", display_handle_, display_id_);

    GetHdrInfo(&display_hdr_info);
    if (display_hdr_info.hdr_supported) {
        DLOGI("Display Device supports HDR functionality");
    } else {
        DLOGI("Display Device doesn't support HDR functionality");
    }
/*
    SDMColorModeIntfFactory *color_mode_factory = GetSDMColorModeIntfFactory();
    color_mode_intf_ = color_mode_factory->GetSDMColorModeIntf(display_type_, display_handle_,
                                                                caps_intf_, settings_intf_);
    if (color_mode_intf_) {
        error = color_mode_intf_->Init();
        if (error != kErrorNone) {
            DLOGW("Failed to initialize color mode intf, error %d !", error);
            color_mode_intf_ = nullptr;
        }
    } else {
        DLOGW("Display %d-%d, failed to create color mode intf!!", display_id_, display_type_);
    }
*/
    DLOGI("<<< SdmDisplay::CreateDisplay END - SUCCESS");
    return kErrorNone;
}

DisplayError SdmDisplay::DestroyDisplay() {
    DisplayError error = kErrorNone;

    if (display_handle_ != 0) {
        error = lifecycle_intf_->DestroyDisplay(display_handle_);
        display_handle_ = 0;
    }

    return error;
}

DisplayError SdmDisplay::VSync(const DisplayEventVSync &vsync) {
    if (vblank_cb_) {
      vblank_cb_(display_id_, vsync.timestamp, drm_output_);
    } else {
      DLOGI("vsync not registered");
    }

    return kErrorNone;
}

DisplayError SdmDisplay::VSync(int fd, unsigned int sequence, unsigned int tv_sec,
                               unsigned int tv_usec, void *data) {

    DLOGW("Not implemented\n");

    return kErrorNone;
}

DisplayError SdmDisplay::PFlip(int fd, unsigned int sequence, unsigned int tv_sec,
                               unsigned int tv_usec, void *data) {

    DLOGW("Not implemented");
    return kErrorNone;
}

DisplayError SdmDisplay::Refresh() {
    if (client_event_handler_) {
        client_event_handler_->Refresh();
    }

    return kErrorNone;
}

DisplayError SdmDisplay::CECMessage(char *message) {
    DLOGW("Not implemented");

    return kErrorNone;
}

DisplayError SdmDisplay::SetDisplayState(DisplayState state, bool teardown,
                                         shared_ptr<Fence> *release_fence) {
    DisplayError error;
    
    DLOGI(">>> SetDisplayState: state=%d, teardown=%d", state, teardown);
    
    // SDM 2.0: SetDisplayState is no-op in concurrency manager
    // Use SetPowerMode instead (also part of lifecycle_intf_)
    int32_t power_mode;
    if (state == kStateOn) {
        power_mode = 2;  // Power ON
    } else {
        power_mode = 0;  // Power OFF
    }
    
    error = lifecycle_intf_->SetPowerMode(display_handle_, power_mode);
    if (error != kErrorNone) {
        DLOGI("SDM 2.0 SetPowerMode failed. Error = %d", error);
        return error;
    }
    
    DLOGI("<<< SetDisplayState: SetPowerMode SUCCESS");
    return kErrorNone;
}

DisplayError SdmDisplay::HistogramEvent(int /* fd */, uint32_t /* blob_fd */) {
  return kErrorNone;
}

void SdmDisplay::RefreshWithCachedLayerstack()
{
    DisplayError error = kErrorNone;
    shared_ptr<Fence> retire_fence = nullptr;
    uint32_t num_types = 0;
    uint32_t num_requests = 0;
    bool needs_commit = false;

    if (prev_layer_ids_.empty() || !prev_output_) {
        DLOGI("No cached layer IDs or output for ESD recovery");
        return;
    }

    layer_ids_ = prev_layer_ids_;

    error = drawcycle_intf_->CommitOrPrepare(display_handle_, false, &retire_fence,
                                             &num_types, &num_requests, &needs_commit);
    if (error != kErrorNone) {
        DLOGI("CommitOrPrepare failed with error %d", error);
        return;
    }

    if (needs_commit) {
        error = drawcycle_intf_->PresentDisplay(display_handle_, &retire_fence);
        if (error != kErrorNone) {
            DLOGI("PresentDisplay failed with error %d", error);
            return;
        }
    }

    retire_fence_ = retire_fence;
    PostCommit();
    NotifyOnRefresh(prev_output_);
}

void SdmDisplay::HandlePanelDead()
{
    uint32_t i = 0;
    //TODO(user): extend for multi display if needed. currently handle for primary display.
    if (display_type_ != kPrimary) {
      DLOGI("Current display is not primary");
      return;
    }

    esd_reset_panel_ = true;
    shared_ptr<Fence> *release_fence = nullptr;
    DisplayError error = kErrorNone;

    DisplayState last_display_state = kStateOn;

    error = SetDisplayState(kStateOff, true, release_fence);
    if (error != kErrorNone) {
        DLOGI("Failed to power off the display with error %d", error);
        return;
    }

    error = SetDisplayState(last_display_state, false, release_fence);
    if (error != kErrorNone) {
        DLOGI("Failed to SetDisplayState with error %d", error);
        return;
    }

    do {
        error = drawcycle_intf_->SetVsyncEnabled(display_handle_, true);
        if (error != kErrorNone) {
            usleep(1000);
            i++;
        }
    } while (error && i < 16);

    if (error) {
        DLOGI("Failed to SetVsyncEnabled with error %d", error);
    }

    RefreshWithCachedLayerstack();
    esd_reset_panel_ = false;
}
/*
ColorMode SdmDisplay::GetBestHDRColorMode(ColorPrimaries layer_gamut,
                                                GammaTransfer layer_gamma) {
  ColorMode hdr_mode = {.gamut = layer_gamut,
                      .gamma = layer_gamma,
                      .intent = kRenderIntentColorimetric};

  if (color_mode_intf_->IsModeSupported(hdr_mode.gamut, hdr_mode.gamma, hdr_mode.intent)) {
    return hdr_mode;
  }

  // Fallback to Gamma HDR if HLG not present
  hdr_mode.gamma = Transfer_SMPTE_ST2084;
  if (color_mode_intf_->IsModeSupported(hdr_mode.gamut, hdr_mode.gamma, hdr_mode.intent)) {
    return hdr_mode;
  }

  hdr_mode.gamut = ColorPrimaries_Max;
  hdr_mode.gamma = Transfer_Max;
  return hdr_mode;
}

ColorMode SdmDisplay::SelectBestColorSpace(bool isHdrSupported) {
    ColorMode best_color_mode = {.gamut = ColorPrimaries_BT709_5,
                                .gamma = Transfer_sRGB,
                                .intent = kRenderIntentColorimetric};
    ColorMode best_hdr_mode = {.gamut = ColorPrimaries_Max,
                                .gamma = Transfer_Max,
                                .intent = kRenderIntentColorimetric};

    hdr_present_ = false;

    for (size_t i = 0; i < layer_ids_.size(); i++) {
        SDMColorMetadata color_metadata = {};
        DisplayError error = layer_builder_intf_->GetLayerColorMetadata(display_handle_, 
                                                                         layer_ids_[i], 
                                                                         &color_metadata);
        if (error != kErrorNone) {
            DLOGW("Failed to get color metadata for layer %zu, error %d", i, error);
            continue;
        }

        ColorMode color_mode = {.intent = kRenderIntentColorimetric};
        color_mode.gamut = static_cast<ColorPrimaries>(color_metadata.colorPrimaries);
        color_mode.gamma = static_cast<GammaTransfer>(color_metadata.transfer);

        bool hdr_layer = color_mode.gamut == ColorPrimaries_BT2020 &&
                         (color_mode.gamma == Transfer_SMPTE_ST2084 ||
                          color_mode.gamma == Transfer_HLG);

        if (!disable_hdr_handling_ && hdr_layer) {
            hdr_present_ = true;
        }

        switch (color_mode.gamut) {
            case ColorPrimaries_DCIP3:
                best_color_mode.gamut = ColorPrimaries_DCIP3;
                best_color_mode.gamma = Transfer_sRGB;
                break;
            case ColorPrimaries_BT2020:
                best_color_mode.gamut = ColorPrimaries_DCIP3;
                best_color_mode.gamma = Transfer_sRGB;

                if (!isHdrSupported) {
                    continue;
                }

                if (color_mode.gamma == Transfer_HLG &&
                    best_hdr_mode.gamma == Transfer_SMPTE_ST2084) {
                    continue;
                }
                best_hdr_mode = GetBestHDRColorMode(color_mode.gamut, color_mode.gamma);
                break;
            default:
                break;
        }
    }

    if (best_hdr_mode.gamut != ColorPrimaries_Max && best_hdr_mode.gamma != Transfer_Max) {
        return best_hdr_mode;
    }

    if (best_color_mode.gamut == ColorPrimaries_DCIP3) {
      if (color_mode_intf_->IsModeSupported(best_color_mode.gamut, best_color_mode.gamma,
                                            best_color_mode.intent)) {
        return best_color_mode;
      }

      best_color_mode.gamut = ColorPrimaries_BT2020;
      best_color_mode.gamma = Transfer_sRGB;
      if (color_mode_intf_->IsModeSupported(best_color_mode.gamut, best_color_mode.gamma,
                                            best_color_mode.intent)) {
        return best_color_mode;
      }

      best_color_mode.gamut = ColorPrimaries_BT709_5;
    }

    if (!color_mode_intf_->IsModeSupported(best_color_mode.gamut, best_color_mode.gamma,
                                            best_color_mode.intent)) {
      best_color_mode.intent = kRenderIntentNative;
    }

    return best_color_mode;
}
*/
DisplayError SdmDisplay::HandleEvent(DisplayEvent event) {
    switch (event) {
        case kPanelDeadEvent:
            HandlePanelDead();
            break;
        default:
            DLOGW("Unknown event: %d", event);
            break;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::SetVSyncState(bool VSyncState, struct drm_output *output) {
    DisplayError error;

    if (!output) {
        DLOGI("No output to set VSync");
        return kErrorNone;
    }

    drm_output_ = output;
    
    // Register SDM 2.0 callbacks now that output is ready
    if (VSyncState && compositor_callbacks_) {
        if (vblank_cb_) {
            compositor_callbacks_->SetVblankCallback(vblank_cb_, drm_output_, display_handle_);
            DLOGI("VBlank callback set");
        }
        if (hotplug_cb_) {
            compositor_callbacks_->SetHotplugCallback(hotplug_cb_, drm_output_, display_handle_);
            DLOGI("Hotplug callback set");
        }
        lifecycle_intf_->RegisterCompositorCallback(compositor_callbacks_.get(), true);
        DLOGI("SDM 2.0 compositor callbacks registered with output ready");
    }
    
    error = drawcycle_intf_->SetVsyncEnabled(display_handle_, VSyncState);
    if (error != kErrorNone) {
        DLOGI("VSync state setting failed. Error = %d", error);
        return error;
    }

    return kErrorNone;
}

DisplayError SdmDisplay::SetPanelBrightness(float brightness) {
    return settings_intf_->SetPanelBrightness(display_handle_, brightness);
}

DisplayError SdmDisplay::GetPanelBrightness(float *brightness) {
    return settings_intf_->GetPanelBrightness(display_handle_, brightness);
}

DisplayError SdmDisplay::GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
    DisplayError error = kErrorNone;
    std::vector<int32_t> configs;

    error = caps_intf_->GetDisplayConfigs(display_handle_, &configs);
    if (error != kErrorNone || configs.empty()) {
        DLOGI("GetDisplayConfigs failed. Error = %d", error);
        return error;
    }

    DisplayConfigFixedInfo fixed_info = {};
    error = caps_intf_->GetFixedConfig(display_handle_, &fixed_info);
    if (error != kErrorNone) {
        DLOGI("GetFixedConfig failed. Error = %d", error);
        return error;
    }

    DisplayConfigVariableInfo variable_info = {};
    variable_info.x_pixels = 1920;
    variable_info.y_pixels = 1080;
    variable_info.x_dpi = 25.4f;
    variable_info.y_dpi = 25.4f;
    variable_info.fps = 60;
    variable_info.vsync_period_ns = 16666666;
    variable_info.is_yuv = false;

    display_config->x_pixels     = variable_info.x_pixels;
    display_config->y_pixels     = variable_info.y_pixels;
    display_config->x_dpi        = variable_info.x_dpi;
    display_config->y_dpi        = variable_info.y_dpi;
    display_config->fps          = variable_info.fps;
    display_config->vsync_period_ns = variable_info.vsync_period_ns;
    display_config->is_yuv       = variable_info.is_yuv;
    fps_                         = variable_info.fps;
    display_config->is_connected = true;

    return kErrorNone;
}

DisplayError SdmDisplay::RegisterCb(int display_id, vblank_cb_t vbcb, hotplug_cb_t hpcb) {
    DisplayError error = kErrorNone;

    vblank_cb_   = vbcb;
    hotplug_cb_  = hpcb;
    display_id_  = display_id;
    
    // Note: drm_output_ is NULL at this point (output not created yet)
    // We'll register the callbacks later in SetVSyncState when output is ready
    DLOGI("VBlank and hotplug callbacks stored, will register with SDM 2.0 when output is ready");

    return error;
}

int SdmDisplay::OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level) {
  DisplayError error = drawcycle_intf_->MinHdcpEncryptionLevelChanged(display_handle_, min_enc_level);
  if (error != kErrorNone) {
    DLOGI("Failed. Error = %d", error);
    return -1;
  }

  return 0;
}

DisplayError SdmDisplay::CreateLayersForFrame(struct drm_output *output) {
    DisplayError error = kErrorNone;
    layer_ids_.clear();
    
    for (size_t i = 0; i < output->view_count - 1; i++) {
        int64_t layer_id = 0;
        error = layer_builder_intf_->CreateLayer(display_handle_, &layer_id);
        if (error != kErrorNone) {
            DLOGI("Failed to create layer %zu, error %d", i, error);
            DestroyLayersForFrame();
            return error;
        }
        layer_ids_.push_back(layer_id);
    }
    
    return kErrorNone;
}

DisplayError SdmDisplay::DestroyLayersForFrame() {
    DisplayError error = kErrorNone;
    
    for (int64_t layer_id : layer_ids_) {
        DisplayError destroy_error = layer_builder_intf_->DestroyLayer(display_handle_, layer_id);
        if (destroy_error != kErrorNone) {
            DLOGI("Failed to destroy layer %lld, error %d", layer_id, destroy_error);
            error = destroy_error;
        }
    }
    
    layer_ids_.clear();
    return error;
}

DisplayError SdmDisplay::SetLayerProperties(int64_t layer_id, struct LayerGeometry *glayer) {
    DisplayError error = kErrorNone;
    SDMRect src_crop = {
        static_cast<int>(glayer->src_rect.left),
        static_cast<int>(glayer->src_rect.top),
        static_cast<int>(glayer->src_rect.right),
        static_cast<int>(glayer->src_rect.bottom)
    };
    
    SDMRect display_frame = {
        static_cast<int>(glayer->dst_rect.left),
        static_cast<int>(glayer->dst_rect.top),
        static_cast<int>(glayer->dst_rect.right),
        static_cast<int>(glayer->dst_rect.bottom)
    };
    error = layer_builder_intf_->SetLayerSourceCrop(display_handle_, layer_id, src_crop);
    if (error != kErrorNone) {
        DLOGI("SetLayerSourceCrop failed, error %d", error);
        return error;
    }
    
    error = layer_builder_intf_->SetLayerDisplayFrame(display_handle_, layer_id, display_frame);
    if (error != kErrorNone) {
        DLOGI("SetLayerDisplayFrame failed, error %d", error);
        return error;
    }
    
    int32_t blend_mode = ConvertToSDMBlendMode(glayer->blending);
    error = layer_builder_intf_->SetLayerBlendMode(display_handle_, layer_id, blend_mode);
    if (error != kErrorNone) {
        DLOGI("SetLayerBlendMode failed, error %d", error);
        return error;
    }
    
    float alpha = static_cast<float>(glayer->plane_alpha) / 255.0f;
    error = layer_builder_intf_->SetLayerPlaneAlpha(display_handle_, layer_id, alpha);
    if (error != kErrorNone) {
        DLOGI("SetLayerPlaneAlpha failed, error %d", error);
        return error;
    }
    
    SDMTransform transform = ConvertToSDMTransform(glayer->transform);
    error = layer_builder_intf_->SetLayerTransform(display_handle_, layer_id, transform);
    if (error != kErrorNone) {
        DLOGI("SetLayerTransform failed, error %d", error);
        return error;
    }
    
    SDMRegion visible_region = ConvertToSDMRegion(glayer->visible_regions);
    error = layer_builder_intf_->SetLayerVisibleRegion(display_handle_, layer_id, visible_region);
    if (error != kErrorNone) {
        DLOGI("SetLayerVisibleRegion failed, error %d", error);
        return error;
    }
    
    SDMRegion damage_region = ConvertToSDMRegion(glayer->dirty_regions);
    error = layer_builder_intf_->SetLayerSurfaceDamage(display_handle_, layer_id, damage_region);
    if (error != kErrorNone) {
        DLOGI("SetLayerSurfaceDamage failed, error %d", error);
        return error;
    }
    
    SDMLayerTypes layer_type = kLayerApp;
    error = layer_builder_intf_->SetLayerType(display_handle_, layer_id, layer_type);
    if (error != kErrorNone) {
        DLOGI("SetLayerType failed, error %d", error);
        return error;
    }
    
    SDMLayerFlag layer_flag = glayer->flags.skip ? LAYER_FLAG_COMPATIBLE : LAYER_FLAG_DEFAULT;
    error = layer_builder_intf_->SetLayerFlag(display_handle_, layer_id, layer_flag);
    if (error != kErrorNone) {
        DLOGI("SetLayerFlag failed, error %d", error);
        return error;
    }
    
    int32_t composition_type = (glayer->composition == SDM_COMPOSITION_FB_TARGET) ? 
                               COMP_CLIENT : COMP_DEVICE;
	error = layer_builder_intf_->SetLayerCompositionType(display_handle_, layer_id, composition_type);
    if (error != kErrorNone) {
        DLOGI("SetLayerCompositionType failed, error %d", error);
        return error;
    }
    
    // SDM 2.0: Set layer color (solid color for the layer)
    // Using transparent/default color
    SDMColor layer_color = {};
    layer_color.r = 0;
    layer_color.g = 0;
    layer_color.b = 0;
    layer_color.a = 0;  // Transparent
    
    error = layer_builder_intf_->SetLayerColor(display_handle_, layer_id, layer_color);
    if (error != kErrorNone) {
        DLOGI("SetLayerColor failed, error %d (may be optional)", error);
        // Don't return error - this might be optional for non-solid-color layers
    }
    
    return kErrorNone;
}

SDMRegion SdmDisplay::ConvertToSDMRegion(const RectArray &rect_array) {
    SDMRegion region;
    region.num_rects = rect_array.count;
    region.rects.clear();
    
    for (uint32_t i = 0; i < rect_array.count; i++) {
        SDMRect rect = {
            static_cast<int>(rect_array.rects[i].left),
            static_cast<int>(rect_array.rects[i].top),
            static_cast<int>(rect_array.rects[i].right),
            static_cast<int>(rect_array.rects[i].bottom)
        };
        region.rects.push_back(rect);
    }
    
    return region;
}

SDMTransform SdmDisplay::ConvertToSDMTransform(uint32_t transform) {
    SDMTransform sdm_transform = TRANSFORM_NONE;
    
    if (transform & SDM_TRANSFORM_FLIP_H) {
        sdm_transform = static_cast<SDMTransform>(sdm_transform | TRANSFORM_FLIP_H);
    }
    if (transform & SDM_TRANSFORM_FLIP_V) {
        sdm_transform = static_cast<SDMTransform>(sdm_transform | TRANSFORM_FLIP_V);
    }
    if (transform & SDM_TRANSFORM_90) {
        sdm_transform = static_cast<SDMTransform>(sdm_transform | TRANSFORM_ROT_90);
    }
    
    return sdm_transform;
}

int32_t SdmDisplay::ConvertToSDMBlendMode(uint32_t blending) {
    switch (blending) {
        case SDM_BLENDING_PREMULTIPLIED:
            return 1;
        case SDM_BLENDING_COVERAGE:
            return 2;
        case SDM_BLENDING_NONE:
        default:
            return 0;
    }
}

uint32_t SdmDisplay::ConvertSDMFormatToGbmFormat(uint32_t sdm_format) {
    // Convert SDM backend format to GBM/DRM format for dumb buffer creation
    // Only formats with .depth and .bpp set in pixel_format_table work with dumb buffers
    // From the table: DRM_FORMAT_XRGB8888 (.depth=24, .bpp=32) and DRM_FORMAT_ARGB8888 (.depth=32, .bpp=32)
    // and DRM_FORMAT_RGB565 (.depth=16, .bpp=16) and DRM_FORMAT_XRGB1555 (.depth=15, .bpp=16)
    switch (sdm_format) {
        case SDM_BUFFER_FORMAT_RGBA_8888:
        case SDM_BUFFER_FORMAT_BGRA_8888:
        case SDM_BUFFER_FORMAT_ARGB_8888:
        case SDM_BUFFER_FORMAT_ABGR_8888:
            // All RGBA variants -> ARGB8888 (has .depth=32, .bpp=32)
            return DRM_FORMAT_ARGB8888;
        case SDM_BUFFER_FORMAT_RGBX_8888:
        case SDM_BUFFER_FORMAT_BGRX_8888:
            // All RGBX variants -> XRGB8888 (has .depth=24, .bpp=32)
            return DRM_FORMAT_XRGB8888;
        case SDM_BUFFER_FORMAT_RGB_565:
        case SDM_BUFFER_FORMAT_BGR_565:
            // RGB565 (has .depth=16, .bpp=16)
            return DRM_FORMAT_RGB565;
        default:
            DLOGI("Unsupported SDM format %d for dumb buffer, defaulting to XRGB8888", sdm_format);
            return DRM_FORMAT_XRGB8888;
    }
}

vendor_qti_hardware_display_common_PixelFormat SdmDisplay::ConvertSDMFormatToSnapAllocFormat(uint32_t sdm_format) {
    // Convert SDM backend format to SnapAlloc PixelFormat enum
    switch (sdm_format) {
        case SDM_BUFFER_FORMAT_RGBA_8888:
            return vendor_qti_hardware_display_common_PixelFormat::RGBA_8888;
        case SDM_BUFFER_FORMAT_RGBX_8888:
            return vendor_qti_hardware_display_common_PixelFormat::RGBX_8888;
        case SDM_BUFFER_FORMAT_RGB_888:
            return vendor_qti_hardware_display_common_PixelFormat::RGB_888;
        case SDM_BUFFER_FORMAT_RGB_565:
            return vendor_qti_hardware_display_common_PixelFormat::RGB_565;
        case SDM_BUFFER_FORMAT_BGRA_8888:
            return vendor_qti_hardware_display_common_PixelFormat::BGRA_8888;
        case SDM_BUFFER_FORMAT_RGBA_5551:
            return vendor_qti_hardware_display_common_PixelFormat::RGBA_5551;
        case SDM_BUFFER_FORMAT_RGBA_4444:
            return vendor_qti_hardware_display_common_PixelFormat::RGBA_4444;
        case SDM_BUFFER_FORMAT_BGRX_8888:
            return vendor_qti_hardware_display_common_PixelFormat::BGRX_8888;
        case SDM_BUFFER_FORMAT_BGR_565:
            return vendor_qti_hardware_display_common_PixelFormat::BGR_565;
        case SDM_BUFFER_FORMAT_YCbCr_422_SP:
            return vendor_qti_hardware_display_common_PixelFormat::YCBCR_422_SP;
        case SDM_BUFFER_FORMAT_YCrCb_420_SP:
            return vendor_qti_hardware_display_common_PixelFormat::YCrCb_420_SP;
        case SDM_BUFFER_FORMAT_YCbCr_420_SP:
        case SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
        case SDM_BUFFER_FORMAT_NV12_ENCODEABLE:
            return vendor_qti_hardware_display_common_PixelFormat::YCbCr_420_SP;
        case SDM_BUFFER_FORMAT_YCbCr_422_I:
            return vendor_qti_hardware_display_common_PixelFormat::YCBCR_422_I;
        case SDM_BUFFER_FORMAT_RGBA_2101010:
            return vendor_qti_hardware_display_common_PixelFormat::RGBA_1010102;
        case SDM_BUFFER_FORMAT_ABGR_2101010:
            return vendor_qti_hardware_display_common_PixelFormat::ABGR_2101010;
        case SDM_BUFFER_FORMAT_YV12:
            return vendor_qti_hardware_display_common_PixelFormat::YV12;
        case SDM_BUFFER_FORMAT_P010:
            return vendor_qti_hardware_display_common_PixelFormat::YCBCR_P010;
        case SDM_BUFFER_FORMAT_BGR_888:
            return vendor_qti_hardware_display_common_PixelFormat::BGR_888;
        case SDM_BUFFER_FORMAT_ARGB_8888:
            return vendor_qti_hardware_display_common_PixelFormat::BGRA_8888;
        case SDM_BUFFER_FORMAT_ABGR_8888:
            return vendor_qti_hardware_display_common_PixelFormat::RGBA_8888;
        default:
            DLOGI("Unsupported SDM format %d, defaulting to RGBA_8888", sdm_format);
            return vendor_qti_hardware_display_common_PixelFormat::RGBA_8888;
    }
}

SnapHandle* SdmDisplay::CreateSnapHandleFromBuffer(struct drm_fb *fb, struct drm_output *output,
                                                    struct LayerGeometry *glayer, int64_t layer_id) {
    struct drm_fb *fb_to_use = fb;
    bool created_dumb = false;
    
    // If no fb, create dumb buffer and mark for GPU composition
    if (!fb_to_use && glayer) {
        uint32_t gbm_format = ConvertSDMFormatToGbmFormat(glayer->format);
        DLOGI("No fb provided, creating dumb buffer as marked for client composition, width=%d height=%d sdm_format=%d gbm_format=0x%x\n", 
              glayer->width, glayer->height, glayer->format, gbm_format);
        fb_to_use = drm_fb_create_dumb(output->backend, 
                                       glayer->width, glayer->height,
                                       gbm_format);
        if (fb_to_use) {
            created_dumb = true;
            // Mark this layer for GPU composition
            if (layer_id >= 0) {
                layer_builder_intf_->SetLayerCompositionType(display_handle_, layer_id, COMP_CLIENT);
            }
        } else {
            DLOGI("Failed to create dumb buffer\n");
            return nullptr;
        }
    }
    
    // Create SnapHandle from fb
    if (!fb_to_use || fb_to_use->ion_fd < 0) {
        DLOGI("Invalid fb or ion_fd\n");
        if (created_dumb && fb_to_use) {
            drm_fb_unref(fb_to_use);
        }
        return nullptr;
    }
    
    // Get snapallocator instance
    if (buffer_allocator_->GetSnapInstance() != 0) {
        DLOGI("Failed to get snap instance");
        if (created_dumb && fb_to_use) {
            drm_fb_unref(fb_to_use);
        }
        return nullptr;
    }
    
    BufferDescriptor buf_desc = {};
    buf_desc.width = glayer ? glayer->width : fb_to_use->width;
    buf_desc.height = glayer ? glayer->height : fb_to_use->height;
    
    // Convert SDM format to SnapAlloc PixelFormat
    uint32_t sdm_format = glayer ? glayer->format : GetMappedFormatFromGbm(fb_to_use->format->format);
    buf_desc.format = ConvertSDMFormatToSnapAllocFormat(sdm_format);
    
    buf_desc.layerCount = 1;
    std::string name = std::string("SDM_Backend");
    auto nameLength = std::min(name.length(), static_cast<size_t>(QTI_MAX_NAME_LEN - 1));
    memcpy(buf_desc.name, name.data(), nameLength);

    AllocationResult snap_result;
    int buffer_count = 1;

    auto status = buffer_allocator_->snapallocator_->Allocate2(buf_desc, fb_to_use->ion_fd, 
                                                                 buffer_count, &snap_result);

    
	if (status != 0) {
      DLOGI("%s: Failed to allocate via SnapAlloc. Error code: %d", __FUNCTION__, static_cast<int>(status));
      if (created_dumb && fb_to_use) {
          drm_fb_unref(fb_to_use);
      }
      return nullptr;
    }
    
    DLOGI("Created SnapHandle: ion_fd=%d, %dx%d, stride=%d, format=0x%x, modifier=0x%llx%s\n",
          fb_to_use->ion_fd, fb_to_use->width, fb_to_use->height, 
          fb_to_use->strides[0], fb_to_use->format->format, fb_to_use->modifier,
          created_dumb ? " (dumb buffer)" : "");
    
    // Cleanup dumb buffer reference (SnapHandle now owns the data)
    if (created_dumb && fb_to_use) {
        drm_fb_unref(fb_to_use);
    }
    
    return snap_result.handles[0];
}


DisplayError SdmDisplay::FreeLayerGeometry(struct LayerGeometry *glayer) {
    if (glayer->dirty_regions.count)
        free(glayer->dirty_regions.rects);
    if (glayer->visible_regions.count)
        free(glayer->visible_regions.rects);

    free(glayer);

    return kErrorNone;
}


int SdmDisplay::PrepareFbLayerGeometry(struct drm_output *output,
                        struct LayerGeometry **fb_glayer) {
    struct LayerGeometry *fb_layer = NULL;
    *fb_glayer = fb_layer = reinterpret_cast<struct LayerGeometry *> \
                              (zalloc(sizeof *fb_layer));
    if (!fb_layer) {
        DLOGI("fb_layer is NULL\n");
        return -1;
    }

    int32_t mode_width = output->base.current_mode->width;
    int32_t mode_height = output->base.current_mode->height;
    fb_layer->width = mode_width;
    fb_layer->height = mode_height;
    fb_layer->unaligned_width = mode_width;
    fb_layer->unaligned_height = mode_height;

    fb_layer->format = GetMappedFormatFromGbm(output->format->format);
    fb_layer->composition = SDM_COMPOSITION_FB_TARGET;

    fb_layer->src_rect.left = (float)0.0;
    fb_layer->src_rect.top = (float)0.0;
    fb_layer->src_rect.right = (float)mode_width;
    fb_layer->src_rect.bottom = (float)mode_height;
    fb_layer->dst_rect.left = (float)0.0;
    fb_layer->dst_rect.top = (float)0.0;
    fb_layer->dst_rect.right = (float)mode_width;
    fb_layer->dst_rect.bottom = (float)mode_height;

    fb_layer->visible_regions.rects = reinterpret_cast<struct Rect *> \
                              (zalloc(sizeof(struct Rect)));
    if (fb_layer->visible_regions.rects == NULL) {
        DLOGI("out of memory for allocating fb visible region\n");
        return -1;
    }
    fb_layer->visible_regions.rects[0] = fb_layer->dst_rect;
    fb_layer->visible_regions.count = 1;

    fb_layer->dirty_regions.rects = reinterpret_cast<struct Rect *> \
                              (zalloc(sizeof(struct Rect)));
    if (fb_layer->dirty_regions.rects == NULL) {
        DLOGI("out of memory for allocating fb dirty region\n");
        return -1;
    }

    fb_layer->dirty_regions.rects[0] = fb_layer->dst_rect;
    fb_layer->dirty_regions.count = 1;

    fb_layer->blending = SDM_BLENDING_PREMULTIPLIED;
    /*
     * output->base.width/height should be already transformed, so just
     * keep no transform for output.
     */
    fb_layer->transform = SDM_TRANSFORM_NORMAL;
    fb_layer->plane_alpha = 0xFF;

    fb_layer->flags.skip = 0;
    fb_layer->flags.is_cursor = 0;
    fb_layer->flags.has_ubwc_buf = false;

    return 0;
}

int SdmDisplayInterface::GetDrmMasterFd() {
    DRMMaster *master = nullptr;
    int ret = DRMMaster::GetInstance(&master);
    int fd;

    if (ret < 0) {
        DLOGI("Failed to acquire DRMMaster instance");
        return kErrorNotSupported;
        }
    master->GetHandle(&fd);
    return fd;
}


void SdmDisplayInterface::SetDrmMasterFd(int fd) {
    DRMMaster::SetHandle(fd);
}

int SdmDisplay::PrepareNormalLayerGeometry(struct drm_output *output,
                         struct LayerGeometry **glayer,
                         struct sdm_layer *sdm_layer) {
    struct LayerGeometry *layer;
    struct weston_view *ev = sdm_layer->view;
    struct weston_surface *es = ev->surface;
    bool is_cursor = sdm_layer->is_cursor;
    uint32_t format = GBM_FORMAT_XBGR8888;
    pixman_region32_t r;

    *glayer = layer = reinterpret_cast<struct LayerGeometry *> \
                           (zalloc(sizeof *layer));
    if (!layer) {
        DLOGI("layer is NULL\n");
        return -1;
    }

    /* Prepare layer buffer information */
    layer->width = es->width;
    layer->height = es->height;
    layer->unaligned_width = es->width;
    layer->unaligned_height = es->height;
    layer->fb_id = -1;
    layer->format = SDM_BUFFER_FORMAT_RGBA_8888;  // Default to RGBA_8888 (value 2)

    if (sdm_layer->fb && sdm_layer->fb->bo) {
        struct gbm_bo *bo = sdm_layer->fb->bo;
        if (bo == NULL) {
            DLOGI("fail to import gbm bo!\n");
            return -1;
        } else {
            uint32_t width, height;

            //save gbm bo in sdm layer for future reference.
            sdm_layer->bo = bo;

            width = gbm_bo_get_width(bo);
            height = gbm_bo_get_height(bo);
            format = gbm_bo_get_format(bo);

            uint32_t alignedWidth = width;
            uint32_t alignedHeight = height;
            uint32_t secure_status = 0;
            uint32_t ubwc_status = 0;

            // Override buffer width/height to reflect aligned width and aligned height.
            layer->width = alignedWidth;
            layer->height = alignedHeight;
            layer->unaligned_width = width;
            layer->unaligned_height = height;
            layer->ion_fd = sdm_layer->fb->ion_fd;
            layer->flags.secure_present = secure_status;
            layer->flags.has_ubwc_buf = ubwc_status;

            // HDR handling disabled: LayerGeometry no longer has color_metadata
            layer->flags.hdr_present = false;
        }
    }

    if (NeedConvertGbmFormat(ev, format)) {
        format = ConvertToOpaqueGbmFormat(format);
	}
    layer->format = GetMappedFormatFromGbm(format);


    /* Get src/dst rect */
    ComputeSrcDstRect(output, ev, &layer->src_rect, &layer->dst_rect);
    /* Get visible rects */
    if (GetVisibleRegion(output, ev, &sdm_layer->overlap, &layer->visible_regions))
        return -1;
    /*
     * Get dirty rects. It's not surprised a view has null damage region, that means
     * the buffer content of view is not changed since last frame, eg. cursor.
     */
    if (ComputeDirtyRegion(ev, &layer->dirty_regions))
        return -1;

    /* Set no transform for view since bounding box already been applied by transform */
    layer->transform = SDM_TRANSFORM_NORMAL;
    /* Get global alpha */
    layer->plane_alpha = GetGlobalAlpha(ev);

    /* TODO: update property src_config, csc, color_fill, scaler ... */
    layer->flags.is_cursor = is_cursor;
    layer->flags.video_present = GetVideoPresenceByFormatFromGbm(format);

    /* compute whether this view has no blending */
    pixman_region32_init_rect(&r, 0, 0, ev->surface->width, ev->surface->height);
    pixman_region32_subtract(&r, &r, &ev->surface->opaque);

    if (!pixman_region32_not_empty(&r) && (layer->plane_alpha == 0xFF))
        layer->blending = SDM_BLENDING_NONE;
    else
        layer->blending = SDM_BLENDING_COVERAGE;

    // Video layers are always opaque
    if (layer->flags.video_present) {
        layer->blending = SDM_BLENDING_NONE;
    }

    /* Initialize all views with GPU composition first, SDM will update them after prepare */
    layer->composition = SDM_COMPOSITION_GPU;

    return 0;
}

DisplayError SdmDisplay::PrePrepareLayerStack(struct drm_output *output) {
    DisplayError error = kErrorNone;
    struct sdm_layer *sdm_layer = NULL;
    struct LayerGeometry *glayer = NULL;
    uint32_t gpu_target_index = GET_GPU_TARGET_SLOT(output->view_count);
    uint32_t index = 0;

    if (shutdown_pending_) {
        return kErrorShutDown;
    }

    DestroyLayersForFrame();
    
    error = CreateLayersForFrame(output);
    if (error != kErrorNone) {
        DLOGI("Failed to create layers for frame, error %d", error);
        return error;
    }

    DLOGI("gpu_target_index = %d\n", gpu_target_index);

    if (gpu_target_index > 0) {
        wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
            glayer = NULL;
            if (PrepareNormalLayerGeometry(output, &glayer, sdm_layer)) {
                DLOGI("fail to prepare normal layer geometry.");
                DestroyLayersForFrame();
                return kErrorUndefined;
            }

            shared_ptr<Fence> acquire_fence = Fence::Create(dup(sdm_layer->acquire_fence_fd), 
                                                            "App_Layer_Fence");
            

            if (sdm_layer->fb) {

            error = SetLayerProperties(layer_ids_[index], glayer);
            if (error != kErrorNone) {
                DLOGI("Failed to set layer properties for layer %d, error %d", index, error);
                FreeLayerGeometry(glayer);
                DestroyLayersForFrame();
                return error;
            }
			
            SnapHandle *snap_handle = CreateSnapHandleFromBuffer(sdm_layer->fb, output, glayer, layer_ids_[index]);

            error = layer_builder_intf_->SetLayerBuffer(display_handle_, layer_ids_[index], 
                                                        snap_handle, acquire_fence);

			if (error != kErrorNone) {
                DLOGI("Failed to set layer buffer for layer %d error=%d \n", index, error);
            } else {
                DLOGI("SetLayerBuffer SUCCESS for layer %d \n", index);
            }
            
            DLOGI("Acquire fence fd: %s for layer index %d \n",
                     Fence::GetStr(acquire_fence).c_str(), index);
            
            // Cleanup SnapHandle
            if (snap_handle) {
                snap_handle_delete(snap_handle);
            }
            }

            FreeLayerGeometry(glayer);
            index++;
        }
    }
	
    return kErrorNone;
}

DisplayError SdmDisplay::PrePrepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;

    error = PrePrepareLayerStack(output);
    if (error ) {
        DLOGI("function failed!\n", error);
    }
/*
    if (hdr_supported_ && !disable_hdr_handling_ && color_mode_intf_) {
        ColorMode mode = SelectBestColorSpace(hdr_supported_);
        DLOGV("Display: %d-%d Setting Best Color Mode: gamut %d gamma %d intent %d",
                display_id_, display_type_, mode.gamut, mode.gamma, mode.intent);
        error = color_mode_intf_->SetColorModeWithRenderIntent(mode, hdr_present_);
        if (error != kErrorNone) {
            DLOGI("Setting color mode failed, error %d !",error);
        }
    }
*/
    return error;
}

DisplayError SdmDisplay::PostPrepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;
    size_t index = 0;
    struct sdm_layer *sdm_layer = NULL;

    wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
      if (index >= layer_ids_.size()) {
        DLOGI("Layer index %zu out of bounds", index);
        break;
      }

      int32_t composition_type = 0;
      SDMCompositionType comp = layer_builder_intf_->GetDeviceSelectedCompositionType(display_handle_, 
                                                                                       layer_ids_[index]);
      if (comp == COMP_CLIENT) {
        sdm_layer->composition_type = SDM_COMPOSITION_GPU;
      } else {
        sdm_layer->composition_type = SDM_COMPOSITION_OVERLAY;
      }
      index++;
    }

    if (output->backend->use_pixman)
    {
        if (prev_retire_fence_) {
            int ret = -1;
            ret = Fence::Wait(prev_retire_fence_);
            if (ret != kErrorNone) {
                DLOGI("retire_fence wait timeout! ret=%d\n", ret);
            }
        }
    }

    return error;
}

DisplayError SdmDisplay::Prepare(struct drm_output *output)
{
    DisplayError error = kErrorNone;

    DLOGI("=== SDM 2.0 Prepare START === display_handle=%llu", display_handle_);

    if (esd_reset_panel_) {
      DLOGI("ESD reset panel in progress, skipping prepare");
      return kErrorNotSupported;
    }

    DLOGI("Calling PrePrepare...");
    error = PrePrepare(output);
    if (error != kErrorNone) {
        DLOGI("PrePrepare FAILED with error=%d", error);
        return error;
    }
    DLOGI("PrePrepare SUCCESS");

    shared_ptr<Fence> retire_fence = nullptr;
    uint32_t num_types = 0;
    uint32_t num_requests = 0;
    bool needs_commit = false;

    DLOGI("Calling SDM 2.0 CommitOrPrepare (prepare mode)...");
    error = drawcycle_intf_->CommitOrPrepare(display_handle_, false, &retire_fence,
                                             &num_types, &num_requests, &needs_commit);
    if (!(error == kErrorNone || error == kErrorNeedsCommit)) {
        DLOGI("SDM 2.0 CommitOrPrepare FAILED with error=%d (%s)", error, to_string(error).c_str());
        return error;
    }
    DLOGI("SDM 2.0 CommitOrPrepare SUCCESS: num_types=%u, num_requests=%u, needs_commit=%d",
          num_types, num_requests, needs_commit);

    DLOGI("Calling PostPrepare...");
    error = PostPrepare(output);
    if (error != kErrorNone) {
        DLOGI("PostPrepare FAILED with error=%d", error);
        return error;
    }
    DLOGI("PostPrepare SUCCESS");

    DLOGI("=== SDM 2.0 Prepare COMPLETE ===");
    return error;
}

DisplayError SdmDisplay::PreCommit()
{
    return kErrorNone;
}


DisplayError SdmDisplay::PostCommit()
{
    DisplayError error = kErrorNone;

    if (prev_retire_fence_) {
        int ret = -1;
        ret = Fence::Wait(prev_retire_fence_);
        if (ret != kErrorNone) {
            DLOGI("retire_fence wait timeout! ret=%d\n", ret);
        }
    }

    prev_layer_ids_ = layer_ids_;
    prev_retire_fence_ = retire_fence_;

    return error;
}

DisplayError SdmDisplay::Commit(struct drm_output *output)
{
    DTRACE_SCOPED();
    DisplayError ret = kErrorNone;

    DLOGI("=== SDM 2.0 Commit START === display_handle=%llu", display_handle_);

    if (esd_reset_panel_) {
      DLOGI("ESD reset panel in progress, skipping commit");
      return kErrorNotSupported;
    }

    size_t layer_count = layer_ids_.size();
    if (layer_count == 0) {
        DLOGI("No layers to commit");
        return kErrorUndefined;
    }
    DLOGI("Layer count: %zu", layer_count);

    size_t gpu_target_index = layer_count - 1;

    if (!output->next_fb) {
        DLOGI("Scanout state fb not found for output=%d", output->base.id);
        return kErrorUndefined;
    }

	struct LayerGeometry *glayer = NULL;
    int err = PrepareFbLayerGeometry(output, &glayer);
    if (err) {
        DLOGI("failed to prepare Layer Geometry Fb target\n");
        DestroyLayersForFrame();
        return kErrorUndefined;
    }
    SnapHandle *fb_snap_handle = CreateSnapHandleFromBuffer(output->next_fb, output, glayer, layer_ids_[gpu_target_index]);
	shared_ptr<Fence> fb_fence = Fence::Create(-1, "FB_Target_Fence");
    sdm::SDMRegion region = {};
	int32_t dataspace = 0;
	ret = drawcycle_intf_->SetClientTarget(display_id_, fb_snap_handle, fb_fence, dataspace, region, 0);
    ret = layer_builder_intf_->SetLayerBuffer(display_id_, layer_ids_[gpu_target_index], 
                                               fb_snap_handle, fb_fence);
   /* if (ret != kErrorNone) {
        DLOGI("Failed to set GPU target buffer, error %d", ret);
        if (fb_snap_handle) {
            snap_handle_delete(fb_snap_handle);
        }
        return ret;
    }*/
    DLOGI("GPU target buffer set SUCCESS");
    FreeLayerGeometry(glayer);     
    // Cleanup SnapHandle
    if (fb_snap_handle) {
        snap_handle_delete(fb_snap_handle);
    }

    DLOGI("Calling PreCommit...");
    PreCommit();
    prev_output_ = output;

    shared_ptr<Fence> retire_fence = nullptr;
    uint32_t num_types = 0;
    uint32_t num_requests = 0;
    bool needs_commit = false;

    DLOGI("Calling SDM 2.0 PresentDisplay...");
    ret = drawcycle_intf_->PresentDisplay(display_handle_, &retire_fence);
    if (ret != kErrorNone) {
        DLOGI("SDM 2.0 PresentDisplay FAILED with error %d (%s)", ret, to_string(ret).c_str());
        return ret;
    }
    DLOGI("SDM 2.0 PresentDisplay SUCCESS - FRAME SENT TO DISPLAY!");

    retire_fence_ = retire_fence;

    DLOGI("Calling PostCommit...");
    PostCommit();

    uint32_t num_fences = 0;
    LayerId *release_layer_ids = nullptr;
    std::vector<shared_ptr<Fence>> release_fences_vec;

    ret = drawcycle_intf_->GetReleaseFences(display_handle_, &num_fences, 
                                            release_layer_ids, &release_fences_vec);
    if (ret != kErrorNone) {
        DLOGI("Failed to get release fences, error %d", ret);
    } else {
        DLOGI("Got %u release fences", num_fences);
        struct sdm_layer *sdm_layer = NULL;
        size_t index = 0;
        wl_list_for_each_reverse(sdm_layer, &output->sdm_layer_list, link) {
            if (index < num_fences && index < release_fences_vec.size() && release_fences_vec[index]) {
                sdm_layer->release_fence_fd = Fence::Dup(release_fences_vec[index]);
            } else {
                sdm_layer->release_fence_fd = -1;
            }
            index++;
        }
    }

    DLOGI("=== SDM 2.0 Commit COMPLETE ===");
    return ret;
}

/* Adding following  support functions */
LayerBufferFormat SdmDisplay::GetSDMFormat(uint32_t src_fmt, struct LayerGeometryFlags flags)
{
    LayerBufferFormat format = kFormatInvalid;

    if (flags.has_ubwc_buf) {
        switch (src_fmt) {
            case SDM_BUFFER_FORMAT_RGBA_8888:
                format = sdm::kFormatRGBA8888Ubwc;
                break;
            case SDM_BUFFER_FORMAT_RGBX_8888:
                format = sdm::kFormatRGBX8888Ubwc;
                break;
            case SDM_BUFFER_FORMAT_BGR_565:
                format = sdm::kFormatBGR565Ubwc;
                break;
            case SDM_BUFFER_FORMAT_RGBA_2101010:
                format = sdm::kFormatRGBA1010102Ubwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
            case SDM_BUFFER_FORMAT_NV12_ENCODEABLE:
                format = sdm::kFormatYCbCr420SPVenusUbwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC:
                format = sdm::kFormatYCbCr420TP10Ubwc;
                break;
            case SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC:
                format = sdm::kFormatYCbCr420P010Ubwc;
                break;
            default:
                DLOGI("Unsupported UBWC format %d\n", src_fmt);
                return sdm::kFormatInvalid;
        }

        return format;
    }

    switch (src_fmt) {
        case SDM_BUFFER_FORMAT_RGBA_8888:
            format = sdm::kFormatRGBA8888;
            break;
        case SDM_BUFFER_FORMAT_RGBA_5551:
            format = sdm::kFormatRGBA5551;
            break;
        case SDM_BUFFER_FORMAT_RGBA_4444:
            format = sdm::kFormatRGBA4444;
            break;
        case SDM_BUFFER_FORMAT_BGRA_8888:
            format = sdm::kFormatBGRA8888;
            break;
        case SDM_BUFFER_FORMAT_RGBX_8888:
            format = sdm::kFormatRGBX8888;
            break;
        case SDM_BUFFER_FORMAT_BGRX_8888:
            format = sdm::kFormatBGRX8888;
            break;
        case SDM_BUFFER_FORMAT_RGB_888:
            format = sdm::kFormatRGB888;
            break;
        case SDM_BUFFER_FORMAT_RGB_565:
            format = sdm::kFormatRGB565;
            break;
        case SDM_BUFFER_FORMAT_BGR_565:
            format = sdm::kFormatBGR565;
            break;
        case SDM_BUFFER_FORMAT_NV12_ENCODEABLE:
        case SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS:
            format = sdm::kFormatYCbCr420SemiPlanarVenus;
            break;
        case SDM_BUFFER_FORMAT_ABGR_2101010:
            format = sdm::kFormatABGR2101010;
            break;
        case SDM_BUFFER_FORMAT_RGBA_2101010:
            format = sdm::kFormatRGBA1010102;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_TP10_UBWC:
            format = sdm::kFormatYCbCr420TP10Ubwc;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_P010_UBWC:
            format = sdm::kFormatYCbCr420P010Ubwc;
            break;
        case SDM_BUFFER_FORMAT_YV12:
            format = sdm::kFormatYCrCb420PlanarStride16;
            break;
        case SDM_BUFFER_FORMAT_YCrCb_420_SP:
            format = sdm::kFormatYCrCb420SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_420_SP:
            format = sdm::kFormatYCbCr420SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_422_SP:
            format = sdm::kFormatYCbCr422H2V1SemiPlanar;
            break;
        case SDM_BUFFER_FORMAT_YCbCr_422_I:
            format = sdm::kFormatYCbCr422H2V1Packed;
            break;
        case SDM_BUFFER_FORMAT_P010:
            format = sdm::kFormatYCbCr420P010;
            break;
        default:
            DLOGI("Unsupported format %d\n", src_fmt);
            return sdm::kFormatInvalid;
    }

    return format;
}

LayerBlending SdmDisplay::GetSDMBlending(uint32_t source)
{
    sdm::LayerBlending blending = sdm::kBlendingPremultiplied;

    switch (source) {
    case SDM_BLENDING_PREMULTIPLIED:
         blending = sdm::kBlendingPremultiplied;
         break;
    case SDM_BLENDING_COVERAGE:
         blending = sdm::kBlendingCoverage;
         break;
    case SDM_BLENDING_NONE:
    default:
         blending = sdm::kBlendingOpaque;
         break;
    }

    return blending;
}

bool SdmDisplay::GetVideoPresenceByFormatFromGbm(uint32_t fmt)
{
    bool is_video_present = false;

    switch (fmt) {
       case GBM_FORMAT_NV12:
       case GBM_FORMAT_NV21:
            is_video_present = true;
            break;
       default:
            is_video_present = false;
            break;
    }

    return is_video_present;
}

uint32_t SdmDisplay::GetMappedFormatFromGbm(uint32_t fmt)
{
    uint32_t ret = SDM_BUFFER_FORMAT_INVALID;

    switch (fmt) {
    case GBM_FORMAT_ARGB8888:
         ret = SDM_BUFFER_FORMAT_BGRA_8888;
         break;
    case GBM_FORMAT_XRGB8888:
         ret = SDM_BUFFER_FORMAT_BGRX_8888;
         break;
    case GBM_FORMAT_ABGR8888:
         ret = SDM_BUFFER_FORMAT_RGBA_8888;
         break;
    case GBM_FORMAT_XBGR8888:
         ret = SDM_BUFFER_FORMAT_RGBX_8888;
         break;
    case GBM_FORMAT_BGRA8888:
    case GBM_FORMAT_RGBA8888:
         ret = SDM_BUFFER_FORMAT_ARGB_8888;
         break;
    case GBM_FORMAT_ARGB4444:
    case GBM_FORMAT_ABGR4444:
         ret = SDM_BUFFER_FORMAT_RGBA_4444;
         break;
    case GBM_FORMAT_ARGB1555:
    case GBM_FORMAT_ABGR1555:
         ret = SDM_BUFFER_FORMAT_RGBA_5551;
         break;
    case GBM_FORMAT_RGB565:
         ret = SDM_BUFFER_FORMAT_BGR_565;
         break;
    case GBM_FORMAT_BGR565:
         ret = SDM_BUFFER_FORMAT_RGB_565;
         break;
    case GBM_FORMAT_RGB888:
         ret = SDM_BUFFER_FORMAT_BGR_888;
         break;
    case GBM_FORMAT_BGR888:
         ret = SDM_BUFFER_FORMAT_RGB_888;
         break;
    case GBM_FORMAT_NV12:
         ret = SDM_BUFFER_FORMAT_YCbCr_420_SP_VENUS;
         break;
    case GBM_FORMAT_ABGR2101010:
         ret = SDM_BUFFER_FORMAT_RGBA_2101010;
         break;
    default:
         DLOGI("Unsupported GBM format %s\n", FourccToString(fmt));
         break;
    }

    return ret;
}

bool SdmDisplay::NeedConvertGbmFormat(struct weston_view *ev, uint32_t format)
{
    pixman_region32_t r;
    bool need_convert = false;

    if (IsTransparentGbmFormat(format)) {
     pixman_region32_init_rect(&r, 0, 0,
             ev->surface->width,
             ev->surface->height);
     pixman_region32_subtract(&r, &r, &ev->surface->opaque);

     if (!pixman_region32_not_empty(&r))
         need_convert = true;

     pixman_region32_fini(&r);
    }

    return need_convert;
}

uint32_t SdmDisplay::ConvertToOpaqueGbmFormat(uint32_t format)
{
    uint32_t ret = GBM_FORMAT_XRGB8888;

    switch (format) {
    case GBM_FORMAT_ARGB8888:
     ret = GBM_FORMAT_XRGB8888;
     break;
    case GBM_FORMAT_BGRA8888:
     ret = GBM_FORMAT_BGRX8888;
     break;
    case GBM_FORMAT_RGBA8888:
     ret = GBM_FORMAT_RGBX8888;
     break;
    case GBM_FORMAT_ABGR8888:
     ret = GBM_FORMAT_XBGR8888;
     break;
    default:
     break;
    }

    return ret;
}

void SdmDisplay::ComputeSrcDstRect(struct drm_output *output, struct weston_view *ev,
                    struct Rect *src_ret, struct Rect *dst_ret)
{
    pixman_region32_t src_rect, dest_rect;
    float sx1, sy1, sx2, sy2;

    /* dst rect */
    pixman_region32_init(&dest_rect);
    pixman_region32_intersect(&dest_rect, &ev->transform.boundingbox, &output->base.region);

    pixman_region32_translate(&dest_rect, -output->base.pos.c.x, -output->base.pos.c.y);

    pixman_box32_t *box, tbox = {0};
    box = pixman_region32_extents(&dest_rect);

    sdm_weston_transformed_rect(output, &tbox, *box);

    dst_ret->left = tbox.x1;
    dst_ret->right = tbox.x2;
    dst_ret->top = tbox.y1;
    dst_ret->bottom = tbox.y2;

    pixman_region32_fini(&dest_rect);

    /* src rect */
    pixman_region32_init(&src_rect);
    pixman_region32_intersect(&src_rect, &ev->transform.boundingbox,
                              &output->base.region);
    box = pixman_region32_extents(&src_rect);

    sdm_weston_global_transform_rect(ev, box, &sx1, &sy1, &sx2, &sy2);

    pixman_region32_fini(&src_rect);

    src_ret->left = sx1;
    src_ret->top = sy1;
    src_ret->right = sx2;
    src_ret->bottom = sy2;
}

int SdmDisplay::ComputeDirtyRegion(struct weston_view *ev,
                    struct RectArray *dirty) {
    pixman_region32_t temp;
    struct weston_surface *es = ev->surface;
    pixman_box32_t *rectangles = NULL;
    int n, i;

    pixman_region32_init(&temp);
    pixman_region32_intersect_rect(&temp, &es->damage, 0,
                    0, es->width, es->height);
    rectangles = pixman_region32_rectangles(&temp, &n);
    if (!n) {
     pixman_region32_fini(&temp);
     return 0;
    }

    dirty->rects = reinterpret_cast<struct Rect *> \
                    (zalloc(n * sizeof(struct Rect)));
    if (dirty->rects == NULL) {
     DLOGI("out of memory for allocating dirty region\n");
     return -1;
    }
    for (i = 0; i < n; i++) {
     dirty->rects[i].left = (float)rectangles[i].x1;
     dirty->rects[i].top = (float)rectangles[i].y1;
     dirty->rects[i].right = (float)rectangles[i].x2;
     dirty->rects[i].bottom = (float)rectangles[i].y2;
    }
    dirty->count = n;
    pixman_region32_fini(&temp);

    return 0;
}

uint8_t SdmDisplay::GetGlobalAlpha(struct weston_view *ev)
{
    if (ev->alpha > 1.0f)
     return 0xFF;
    else if (ev->alpha < 0.0f)
     return 0;
    else
     return (uint8_t)(0xFF * ev->alpha);
}

int SdmDisplay::GetVisibleRegion(struct drm_output *output, struct weston_view *ev,
                                         pixman_region32_t *aboved_opaque,
                                         struct RectArray *visible)
{
    pixman_region32_t temp;
    pixman_box32_t *rectangles = NULL;
    int n, i;

    pixman_region32_init(&temp);
    pixman_region32_copy(&temp, &ev->transform.boundingbox);
    pixman_region32_subtract(&temp, &temp, aboved_opaque);
    pixman_region32_intersect(&temp, &output->base.region, &temp);
    pixman_region32_translate(&temp, -output->base.pos.c.x, -output->base.pos.c.y);

    rectangles = pixman_region32_rectangles(&temp, &n);
    if (!n) {
     pixman_region32_fini(&temp);
     return 0;
    }

    visible->rects = reinterpret_cast<struct Rect *> (zalloc(n * sizeof(struct Rect)));
    if (visible->rects == NULL) {
     DLOGI("out of memory for allocating visible region\n");
     return -1;
    }
    for (i = 0; i < n; i++) {
     visible->rects[i].left = (float)rectangles[i].x1;
     visible->rects[i].top = (float)rectangles[i].y1;
     visible->rects[i].right = (float)rectangles[i].x2;
     visible->rects[i].bottom = (float)rectangles[i].y2;
    }
    visible->count = n;
    pixman_region32_fini(&temp);

    return 0;
}

bool SdmDisplay::IsTransparentGbmFormat(uint32_t format)
{
    return false;
}

const char *SdmDisplay::GetDisplayString() {
    switch (display_type_) {
    case kPrimary:
      return "primary";
    case kHDMI:
      return "hdmi";
    case kVirtual:
      return "virtual";
    default:
      return "invalid";
  }
}

DisplayError SdmDisplay::GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
  DisplayError error = kErrorNone;

  uint32_t num_types = 0;
  std::vector<int32_t> types(16);
  float max_lum = 0.0f, avg_lum = 0.0f, min_lum = 0.0f;

  error = caps_intf_->GetHdrCapabilities(display_handle_,
                                         &num_types,
                                         types.data(),
                                         &max_lum,
                                         &avg_lum,
                                         &min_lum);
  if (error != kErrorNone) {
      DLOGI("GetHdrCapabilities failed. Error = %d", error);
      display_hdr_info->hdr_supported = false;
      return error;
  }

  hdr_supported_ = (num_types > 0);

  display_hdr_info->hdr_supported = hdr_supported_;
  display_hdr_info->hdr_eotf = 0;
  display_hdr_info->hdr_metadata_type_one = false;
  display_hdr_info->max_luminance = max_lum;
  display_hdr_info->average_luminance = avg_lum;
  display_hdr_info->min_luminance = min_lum;

  return error;
}

SdmNullDisplay::SdmNullDisplay(DisplayType type,
                               std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf,
                               std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf,
                               std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf,
                               std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                               std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                               std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf,
                               std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf) {
}

SdmNullDisplay::~SdmNullDisplay() {
}

DisplayError SdmNullDisplay::CreateDisplay(uint32_t display_id) {
  return kErrorNone;
}
DisplayError SdmNullDisplay::DestroyDisplay() {
  return kErrorNone;
}
DisplayError SdmNullDisplay::Prepare(struct drm_output *output) {
  return kErrorNone;
}
DisplayError SdmNullDisplay::Commit(struct drm_output *output) {
  /**
   * TODO: We need to handle releasing the buffer references such that
   * the video buffers/frames keep moving forward in time even though
   * not displayed. This will be done at a later point of time.
   */
  return kErrorNone;
}
DisplayError SdmNullDisplay::SetDisplayState(DisplayState state, bool teardown,
                                             shared_ptr<Fence> *release_fence) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetPanelBrightness(float brightness) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::GetPanelBrightness(float *brightness) {
  return kErrorNone;
}

DisplayError SdmNullDisplay::SetVSyncState(bool enable, struct drm_output *output) {
  /**
   * TODO: drm_output_ needs to be re-initialized based on the preferred supported mode
   *       of the plugged-in display. The recent Weston release contains better APIs
   *       to handle this case. Hence this implementation will be improved based upon
   *       the recent Weston release updates.
   */
  drm_output_ = output;
  return kErrorNone;
}

DisplayError SdmNullDisplay::GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
  uint32_t props_value[3] = {0};
  char null_display_props[MAX_PROP_STR_SIZE] = {0};
  char *prop = NULL, *saveptr = NULL;

  // sdm.null.resolution format is width:height:fps
  SdmDisplayDebugger::Get()->GetProperty(SDM_NULL_DISPLAY_RESOLUTON_PROP_NAME, null_display_props);

  prop = strtok_r(null_display_props, ":", &saveptr);
  for (int i =0; i<3 && prop != NULL; i++)
  {
    props_value[i] = UINT32(atoi(prop));
    prop = strtok_r(NULL, ":", &saveptr);
  }

  if (props_value[0] == 0 || props_value[1] == 0) {
    display_config->x_pixels = SDM_DEAFULT_NULL_DISPLAY_WIDTH;
    display_config->y_pixels = SDM_DEAFULT_NULL_DISPLAY_HEIGHT;
  } else {
    display_config->x_pixels = props_value[0];
    display_config->y_pixels = props_value[1];
  }

  if (props_value[2] == 0)
    display_config->fps = SDM_DEAFULT_NULL_DISPLAY_FPS;
  else
    display_config->fps = props_value[2];

  display_config->x_dpi = SDM_DEAFULT_NULL_DISPLAY_X_DPI;
  display_config->y_dpi = SDM_DEAFULT_NULL_DISPLAY_Y_DPI;
  display_config->vsync_period_ns = UINT32(1000000000/display_config->fps);
  display_config->is_yuv = SDM_DEAFULT_NULL_DISPLAY_IS_YUV;

  return kErrorNone;
}
DisplayError SdmNullDisplay::RegisterCb(int display_id, vblank_cb_t vbcb, hotplug_cb_t hpcb) {
  vblank_cb_   = vbcb;

  return kErrorNone;
}

DisplayError SdmNullDisplay::GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
  return kErrorNone;
}

SdmDisplayProxy::SdmDisplayProxy(DisplayType type,
                                 std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf,
                                 std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf,
                                 std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf,
                                 std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                                 std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                                 std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf,
                                 std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf,
                                 SdmDisplayBufferAllocator *buffer_allocator)
  : disp_type_(type),
    lifecycle_intf_(lifecycle_intf),
    drawcycle_intf_(drawcycle_intf),
    layer_builder_intf_(layer_builder_intf),
    caps_intf_(caps_intf),
    settings_intf_(settings_intf),
    sideband_intf_(sideband_intf),
    aiqe_intf_(aiqe_intf),
    sdm_disp_(type, lifecycle_intf, drawcycle_intf, layer_builder_intf,
              caps_intf, settings_intf, sideband_intf, aiqe_intf, buffer_allocator),
    null_disp_(type, lifecycle_intf, drawcycle_intf, layer_builder_intf,
               caps_intf, settings_intf, sideband_intf, aiqe_intf) {

    display_intf_ = &sdm_disp_;
    buffer_allocator_ = buffer_allocator;
    std::thread uevent_thread(UeventThread, this);
    uevent_thread_.swap(uevent_thread);
}

SdmDisplayProxy::~SdmDisplayProxy () {
    uevent_thread_exit_ = true;
    uevent_thread_.detach();
}

int SdmDisplayProxy::HandleHotplug(bool connected) {
  return kErrorNone;
}

void *SdmDisplayProxy::UeventThread(void *context) {
  if (context) {
    return reinterpret_cast<SdmDisplayProxy *>(context)->UeventThreadHandler();
  }

  return NULL;
}

#define HAL_PRIORITY_URGENT_DISPLAY (-8)

void *SdmDisplayProxy::UeventThreadHandler() {
  static char uevent_data[4096];
  int length = 0;
  prctl(PR_SET_NAME, uevent_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
  if (!uevent_init()) {
    DLOGW("Failed to init uevent");
    pthread_exit(0);
    return NULL;
  }

  DLOGI("SdmDisplayProxy::UeventThreadHandler");

  while (!uevent_thread_exit_) {
    // keep last 2 zeroes to ensure double 0 termination
    length = uevent_next_event(uevent_data, INT32(sizeof(uevent_data)) - 2);
    string s(uevent_data, length);

    if (s.find("name=DSI-1") != string::npos) {
      bool connected = s.find("status=connected") != string::npos;
      DLOGI("DSI is %s !\n", connected ? "connected" : "removed");
      HandleHotplug(connected);
    }
  }
  pthread_exit(0);

  return NULL;
}

}  // namespace sdm
