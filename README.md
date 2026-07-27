# SDM 2.0 Migration - Complete

## High-Level Summary
 
**Project Name:** Qualcomm Weston SDM Backend  
**Core Purpose:** Routing standard Upstream Weston calls into Qualcomm's downstream open-source Snapdragon Display Manager (SDM), which in turn manages all interactions with the standard Linux DRM/KMS display driver.
 
---
 
### 1. Architectural Division of Labor & Data Flow
 
The software acts purely as a **routing bridge**. It does not perform any hardware-level scheduling, composition planning, or device programming itself. Instead, it coordinates calls across the following standard pipeline:
 
```plaintext
[ Upstream Weston Compositor ]
             |
             | (1. Sends standard frame repaint, layout, & buffer updates)
             v
========================================================================
=== THIS CODEBASE: WESTON SDM BACKEND (sdm-backend.so Router)        ===
========================================================================
             |
             | (2. Packages and routes these standard calls directly into SDM)
             v
[ Qualcomm Downstream Open-Source SDM Client/Libraries ]
             |
             | (3. Determines composition strategies & manages SDE resources)
             v
[ Linux Kernel DRM / KMS Driver Stack ]
```
 
1. **Upstream Weston**: Manages the window manager shell, tracks window hierarchies, schedules frame repaints, and generates client buffers.
2. **Weston SDM Backend (This Codebase)**: Serves as the standard **routing layer**. It captures standard Weston calls (like output repaints, display hotplugs, VBlanks, and configurations) and routes them straight into the abstract C++ interfaces of the downstream SDM client. **It contains no composition strategy or hardware-direct orchestration of its own.**
3. **Downstream SDM**: Acts as the intelligent hardware and display controller manager. It evaluates display layouts, implements the actual composition strategies, schedules hardware display pipelines, and **manages all interactions and programming with the Linux Kernel DRM/KMS driver**.

---

## Architecture Overview

### High-Level System Pipeline
![Weston SDM Backend Pipeline Architecture](weston-sdm-architecture.png)

This diagram contrasts the standard **Upstream** display pipeline with Qualcomm's **Downstream** SDM-based display pipeline:

- **Upstream Pipeline**: The Weston Compositor interacts with standard `libGBM` and the `DRM Backend`, which goes through standard `libDRM` to the kernel-space `DRM/KMS` subsystem.
- **Downstream Pipeline (SDM)**: The Weston Compositor utilizes Qualcomm's `SDM Backend` and `SDM` layers (incorporating proprietary `SDM Prop` components and Qualcomm Open Source services) to perform composition, which then communicates with the kernel-space vendor DPU/panel drivers via `libDRM`.

### Detailed Component Architecture
![Weston SDM Detailed Component Architecture](weston-sdm-component-architecture.png)

This diagram outlines the internal division of labor between Upstream, Open Source, and Proprietary layers within the SDM stack:

- **Wayland Client & Compositor (Weston)**: Upstream standard interfaces.
- **SDM-Backend & SDM 2.0 Interface**: Open-source translation layer routing Weston compositor calls to the downstream SDM client.
- **SDM-Client & SDM Interfaces**: Core management layer separating the open-source display/composition manager from proprietary strategies.
- **Proprietary Plugins (Blue/Orange)**: Handle strategy implementations, resource management, partial updates, pipe allocation, rotators, scalars, QoS, and tone-mapping.
- **Downstream DRM Driver & DPU**: Target hardware-specific execution layer.

---

## SDM 1.0 to SDM 2.0 API Replacement

### Core Interfaces

