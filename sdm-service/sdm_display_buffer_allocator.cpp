/*
* Copyright (c) 2017, 2020-2021, The Linux Foundation. All rights reserved.
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
*
* Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
* SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "sdm-service/sdm_display_debugger.h"
#include "sdm-service/sdm_display_buffer_allocator.h"
#include <gbm.h>
#include <dlfcn.h>
#include <PixelFormat.h>

#define __CLASS__ "SdmDisplayBufferAllocator"

#ifdef __cplusplus
extern "C" {
#endif
namespace sdm {

SdmDisplayBufferAllocator::SdmDisplayBufferAllocator(bool use_pixman) {
    int drm_fd = get_drm_master_fd();

    gbm_ = gbm_create_device(drm_fd);
    int gpu_fd = open("/dev/kgsl-3d0", O_RDWR);
    if (gpu_fd < 0) {
      is_gpu_available_ = false;
    }
    if (gpu_fd >= 0) {
      close(gpu_fd);
      gpu_fd = -1;
    }

    is_pixman_available_ = use_pixman;
}

LayerBufferFormat GetLayerBufferFormat(uint32_t format, uint32_t ubwc_status) {
   LayerBufferFormat layer_buffer_format = kFormatInvalid;

   switch (format) {
       case GBM_FORMAT_ABGR8888:
          layer_buffer_format = ubwc_status ? kFormatRGBA8888Ubwc: kFormatRGBA8888;
          break;
       case GBM_FORMAT_XBGR8888:
          layer_buffer_format = ubwc_status ? kFormatRGBX8888Ubwc: kFormatRGBX8888;
          break;
       case GBM_FORMAT_BGR888:
          layer_buffer_format = kFormatRGB888;
          break;
       case GBM_FORMAT_BGR565:
          layer_buffer_format = kFormatRGB565;
          break;
       case GBM_FORMAT_RGB565:
          layer_buffer_format = ubwc_status ? kFormatBGR565Ubwc: kFormatBGR565;
          break;
       case GBM_FORMAT_ARGB8888:
          layer_buffer_format = kFormatBGRA8888;
          break;
       case GBM_FORMAT_XRGB8888:
          layer_buffer_format = kFormatBGRX8888;
          break;
       case GBM_FORMAT_NV12:
          layer_buffer_format =
            ubwc_status ? kFormatYCbCr420SPVenusUbwc: kFormatYCbCr420SemiPlanarVenus;
          break;
       case GBM_FORMAT_ABGR2101010:
         layer_buffer_format = ubwc_status ? kFormatRGBA1010102Ubwc: kFormatRGBA1010102;
         break;
       default:
         layer_buffer_format = kFormatInvalid;
         break;
   }

   return layer_buffer_format;
}

int SdmDisplayBufferAllocator::AllocateBuffer(BufferInfo *buffer_info) {
  const BufferConfig &buffer_config = buffer_info->buffer_config;
  AllocatedBufferInfo *alloc_buffer_info = &buffer_info->alloc_buffer_info;
  uint32_t width = buffer_config.width;
  uint32_t height = buffer_config.height;
  uint32_t format;
  int metadata_fd = -1;
  uint64_t alloc_flags = 0;
  uint32_t ubwc_status;
  int format_int = static_cast<int>(format);
  int error = SetBufferInfo(buffer_config.format, &format_int, &alloc_flags);
  format = static_cast<uint32_t>(format_int);
  if (error != 0) {
    return kErrorParameters;
  }

  // CreateBuffer
  struct gbm_bo *bo = gbm_bo_create(gbm_, width, height, format, alloc_flags);
  if (bo) {
      alloc_buffer_info->fd = gbm_bo_get_fd(bo);
      alloc_buffer_info->stride = gbm_bo_get_stride(bo);
      uint32_t alignedWidth = 0;
      uint32_t alignedHeight = 0;
      /*gbm_msm_bo *msm_bo = reinterpret_cast<gbm_msm_bo*>(bo);
      alignedWidth = msm_bo->aligned_width;
      alignedHeight = msm_bo->aligned_height;*/
      alloc_buffer_info->aligned_width = alignedWidth;
      alloc_buffer_info->aligned_height = alignedHeight;
      uint32_t bo_size = 0;
      //bo_size = msm_bo->size;
      alloc_buffer_info->size = bo_size;
      alloc_buffer_info->format = GetLayerBufferFormat(gbm_bo_get_format(bo), ubwc_status);
  } else {
      DLOGI("Failed to allocate memory");

      return kErrorMemory;
  }

  buffer_info->private_data = reinterpret_cast<void *>(bo);

  return kErrorNone;
}

