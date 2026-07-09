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
 * ​​​​​Changes from Qualcomm Technologies, Inc. are provided under the following license:
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */
#ifndef __SDM_COMPOSITOR_CB_INTF_H__
#define __SDM_COMPOSITOR_CB_INTF_H__

#include <core/sdm_types.h>

namespace sdm {

class SDMCompositorCbIntf {
public:
  virtual ~SDMCompositorCbIntf() {}

  virtual void OnHotplug(uint64_t in_display, bool in_connected) {}

  virtual void OnRefresh(uint64_t in_display) {}

  virtual void OnVsync(uint64_t in_display, int64_t in_timestamp,
                       int32_t in_vsync_period_nanos) {}

  virtual void OnSeamlessPossible(uint64_t in_display) {}

  virtual void OnVsyncIdle(uint64_t in_display) {}

  virtual void
  OnVsyncPeriodTimingChanged(uint64_t in_display,
                             const SDMVsyncPeriodChangeTimeline &timeline) {}

  virtual void onHdcpLevelsChanged(uint64_t display, int32_t min_enc_level) {}
};

} // namespace sdm

#endif // __SDM_COMPOSITOR_CB_INTF_H__
