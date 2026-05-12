/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "sdm_compositor_callbacks.h"
#include <utils/debug.h>

#define __CLASS__ "SDMCompositorCallbacks"

extern "C" void NotifyOnRefresh(struct drm_output *);

namespace sdm {

// Initialize static members
std::map<uint64_t, SDMCompositorCallbacks::CallbackData> SDMCompositorCallbacks::callback_map_;
std::mutex SDMCompositorCallbacks::callback_mutex_;

SDMCompositorCallbacks::SDMCompositorCallbacks() = default;

SDMCompositorCallbacks::~SDMCompositorCallbacks() = default;

void SDMCompositorCallbacks::SetVblankCallback(vblank_cb_t callback, struct drm_output *output, uint64_t display_id) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    DLOGI("SetVblankCallback: this=%p, display_id=%llu, callback=%p, output=%p", 
          this, (unsigned long long)display_id, callback, output);
    
    callback_map_[display_id].vblank_cb = callback;
    callback_map_[display_id].vblank_output = output;
    
    DLOGI("SetVblankCallback: Stored in static map for display %llu", (unsigned long long)display_id);
}

void SDMCompositorCallbacks::SetHotplugCallback(hotplug_cb_t callback, struct drm_output *output, uint64_t display_id) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    DLOGI("SetHotplugCallback: this=%p, display_id=%llu, callback=%p, output=%p", 
          this, (unsigned long long)display_id, callback, output);
    
    callback_map_[display_id].hotplug_cb = callback;
    callback_map_[display_id].hotplug_output = output;
}

void SDMCompositorCallbacks::OnHotplug(uint64_t in_display, bool in_connected) {
    DLOGI("OnHotplug ENTRY: this=%p, display=%llu, connected=%d", 
          this, (unsigned long long)in_display, in_connected);
    
    // Get callback data without holding lock during callback execution
    hotplug_cb_t callback = nullptr;
    struct drm_output *output = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        auto it = callback_map_.find(in_display);
        if (it == callback_map_.end()) {
            DLOGW("OnHotplug: No callback data found for display %llu", (unsigned long long)in_display);
            return;
        }
        callback = it->second.hotplug_cb;
        output = it->second.hotplug_output;
    }
    
    DLOGI("OnHotplug: Found callback data - hotplug_cb=%p, hotplug_output=%p", callback, output);
    
    if (callback && output) {
        DLOGI("OnHotplug: Calling callback with output=%p", output);
        callback(static_cast<int>(in_display), in_connected, output);
        DLOGI("OnHotplug: callback invoked successfully");
    } else {
        DLOGW("OnHotplug: callback=%p or output=%p is NULL", callback, output);
    }
}

void SDMCompositorCallbacks::OnRefresh(uint64_t in_display) {
    DLOGI("OnRefresh: display=%llu", (unsigned long long)in_display);
    // Not used in current implementation
}

void SDMCompositorCallbacks::OnVsync(uint64_t in_display, int64_t in_timestamp, 
                                     int32_t in_vsync_period_nanos) {
    DLOGI("OnVsync: display=%llu, timestamp=%lld, period=%d", 
          (unsigned long long)in_display, (long long)in_timestamp, in_vsync_period_nanos);
    
    // Get callback data without holding lock during callback execution
    vblank_cb_t callback = nullptr;
    struct drm_output *output = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        auto it = callback_map_.find(in_display);
        if (it != callback_map_.end()) {
            callback = it->second.vblank_cb;
            output = it->second.vblank_output;
        }
    }
    
    if (callback && output) {
        callback(in_display, in_timestamp, output);
    }
}

void SDMCompositorCallbacks::OnSeamlessPossible(uint64_t in_display) {
    DLOGI("OnSeamlessPossible: display=%llu", (unsigned long long)in_display);
    // Not used in current implementation
}

void SDMCompositorCallbacks::OnVsyncIdle(uint64_t in_display) {
    DLOGI("OnVsyncIdle: display=%llu", (unsigned long long)in_display);
    // Not used in current implementation
}

void SDMCompositorCallbacks::OnVsyncPeriodTimingChanged(uint64_t in_display, 
                                                         const SDMVsyncPeriodChangeTimeline &timeline) {
    DLOGI("OnVsyncPeriodTimingChanged: display=%llu", (unsigned long long)in_display);
    // Not used in current implementation
}

} // namespace sdm
