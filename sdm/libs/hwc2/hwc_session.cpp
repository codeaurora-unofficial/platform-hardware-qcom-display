/*
 * Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
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

#include <core/dump_interface.h>
#include <core/buffer_allocator.h>
#include <private/color_params.h>
#include <utils/constants.h>
#include <utils/String16.h>
#include <cutils/properties.h>
#include <hardware_legacy/uevent.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <binder/Parcel.h>
#include <QService.h>
#include <display_config.h>
#include <utils/debug.h>
#include <sync/sync.h>
#include <profiler.h>
#include <algorithm>
#include <string>
#include <bitset>

#include "hwc_buffer_allocator.h"
#include "hwc_buffer_sync_handler.h"
#include "hwc_session.h"
#include "hwc_debugger.h"
#include "hwc_display_primary.h"
#include "hwc_display_virtual.h"
#include "hwc_sideband.h"

#define __CLASS__ "HWCSession"

#define HWC_UEVENT_SWITCH_HDMI "change@/devices/virtual/switch/hdmi"
#define HWC_UEVENT_GRAPHICS_FB0 "change@/devices/virtual/graphics/fb0"
#define HWC_UEVENT_DRM_EXT_HOTPLUG "mdss_mdp/drm/card"
#define HWC_UEVENT_DRM_SPLASH_RESOURCE_UPDATE "sde_kms/drm/card0"

static sdm::HWCSession::HWCModuleMethods g_hwc_module_methods;

hwc_module_t HAL_MODULE_INFO_SYM = {
  .common = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 3,
    .version_minor = 0,
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "QTI Hardware Composer Module",
    .author = "CodeAurora Forum",
    .methods = &g_hwc_module_methods,
    .dso = 0,
    .reserved = {0},
  }
};

namespace sdm {
Locker HWCSession::locker_;

HWCSession::HWCSession(const hw_module_t *module) {
  hwc2_device_t::common.tag = HARDWARE_DEVICE_TAG;
  hwc2_device_t::common.version = HWC_DEVICE_API_VERSION_2_0;
  hwc2_device_t::common.module = const_cast<hw_module_t *>(module);
  hwc2_device_t::common.close = Close;
  hwc2_device_t::getCapabilities = GetCapabilities;
  hwc2_device_t::getFunction = GetFunction;
}

uint32_t HWCSession::GetDisplayCount(void) {
    uint32_t count = 0;

    core_intf_->GetDisplayCount(&count);
    return count;
}

int HWCSession::GetDisplayInfos(void) {
    DisplayError error = kErrorNone;

    error = core_intf_->GetDisplayInterfaceTypeByOrder(hw_disp_info_);
    if (error != kErrorNone) {
        DLOGE("function GetDisplayType failed: error = %d",
              error);
        return error;
    }
    return 0;
}

DisplayOrder HWCSession::GetDisplayOrder(uint32_t display_id) {
    if (display_id < HWC_DISPLAY_VIRTUAL)
        return hw_disp_info_[display_id].order;
    else if (display_id >= HWC_DISPLAY_VIRTUAL && display_id < HWC_DISPLAY_VIRTUAL + MAX_VIRTUAL_DISPLAY_NUM)
        return kOrderMax;
    else if (display_id - MAX_VIRTUAL_DISPLAY_NUM < kOrderMax)
        return hw_disp_info_[display_id - MAX_VIRTUAL_DISPLAY_NUM].order;
    else
        return kOrderMax;
}

int HWCSession::Init() {
  int status = -EINVAL;
  const char *qservice_name = "display.qservice";
  uint32_t display_count = 0;

  // Start QService and connect to it.
  qService::QService::init();
  android::sp<qService::IQService> iqservice = android::interface_cast<qService::IQService>(
      android::defaultServiceManager()->getService(android::String16(qservice_name)));

  if (iqservice.get()) {
    iqservice->connect(android::sp<qClient::IQClient>(this));
    qservice_ = reinterpret_cast<qService::QService *>(iqservice.get());
  } else {
    DLOGE("Failed to acquire %s", qservice_name);
    return -EINVAL;
  }

  StartServices();

  buffer_allocator_ = new HWCBufferAllocator();

  DisplayError error = CoreInterface::CreateCore(HWCDebugHandler::Get(), buffer_allocator_,
                                                 &buffer_sync_handler_, &socket_handler_,
                                                 &core_intf_);
  if (error != kErrorNone) {
    DLOGE("Display core initialization failed. Error = %d", error);
    return -EINVAL;
  }

  for (uint32_t i = HWC_DISPLAY_PRIMARY; i < MAX_TOTAL_DISPLAY_NUM; i++) {
    hwc_display_[i] = NULL;
  }

  display_count = GetDisplayCount();
  if (!display_count) {
      DLOGE("fail to get display from SDM! count=%d \n", display_count);
      return -1;
  }
  DLOGE("%d displays are connected\n", display_count);

  if (GetDisplayInfos()) {
      DLOGE("fail to get display info from SDM!\n");
      return -EINVAL;
  }

  // Read which display is first, create and store it in primary slot
  error = core_intf_->GetFirstDisplayInterfaceType(&hw_disp_info_[0]);

  if (error == kErrorNone && hw_disp_info_[0].type == kHDMI) {
    // HDMI is primary display.
    DLOGE("Display type is kHDMI & order = %d", hw_disp_info_[0].order);
    status = HWCDisplayExternal::Create(core_intf_, buffer_allocator_, &callbacks_, qservice_,
                                        hw_disp_info_[0].order, &hwc_display_[HWC_DISPLAY_PRIMARY]);
  } else {
    // Create and power on primary display
    DLOGE("Display type is kPrimary & order = %d", hw_disp_info_[0].order);
    status = HWCDisplayPrimary::Create(core_intf_, buffer_allocator_, &callbacks_, qservice_,
                                       hw_disp_info_[0].order, &hwc_display_[HWC_DISPLAY_PRIMARY]);
  }

  if (status) {
    DLOGE("Creation failed for first display, status=%d", status);
    CoreInterface::DestroyCore();
    return status;
  }

  color_mgr_ = HWCColorManager::CreateColorManager(buffer_allocator_);
  if (!color_mgr_) {
    DLOGW("Failed to load HWCColorManager.");
  }

  sideband_stream_.Init(this);

//fix: Now we are creating number of displays configured in kernel.
//if ENABLE_THREE_DISPLAYS is not enabled, still we will be creating
//displays but will not send notification to SurfaceFlinger

// To keep backward compability, the layout of displayId was set to:
// 0: Primary
// 1: External
// 2: Virtual
// 3~inf: External
  for (uint32_t i = HWC_DISPLAY_EXTERNAL; i < display_count + MAX_VIRTUAL_DISPLAY_NUM; i ++) {
    if (display_count == 1) {
        break;
    }

    if (i >= HWC_DISPLAY_VIRTUAL && i < HWC_DISPLAY_VIRTUAL + MAX_VIRTUAL_DISPLAY_NUM) {
        continue;
    }

    DisplayOrder display_order = GetDisplayOrder(i);

    if (error == kErrorNone && hw_disp_info_[display_order].type == kHDMI) {
      DLOGE("Display type is kHDMI & order = %d, i = %d", display_order, i);
      status = HWCDisplayExternal::Create(core_intf_, buffer_allocator_, &callbacks_, qservice_,
                                          display_order, &hwc_display_[i]);
    } else {
      DLOGE("Display type is kPrimary & order = %d, i = %d", display_order, i);
      status = HWCDisplayPrimary::Create(core_intf_, buffer_allocator_, &callbacks_, qservice_,
                                         display_order, &hwc_display_[i]);
    }

    if (status) {
      DLOGE("Failed to create display %d, status=%d", i, status);
      error = kErrorHardware;
      break;
    }
  }

  if (error != kErrorNone) {
    // clean up if there is error during display creation
    for (uint32_t i = HWC_DISPLAY_PRIMARY; i < MAX_TOTAL_DISPLAY_NUM; i ++) {
       if (hwc_display_[i] != NULL) {
           HWCDisplayPrimary::Destroy(hwc_display_[i]);
           hwc_display_[i] = NULL;
       }
    }

    CoreInterface::DestroyCore();
    return -EINVAL;
  }

  if (pthread_create(&uevent_thread_, NULL, &HWCUeventThread, this) < 0) {
    DLOGE("Failed to start = %s, error = %s", uevent_thread_name_, strerror(errno));
    HWCDisplayPrimary::Destroy(hwc_display_[HWC_DISPLAY_PRIMARY]);
    hwc_display_[HWC_DISPLAY_PRIMARY] = 0;
    CoreInterface::DestroyCore();
    return -errno;
  }

  struct rlimit fd_limit = {};
  getrlimit(RLIMIT_NOFILE, &fd_limit);
  fd_limit.rlim_cur = fd_limit.rlim_cur * 2;
  if (fd_limit.rlim_cur < fd_limit.rlim_max) {
    auto err = setrlimit(RLIMIT_NOFILE, &fd_limit);
    if (err) {
      DLOGW("Unable to increase fd limit -  err:%d, %s", errno, strerror(errno));
    }
  }
  return 0;
}

int HWCSession::Deinit() {
  for(uint32_t i = HWC_DISPLAY_PRIMARY; i < MAX_TOTAL_DISPLAY_NUM; i ++) {
     if (hwc_display_[i] != NULL) {
         HWCDisplayPrimary::Destroy(hwc_display_[i]);
         hwc_display_[i] = NULL;
     }
  }

  if (color_mgr_) {
    color_mgr_->DestroyColorManager();
  }
  uevent_thread_exit_ = true;
  DLOGD("Terminating uevent thread");
  // TODO(user): on restarting HWC in the same process, the uevent thread does not restart
  // cleanly.
  Sys::pthread_cancel_(uevent_thread_);

  DisplayError error = CoreInterface::DestroyCore();
  if (error != kErrorNone) {
    DLOGE("Display core de-initialization failed. Error = %d", error);
  }

  if (buffer_allocator_ != nullptr) {
    delete buffer_allocator_;
  }
  buffer_allocator_ = nullptr;

  return 0;
}

int HWCSession::Open(const hw_module_t *module, const char *name, hw_device_t **device) {
  SCOPE_LOCK(locker_);

  if (!module || !name || !device) {
    DLOGE("Invalid parameters.");
    return -EINVAL;
  }

  if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
    HWCSession *hwc_session = new HWCSession(module);
    if (!hwc_session) {
      return -ENOMEM;
    }

    int status = hwc_session->Init();
    if (status != 0) {
      return status;
    }

    hwc2_device_t *composer_device = hwc_session;
    *device = reinterpret_cast<hw_device_t *>(composer_device);
  }

  return 0;
}

int HWCSession::Close(hw_device_t *device) {
  SCOPE_LOCK(locker_);

  if (!device) {
    return -EINVAL;
  }

  hwc2_device_t *composer_device = reinterpret_cast<hwc2_device_t *>(device);
  HWCSession *hwc_session = static_cast<HWCSession *>(composer_device);

  hwc_session->Deinit();

  return 0;
}

void HWCSession::GetCapabilities(struct hwc2_device *device, uint32_t *outCount,
                                 int32_t *outCapabilities) {
  if (!outCount) {
    return;
  }

  if (outCapabilities != nullptr && *outCount >= 2) {
    outCapabilities[0] = HWC2_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM;
    outCapabilities[1] = HWC2_CAPABILITY_SIDEBAND_STREAM;
  }
  *outCount = 2;
}

template <typename PFN, typename T>
static hwc2_function_pointer_t AsFP(T function) {
  static_assert(std::is_same<PFN, T>::value, "Incompatible function pointer");
  return reinterpret_cast<hwc2_function_pointer_t>(function);
}

// HWC2 functions returned in GetFunction
// Defined in the same order as in the HWC2 header

int32_t HWCSession::AcceptDisplayChanges(hwc2_device_t *device, hwc2_display_t display) {
  SCOPE_LOCK(locker_);
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::AcceptDisplayChanges);
}

int32_t HWCSession::CreateLayer(hwc2_device_t *device, hwc2_display_t display,
                                hwc2_layer_t *out_layer_id) {
  if (!out_layer_id) {
    return  HWC2_ERROR_BAD_PARAMETER;
  }
  SCOPE_LOCK(locker_);
  return CallDisplayFunction(device, display, &HWCDisplay::CreateLayer, out_layer_id);
}

int32_t HWCSession::CreateVirtualDisplay(hwc2_device_t *device, uint32_t width, uint32_t height,
                                         int32_t *format, hwc2_display_t *out_display_id) {
  // TODO(user): Handle concurrency with HDMI
  SCOPE_LOCK(locker_);
  if (!device) {
    return HWC2_ERROR_BAD_DISPLAY;
  }

  if (!out_display_id || !width || !height || !format) {
    return  HWC2_ERROR_BAD_PARAMETER;
  }

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  auto status = hwc_session->CreateVirtualDisplayObject(width, height, format);
  if (status == HWC2::Error::None) {
    *out_display_id = HWC_DISPLAY_VIRTUAL;
    DLOGI("Created virtual display id:% " PRIu64 " with res: %dx%d",
          *out_display_id, width, height);
  } else {
    DLOGE("Failed to create virtual display: %s", to_string(status).c_str());
  }
  return INT32(status);
}

int32_t HWCSession::DestroyLayer(hwc2_device_t *device, hwc2_display_t display,
                                 hwc2_layer_t layer) {
  SCOPE_LOCK(locker_);
  if (!device)
    return HWC2_ERROR_BAD_DISPLAY;

  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  int32_t ret = CallDisplayFunction(device, display, &HWCDisplay::DestroyLayer, layer);
  hwc_session->sideband_stream_.DestroyLayer(display, layer);
  return ret;
}

int32_t HWCSession::DestroyVirtualDisplay(hwc2_device_t *device, hwc2_display_t display) {
  SCOPE_LOCK(locker_);
  if (!device) {
    return HWC2_ERROR_BAD_DISPLAY;
  }

  DLOGI("Destroying virtual display id:%" PRIu64, display);
  auto *hwc_session = static_cast<HWCSession *>(device);

  if (hwc_session->hwc_display_[display]) {
    HWCDisplayVirtual::Destroy(hwc_session->hwc_display_[display]);
    hwc_session->hwc_display_[display] = nullptr;
    return HWC2_ERROR_NONE;
  } else {
    return HWC2_ERROR_BAD_DISPLAY;
  }
}

void HWCSession::Dump(hwc2_device_t *device, uint32_t *out_size, char *out_buffer) {
  SCOPE_LOCK(locker_);

  if (!device || !out_size) {
    return;
  }
  auto *hwc_session = static_cast<HWCSession *>(device);
  const size_t max_dump_size = 16384; //assume we have 3 displays and 1 virtual

  if (out_buffer == nullptr) {
    *out_size = max_dump_size;
  } else {
    char sdm_dump[4096];
    DumpInterface::GetDump(sdm_dump, 4096);  // TODO(user): Fix this workaround
    std::string s("");
    for (uint32_t id = HWC_DISPLAY_PRIMARY; id < MAX_TOTAL_DISPLAY_NUM; id++) {
      if (hwc_session->hwc_display_[id]) {
        s += hwc_session->hwc_display_[id]->Dump();
      }
    }
    s += sdm_dump;
    auto copied = s.copy(out_buffer, std::min(s.size(), max_dump_size), 0);
    *out_size = UINT32(copied);
  }
}

static int32_t GetActiveConfig(hwc2_device_t *device, hwc2_display_t display,
                               hwc2_config_t *out_config) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetActiveConfig, out_config);
}

static int32_t GetChangedCompositionTypes(hwc2_device_t *device, hwc2_display_t display,
                                          uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                          int32_t *out_types) {
  // null_ptr check only for out_num_elements, as out_layers and out_types can be null.
  if (!out_num_elements) {
    return  HWC2_ERROR_BAD_PARAMETER;
  }
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetChangedCompositionTypes,
                                         out_num_elements, out_layers, out_types);
}

static int32_t GetClientTargetSupport(hwc2_device_t *device, hwc2_display_t display, uint32_t width,
                                      uint32_t height, int32_t format, int32_t dataspace) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetClientTargetSupport,
                                         width, height, format, dataspace);
}

static int32_t GetColorModes(hwc2_device_t *device, hwc2_display_t display, uint32_t *out_num_modes,
                             int32_t /*android_color_mode_t*/ *int_out_modes) {
  auto out_modes = reinterpret_cast<android_color_mode_t *>(int_out_modes);
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetColorModes, out_num_modes,
                                         out_modes);
}

