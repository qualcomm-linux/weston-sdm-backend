// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __COMMON_ROIRECTMETADATA_H__
#define __COMMON_ROIRECTMETADATA_H__

#include <cstdint>

#include "Rect.h"

// Maximum of 10 rectangles
#define QTI_ROI_METADATA_RECT_MAX 10

typedef struct vendor_qti_hardware_display_common_ROIRectMetadata {
  uint64_t timestamp;
  /* Number of rects */
  uint8_t size;
  vendor_qti_hardware_display_common_Rect rect[QTI_ROI_METADATA_RECT_MAX];
} vendor_qti_hardware_display_common_ROIRectMetadata;

#endif // __COMMON_ROIRECTMETADATA_H__
