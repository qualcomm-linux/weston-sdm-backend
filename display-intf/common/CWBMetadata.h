// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#ifndef __COMMON_QTICWBMETADATA_H__
#define __COMMON_QTICWBMETADATA_H__

#include <cstdint>
#include <string>

typedef struct vendor_qti_hardware_display_common_capture_metadata {
  uint64_t cwb_timestamp;
  uint32_t cwb_rotation;
  bool cwb_sensitive_layer;
} vendor_qti_hardware_display_common_capture_metadata;

typedef struct
    vendor_qti_hardware_display_common_smart_selection_algo_metadata {
  uint32_t smart_selection_score;
  bool selected;
  char app_id[256];
} vendor_qti_hardware_display_common_smart_selection_algo_metadata;

typedef struct vendor_qti_hardware_display_common_algo_metadata {
  struct vendor_qti_hardware_display_common_smart_selection_algo_metadata
      smart_selection_algo_metadata;
} vendor_qti_hardware_display_common_algo_metadata;

typedef struct vendor_qti_hardware_display_common_cwb_metadata {
  struct vendor_qti_hardware_display_common_capture_metadata capture_metadata;
  struct vendor_qti_hardware_display_common_algo_metadata algo_metadata;
} vendor_qti_hardware_display_common_cwb_metadata;

#endif // __COMMON_QTICWBMETADATA_H__