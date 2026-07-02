/*
* Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#ifndef __SDM_COMPOSITOR_CALLBACKS_H__
#define __SDM_COMPOSITOR_CALLBACKS_H__

#include <sdmclient/sdm_compositor_cb_intf.h>
#include "sdm_display_interface.h"
#include <map>
#include <mutex>

namespace sdm {

class SDMCompositorCallbacks : public SDMCompositorCbIntf {
public:
    SDMCompositorCallbacks();
    virtual ~SDMCompositorCallbacks();
    
    // Set callbacks - use static storage to survive object copies
    void SetVblankCallback(vblank_cb_t callback, struct drm_output *output, uint64_t display_id);
    void SetHotplugCallback(hotplug_cb_t callback, struct drm_output *output, uint64_t display_id);
    
    // SDMCompositorCbIntf implementation
    virtual void OnHotplug(uint64_t in_display, bool in_connected) override;
    virtual void OnRefresh(uint64_t in_display) override;
    virtual void OnVsync(uint64_t in_display, int64_t in_timestamp, 
                        int32_t in_vsync_period_nanos) override;
    virtual void OnSeamlessPossible(uint64_t in_display) override;
    virtual void OnVsyncIdle(uint64_t in_display) override;
    virtual void OnVsyncPeriodTimingChanged(uint64_t in_display, 
                                           const SDMVsyncPeriodChangeTimeline &timeline) override;

private:
    struct CallbackData {
        vblank_cb_t vblank_cb;
        hotplug_cb_t hotplug_cb;
        struct drm_output *vblank_output;
        struct drm_output *hotplug_output;
    };
    
    // Static storage survives object copies by SDM
    static std::map<uint64_t, CallbackData> callback_map_;
    static std::mutex callback_mutex_;
};

} // namespace sdm

#endif // __SDM_COMPOSITOR_CALLBACKS_H__