static int32_t GetDisplayAttribute(hwc2_device_t *device, hwc2_display_t display,
                                   hwc2_config_t config, int32_t int_attribute,
                                   int32_t *out_value) {
  auto attribute = static_cast<HWC2::Attribute>(int_attribute);
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetDisplayAttribute, config,
                                         attribute, out_value);
}

static int32_t GetDisplayConfigs(hwc2_device_t *device, hwc2_display_t display,
                                 uint32_t *out_num_configs, hwc2_config_t *out_configs) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetDisplayConfigs,
                                         out_num_configs, out_configs);
}

static int32_t GetDisplayName(hwc2_device_t *device, hwc2_display_t display, uint32_t *out_size,
                              char *out_name) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetDisplayName, out_size,
                                         out_name);
}

static int32_t GetDisplayRequests(hwc2_device_t *device, hwc2_display_t display,
                                  int32_t *out_display_requests, uint32_t *out_num_elements,
                                  hwc2_layer_t *out_layers, int32_t *out_layer_requests) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetDisplayRequests,
                                         out_display_requests, out_num_elements, out_layers,
                                         out_layer_requests);
}

static int32_t GetDisplayType(hwc2_device_t *device, hwc2_display_t display, int32_t *out_type) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetDisplayType, out_type);
}

static int32_t GetDozeSupport(hwc2_device_t *device, hwc2_display_t display, int32_t *out_support) {
  if (display == HWC_DISPLAY_PRIMARY) {
    *out_support = 1;
  } else {
    // TODO(user): Port over connect_display_ from HWC1
    // Return no error for connected displays
    *out_support = 0;
    return HWC2_ERROR_BAD_DISPLAY;
  }
  return HWC2_ERROR_NONE;
}

