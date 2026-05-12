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
*
* Changes from Qualcomm Innovation Center, Inc. are provided under the following license:
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*
*/

#ifndef __SDM_DISPLAY_H__
#define __SDM_DISPLAY_H__

#include "sdm_interface_factory.h"
#include <sdm/core/sdm_types.h>
#include <sdmclient/sdm_display_intf_lifecycle.h>
#include <sdmclient/sdm_display_intf_drawcycle.h>
#include <sdmclient/sdm_display_intf_drawcycle_v2.h>
#include <sdmclient/sdm_display_intf_layer_builder.h>
#include <sdmclient/sdm_display_intf_caps.h>
#include <sdmclient/sdm_display_intf_settings.h>
#include <sdmclient/sdm_display_intf_sideband.h>
#include <sdmclient/sdm_display_intf_aiqe.h>
#include <debug_handler.h>
#include <utils/debug.h>
#include <utils/constants.h>
#include <utils/formats.h>
#include <private/color_params.h>
#include <private/snapdragon_color_intf.h>
#include <color_metadata.h>
#include <stdio.h>
#include <string>
#include <utility>
#include <map>
#include <vector>
#include <iostream>
#include <thread>
#include <atomic>
#include <memory>

#include "sdm-service/sdm_display_interface.h"
#include "sdm-service/sdm_display_debugger.h"
#include "sdm-service/sdm_display_buffer_allocator.h"
#include "sdm-service/sdm_display_buffer_sync_handler.h"
#include "sdm-service/sdm_display_socket_handler.h"
#include "sdm-service/sdm_compositor_callbacks.h"
#include "sdm-service/sdm_color_mode_intf.h"
#include "sdm-internal.h"
#include "drm_master.h"

namespace sdm {
using namespace drm_utils;

using std::vector;
using std::iterator;
using std::string;
using std::to_string;
using std::map;
using std::pair;
using std::fstream;

class SdmDisplayProxy;

enum SdmDisplayIntfType {null_disp, sdm_disp};

typedef SDMDisplayType DisplayType;

typedef std::map<uint32_t, HWDisplayInfo> SdmDisplaysInfo;

// map<display_id, sdm_display>
typedef std::map<uint32_t, SdmDisplayProxy *> CreatedDisplaysInfo;

class SdmDisplayInterface {
  public:
    virtual ~SdmDisplayInterface() {}

    virtual DisplayError CreateDisplay(uint32_t display_id) = 0;
    virtual DisplayError DestroyDisplay() = 0;
    virtual DisplayError Prepare(struct drm_output *output) = 0;
    virtual DisplayError Commit(struct drm_output *output) = 0;
    virtual DisplayError SetDisplayState(DisplayState state, bool teardown,
                                         shared_ptr<Fence> *release_fence) = 0;
    virtual DisplayError SetVSyncState(bool enable, struct drm_output *output) = 0;
    virtual DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config) = 0;
    virtual DisplayError RegisterCb(int display_id, vblank_cb_t vbcb, hotplug_cb_t hpcb = nullptr) = 0;
    virtual SdmDisplayIntfType GetDisplayIntfType() = 0;
    virtual DisplayError SetPanelBrightness(float brightness) = 0;
    virtual DisplayError GetPanelBrightness(float *brightness) = 0;
    virtual DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) = 0;
    static int GetDrmMasterFd();
    static void SetDrmMasterFd(int fd);
    struct drm_output *drm_output_;
    struct drm_output *prev_output_;
};

class SdmNullDisplay : public SdmDisplayInterface {
  public:
    SdmNullDisplay(DisplayType type,
                   std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf,
                   std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf,
                   std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf,
                   std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                   std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                   std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf,
                   std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf);
    ~SdmNullDisplay();

    SdmDisplayIntfType GetDisplayIntfType() {
      return null_disp;
    }
    DisplayError CreateDisplay(uint32_t display_id);
    DisplayError DestroyDisplay();
    DisplayError Prepare(struct drm_output *output);
    DisplayError Commit(struct drm_output *output);
    DisplayError SetDisplayState(DisplayState state, bool teardown,
                                 shared_ptr<Fence> *release_fence);
    DisplayError SetVSyncState(bool enable, struct drm_output *output);
    DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config);
    DisplayError RegisterCb(int display_id, vblank_cb_t vbcb, hotplug_cb_t hpcb = nullptr);
    DisplayError SetPanelBrightness(float brightness);
    DisplayError GetPanelBrightness(float *brightness);
    DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info);
};

class SdmDisplay : public SdmDisplayInterface, DisplayEventHandler, SdmDisplayDebugger {

 public:
    SdmDisplay(DisplayType type,
               std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf,
               std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf,
               std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf,
               std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
               std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
               std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf,
               std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf,
               SdmDisplayBufferAllocator *buffer_allocator);
    ~SdmDisplay();

