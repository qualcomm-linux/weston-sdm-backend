/*
 * Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __SDM_COLOR_MODE_INTF_H__
#define __SDM_COLOR_MODE_INTF_H__

#include <color_metadata.h>
#include <private/snapdragon_color_intf.h>

namespace sdm {

class SDMColorModeIntf {
 public:
  virtual ~SDMColorModeIntf() {}
  virtual DisplayError Init() = 0;
  virtual DisplayError DeInit() = 0;
  virtual bool IsModeSupported(ColorPrimaries primary, GammaTransfer transfer,
                                          snapdragoncolor::RenderIntent intent) = 0;
  virtual DisplayError SetColorModeWithRenderIntent(snapdragoncolor::ColorMode mode, bool hdr_present) = 0;
  virtual DisplayError SetColorTransform(const float *matrix) = 0;
};

class SDMColorModeIntfFactory {
 public:
  std::unique_ptr<SDMColorModeIntf> GetSDMColorModeIntf(DisplayType display_type,
                                                std::shared_ptr<SDMDisplayCapsIntf> caps_intf,
                                                std::shared_ptr<SDMDisplaySettingsIntf> settings_intf,
                                                uint64_t display_handle);
};

extern "C" SDMColorModeIntfFactory *GetSDMColorModeIntfFactory();

}  // namespace sdm

#endif  // __SDM_COLOR_MODE_INTF_H__