static int32_t GetHdrCapabilities(hwc2_device_t* device, hwc2_display_t display,
                                  uint32_t* out_num_types, int32_t* out_types,
                                  float* out_max_luminance, float* out_max_average_luminance,
                                  float* out_min_luminance) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetHdrCapabilities,
                                         out_num_types, out_types, out_max_luminance,
                                         out_max_average_luminance, out_min_luminance);
}

static uint32_t GetMaxVirtualDisplayCount(hwc2_device_t *device) {
  return MAX_VIRTUAL_DISPLAY_NUM;
}

static int32_t GetReleaseFences(hwc2_device_t *device, hwc2_display_t display,
                                uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                int32_t *out_fences) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::GetReleaseFences,
                                         out_num_elements, out_layers, out_fences);
}

int32_t HWCSession::PresentDisplay(hwc2_device_t *device, hwc2_display_t display,
                                   int32_t *out_retire_fence) {
  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  DTRACE_SCOPED();
  SCOPE_LOCK(locker_);

  if (!device) {
    return HWC2_ERROR_BAD_DISPLAY;
  }

  auto status = HWC2::Error::BadDisplay;
  // TODO(user): Handle virtual display/HDMI concurrency
  if (hwc_session->hwc_display_[display]) {
    status = hwc_session->hwc_display_[display]->Present(out_retire_fence);
    // This is only indicative of how many times SurfaceFlinger posts
    // frames to the display.
    CALC_FPS();
  }

  hwc_session->sideband_stream_.StopPresentation(display);
  return INT32(status);
}

int32_t HWCSession::RegisterCallback(hwc2_device_t *device, int32_t descriptor,
                                     hwc2_callback_data_t callback_data,
                                     hwc2_function_pointer_t pointer) {
  HWCSession *hwc_session = static_cast<HWCSession *>(device);

  if (!device) {
    return HWC2_ERROR_BAD_DISPLAY;
  }
  SCOPE_LOCK(hwc_session->callbacks_lock_);
  auto desc = static_cast<HWC2::Callback>(descriptor);
  auto error = hwc_session->callbacks_.Register(desc, callback_data, pointer);

  DLOGD("Registering callback: %s", to_string(desc).c_str());

  if (descriptor == HWC2_CALLBACK_HOTPLUG) {
     DLOGE("Notify SurfaceFlinger for HWC_DISPLAY_PRIMARY\n");
     hwc_session->callbacks_.Hotplug(HWC_DISPLAY_PRIMARY, HWC2::Connection::Connected);
  }
  hwc_session->callbacks_lock_.Broadcast();
  return INT32(error);
}

static int32_t SetActiveConfig(hwc2_device_t *device, hwc2_display_t display,
                               hwc2_config_t config) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::SetActiveConfig, config);
}

static int32_t SetClientTarget(hwc2_device_t *device, hwc2_display_t display,
                               buffer_handle_t target, int32_t acquire_fence,
                               int32_t dataspace, hwc_region_t damage) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::SetClientTarget, target,
                                         acquire_fence, dataspace, damage);
}

int32_t HWCSession::SetColorMode(hwc2_device_t *device, hwc2_display_t display,
                                 int32_t /*android_color_mode_t*/ int_mode) {
  auto mode = static_cast<android_color_mode_t>(int_mode);
  SCOPE_LOCK(locker_);
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::SetColorMode, mode);
}

int32_t HWCSession::SetColorTransform(hwc2_device_t *device, hwc2_display_t display,
                                      const float *matrix,
                                      int32_t /*android_color_transform_t*/ hint) {
  SCOPE_LOCK(locker_);
  android_color_transform_t transform_hint = static_cast<android_color_transform_t>(hint);
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::SetColorTransform, matrix,
                                         transform_hint);
}

static int32_t SetCursorPosition(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                 int32_t x, int32_t y) {
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::SetCursorPosition, layer, x,
                                         y);
}

static int32_t SetLayerBlendMode(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                 int32_t int_mode) {
  auto mode = static_cast<HWC2::BlendMode>(int_mode);
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerBlendMode, mode);
}

static int32_t SetLayerBuffer(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                              buffer_handle_t buffer, int32_t acquire_fence) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerBuffer, buffer,
                                       acquire_fence);
}

static int32_t SetLayerColor(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                             hwc_color_t color) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerColor, color);
}

static int32_t SetLayerCompositionType(hwc2_device_t *device, hwc2_display_t display,
                                       hwc2_layer_t layer, int32_t int_type) {
  auto type = static_cast<HWC2::Composition>(int_type);
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerCompositionType,
                                       type);
}

static int32_t SetLayerDataspace(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                 int32_t dataspace) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerDataspace,
                                       dataspace);
}

static int32_t SetLayerDisplayFrame(hwc2_device_t *device, hwc2_display_t display,
                                    hwc2_layer_t layer, hwc_rect_t frame) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerDisplayFrame,
                                       frame);
}

static int32_t SetLayerPlaneAlpha(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                  float alpha) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerPlaneAlpha,
                                       alpha);
}

int32_t HWCSession::SetLayerSidebandStream(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                  buffer_handle_t stream) {
  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  SCOPE_LOCK(locker_);
  return hwc_session->sideband_stream_.SetLayerSidebandStream(display, layer, stream);
}

static int32_t SetLayerSourceCrop(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                  hwc_frect_t crop) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerSourceCrop, crop);
}

static int32_t SetLayerSurfaceDamage(hwc2_device_t *device, hwc2_display_t display,
                                     hwc2_layer_t layer, hwc_region_t damage) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerSurfaceDamage,
                                       damage);
}

static int32_t SetLayerTransform(hwc2_device_t *device, hwc2_display_t display, hwc2_layer_t layer,
                                 int32_t int_transform) {
  auto transform = static_cast<HWC2::Transform>(int_transform);
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerTransform,
                                       transform);
}