    SdmDisplayIntfType GetDisplayIntfType() {
      return sdm_disp;
    }

    DisplayError CreateDisplay(uint32_t display_id);
    DisplayError DestroyDisplay();
    DisplayError Prepare(struct drm_output *output);
    DisplayError Commit(struct drm_output *output);
    DisplayError SetDisplayState(DisplayState state, bool teardown,
                                 shared_ptr<Fence> *release_fence);
    DisplayError SetVSyncState(bool enable, struct drm_output *output);
    DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config);
    DisplayError RegisterCb(int display_id, vblank_cb_t vbcb, hotplug_cb_t hpcb = nullptr);
    DisplayError SetPanelBrightness(float brightness);
    DisplayError GetPanelBrightness(float *brightness);

    int OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
    DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info);

 protected:
    virtual DisplayError VSync(const DisplayEventVSync &vsync);
    virtual DisplayError VSync(int fd, unsigned int sequence,
                               unsigned int tv_sec, unsigned int tv_usec,
                               void *data);
    virtual DisplayError PFlip(int fd, unsigned int sequence,
                               unsigned int tv_sec, unsigned int tv_usec,
                               void *data);
    virtual DisplayError CECMessage(char *message);
    virtual DisplayError HistogramEvent(int source_fd, uint32_t blob_id);

    /*! @brief Event handler for events received by Display HAL. */
    virtual DisplayError HandleEvent(DisplayEvent event);
    virtual DisplayError Refresh();
    virtual void MMRMEvent(bool restricted){
      return;
    }

 private:
    static const int kBufferDepth = 2;
    
    // Layer management using SDM 2.0 Layer Builder API
    DisplayError CreateLayersForFrame(struct drm_output *output);
    DisplayError DestroyLayersForFrame();
    DisplayError SetLayerProperties(int64_t layer_id, struct LayerGeometry *glayer);
    DisplayError FreeLayerGeometry(struct LayerGeometry *glayer);
    
    // Helper functions for SDM 2.0 API conversion
    SDMRegion ConvertToSDMRegion(const RectArray &rect_array);
    SDMTransform ConvertToSDMTransform(uint32_t transform);
    int32_t ConvertToSDMBlendMode(uint32_t blending);
    uint32_t ConvertSDMFormatToGbmFormat(uint32_t sdm_format);
    vendor_qti_hardware_display_common_PixelFormat ConvertSDMFormatToSnapAllocFormat(uint32_t sdm_format);
    
    // SnapHandle creation helper
    SnapHandle* CreateSnapHandleFromBuffer(struct drm_fb *fb, struct drm_output *output, 
                                           struct LayerGeometry *glayer, int64_t layer_id);
    int PrepareNormalLayerGeometry(struct drm_output *output,
                                   struct LayerGeometry **glayer,
                                   struct sdm_layer *sdm_layer);
    int PrepareFbLayerGeometry(struct drm_output *output,
                               struct LayerGeometry **fb_glayer);
    DisplayError PrePrepareLayerStack(struct drm_output *output);
    DisplayError PrePrepare(struct drm_output *output);
    DisplayError PostPrepare(struct drm_output *output);
    DisplayError PreCommit();

    DisplayError PostCommit();
    LayerBufferFormat GetSDMFormat(uint32_t src_fmt,
                                   struct LayerGeometryFlags flags);
    LayerBlending GetSDMBlending(uint32_t source);
    void DumpInputBuffers(void *compositor_output);
    void DumpOutputBuffer(const BufferInfo& buffer_info,
                          void *base, int fence);
    const char*  GetDisplayString();
    /* support functions */
    const char * FourccToString(uint32_t fourcc);
    uint32_t GetMappedFormatFromGbm(uint32_t fmt);
    bool GetVideoPresenceByFormatFromGbm(uint32_t fmt);
    uint32_t GetMappedFormatFromShm(uint32_t fmt);
    bool NeedConvertGbmFormat(struct weston_view *ev, uint32_t format);
    uint32_t ConvertToOpaqueGbmFormat(uint32_t format);
    void ComputeSrcDstRect(struct drm_output *output, struct weston_view *ev,
                                  struct Rect *src_ret, struct Rect *dst_ret);
    int  ComputeDirtyRegion(struct weston_view *ev, struct RectArray *dirty);
    uint8_t GetGlobalAlpha(struct weston_view *ev);
    int GetVisibleRegion(struct drm_output *output, struct weston_view *ev,
                         pixman_region32_t *aboved_opaque, struct RectArray *visible);
    bool IsTransparentGbmFormat(uint32_t format);
    void HandlePanelDead();
    void RefreshWithCachedLayerstack();
    snapdragoncolor::ColorMode SelectBestColorSpace(bool isHdrSupported);
    snapdragoncolor::ColorMode GetBestHDRColorMode(ColorPrimaries layer_gamut, GammaTransfer layer_gamma);

    std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf_;
    std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf_;
    std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf_;
    std::shared_ptr<SDMDisplayCapsIntf> caps_intf_;
    std::shared_ptr<SDMDisplaySettingsIntf> settings_intf_;
    std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf_;
    std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf_;
    SdmDisplayBufferAllocator *buffer_allocator_;
    SdmDisplaySocketHandler socket_handler_;
    DisplayEventHandler *client_event_handler_ = NULL;
    SDMDisplayType display_type_ = kDisplayMax;
    DisplayConfigVariableInfo variable_info_;
    HWDisplayInterfaceInfo hw_disp_info_;
    bool shutdown_pending_ = false;
    
    // SDM 2.0 Layer Management - store layer IDs instead of Layer objects
    std::vector<int64_t> layer_ids_;
    std::vector<int64_t> prev_layer_ids_;
    shared_ptr<Fence> prev_retire_fence_;
    shared_ptr<Fence> retire_fence_;
    bool hdr_present_ = false;
    
    int  display_id_ = -1;
    uint64_t display_handle_ = 0;
    uint32_t fps_ = 0;
    float max_luminance_ = 0.0;
    float max_average_luminance_ = 0.0;
    float min_luminance_ = 0.0;
    bool esd_reset_panel_ = false;

    int disable_hdr_handling_ = 1;
    bool hdr_supported_ = false;
    std::unique_ptr<SDMColorModeIntf> color_mode_intf_ = nullptr; 
  
    // SDM 2.0 Compositor Callbacks
    std::shared_ptr<SDMCompositorCallbacks> compositor_callbacks_;
    hotplug_cb_t hotplug_cb_ = nullptr;
};