| SDM 1.0 API | SDM 2.0 API |
|-------------|-------------|
| `CoreInterface::CreateDisplay()` | `SDMDisplayLifeCycleIntf::CreateDisplay()` |
| `DisplayInterface::Prepare()` | `SDMDisplayDrawCycleIntf::CommitOrPrepare()` |
| `DisplayInterface::Commit()` | `SDMDisplayDrawCycleIntf::PresentDisplay()` |
| `DisplayInterface::SetDisplayState()` | `SDMDisplayLifeCycleIntf::SetDisplayState()` |
| `DisplayInterface::SetVSyncState()` | `SDMDisplayDrawCycleIntf::SetVsyncEnabled()` |
| `DisplayInterface::GetDisplayAttributes()` | `SDMDisplayCapsIntf::GetDisplayConfig()` |
| `DisplayInterface::SetPanelBrightness()` | `SDMDisplaySettingsIntf::SetPanelBrightness()` |
| `DisplayInterface::GetPanelBrightness()` | `SDMDisplaySettingsIntf::GetPanelBrightness()` |
| `DisplayInterface::MinHdcpEncryptionLevelChanged()` | `SDMDisplayDrawCycleIntf::MinHdcpEncryptionLevelChanged()` |

### Layer Management

| SDM 1.0 API | SDM 2.0 API |
|-------------|-------------|
| `LayerStack` structure with `Layer*` pointers | `std::vector<int64_t> layer_ids_` |
| Manual `Layer` object allocation | `SDMDisplayLayerBuilderIntf::CreateLayer()` |
| Manual `Layer` object deallocation | `SDMDisplayLayerBuilderIntf::DestroyLayer()` |
| Direct `Layer` field assignment | `SDMDisplayLayerBuilderIntf::SetLayerBuffer()`, `SetLayerSourceCrop()`, `SetLayerDisplayFrame()`, `SetLayerVisibleRegion()`, `SetLayerSurfaceDamage()`, `SetLayerTransform()`, `SetLayerBlendMode()`, `SetLayerPlaneAlpha()`, `SetLayerType()`, `SetLayerFlag()`, `SetLayerCompositionType()` |
| `layer->composition` field access | `SDMDisplayLayerBuilderIntf::GetDeviceSelectedCompositionType()` |
| `layer->input_buffer.color_metadata` field access | `SDMDisplayLayerBuilderIntf::GetLayerColorMetadata()` |
| `layer->input_buffer.release_fence_fd` field access | `SDMDisplayDrawCycleIntf::GetReleaseFences()` |
| `layer_stack_.retire_fence` field | `retire_fence_` member variable |
| `layer_stack_.flags.hdr_present` field | `hdr_present_` member variable |

### Interface Replacement

| SDM 1.0 | SDM 2.0 |
|---------|---------|
| `CoreInterface*` (single interface) | `SDMDisplayLifeCycleIntf*`, `SDMDisplayDrawCycleIntf*`, `SDMDisplayLayerBuilderIntf*`, `SDMDisplayCapsIntf*`, `SDMDisplaySettingsIntf*`, `SDMDisplaySideBandIntf*`, `SDMDisplayAiqeIntf*` |
| `DisplayInterface*` (single interface) | Split into 7 specialized SDM 2.0 interfaces above |

---

## Files Modified

- **backend-sdm-2.0.1/README.md** (Added)
- **backend-sdm-2.0.1/meson.build** (Modified - Added libsdmclient dependency)
- **backend-sdm-2.0.1/sdm-service/sdm_color_mode_intf.h** (Modified)
- **backend-sdm-2.0.1/sdm-service/sdm_color_mode_intf_impl.h** (Modified)
- **backend-sdm-2.0.1/sdm-service/sdm_color_mode_intf_impl.cpp** (Modified)
- **backend-sdm-2.0.1/sdm-service/sdm_display.h** (Modified)
- **backend-sdm-2.0.1/sdm-service/sdm_display.cpp** (Modified)
- **backend-sdm-2.0.1/sdm-service/sdm_display_connect.h** (Modified)
- **backend-sdm-2.0.1/sdm-service/sdm_display_connect.cpp** (Modified)

---

## Status

✅ **Migration Complete** - All SDM 1.0 APIs replaced with SDM 2.0 APIs

## License

This project is licensed under the [LICENSE](LICENSE).