static int32_t SetLayerVisibleRegion(hwc2_device_t *device, hwc2_display_t display,
                                     hwc2_layer_t layer, hwc_region_t visible) {
  return HWCSession::CallLayerFunction(device, display, layer, &HWCLayer::SetLayerVisibleRegion,
                                       visible);
}

int32_t HWCSession::SetLayerZOrder(hwc2_device_t *device, hwc2_display_t display,
                                   hwc2_layer_t layer, uint32_t z) {
  SCOPE_LOCK(locker_);
  return CallDisplayFunction(device, display, &HWCDisplay::SetLayerZOrder, layer, z);
}

int32_t HWCSession::SetOutputBuffer(hwc2_device_t *device, hwc2_display_t display,
                                    buffer_handle_t buffer, int32_t releaseFence) {
  if (!device) {
    return HWC2_ERROR_BAD_DISPLAY;
  }

  auto *hwc_session = static_cast<HWCSession *>(device);
  if (display == HWC_DISPLAY_VIRTUAL && hwc_session->hwc_display_[display]) {
    auto vds = reinterpret_cast<HWCDisplayVirtual *>(hwc_session->hwc_display_[display]);
    auto status = vds->SetOutputBuffer(buffer, releaseFence);
    return INT32(status);
  } else {
    return HWC2_ERROR_BAD_DISPLAY;
  }
}

int32_t HWCSession::SetPowerMode(hwc2_device_t *device, hwc2_display_t display, int32_t int_mode) {
  auto mode = static_cast<HWC2::PowerMode>(int_mode);
  int32_t ret;
  static int32_t notify = 1;
  HWCSession *hwc_session = static_cast<HWCSession *>(device);

  SCOPE_LOCK(locker_);
  ret = CallDisplayFunction(device, display, &HWCDisplay::SetPowerMode, mode);
//ToDo: Notify SF about secondary & tertiary display from right place
// this is a  workaround to fix an issue where SF couldn't allocate
// framebuffersurface & bufferqueue for external & tertiary display
  if(display == kFirst && notify == 1) {
     notify = 0;
     if (hwc_session->hwc_display_[HWC_DISPLAY_EXTERNAL] != NULL) {
       DLOGE("Notify SurfaceFlinger for display kSecondary\n");
       hwc_session->callbacks_.Hotplug(HWC_DISPLAY_EXTERNAL, HWC2::Connection::Connected);
     }

#ifdef ENABLE_THREE_DISPLAYS
     for (uint32_t i = HWC_DISPLAY_VIRTUAL + MAX_VIRTUAL_DISPLAY_NUM; i < MAX_TOTAL_DISPLAY_NUM; i++) {
         if (hwc_session->hwc_display_[i] != NULL) {
           DLOGE("Notify SurfaceFlinger for display kTertiary\n");
           hwc_session->callbacks_.Hotplug(i, HWC2::Connection::Connected);
         }
     }
#endif
  }
  return ret;
}

static int32_t SetVsyncEnabled(hwc2_device_t *device, hwc2_display_t display, int32_t int_enabled) {
  auto enabled = static_cast<HWC2::Vsync>(int_enabled);
  return HWCSession::CallDisplayFunction(device, display, &HWCDisplay::SetVsyncEnabled, enabled);
}

int32_t HWCSession::ValidateDisplay(hwc2_device_t *device, hwc2_display_t display,
                                    uint32_t *out_num_types, uint32_t *out_num_requests) {
  DTRACE_SCOPED();
  SCOPE_LOCK(locker_);
  HWCSession *hwc_session = static_cast<HWCSession *>(device);
  if (!device) {
    return HWC2_ERROR_BAD_DISPLAY;
  }

  // TODO(user): Handle secure session, handle QDCM solid fill
  // Handle external_pending_connect_ in CreateVirtualDisplay
  auto status = HWC2::Error::BadDisplay;
  if (hwc_session->hwc_display_[display]) {
    if (display == HWC_DISPLAY_PRIMARY) {
      // TODO(user): This can be moved to HWCDisplayPrimary
      if (hwc_session->reset_panel_) {
        DLOGW("panel is in bad state, resetting the panel");
        hwc_session->ResetPanel();
      }

      if (hwc_session->need_invalidate_) {
        hwc_session->Refresh(display);
      }

      if (hwc_session->color_mgr_) {
        hwc_session->color_mgr_->SetColorModeDetailEnhancer(hwc_session->hwc_display_[display]);
      }
    }

    status = hwc_session->hwc_display_[display]->Validate(out_num_types, out_num_requests);

    hwc_session->sideband_stream_.StartPresentation(display);
  }
  return INT32(status);
}