class SdmDisplayProxy {
  public:
    SdmDisplayProxy(DisplayType type,
                    std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf,
                    std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf,
                    std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf,
                    std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                    std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                    std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf,
                    std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf,
                    SdmDisplayBufferAllocator *buffer_allocator);
    ~SdmDisplayProxy();

    DisplayError CreateDisplay(uint32_t display_id) {
      DisplayError rc = display_intf_->CreateDisplay(display_id);
      if (rc != kErrorNone)
        display_intf_ = &null_disp_;
      return kErrorNone;
    }
    DisplayError DestroyDisplay() { return display_intf_->DestroyDisplay(); }
    DisplayError Prepare(struct drm_output *output) {
      return display_intf_->Prepare(output);
    }
    DisplayError Commit(struct drm_output *output) {
      return display_intf_->Commit(output);
    }
    DisplayError SetDisplayState(DisplayState state, bool teardown,
                                 shared_ptr<Fence> *release_fence) {
      return display_intf_->SetDisplayState(state, teardown, release_fence);
    }
    DisplayError SetVSyncState(bool enable, struct drm_output *output) {
      return display_intf_->SetVSyncState(enable, output);
    }
    DisplayError GetDisplayConfiguration(struct DisplayConfigInfo *display_config) {
      return display_intf_->GetDisplayConfiguration(display_config);
    }
    DisplayError RegisterCbs(int display_id, sdm_cbs_t *cbs) {
      hotplug_cb_ = cbs->hotplug_cb;
      return display_intf_->RegisterCb(display_id, cbs->vblank_cb, cbs->hotplug_cb);
    }
    DisplayError SetPanelBrightness(float brightness) {
      return display_intf_->SetPanelBrightness(brightness);
    }
    DisplayError GetPanelBrightness(float *brightness) {
      return display_intf_->GetPanelBrightness(brightness);
    }
    int HandleHotplug(bool connected);

    DisplayError OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
    DisplayError GetHdrInfo(struct DisplayHdrInfo *display_hdr_info) {
      return display_intf_->GetHdrInfo(display_hdr_info);
    }

  private:
    // Uevent thread
    static void *UeventThread(void *context);
    void *UeventThreadHandler();

    SdmDisplayInterface *display_intf_;
    DisplayType disp_type_;
    std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf_;
    std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf_;
    std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf_;
    std::shared_ptr<SDMDisplayCapsIntf> caps_intf_;
    std::shared_ptr<SDMDisplaySettingsIntf> settings_intf_;
    std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf_;
    std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf_;
    SdmNullDisplay null_disp_;
    SdmDisplayBufferAllocator *buffer_allocator_;
    SdmDisplay sdm_disp_;
    std::thread uevent_thread_;
    bool uevent_thread_exit_ = false;
    const char *uevent_thread_name_ = "SDM_UeventThread";
    hotplug_cb_t hotplug_cb_;
};

}  // namespace sdm

#endif  // __SDM_DISPLAY_H__
