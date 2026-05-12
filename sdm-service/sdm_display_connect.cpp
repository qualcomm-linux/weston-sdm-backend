/*
* Copyright (c) 2017,2020-2021 The Linux Foundation. All rights reserved.
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
* Changes from Qualcomm Innovation Center are provided under the following license:
* Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include <display_properties.h>

#include "sdm-service/sdm_display.h"
#include "sdm-service/sdm_display_connect.h"
#include "sdm-service/uevent.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __CLASS__ "SdmDisplayConnect"
namespace sdm {

#define SDM_DISPLAY_DEBUG 0

enum {
       FAIL,
       SUCCESS
};

static int property_set(const char *key, const char *value) {
      return setenv(key, value, 1);
}

std::shared_ptr<SDMDisplayLifeCycleIntf> lifecycle_intf_ = nullptr;
std::shared_ptr<SDMDisplayDrawCycleIntfV2> drawcycle_intf_ = nullptr;
std::shared_ptr<SDMDisplayLayerBuilderIntf> layer_builder_intf_ = nullptr;
std::shared_ptr<SDMDisplayCapsIntf> caps_intf_ = nullptr;
std::shared_ptr<SDMDisplaySettingsIntf> settings_intf_ = nullptr;
std::shared_ptr<SDMDisplaySideBandIntf> sideband_intf_ = nullptr;
std::shared_ptr<SDMDisplayAiqeIntf> aiqe_intf_ = nullptr;
SdmDisplayBufferAllocator *buffer_allocator_;
SdmDisplayBufferSyncHandler buffer_sync_handler_;
SdmDisplaySocketHandler socket_handler_;
HWDisplayInterfaceInfo hw_disp_info_;

CreatedDisplaysInfo displays_;

HWDisplaysInfo hw_displays_info_ = {};
// ordered by output id
SdmDisplaysInfo sdm_displays_info_ = {};

SdmDisplayProxy *GetDisplayFromId(uint32_t display_id) {
    auto it = displays_.find(display_id);
    if (it == displays_.end()) {
        DLOGI("No display available with display_id: %d.", display_id);
        return NULL;
    }
    return it->second;
}

bool IsDisplayCreated(uint32_t display_id) {
    return displays_.find(display_id) != displays_.end();
}

int GetConnectedDisplaysIds(int num_displays, uint32_t *connector_ids) {
    if (!connector_ids) {
        return -1;
    }
    SdmDisplaysInfo::iterator iter = sdm_displays_info_.begin();
    int idx = 0;

    if (num_displays != sdm_displays_info_.size()) {
        DLOGI("Connected display sizes doesn't match.");
        return -1;
    }

    for (iter; iter != sdm_displays_info_.end(); ++iter) {
        connector_ids[idx++] = iter->first;
    }

    return 0;
}

static DisplayError
SetProperty(const char *property_name, const char *value)
{
  if (property_set(property_name, value) == 0) {
    return kErrorNone;
  }
  return kErrorNotSupported;
}

int CreateCore(bool use_pixman)
{
    DLOGI(">>> BACKEND CreateCore() START - use_pixman=%d", use_pixman);
    
    if (buffer_allocator_) {
        DLOGI("Allocator already created.");
        return kErrorNone;
    }
    
    DLOGI("Creating SdmDisplayBufferAllocator...");
    buffer_allocator_ = new SdmDisplayBufferAllocator(use_pixman);
    
    // Get the SDM 2.0 factory
    DLOGI("Getting SDM 2.0 Interface Factory...");
    SDMInterfaceFactory *factory = GetSDMInterfaceFactory();
    if (!factory) {
        DLOGI("Failed to get SDM interface factory");
        return kErrorUndefined;
    }
    DLOGI("SDM 2.0 Factory obtained");
    
    // Create all the interfaces - note they're Create not Get
    DLOGI("Creating SDM 2.0 interfaces...");
    lifecycle_intf_ = factory->CreateLifeCycleIntf();
    auto drawcycle_base = factory->CreateDrawCycleIntf();
    drawcycle_intf_ = std::dynamic_pointer_cast<SDMDisplayDrawCycleIntfV2>(drawcycle_base);
    layer_builder_intf_ = factory->CreateLayerBuilderIntf();
    caps_intf_ = factory->CreateCapsIntf();
    settings_intf_ = factory->CreateSettingsIntf();
    sideband_intf_ = factory->CreateSideBandIntf();
    aiqe_intf_ = factory->CreateAiqeIntf();
    
    if (!lifecycle_intf_ || !drawcycle_intf_) {
        DLOGI("Failed to create required SDM interfaces");
        return kErrorUndefined;
    }
    DLOGI("SDM 2.0 interfaces created successfully");
    
    // Initialize lifecycle interface with required parameters
    DLOGI("Initializing SDM 2.0 lifecycle interface...");
    DisplayError error = lifecycle_intf_->Init(buffer_allocator_, &socket_handler_, nullptr);
    if (error != kErrorNone) {
        DLOGI("Failed to initialize lifecycle interface, error = %d", error);
        return error;
    }
    
    DLOGI("<<< BACKEND CreateCore() END - SDM 2.0 Core initialized successfully");
    
    // Try draw method with proper enum values
    DisplayError try_error = lifecycle_intf_->TryDrawMethod(0, static_cast<DisplayDrawMethod>(0));
    if (try_error != kErrorNone) {
        DLOGI("TryDrawMethod returned error: %d (non-fatal)", try_error);
    }
    
    return kErrorNone;
}

int DestroyCore()
{
    // Minimal core teardown: destroy displays and allocator; ignore SDM 2.0 interfaces.
    auto it = displays_.begin();
    while (it != displays_.end()) {
        uint32_t connector_id = it->first;
        delete displays_[connector_id];
        it = displays_.erase(it);
    }

    lifecycle_intf_.reset();
    drawcycle_intf_.reset();
    layer_builder_intf_.reset();
    caps_intf_.reset();
    settings_intf_.reset();
    sideband_intf_.reset();
    aiqe_intf_.reset();

    if (buffer_allocator_) {
        delete buffer_allocator_;
        buffer_allocator_ = nullptr;
    }

    return kErrorNone;
}

int GetFirstDisplayType(int *display_id)
{
    // Stub: return primary by default without SDM core query.
    if (display_id) *display_id = 0;
    return kErrorNone;
}

int CreateDisplay(uint32_t display_id)
{
    DLOGI(">>> BACKEND CreateDisplay() START - display_id=%d", display_id);
    
    DisplayError error = kErrorNone;
    HWDisplayInfo hw_display_info;

    if (IsDisplayCreated(display_id)) {
        DLOGI("Display(%d) was already created.", display_id);
        return kErrorNone;
    }

    auto it = hw_displays_info_.find(display_id);
    if (it == hw_displays_info_.end()) {
        DLOGI("No display information available for display_id: %d.", display_id);
        return kErrorParameters;
    }
    hw_display_info = it->second;

    if (lifecycle_intf_ == nullptr) {
        DLOGI("Core is not created yet.");
        return kErrorNotSupported;
    }

    SetProperty(DISABLE_SINGLE_LM_SPLIT_PROP, "1");

    DLOGI("Creating SdmDisplayProxy for display_type=%d...", hw_display_info.display_type);
    SdmDisplayProxy *sdm_display = new SdmDisplayProxy(hw_display_info.display_type,
                                                       lifecycle_intf_,
                                                       drawcycle_intf_,
                                                       layer_builder_intf_,
                                                       caps_intf_,
                                                       settings_intf_,
                                                       sideband_intf_,
                                                       aiqe_intf_,
                                                       buffer_allocator_);

    displays_[display_id] = sdm_display;
    
    DLOGI("Calling SdmDisplayProxy->CreateDisplay()...");
    error = displays_[display_id]->CreateDisplay(display_id);
    if (error != kErrorNone) {
        DLOGI("Failed to create display(%d), error=%d", display_id, error);
        delete displays_[display_id];
        displays_[display_id] = NULL;
        displays_.erase(display_id);
        return error;
    }

    DLOGI("<<< BACKEND CreateDisplay() END - Display(%d) created successfully", display_id);
    return kErrorNone;
}

int Prepare(uint32_t display_id, struct drm_output *output)
{
    DLOGI(">>> BACKEND Prepare() START - display_id=%d", display_id);
    
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    DLOGI("Calling SdmDisplayProxy->Prepare()...");
    error = dpy->Prepare(output);
    if (error != kErrorNone) {
        DLOGI("SdmDisplayProxy->Prepare() FAILED with error = %d", error);
        return error;
    }

    DLOGI("<<< BACKEND Prepare() END - SUCCESS");
    return kErrorNone;
}

int Commit(uint32_t display_id, struct drm_output *output)
{
    DLOGI(">>> BACKEND Commit() START - display_id=%d", display_id);
    
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    DLOGI("Calling SdmDisplayProxy->Commit()...");
    error = dpy->Commit(output);
    if (error != kErrorNone) {
        DLOGI("SdmDisplayProxy->Commit() FAILED with error = %d", error);
        return error;
    }

    DLOGI("<<< BACKEND Commit() END - SUCCESS");
    return kErrorNone;
}

int DestroyDisplay(uint32_t display_id)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->DestroyDisplay();
    if (error != kErrorNone) {
        DLOGI("function failed with error = %d", error);
        return error;
    }

    delete displays_[display_id];
    displays_[display_id] = NULL;
    displays_.erase(display_id);

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

bool GetDisplayConfiguration(uint32_t display_id, struct DisplayConfigInfo *display_config)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->GetDisplayConfiguration(display_config);

    if (error != kErrorNone) {
        DLOGI("function failed with error = %d", error);
        return FAIL;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return SUCCESS;
}

bool GetDisplayHdrInfo(uint32_t display_id, struct DisplayHdrInfo *display_hdr_info)
{
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return SUCCESS;
}

int RegisterCbs(uint32_t display_id, sdm_cbs *cbs) {
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->RegisterCbs(display_id, cbs);

    if (error != kErrorNone) {
        DLOGI("function failed with error = %d", error);
        return error;
    }

    return kErrorNone;
}

int get_drm_master_fd(void) {

    int fd = SdmDisplayInterface::GetDrmMasterFd();

    #if SDM_DISPLAY_DEBUG
    DLOGD("fd is: %d \n", fd);
    #endif

    return fd;
}


void set_drm_master_fd(int fd) {

    SdmDisplayInterface::SetDrmMasterFd(fd);
    #if SDM_DISPLAY_DEBUG
    DLOGD("master fd is: %d \n", fd);
    #endif
}

int SetDisplayState(uint32_t display_id, int power_mode) {
    DisplayError error = kErrorNone;
    bool teardown;
    shared_ptr<Fence> release_fence;
    sdm::DisplayState disp_state;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    if (power_mode == WESTON_DPMS_ON) {
        teardown = false;
        disp_state = kStateOn;
    } else {
        teardown = true;
        disp_state = kStateOff;
    }
    /* When WESTON_DPMS_ON == 0, set state ON (kStateOn)     */
    /* for all other power modes, i.e. WESTON_DPMS_STANDBY,  */
    /* WESTON_DPMS_SUSPEND, WESTON_DPMS_OFF turn off display */
    /* set state off (kStateOff)                             */
    error = dpy->SetDisplayState(disp_state, teardown,
                                                  &release_fence);
    if (error != kErrorNone) {
        DLOGI("function failed with error = %d", error);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

int SetVSyncState(uint32_t display_id, bool state, struct drm_output *output)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    error = dpy->SetVSyncState(state, output);
    if (error != kErrorNone) {
        DLOGI("function failed with error = %d", error);
        return error;
    }

    #if SDM_DISPLAY_DEBUG
    DLOGD("function successful.");
    #endif

    return kErrorNone;
}