hwc2_function_pointer_t HWCSession::GetFunction(struct hwc2_device *device,
                                                int32_t int_descriptor) {
  auto descriptor = static_cast<HWC2::FunctionDescriptor>(int_descriptor);

  switch (descriptor) {
    case HWC2::FunctionDescriptor::AcceptDisplayChanges:
      return AsFP<HWC2_PFN_ACCEPT_DISPLAY_CHANGES>(HWCSession::AcceptDisplayChanges);
    case HWC2::FunctionDescriptor::CreateLayer:
      return AsFP<HWC2_PFN_CREATE_LAYER>(CreateLayer);
    case HWC2::FunctionDescriptor::CreateVirtualDisplay:
      return AsFP<HWC2_PFN_CREATE_VIRTUAL_DISPLAY>(HWCSession::CreateVirtualDisplay);
    case HWC2::FunctionDescriptor::DestroyLayer:
      return AsFP<HWC2_PFN_DESTROY_LAYER>(DestroyLayer);
    case HWC2::FunctionDescriptor::DestroyVirtualDisplay:
      return AsFP<HWC2_PFN_DESTROY_VIRTUAL_DISPLAY>(HWCSession::DestroyVirtualDisplay);
    case HWC2::FunctionDescriptor::Dump:
      return AsFP<HWC2_PFN_DUMP>(HWCSession::Dump);
    case HWC2::FunctionDescriptor::GetActiveConfig:
      return AsFP<HWC2_PFN_GET_ACTIVE_CONFIG>(GetActiveConfig);
    case HWC2::FunctionDescriptor::GetChangedCompositionTypes:
      return AsFP<HWC2_PFN_GET_CHANGED_COMPOSITION_TYPES>(GetChangedCompositionTypes);
    case HWC2::FunctionDescriptor::GetClientTargetSupport:
      return AsFP<HWC2_PFN_GET_CLIENT_TARGET_SUPPORT>(GetClientTargetSupport);
    case HWC2::FunctionDescriptor::GetColorModes:
      return AsFP<HWC2_PFN_GET_COLOR_MODES>(GetColorModes);
    case HWC2::FunctionDescriptor::GetDisplayAttribute:
      return AsFP<HWC2_PFN_GET_DISPLAY_ATTRIBUTE>(GetDisplayAttribute);
    case HWC2::FunctionDescriptor::GetDisplayConfigs:
      return AsFP<HWC2_PFN_GET_DISPLAY_CONFIGS>(GetDisplayConfigs);
    case HWC2::FunctionDescriptor::GetDisplayName:
      return AsFP<HWC2_PFN_GET_DISPLAY_NAME>(GetDisplayName);
    case HWC2::FunctionDescriptor::GetDisplayRequests:
      return AsFP<HWC2_PFN_GET_DISPLAY_REQUESTS>(GetDisplayRequests);
    case HWC2::FunctionDescriptor::GetDisplayType:
      return AsFP<HWC2_PFN_GET_DISPLAY_TYPE>(GetDisplayType);
    case HWC2::FunctionDescriptor::GetHdrCapabilities:
      return AsFP<HWC2_PFN_GET_HDR_CAPABILITIES>(GetHdrCapabilities);
    case HWC2::FunctionDescriptor::GetDozeSupport:
      return AsFP<HWC2_PFN_GET_DOZE_SUPPORT>(GetDozeSupport);
    case HWC2::FunctionDescriptor::GetMaxVirtualDisplayCount:
      return AsFP<HWC2_PFN_GET_MAX_VIRTUAL_DISPLAY_COUNT>(GetMaxVirtualDisplayCount);
    case HWC2::FunctionDescriptor::GetReleaseFences:
      return AsFP<HWC2_PFN_GET_RELEASE_FENCES>(GetReleaseFences);
    case HWC2::FunctionDescriptor::PresentDisplay:
      return AsFP<HWC2_PFN_PRESENT_DISPLAY>(PresentDisplay);
    case HWC2::FunctionDescriptor::RegisterCallback:
      return AsFP<HWC2_PFN_REGISTER_CALLBACK>(RegisterCallback);
    case HWC2::FunctionDescriptor::SetActiveConfig:
      return AsFP<HWC2_PFN_SET_ACTIVE_CONFIG>(SetActiveConfig);
    case HWC2::FunctionDescriptor::SetClientTarget:
      return AsFP<HWC2_PFN_SET_CLIENT_TARGET>(SetClientTarget);
    case HWC2::FunctionDescriptor::SetColorMode:
      return AsFP<HWC2_PFN_SET_COLOR_MODE>(SetColorMode);
    case HWC2::FunctionDescriptor::SetColorTransform:
      return AsFP<HWC2_PFN_SET_COLOR_TRANSFORM>(SetColorTransform);
    case HWC2::FunctionDescriptor::SetCursorPosition:
      return AsFP<HWC2_PFN_SET_CURSOR_POSITION>(SetCursorPosition);
    case HWC2::FunctionDescriptor::SetLayerBlendMode:
      return AsFP<HWC2_PFN_SET_LAYER_BLEND_MODE>(SetLayerBlendMode);
    case HWC2::FunctionDescriptor::SetLayerBuffer:
      return AsFP<HWC2_PFN_SET_LAYER_BUFFER>(SetLayerBuffer);
    case HWC2::FunctionDescriptor::SetLayerColor:
      return AsFP<HWC2_PFN_SET_LAYER_COLOR>(SetLayerColor);
    case HWC2::FunctionDescriptor::SetLayerCompositionType:
      return AsFP<HWC2_PFN_SET_LAYER_COMPOSITION_TYPE>(SetLayerCompositionType);
    case HWC2::FunctionDescriptor::SetLayerDataspace:
      return AsFP<HWC2_PFN_SET_LAYER_DATASPACE>(SetLayerDataspace);
    case HWC2::FunctionDescriptor::SetLayerDisplayFrame:
      return AsFP<HWC2_PFN_SET_LAYER_DISPLAY_FRAME>(SetLayerDisplayFrame);
    case HWC2::FunctionDescriptor::SetLayerPlaneAlpha:
      return AsFP<HWC2_PFN_SET_LAYER_PLANE_ALPHA>(SetLayerPlaneAlpha);
    case HWC2::FunctionDescriptor::SetLayerSidebandStream:
      return AsFP<HWC2_PFN_SET_LAYER_SIDEBAND_STREAM>(SetLayerSidebandStream);
    case HWC2::FunctionDescriptor::SetLayerSourceCrop:
      return AsFP<HWC2_PFN_SET_LAYER_SOURCE_CROP>(SetLayerSourceCrop);
    case HWC2::FunctionDescriptor::SetLayerSurfaceDamage:
      return AsFP<HWC2_PFN_SET_LAYER_SURFACE_DAMAGE>(SetLayerSurfaceDamage);
    case HWC2::FunctionDescriptor::SetLayerTransform:
      return AsFP<HWC2_PFN_SET_LAYER_TRANSFORM>(SetLayerTransform);
    case HWC2::FunctionDescriptor::SetLayerVisibleRegion:
      return AsFP<HWC2_PFN_SET_LAYER_VISIBLE_REGION>(SetLayerVisibleRegion);
    case HWC2::FunctionDescriptor::SetLayerZOrder:
      return AsFP<HWC2_PFN_SET_LAYER_Z_ORDER>(SetLayerZOrder);
    case HWC2::FunctionDescriptor::SetOutputBuffer:
      return AsFP<HWC2_PFN_SET_OUTPUT_BUFFER>(SetOutputBuffer);
    case HWC2::FunctionDescriptor::SetPowerMode:
      return AsFP<HWC2_PFN_SET_POWER_MODE>(SetPowerMode);
    case HWC2::FunctionDescriptor::SetVsyncEnabled:
      return AsFP<HWC2_PFN_SET_VSYNC_ENABLED>(SetVsyncEnabled);
    case HWC2::FunctionDescriptor::ValidateDisplay:
      return AsFP<HWC2_PFN_VALIDATE_DISPLAY>(HWCSession::ValidateDisplay);
    default:
      DLOGD("Unknown/Unimplemented function descriptor: %d (%s)", int_descriptor,
            to_string(descriptor).c_str());
      return nullptr;
  }
  return nullptr;
}

// TODO(user): handle locking

HWC2::Error HWCSession::CreateVirtualDisplayObject(uint32_t width, uint32_t height,
                                                   int32_t *format) {
  if (hwc_display_[HWC_DISPLAY_VIRTUAL]) {
    return HWC2::Error::NoResources;
  }

  DisplayOrder display_order = GetDisplayOrder(HWC_DISPLAY_VIRTUAL);

  auto status = HWCDisplayVirtual::Create(core_intf_, buffer_allocator_, &callbacks_, width,
                                          height, format, display_order, &hwc_display_[HWC_DISPLAY_VIRTUAL]);
  // TODO(user): validate width and height support
  if (status)
    return HWC2::Error::Unsupported;

  return HWC2::Error::None;
}

int32_t HWCSession::ConnectDisplay(int disp) {
  DLOGI("Display = %d", disp);

  int status = 0;
  uint32_t primary_width = 0;
  uint32_t primary_height = 0;

  hwc_display_[HWC_DISPLAY_PRIMARY]->GetFrameBufferResolution(&primary_width, &primary_height);

  DisplayOrder display_order = GetDisplayOrder((uint32_t)disp);

  if (disp == HWC_DISPLAY_EXTERNAL) {
    status = HWCDisplayExternal::Create(core_intf_, buffer_allocator_, &callbacks_, primary_width,
                                        primary_height, qservice_, false, display_order, &hwc_display_[disp]);
  } else {
    DLOGE("Invalid display type");
    return -1;
  }

  if (!status) {
    hwc_display_[disp]->SetSecureDisplay(secure_display_active_);
  }

  return status;
}

int HWCSession::DisconnectDisplay(int disp) {
  DLOGI("Display = %d", disp);

  if (disp == HWC_DISPLAY_EXTERNAL) {
    HWCDisplayExternal::Destroy(hwc_display_[disp]);
  } else if (disp == HWC_DISPLAY_VIRTUAL) {
    HWCDisplayVirtual::Destroy(hwc_display_[disp]);
  } else {
    DLOGE("Invalid display type");
    return -1;
  }

  hwc_display_[disp] = NULL;

  return 0;
}