int SdmDisplayBufferAllocator::DumpBuffer(void* bo) {
  return 0;
}

int SdmDisplayBufferAllocator::FreeBuffer(BufferInfo *buffer_info) {
  int err = kErrorNone;
  struct gbm_bo *bo = reinterpret_cast<struct gbm_bo *>(buffer_info->private_data);
  if (bo)
      gbm_bo_destroy(bo);
  else {
      DLOGI("Unable to destroy bo = NULL.\n");
      err = kErrorParameters;
  }
  buffer_info->private_data = NULL;
  if (err == kErrorNone) {
      AllocatedBufferInfo *alloc_buffer_info = &buffer_info->alloc_buffer_info;
      alloc_buffer_info->fd = -1;
      alloc_buffer_info->stride = 0;
      alloc_buffer_info->size = 0;
      alloc_buffer_info->aligned_height = 0;
      alloc_buffer_info->aligned_width = 0;
      alloc_buffer_info->format = kFormatInvalid;
      buffer_info->private_data = NULL;
      buffer_info->buffer_config = {};
  }
  return err;
}

uint32_t SdmDisplayBufferAllocator::GetBufferSize(BufferInfo *buffer_info) {

  uint32_t size = 0;
  return size;
}

int SdmDisplayBufferAllocator::SetBufferInfo(LayerBufferFormat format, int *target, uint64_t *flags) {
  switch (format) {
  case kFormatRGBA8888:                 *target = GBM_FORMAT_ABGR8888;
                                        break;
  case kFormatRGBX8888:                 *target = GBM_FORMAT_XBGR8888;
                                        break;
  case kFormatRGB888:                   *target = GBM_FORMAT_BGR888;               break;
  case kFormatRGB565:                   *target = GBM_FORMAT_BGR565;
                                        break;
  case kFormatBGR565:                   *target = GBM_FORMAT_RGB565;               break;
  case kFormatBGRA8888:                 *target = GBM_FORMAT_ARGB8888;             break;
  case kFormatBGRX8888:                 *target = GBM_FORMAT_XRGB8888;             break;
  case kFormatYCbCr420SemiPlanarVenus:  *target = GBM_FORMAT_NV12;                 break;
  case kFormatYCbCr420SPVenusUbwc:      *target = GBM_FORMAT_NV12;
                                        break;
  case kFormatRGBA8888Ubwc:             *target = GBM_FORMAT_ABGR8888;
                                        break;
  case kFormatRGBX8888Ubwc:             *target = GBM_FORMAT_XBGR8888;
                                        break;
  case kFormatBGR565Ubwc:              *target = GBM_FORMAT_RGB565;
                                        break;
  case kFormatABGR2101010:              *target = GBM_FORMAT_ABGR2101010;
                                        break;
  case kFormatRGBA1010102:              *target = GBM_FORMAT_ABGR2101010;
                                        break;
  case kFormatRGBA1010102Ubwc:          *target = GBM_FORMAT_ABGR2101010;
                                        break;
  default:
    DLOGI("Unsupported format = 0x%x", format);
    return -1;
  }
  return 0;
}

void SdmDisplayBufferAllocator::GetGbmDeviceHandle(void **userdata) {
  *userdata = (void *) gbm_;
}

int SdmDisplayBufferAllocator::GetAllocatedBufferInfo(const BufferConfig \
                                                      &buffer_config,
                                                      AllocatedBufferInfo \
                                                      *allocated_buffer_info) {
  /* This API does not fill or provide stride to the caller in AllocatedBufferInfo structure */

  return kErrorNone;
}

bool SdmDisplayBufferAllocator::IsFormatVideo(uint32_t fmt)
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

bool SdmDisplayBufferAllocator::IsVideoFormatLinear(uint32_t fmt, uint32_t ubwc_status)
{
   bool is_videofmt_linear = false;

   if (!ubwc_status) {
     switch (fmt) {
      case GBM_FORMAT_NV12:
      case GBM_FORMAT_NV21:
        is_videofmt_linear = true;
        break;
      default:
        DLOGW("Unsupported format 0x%x\n", fmt);
        is_videofmt_linear = false;
        break;
     }
   }

  return is_videofmt_linear;
}