int SetPanelBrightness(int display_id, float brightness)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }

    return dpy->SetPanelBrightness(brightness);
}

int GetPanelBrightness(int display_id, float *brightness)
{
    DisplayError error = kErrorNone;
    SdmDisplayProxy *dpy = GetDisplayFromId(display_id);
    if (!dpy) {
        DLOGI("Failed as Display (%d) not created yet.", display_id);
        return kErrorNotSupported;
    }
    return dpy->GetPanelBrightness(brightness);
}

uint32_t GetDisplayCount(void) {
  uint32_t count = 0;

  count = sdm_displays_info_.size();

  return count;
}


void HandlePrimaryDisplayInfo() {
  HWDisplaysInfo::iterator iter = hw_displays_info_.begin();
  sdm_displays_info_.clear();

  for (iter; iter != hw_displays_info_.end(); ++iter) {
    if (!iter->second.is_primary)
      continue;
    if (iter->second.display_type == sdm::kVirtual)
      continue;
    if (!iter->second.is_connected)
      continue;

    // only one primary display
    sdm_displays_info_[iter->first] = iter->second;
    break;
  }
}

void HandleExternalDisplayInfos() {
  HWDisplaysInfo::iterator iter = hw_displays_info_.begin();

  for (iter; iter != hw_displays_info_.end(); ++iter) {
    if (iter->second.is_primary)
      continue;

    if (iter->second.display_type == sdm::kVirtual)
      continue;

    if (!iter->second.is_connected)
      continue;

    sdm_displays_info_[iter->first] = iter->second;
  }
}