// Qclient methods
android::status_t HWCSession::notifyCallback(uint32_t command, const android::Parcel *input_parcel,
                                             android::Parcel *output_parcel) {
  android::status_t status = 0;

  switch (command) {
    case qService::IQService::DYNAMIC_DEBUG:
      DynamicDebug(input_parcel);
      break;

    case qService::IQService::SCREEN_REFRESH:
      refreshScreen();
      break;

    case qService::IQService::SET_IDLE_TIMEOUT:
      setIdleTimeout(UINT32(input_parcel->readInt32()));
      break;

    case qService::IQService::SET_FRAME_DUMP_CONFIG:
      SetFrameDumpConfig(input_parcel);
      break;

    case qService::IQService::SET_MAX_PIPES_PER_MIXER:
      status = SetMaxMixerStages(input_parcel);
      break;

    case qService::IQService::SET_DISPLAY_MODE:
      status = SetDisplayMode(input_parcel);
      break;

    case qService::IQService::SET_SECONDARY_DISPLAY_STATUS: {
        int disp_id = INT(input_parcel->readInt32());
        HWCDisplay::DisplayStatus disp_status =
              static_cast<HWCDisplay::DisplayStatus>(input_parcel->readInt32());
        status = SetSecondaryDisplayStatus(disp_id, disp_status);
        output_parcel->writeInt32(status);
      }
      break;

    case qService::IQService::CONFIGURE_DYN_REFRESH_RATE:
      status = ConfigureRefreshRate(input_parcel);
      break;

    case qService::IQService::SET_VIEW_FRAME:
      break;

    case qService::IQService::TOGGLE_SCREEN_UPDATES: {
        int32_t input = input_parcel->readInt32();
        status = toggleScreenUpdate(input == 1);
        output_parcel->writeInt32(status);
      }
      break;

    case qService::IQService::QDCM_SVC_CMDS:
      status = QdcmCMDHandler(input_parcel, output_parcel);
      break;

    case qService::IQService::MIN_HDCP_ENCRYPTION_LEVEL_CHANGED: {
        int disp_id = input_parcel->readInt32();
        uint32_t min_enc_level = UINT32(input_parcel->readInt32());
        status = MinHdcpEncryptionLevelChanged(disp_id, min_enc_level);
        output_parcel->writeInt32(status);
      }
      break;

    case qService::IQService::CONTROL_PARTIAL_UPDATE: {
        int disp_id = input_parcel->readInt32();
        uint32_t enable = UINT32(input_parcel->readInt32());
        status = ControlPartialUpdate(disp_id, enable == 1);
        output_parcel->writeInt32(status);
      }
      break;

    case qService::IQService::SET_ACTIVE_CONFIG: {
        uint32_t config = UINT32(input_parcel->readInt32());
        int disp_id = input_parcel->readInt32();
        status = SetActiveConfigIndex(disp_id, config);
      }
      break;

    case qService::IQService::GET_ACTIVE_CONFIG: {
        int disp_id = input_parcel->readInt32();
        uint32_t config = 0;
        status = GetActiveConfigIndex(disp_id, &config);
        output_parcel->writeInt32(INT(config));
      }
      break;

    case qService::IQService::GET_CONFIG_COUNT: {
        int disp_id = input_parcel->readInt32();
        uint32_t count = 0;
        status = GetConfigCount(disp_id, &count);
        output_parcel->writeInt32(INT(count));
      }
      break;

    case qService::IQService::GET_DISPLAY_ATTRIBUTES_FOR_CONFIG:
      status = HandleGetDisplayAttributesForConfig(input_parcel, output_parcel);
      break;

    case qService::IQService::GET_PANEL_BRIGHTNESS: {
        int level = 0;
        status = GetPanelBrightness(&level);
        output_parcel->writeInt32(level);
      }
      break;

    case qService::IQService::SET_PANEL_BRIGHTNESS: {
        uint32_t level = UINT32(input_parcel->readInt32());
        status = setPanelBrightness(level);
        output_parcel->writeInt32(status);
      }
      break;

    case qService::IQService::GET_DISPLAY_VISIBLE_REGION:
      status = GetVisibleDisplayRect(input_parcel, output_parcel);
      break;

    case qService::IQService::SET_CAMERA_STATUS: {
        uint32_t camera_status = UINT32(input_parcel->readInt32());
        status = setCameraLaunchStatus(camera_status);
      }
      break;

    case qService::IQService::GET_BW_TRANSACTION_STATUS: {
        bool state = true;
        status = DisplayBWTransactionPending(&state);
        output_parcel->writeInt32(state);
      }
      break;

    case qService::IQService::SET_LAYER_MIXER_RESOLUTION:
      status = SetMixerResolution(input_parcel);
      break;

    case qService::IQService::SET_COLOR_MODE:
      status = SetColorModeOverride(input_parcel);
      break;

    default:
      DLOGW("QService command = %d is not supported", command);
      return -EINVAL;
  }

  return status;
}

android::status_t HWCSession::HandleGetDisplayAttributesForConfig(const android::Parcel
                                                                  *input_parcel,
                                                                  android::Parcel *output_parcel) {
  SCOPE_LOCK(locker_);

  int config = input_parcel->readInt32();
  int dpy = input_parcel->readInt32();
  int error = android::BAD_VALUE;
  DisplayConfigVariableInfo display_attributes;

  if (dpy < HWC_DISPLAY_PRIMARY || dpy >= MAX_TOTAL_DISPLAY_NUM || config < 0) {
    return android::BAD_VALUE;
  }

  if (hwc_display_[dpy]) {
    error = hwc_display_[dpy]->GetDisplayAttributesForConfig(config, &display_attributes);
    if (error == 0) {
      output_parcel->writeInt32(INT(display_attributes.vsync_period_ns));
      output_parcel->writeInt32(INT(display_attributes.x_pixels));
      output_parcel->writeInt32(INT(display_attributes.y_pixels));
      output_parcel->writeFloat(display_attributes.x_dpi);
      output_parcel->writeFloat(display_attributes.y_dpi);
      output_parcel->writeInt32(0);  // Panel type, unsupported.
    }
  }

  return error;
}

android::status_t HWCSession::ConfigureRefreshRate(const android::Parcel *input_parcel) {
  SCOPE_LOCK(locker_);

  uint32_t operation = UINT32(input_parcel->readInt32());
  HWCDisplay *hwc_display = hwc_display_[HWC_DISPLAY_PRIMARY];

  switch (operation) {
    case qdutils::DISABLE_METADATA_DYN_REFRESH_RATE:
      return hwc_display->Perform(HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, false);

    case qdutils::ENABLE_METADATA_DYN_REFRESH_RATE:
      return hwc_display->Perform(HWCDisplayPrimary::SET_METADATA_DYN_REFRESH_RATE, true);

    case qdutils::SET_BINDER_DYN_REFRESH_RATE: {
      uint32_t refresh_rate = UINT32(input_parcel->readInt32());
      return hwc_display->Perform(HWCDisplayPrimary::SET_BINDER_DYN_REFRESH_RATE, refresh_rate);
    }

    default:
      DLOGW("Invalid operation %d", operation);
      return -EINVAL;
  }

  return 0;
}

android::status_t HWCSession::SetDisplayMode(const android::Parcel *input_parcel) {
  SCOPE_LOCK(locker_);

  uint32_t mode = UINT32(input_parcel->readInt32());
  return hwc_display_[HWC_DISPLAY_PRIMARY]->Perform(HWCDisplayPrimary::SET_DISPLAY_MODE, mode);
}

android::status_t HWCSession::SetMaxMixerStages(const android::Parcel *input_parcel) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  std::bitset<32> bit_mask_display_type = UINT32(input_parcel->readInt32());
  uint32_t max_mixer_stages = UINT32(input_parcel->readInt32());

  for (uint32_t i = HWC_DISPLAY_PRIMARY; i < MAX_TOTAL_DISPLAY_NUM; i++) {
      if (bit_mask_display_type[i]) {
        if (hwc_display_[i]) {
          error = hwc_display_[i]->SetMaxMixerStages(max_mixer_stages);
          if (error != kErrorNone) {
            return -EINVAL;
          }
        }
      }
  }

  return 0;
}

void HWCSession::SetFrameDumpConfig(const android::Parcel *input_parcel) {
  SCOPE_LOCK(locker_);

  uint32_t frame_dump_count = UINT32(input_parcel->readInt32());
  std::bitset<32> bit_mask_display_type = UINT32(input_parcel->readInt32());
  uint32_t bit_mask_layer_type = UINT32(input_parcel->readInt32());

  for (uint32_t i = HWC_DISPLAY_PRIMARY; i < MAX_TOTAL_DISPLAY_NUM; i++) {
      if (bit_mask_display_type[i]) {
        if (hwc_display_[i]) {
          hwc_display_[i]->SetFrameDumpConfig(frame_dump_count, bit_mask_layer_type);
        }
      }
  }
}

android::status_t HWCSession::SetMixerResolution(const android::Parcel *input_parcel) {
  SCOPE_LOCK(locker_);

  DisplayError error = kErrorNone;
  uint32_t dpy = UINT32(input_parcel->readInt32());

  if (dpy != HWC_DISPLAY_PRIMARY) {
    DLOGI("Resoulution change not supported for this display %d", dpy);
    return -EINVAL;
  }

  if (!hwc_display_[HWC_DISPLAY_PRIMARY]) {
    DLOGI("Primary display is not initialized");
    return -EINVAL;
  }

  uint32_t width = UINT32(input_parcel->readInt32());
  uint32_t height = UINT32(input_parcel->readInt32());

  error = hwc_display_[HWC_DISPLAY_PRIMARY]->SetMixerResolution(width, height);
  if (error != kErrorNone) {
    return -EINVAL;
  }

  return 0;
}