bool SdmDisplayBufferAllocator::IsVideoFormatUBWC(uint32_t fmt, uint32_t ubwc_status)
{
   bool is_videofmt_ubwc = false;

   if (ubwc_status) {
     switch (fmt) {
      case GBM_FORMAT_NV12:
      case GBM_FORMAT_NV21:
        is_videofmt_ubwc = true;
        break;
      default:
        DLOGW("Unsupported format 0x%x\n", fmt);
        is_videofmt_ubwc = false;
        break;
     }
   }

  return is_videofmt_ubwc;
}

int SdmDisplayBufferAllocator::GetBufferLayout(const AllocatedBufferInfo &buf_info,
                                                 uint32_t stride[4], uint32_t offset[4],
                                                 uint32_t *num_planes) {
    struct gbm_bo *bo;
    struct gbm_import_fd_data import_fd_data;
    uint32_t format1 = GBM_FORMAT_ARGB8888;
    uint64_t flags = 0;
    uint32_t ubwc_status = 0;
    uint32_t aligned_width = 0;

    int format1_int = static_cast<int>(format1);
    SetBufferInfo(buf_info.format, &format1_int, &flags);
    format1 = static_cast<uint32_t>(format1_int);

    import_fd_data.fd = buf_info.fd;
    import_fd_data.format = format1;
    import_fd_data.width = buf_info.aligned_width;
    import_fd_data.height = buf_info.aligned_height;

    // Import gbm bo from buf_info
    /*bo = gbm_bo_import(gbm_, GBM_BO_IMPORT_FD, &import_fd_data, flags);

    if (bo == NULL) {
        return kErrorNone;
    }*/

    stride[0] = import_fd_data.width * 4;
    offset[0] = 0;
    *num_planes++;

  return kErrorNone;
}

// Provide minimal implementations to satisfy BufferAllocator pure virtuals.
// These can be refined as needed once specific dataspace/format behavior is required.

int SdmDisplayBufferAllocator::GetAlignedWidthAndHeight(int width, int height, int format,
                                                        uint32_t alloc_type,
                                                        int *aligned_width, int *aligned_height) {
  // Basic passthrough alignment; refine if UBWC or special alignment is needed.
  if (aligned_width) *aligned_width = width;
  if (aligned_height) *aligned_height = height;
  return 0;
}

bool SdmDisplayBufferAllocator::GetSDMColorSpace(const int /*int_dataspace*/,
                                                 QtiDataspace *dataspace) {
  // Stub: no colorspace mapping performed.
  if (dataspace) {
    // Leave as default; caller should not rely on this stub for actual color handling.
  }
  return false;
}