int GetDisplayInfos(void) {
  // Stub: single primary display connected
  hw_displays_info_.clear();
  HWDisplayInfo hw_info = {};
  hw_info.display_type = kBuiltIn;
  hw_info.is_connected = true;
  hw_info.is_primary = true;
  hw_info.display_id = 0;
  hw_displays_info_[hw_info.display_id] = hw_info;

  HandlePrimaryDisplayInfo();
  HandleExternalDisplayInfos();
  return 0;
}

char *GetConnectorName(uint32_t display_id) {
  char name[100]={};
  const char *type_name = NULL;
  auto iter = sdm_displays_info_.find(display_id);

  switch(iter->second.display_type) {
    case kBuiltIn:
      if (iter->second.is_primary) {
        type_name = "DSI-1";
      } else {
        type_name = "DSI-2";
      }
      break;
    case kPluggable:
      type_name = "DP";
      break;
    default:
      type_name = "unKnown";
      break;
  }

  snprintf(name, sizeof name, "%s-%d", type_name, display_id);

  return strdup(name);
}

uint32_t GetConnectorId(uint32_t display_id) {
  auto iter = sdm_displays_info_.find(display_id);
  if (iter == sdm_displays_info_.end()) {
    return -1;
  }
  return iter->second.display_id;
}