android::status_t HWCSession::SetColorModeOverride(const android::Parcel *input_parcel) {
  SCOPE_LOCK(locker_);

  auto display = static_cast<hwc2_display_t >(input_parcel->readInt32());
  auto mode = static_cast<android_color_mode_t>(input_parcel->readInt32());
  auto device = static_cast<hwc2_device_t *>(this);

  auto err = CallDisplayFunction(device, display, &HWCDisplay::SetColorMode, mode);
  if (err != HWC2_ERROR_NONE)
    return -EINVAL;
  return 0;
}

void HWCSession::DynamicDebug(const android::Parcel *input_parcel) {
  SCOPE_LOCK(locker_);

  int type = input_parcel->readInt32();
  bool enable = (input_parcel->readInt32() > 0);
  DLOGI("type = %d enable = %d", type, enable);
  int verbose_level = input_parcel->readInt32();

  switch (type) {
    case qService::IQService::DEBUG_ALL:
      HWCDebugHandler::DebugAll(enable, verbose_level);
      break;

    case qService::IQService::DEBUG_MDPCOMP:
      HWCDebugHandler::DebugStrategy(enable, verbose_level);
      HWCDebugHandler::DebugCompManager(enable, verbose_level);
      break;

    case qService::IQService::DEBUG_PIPE_LIFECYCLE:
      HWCDebugHandler::DebugResources(enable, verbose_level);
      break;

    case qService::IQService::DEBUG_DRIVER_CONFIG:
      HWCDebugHandler::DebugDriverConfig(enable, verbose_level);
      break;

    case qService::IQService::DEBUG_ROTATOR:
      HWCDebugHandler::DebugResources(enable, verbose_level);
      HWCDebugHandler::DebugDriverConfig(enable, verbose_level);
      HWCDebugHandler::DebugRotator(enable, verbose_level);
      break;

    case qService::IQService::DEBUG_QDCM:
      HWCDebugHandler::DebugQdcm(enable, verbose_level);
      break;

    default:
      DLOGW("type = %d is not supported", type);
  }
}

android::status_t HWCSession::QdcmCMDHandler(const android::Parcel *input_parcel,
                                             android::Parcel *output_parcel) {
  SCOPE_LOCK(locker_);

  int ret = 0;
  int32_t *brightness_value = NULL;
  uint32_t display_id(0);
  PPPendingParams pending_action;
  PPDisplayAPIPayload resp_payload, req_payload;

  if (!color_mgr_) {
    return -1;
  }

  pending_action.action = kNoAction;
  pending_action.params = NULL;

  // Read display_id, payload_size and payload from in_parcel.
  ret = HWCColorManager::CreatePayloadFromParcel(*input_parcel, &display_id, &req_payload);
  if (!ret) {
    if (HWC_DISPLAY_PRIMARY == display_id && hwc_display_[HWC_DISPLAY_PRIMARY])
      ret = hwc_display_[HWC_DISPLAY_PRIMARY]->ColorSVCRequestRoute(req_payload, &resp_payload,
                                                                    &pending_action);

    if (HWC_DISPLAY_EXTERNAL == display_id && hwc_display_[HWC_DISPLAY_EXTERNAL])
      ret = hwc_display_[HWC_DISPLAY_EXTERNAL]->ColorSVCRequestRoute(req_payload, &resp_payload,
                                                                     &pending_action);
  }

  if (ret) {
    output_parcel->writeInt32(ret);  // first field in out parcel indicates return code.
    req_payload.DestroyPayload();
    resp_payload.DestroyPayload();
    return ret;
  }

  switch (pending_action.action) {
    case kInvalidating:
      Refresh(HWC_DISPLAY_PRIMARY);
      break;
    case kEnterQDCMMode:
      ret = color_mgr_->EnableQDCMMode(true, hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kExitQDCMMode:
      ret = color_mgr_->EnableQDCMMode(false, hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kApplySolidFill:
      ret =
          color_mgr_->SetSolidFill(pending_action.params, true, hwc_display_[HWC_DISPLAY_PRIMARY]);
      Refresh(HWC_DISPLAY_PRIMARY);
      break;
    case kDisableSolidFill:
      ret =
          color_mgr_->SetSolidFill(pending_action.params, false, hwc_display_[HWC_DISPLAY_PRIMARY]);
      Refresh(HWC_DISPLAY_PRIMARY);
      break;
    case kSetPanelBrightness:
      brightness_value = reinterpret_cast<int32_t *>(resp_payload.payload);
      if (brightness_value == NULL) {
        DLOGE("Brightness value is Null");
        return -EINVAL;
      }
      if (HWC_DISPLAY_PRIMARY == display_id)
        ret = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPanelBrightness(*brightness_value);
      break;
    case kEnableFrameCapture:
      ret = color_mgr_->SetFrameCapture(pending_action.params, true,
                                        hwc_display_[HWC_DISPLAY_PRIMARY]);
      Refresh(HWC_DISPLAY_PRIMARY);
      break;
    case kDisableFrameCapture:
      ret = color_mgr_->SetFrameCapture(pending_action.params, false,
                                        hwc_display_[HWC_DISPLAY_PRIMARY]);
      break;
    case kConfigureDetailedEnhancer:
      ret = color_mgr_->SetDetailedEnhancer(pending_action.params,
                                            hwc_display_[HWC_DISPLAY_PRIMARY]);
      Refresh(HWC_DISPLAY_PRIMARY);
      break;
    case kNoAction:
      break;
    default:
      DLOGW("Invalid pending action = %d!", pending_action.action);
      break;
  }

  // for display API getter case, marshall returned params into out_parcel.
  output_parcel->writeInt32(ret);
  HWCColorManager::MarshallStructIntoParcel(resp_payload, output_parcel);
  req_payload.DestroyPayload();
  resp_payload.DestroyPayload();

  return (ret ? -EINVAL : 0);
}

void *HWCSession::HWCUeventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWCSession *>(context)->HWCUeventThreadHandler();
  }

  return NULL;
}

void *HWCSession::HWCUeventThreadHandler() {
  static char uevent_data[PAGE_SIZE];
  int length = 0;
  prctl(PR_SET_NAME, uevent_thread_name_, 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);
  if (!uevent_init()) {
    DLOGE("Failed to init uevent");
    pthread_exit(0);
    return NULL;
  }

  while (!uevent_thread_exit_) {
    // keep last 2 zeroes to ensure double 0 termination
    length = uevent_next_event(uevent_data, INT32(sizeof(uevent_data)) - 2);

    if (strcasestr(HWC_UEVENT_SWITCH_HDMI, uevent_data)) {
      DLOGI("Uevent HDMI = %s", uevent_data);
      int connected = GetEventValue(uevent_data, length, "SWITCH_STATE=");
      if (connected >= 0) {
        DLOGI("HDMI = %s", connected ? "connected" : "disconnected");
        if (HotPlugHandler(connected) == -1) {
          DLOGE("Failed handling Hotplug = %s", connected ? "connected" : "disconnected");
        }
      }
    } else if (strcasestr(HWC_UEVENT_GRAPHICS_FB0, uevent_data)) {
      DLOGI("Uevent FB0 = %s", uevent_data);
      int panel_reset = GetEventValue(uevent_data, length, "PANEL_ALIVE=");
      if (panel_reset == 0) {
        Refresh(0);
        reset_panel_ = true;
      }
    } else if (strcasestr(uevent_data, HWC_UEVENT_DRM_EXT_HOTPLUG)) {
      HandleExtHPD(uevent_data, length);
    } else if (strcasestr(uevent_data, HWC_UEVENT_DRM_SPLASH_RESOURCE_UPDATE)) {
      DLOGI("find change in sde_kms");
      HWCUpdateResourceInfo(uevent_data, length);
    }
  }
  pthread_exit(0);

  return NULL;
}