LayerBufferFormat SdmDisplayBufferAllocator::GetSDMFormat(const int32_t &source,
                                                          int32_t flags, int64_t usage) {
  // Convert SDMPixelFormat to SDM LayerBufferFormat
  // source is SDMPixelFormat enum value
  // Check if UBWC flag is set
  bool is_ubwc = (flags & 0x02000000) != 0;  // UBWC flag
  
  LayerBufferFormat format = kFormatInvalid;
  
  // SDMPixelFormat enum values
  switch (source) {
    case 0x1:  // PIXEL_FORMAT_RGBA_8888
      format = is_ubwc ? kFormatRGBA8888Ubwc : kFormatRGBA8888;
      break;
    case 0x2:  // PIXEL_FORMAT_RGBX_8888
      format = is_ubwc ? kFormatRGBX8888Ubwc : kFormatRGBX8888;
      break;
    case 0x3:  // PIXEL_FORMAT_RGB_888
      format = kFormatRGB888;
      break;
    case 0x4:  // PIXEL_FORMAT_RGB_565
      format = is_ubwc ? kFormatBGR565Ubwc : kFormatBGR565;
      break;
    case 0x5:  // PIXEL_FORMAT_BGRA_8888
      format = kFormatBGRA8888;
      break;
    case 0x10: // PIXEL_FORMAT_YCBCR_422_SP (NV16)
      format = kFormatYCbCr422H2V1SemiPlanar;
      break;
    case 0x11: // PIXEL_FORMAT_YCRCB_420_SP (NV21)
      format = kFormatYCrCb420SemiPlanar;
      break;
    case 0x14: // PIXEL_FORMAT_YCBCR_422_I (YUY2)
      format = kFormatYCbCr422H2V1Packed;
      break;
    case 0x16: // PIXEL_FORMAT_RGBA_FP16
      // No direct SDM format for FP16, return invalid
      format = kFormatInvalid;
      break;
    case 0x23: // PIXEL_FORMAT_YCBCR_420_888
      format = is_ubwc ? kFormatYCbCr420SPVenusUbwc : kFormatYCbCr420SemiPlanarVenus;
      break;
    case 0x2B: // PIXEL_FORMAT_RGBA_1010102
      format = is_ubwc ? kFormatRGBA1010102Ubwc : kFormatRGBA1010102;
      break;
    case 0x32315659: // PIXEL_FORMAT_YV12
      format = kFormatYCrCb420PlanarStride16;
      break;
    case 0x36: // PIXEL_FORMAT_YCBCR_P010
      format = is_ubwc ? kFormatYCbCr420P010Ubwc : kFormatYCbCr420P010;
      break;
    case 0x3b: // PIXEL_FORMAT_RGBA_10101010
      format = is_ubwc ? kFormatRGBA1010102Ubwc : kFormatRGBA1010102;
      break;
    case 0x3c: // PIXEL_FORMAT_YCBCR_P210
      // No direct SDM format for P210, return invalid
      format = kFormatInvalid;
      break;
    default:
      DLOGI("Unsupported SDMPixelFormat: 0x%x", source);
      format = kFormatInvalid;
      break;
  }
  
  return format;
}

DisplayError SdmDisplayBufferAllocator::ColorMetadataToDataspace(Dataspace /*ds*/,
                                                                 uint32_t *int_dataspace) {
  // Stub: set to 0; no conversion performed.
  if (int_dataspace) *int_dataspace = 0;
  return kErrorNone;
}

int32_t SdmDisplayBufferAllocator::TranslateFromLegacyDataspace(const int32_t &legacy_ds) {
  // Stub: passthrough.
  return legacy_ds;
}

int SdmDisplayBufferAllocator::GetSnapInstance() {
  if (snapmapper_ != nullptr && snapallocator_ != nullptr) {
    return 0;
  }

  const std::string snapalloc_lib_name = "vendor.qti.hardware.display.snapalloc-impl.so";
  void *snap_impl_lib_ = ::dlopen(snapalloc_lib_name.c_str(), RTLD_NOW);
  if (!snap_impl_lib_) {
    DLOGI("Dlopen error for snapalloc impl: %s", dlerror());
    return kErrorCriticalResource;
  }

  // Define function pointer types
  typedef std::shared_ptr<ISnapAlloc> (*FetchISnapAllocFunc)(void *);
  typedef std::shared_ptr<ISnapMapper> (*FetchISnapMapperFunc)(void *);

  FetchISnapAllocFunc LINK_FETCH_ISnapAlloc = nullptr;
  LINK_FETCH_ISnapAlloc = reinterpret_cast<FetchISnapAllocFunc>(::dlsym(snap_impl_lib_, "FETCH_ISnapAlloc"));
  if (LINK_FETCH_ISnapAlloc) {
    snapallocator_ = LINK_FETCH_ISnapAlloc(nullptr);
  }

  if (snapallocator_ == nullptr) {
    DLOGI("%s: Failed to link FETCH_ISnapAlloc", __FUNCTION__);
    return kErrorCriticalResource;
  }

  FetchISnapMapperFunc LINK_FETCH_ISnapMapper = nullptr;
  LINK_FETCH_ISnapMapper = reinterpret_cast<FetchISnapMapperFunc>(::dlsym(snap_impl_lib_, "FETCH_ISnapMapper"));
  if (LINK_FETCH_ISnapMapper) {
    snapmapper_ = LINK_FETCH_ISnapMapper(nullptr);
  }

  if (snapmapper_ == nullptr) {
    DLOGI("%s: Failed to link FETCH_ISnapMapper", __FUNCTION__);
    return kErrorCriticalResource;
  }

  return 0;
}

}  // namespace sdm
#ifdef __cplusplus
}
#endif