static HWDisplayInfo GetSdmDisplayInfo(int display_id) {
  auto iter = sdm_displays_info_.find(display_id);

  return iter->second;
}

int GetDisplayType(uint32_t display_id) {

  auto it = hw_displays_info_.find(display_id);
  if (it == hw_displays_info_.end()) {
    DLOGI("No display information available for display_id: %d.", display_id);
    return -1;
  }

  return (it->second.display_type);
}

void ClearSDMLayers(struct drm_output *output) {
  struct sdm_layer *sdm_layer, *tmp_layer;
  wl_list_for_each_safe(sdm_layer, tmp_layer, &output->prev_sdm_layer_list, link) {
    shared_ptr<Fence> release_fence = Fence::Create(INT(sdm_layer->release_fence_fd), "release");
    if (!Fence::Wait(release_fence)) {
      destroy_sdm_layer(sdm_layer);
    }
  }

  wl_list_init(&output->prev_sdm_layer_list);
  wl_list_for_each_safe(sdm_layer, tmp_layer, &output->sdm_layer_list, link) {
    wl_list_insert(output->prev_sdm_layer_list.prev, &sdm_layer->link);
  }

  wl_list_init(&output->sdm_layer_list);
}

}// namespace sdm
#ifdef __cplusplus
}
#endif