void HWCSession::HandleExtHPD(const char *uevent_data, int length) {
  const char *iterator_str = uevent_data;
  const char *event_info = "status=";
  const char *pstr = NULL;
  while (((iterator_str - uevent_data) <= length) && (*iterator_str)) {
    pstr = strstr(iterator_str, event_info);
    if (pstr != NULL) {
      break;
    }
    iterator_str += strlen(iterator_str) + 1;
  }

  // ignore event if 'status' is not available
  if (pstr) {
    bool connected = !!strcasestr(pstr, "connected");
    if (HotPlugHandler(connected) == -1) {
     DLOGE("Failed handling Hotplug = %s", connected ? "connected" : "disconnected");
    }
  }
}

int HWCSession::HWCUpdateResourceInfo(const char *uevent_data, int length) {
  int pipe_released = 0;

  pipe_released = GetEventValue(uevent_data, length, "pipe");

  if (pipe_released >= 0) {
    DLOGI("start to restore unavailable pipes to idle status");
    UpdateResourceInfo();
  }

  return 0;
}

int HWCSession::UpdateResourceInfo()
{
  SCOPE_LOCK(locker_);

  for (uint32_t i = HWC_DISPLAY_PRIMARY; i < MAX_TOTAL_DISPLAY_NUM; i++) {
    if (hwc_display_[i])
      hwc_display_[i]->UpdateResourceInfo();
  }

  return 0;
}

int HWCSession::GetEventValue(const char *uevent_data, int length, const char *event_info) {
  const char *iterator_str = uevent_data;
  while (((iterator_str - uevent_data) <= length) && (*iterator_str)) {
    const char *pstr = strstr(iterator_str, event_info);
    if (pstr != NULL) {
      return (atoi(iterator_str + strlen(event_info)));
    }
    iterator_str += strlen(iterator_str) + 1;
  }

  return -1;
}

void HWCSession::ResetPanel() {
  HWC2::Error status;

  DLOGI("Powering off primary");
  status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPowerMode(HWC2::PowerMode::Off);
  if (status != HWC2::Error::None) {
    DLOGE("power-off on primary failed with error = %d", status);
  }

  DLOGI("Restoring power mode on primary");
  HWC2::PowerMode mode = hwc_display_[HWC_DISPLAY_PRIMARY]->GetLastPowerMode();
  status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetPowerMode(mode);
  if (status != HWC2::Error::None) {
    DLOGE("Setting power mode = %d on primary failed with error = %d", mode, status);
  }

  status = hwc_display_[HWC_DISPLAY_PRIMARY]->SetVsyncEnabled(HWC2::Vsync::Enable);
  if (status != HWC2::Error::None) {
    DLOGE("enabling vsync failed for primary with error = %d", status);
  }

  reset_panel_ = false;
}

int HWCSession::HotPlugHandler(bool connected) {
  int status = 0;
  bool notify_hotplug = false;
  bool hdmi_primary = false;

  // To prevent sending events to client while a lock is held, acquire scope locks only within
  // below scope so that those get automatically unlocked after the scope ends.
  {
    SCOPE_LOCK(locker_);

    if (!hwc_display_[HWC_DISPLAY_PRIMARY]) {
      DLOGE("Primary display is not connected.");
      return -1;
    }

    HWCDisplay *primary_display = hwc_display_[HWC_DISPLAY_PRIMARY];
    HWCDisplay *external_display = NULL;

    if (primary_display->GetDisplayClass() == DISPLAY_CLASS_EXTERNAL) {
      external_display = static_cast<HWCDisplayExternal *>(hwc_display_[HWC_DISPLAY_PRIMARY]);
      hdmi_primary = true;
    }

    // If primary display connected is a NULL display, then replace it with the external display
    if (connected) {
      // If we are in HDMI as primary and the primary display just got plugged in
      if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
        DLOGE("HDMI is already connected");
        return -1;
      }

      // Connect external display if virtual display is not connected.
      // Else, defer external display connection and process it when virtual display
      // tears down; Do not notify SurfaceFlinger since connection is deferred now.
      if (!hwc_display_[HWC_DISPLAY_VIRTUAL]) {
        status = ConnectDisplay(HWC_DISPLAY_EXTERNAL);
        if (status) {
          return status;
        }
        notify_hotplug = true;
      } else {
        DLOGI("Virtual display is connected, pending connection");
        external_pending_connect_ = true;
      }
    } else {
      // Do not return error if external display is not in connected status.
      // Due to virtual display concurrency, external display connection might be still pending
      // but hdmi got disconnected before pending connection could be processed.

      if (hdmi_primary) {
        assert(external_display != NULL);
        uint32_t x_res, y_res;
        external_display->GetFrameBufferResolution(&x_res, &y_res);
        // Need to manually disable VSYNC as SF is not aware of connect/disconnect cases
        // for HDMI as primary
        external_display->SetVsyncEnabled(HWC2::Vsync::Disable);
        HWCDisplayExternal::Destroy(external_display);

        // In HWC2, primary displays can be hotplugged out
        notify_hotplug = true;
      } else {
        if (hwc_display_[HWC_DISPLAY_EXTERNAL]) {
          status = DisconnectDisplay(HWC_DISPLAY_EXTERNAL);
          notify_hotplug = true;
        }
        external_pending_connect_ = false;
      }
    }
  }

  if (connected && notify_hotplug) {
    // trigger screen refresh to ensure sufficient resources are available to process new
    // new display connection.
    Refresh(0);
    uint32_t vsync_period = UINT32(GetVsyncPeriod(HWC_DISPLAY_PRIMARY));
    usleep(vsync_period * 2 / 1000);
  }
  // notify client
  // Handle HDMI as primary here
  if (notify_hotplug) {
    HotPlug(HWC_DISPLAY_EXTERNAL,
                       connected ? HWC2::Connection::Connected : HWC2::Connection::Disconnected);
  }

  qservice_->onHdmiHotplug(INT(connected));

  return 0;
}

int HWCSession::GetVsyncPeriod(int disp) {
  SCOPE_LOCK(locker_);
  // default value
  int32_t vsync_period = 1000000000l / 60;
  auto attribute = HWC2::Attribute::VsyncPeriod;

  if (hwc_display_[disp]) {
    hwc_display_[disp]->GetDisplayAttribute(0, attribute, &vsync_period);
  }

  return vsync_period;
}

android::status_t HWCSession::GetVisibleDisplayRect(const android::Parcel *input_parcel,
                                                    android::Parcel *output_parcel) {
  SCOPE_LOCK(locker_);

  int dpy = input_parcel->readInt32();

  if (dpy < HWC_DISPLAY_PRIMARY || dpy >= MAX_TOTAL_DISPLAY_NUM) {
    return android::BAD_VALUE;
  }

  if (!hwc_display_[dpy]) {
    return android::NO_INIT;
  }

  hwc_rect_t visible_rect = {0, 0, 0, 0};
  int error = hwc_display_[dpy]->GetVisibleDisplayRect(&visible_rect);
  if (error < 0) {
    return error;
  }

  output_parcel->writeInt32(visible_rect.left);
  output_parcel->writeInt32(visible_rect.top);
  output_parcel->writeInt32(visible_rect.right);
  output_parcel->writeInt32(visible_rect.bottom);

  return android::NO_ERROR;
}

void HWCSession::Refresh(hwc2_display_t display) {
  SCOPE_LOCK(callbacks_lock_);
  HWC2::Error err;
  err = callbacks_.Refresh(display);
  while (err != HWC2::Error::None) {
    callbacks_lock_.Wait();
    err = callbacks_.Refresh(display);
  }
}

void HWCSession::HotPlug(hwc2_display_t display, HWC2::Connection state) {
  SCOPE_LOCK(callbacks_lock_);
  HWC2::Error err;
  err = callbacks_.Hotplug(display, state);
  while (err != HWC2::Error::None) {
    callbacks_lock_.Wait();
    err = callbacks_.Hotplug(display, state);
  }
}

}  // namespace sdm
