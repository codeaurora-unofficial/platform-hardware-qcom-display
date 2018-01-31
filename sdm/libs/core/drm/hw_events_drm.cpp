/*
* Copyright (c) 2017, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above
*       copyright notice, this list of conditions and the following
*       disclaimer in the documentation and/or other materials provided
*       with the distribution.
*     * Neither the name of The Linux Foundation nor the names of its
*       contributors may be used to endorse or promote products derived
*       from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
* ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
* WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
* OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
* IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <drm_master.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <utils/debug.h>
#include <utils/sys.h>
#include <xf86drm.h>

#include <algorithm>
#include <map>
#include <utility>
#include <vector>

#include "hw_events_drm.h"

#define __CLASS__ "HWEventsDRM"

namespace sdm {

using drm_utils::DRMMaster;

DisplayError HWEventsDRM::InitializePollFd() {
  for (uint32_t i = 0; i < event_data_list_.size(); i++) {
    char data[kMaxStringLength]{};
    HWEventData &event_data = event_data_list_[i];
    poll_fds_[i].fd = -1;

    switch (event_data.event_type) {
      case HWEvent::VSYNC: {
        poll_fds_[i].events = POLLIN | POLLPRI | POLLERR;
        DRMMaster *master = nullptr;
        int ret = DRMMaster::GetInstance(&master);
        if (ret < 0) {
          DLOGE("Failed to acquire DRMMaster instance");
          return kErrorNotSupported;
        }
        master->GetHandle(&poll_fds_[i].fd);
        vsync_index_ = i;
      } break;
      case HWEvent::EXIT: {
        // Create an eventfd to be used to unblock the poll system call when
        // a thread is exiting.
        poll_fds_[i].fd = Sys::eventfd_(0, 0);
        poll_fds_[i].events |= POLLIN;
        // Clear any existing data
        Sys::pread_(poll_fds_[i].fd, data, kMaxStringLength, 0);
      } break;
      case HWEvent::IDLE_NOTIFY:
      case HWEvent::CEC_READ_MESSAGE:
      case HWEvent::SHOW_BLANK_EVENT:
      case HWEvent::THERMAL_LEVEL:
      case HWEvent::IDLE_POWER_COLLAPSE:
        break;
    }
  }

  return kErrorNone;
}

DisplayError HWEventsDRM::SetEventParser() {
  DisplayError error = kErrorNone;

  for (auto &event_data : event_data_list_) {
    switch (event_data.event_type) {
      case HWEvent::VSYNC:
        event_data.event_parser = &HWEventsDRM::HandlePageFlip;
        break;
      case HWEvent::IDLE_NOTIFY:
        event_data.event_parser = &HWEventsDRM::HandleIdleTimeout;
        break;
      case HWEvent::CEC_READ_MESSAGE:
        event_data.event_parser = &HWEventsDRM::HandleCECMessage;
        break;
      case HWEvent::EXIT:
        event_data.event_parser = &HWEventsDRM::HandleThreadExit;
        break;
      case HWEvent::SHOW_BLANK_EVENT:
        event_data.event_parser = &HWEventsDRM::HandleBlank;
        break;
      case HWEvent::THERMAL_LEVEL:
        event_data.event_parser = &HWEventsDRM::HandleThermal;
        break;
      case HWEvent::IDLE_POWER_COLLAPSE:
        event_data.event_parser = &HWEventsDRM::HandleIdlePowerCollapse;
        break;
      default:
        error = kErrorParameters;
        break;
    }
  }

  return error;
}

void HWEventsDRM::PopulateHWEventData(const vector<HWEvent> &event_list) {
  for (auto &event : event_list) {
    HWEventData event_data;
    event_data.event_type = event;
    event_data_list_.push_back(std::move(event_data));
  }

  SetEventParser();
  InitializePollFd();
}

DisplayError HWEventsDRM::Init(int display_type, HWEventHandler *event_handler,
                               const vector<HWEvent> &event_list) {
  if (!event_handler)
    return kErrorParameters;

  event_handler_ = event_handler;
  poll_fds_.resize(event_list.size());
  event_thread_name_ += " - " + std::to_string(display_type);

  PopulateHWEventData(event_list);

  if (pthread_create(&event_thread_, NULL, &DisplayEventThread, this) < 0) {
    DLOGE("Failed to start %s, error = %s", event_thread_name_.c_str());
    return kErrorResources;
  }
  // For multiple display case, don't launch multiple threads to handle event together,
  // otherwise only one thread can get chance to handle vblank event after polling and
  // other threads will get stuck on drmHandleVBlank. The result is the vblank callback
  // function will not be executed sometimes since the RegisterVSync will not be called
  // while the whole thread gets stuck. So if any repainting which is triggered by vblank
  // callback function, the repainting is not smooth sometime.
  // One proposal is creating a single thread to handle drm event, then once corresponding
  // event callback function is called, call RegisterVSync again. If so, we only need to
  // call RegisterVSync to register the first vblank event for each display.
  if (sync_event_type_ != kPageFlipEvent)
    RegisterVSync();

  return kErrorNone;
}

DisplayError HWEventsDRM::Deinit() {
  exit_threads_ = true;
  Sys::pthread_cancel_(event_thread_);
  WakeUpEventThread();
  pthread_join(event_thread_, NULL);
  CloseFds();

  return kErrorNone;
}

DisplayError HWEventsDRM::SetEventState(HWEvent event, bool enable, void *arg) {
  switch (event) {
    case HWEvent::VSYNC:
      vsync_enabled_ = enable;

      if (enable) {
        WakeUpEventThread();
      } else {
        //wait till pending vblank is consumed...
        pthread_mutex_lock(&vbl_mutex_);
        while (vbl_pending_) {
          pthread_cond_wait(&vbl_cond_, &vbl_mutex_);
        }
        pthread_mutex_unlock(&vbl_mutex_);
      }
      break;
    default:
      DLOGE("Event not supported");
      return kErrorNotSupported;
  }

  return kErrorNone;
}

void HWEventsDRM::WakeUpEventThread() {
  for (uint32_t i = 0; i < event_data_list_.size(); i++) {
    if (event_data_list_[i].event_type == HWEvent::EXIT) {
      uint64_t exit_value = 1;
      ssize_t write_size = Sys::write_(poll_fds_[i].fd, &exit_value, sizeof(uint64_t));
      if (write_size != sizeof(uint64_t)) {
        DLOGW("Error triggering exit fd (%d). write size = %d, error = %s", poll_fds_[i].fd,
              write_size, strerror(errno));
      }
      break;
    }
  }
}

DisplayError HWEventsDRM::CloseFds() {
  for (uint32_t i = 0; i < event_data_list_.size(); i++) {
    switch (event_data_list_[i].event_type) {
      case HWEvent::VSYNC:
        poll_fds_[i].fd = -1;
        break;
      case HWEvent::EXIT:
        Sys::close_(poll_fds_[i].fd);
        poll_fds_[i].fd = -1;
        break;
      case HWEvent::IDLE_NOTIFY:
      case HWEvent::CEC_READ_MESSAGE:
      case HWEvent::SHOW_BLANK_EVENT:
      case HWEvent::THERMAL_LEVEL:
      case HWEvent::IDLE_POWER_COLLAPSE:
        break;
      default:
        return kErrorNotSupported;
    }
  }

  return kErrorNone;
}

void *HWEventsDRM::DisplayEventThread(void *context) {
  if (context) {
    return reinterpret_cast<HWEventsDRM *>(context)->DisplayEventHandler();
  }

  return NULL;
}

void *HWEventsDRM::DisplayEventHandler() {
  char data[kMaxStringLength]{};

  prctl(PR_SET_NAME, event_thread_name_.c_str(), 0, 0, 0);
  setpriority(PRIO_PROCESS, 0, kThreadPriorityUrgent);

  while (!exit_threads_) {
    if (vsync_enabled_ && RegisterVSync() != kErrorNone) {
      pthread_exit(0);
      return nullptr;
    }

    int error = Sys::poll_(poll_fds_.data(), UINT32(poll_fds_.size()), -1);
    if (error <= 0) {
      DLOGW("poll failed. error = %s", strerror(errno));
      continue;
    }

    for (uint32_t i = 0; i < event_data_list_.size(); i++) {
      pollfd &poll_fd = poll_fds_[i];
      switch (event_data_list_[i].event_type) {
        case HWEvent::VSYNC:
          (this->*(event_data_list_[i]).event_parser)(nullptr);
          break;
        case HWEvent::EXIT:
          if ((poll_fd.revents & POLLIN) &&
              (Sys::read_(poll_fd.fd, data, kMaxStringLength) > 0)) {
            (this->*(event_data_list_[i]).event_parser)(data);
          }
          break;
        case HWEvent::IDLE_NOTIFY:
        case HWEvent::CEC_READ_MESSAGE:
        case HWEvent::SHOW_BLANK_EVENT:
        case HWEvent::THERMAL_LEVEL:
        case HWEvent::IDLE_POWER_COLLAPSE:
          if (poll_fd.fd >= 0 && (poll_fd.revents & POLLPRI) &&
              (Sys::pread_(poll_fd.fd, data, kMaxStringLength, 0) > 0)) {
            (this->*(event_data_list_[i]).event_parser)(data);
          }
          break;
      }
    }
  }

  //pthread_exit(0);

  return nullptr;
}

DisplayError HWEventsDRM::RegisterVSync() {
  pthread_mutex_lock(&vbl_mutex_);
  drmVBlank vblank{};
  vblank.request.type = (drmVBlankSeqType)(DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT);
  vblank.request.sequence = 1;
  // DRM hack to pass in context to unused field signal. Driver will write this to the node being
  // polled on, and will be read as part of drm event handling and sent to handler
  vblank.request.signal = reinterpret_cast<unsigned long>(this);  // NOLINT
  int error = drmWaitVBlank(poll_fds_[vsync_index_].fd, &vblank);
  if (error < 0) {
    DLOGE("drmWaitVBlank failed with err %d", errno);
    pthread_mutex_unlock(&vbl_mutex_);
    return kErrorResources;
  }

  vbl_pending_ = true;
  pthread_mutex_unlock(&vbl_mutex_);

  return kErrorNone;
}

DisplayError HWEventsDRM::RegisterVBlankPFlip(){
  drmEventContext evctx;
  memset(&evctx, 0, sizeof evctx);
  evctx.version = DRM_EVENT_CONTEXT_VERSION;
  evctx.vblank_handler = &HWEventsDRM::VBlankHandlerCallback;
  evctx.page_flip_handler = &HWEventsDRM::PFlipHandlerCallback;
  int error = drmHandleEvent(poll_fds_[vsync_index_].fd, &evctx);
  if (error < 0) {
    DLOGE("drmHandleEvent failed with err %d", errno);
    return kErrorResources;
  }

  return kErrorNone;
}

DisplayError HWEventsDRM::RequestVBlankEvent(drmVBlank *vbl) {
  int error = drmWaitVBlank(poll_fds_[vsync_index_].fd, vbl);
  if (error < 0) {
    DLOGE("drmWaitVBlank failed with err %d", errno);
    return kErrorResources;
  }

  return kErrorNone;
}

DisplayError HWEventsDRM::RequestPageFlip(uint32_t crtc_id,
                                            uint32_t fb_id,
                                            uint32_t flags,
                                            void *userdata) {
  return kErrorNone;
}

void HWEventsDRM::HandleVSync(char *data) {
  if (poll_fds_[vsync_index_].revents & (POLLIN | POLLPRI)) {
    drmEventContext event = {};
    event.version = DRM_EVENT_CONTEXT_VERSION;
    event.vblank_handler = &HWEventsDRM::VSyncHandlerCallback;
    int error = drmHandleEvent(poll_fds_[vsync_index_].fd, &event);
    if (error != 0) {
      DLOGE("drmHandleEvent failed: %i", error);
    }
  }
}

void HWEventsDRM::VSyncHandlerCallback(int fd, unsigned int sequence, unsigned int tv_sec,
                                       unsigned int tv_usec, void *data) {
  int64_t timestamp = (int64_t)(tv_sec)*1000000000 + (int64_t)(tv_usec)*1000;
  reinterpret_cast<HWEventsDRM *>(data)->event_handler_->VSync(timestamp);
}

void HWEventsDRM::HandleVBlank(char *data) {
  if (poll_fds_[vsync_index_].revents & (POLLIN | POLLPRI)) {
    pthread_mutex_lock(&vbl_mutex_);

    drmEventContext event = {};
    event.version = DRM_EVENT_CONTEXT_VERSION;
    event.vblank_handler = &HWEventsDRM::VBlankHandlerCallback;
    int error = drmHandleEvent(poll_fds_[vsync_index_].fd, &event);
    if (error != 0) {
      DLOGE("drmHandleEvent failed: %i", error);
    }

    vbl_pending_ = false;
    pthread_cond_signal(&vbl_cond_);

    pthread_mutex_unlock(&vbl_mutex_);
  }
}

void HWEventsDRM::HandlePageFlip(char *data) {
  if (poll_fds_[vsync_index_].revents & (POLLIN | POLLPRI)) {
    pthread_mutex_lock(&vbl_mutex_);

    drmEventContext event = {};
    event.version = DRM_EVENT_CONTEXT_VERSION;
    event.page_flip_handler = &HWEventsDRM::PFlipHandlerCallback;
    int error = drmHandleEvent(poll_fds_[vsync_index_].fd, &event);
    if (error != 0) {
      DLOGE("drmHandleEvent failed: %i", error);
    }

    vbl_pending_ = false;
    pthread_cond_signal(&vbl_cond_);

    pthread_mutex_unlock(&vbl_mutex_);
  }
}

void HWEventsDRM::VBlankHandlerCallback(int fd, unsigned int sequence, unsigned int tv_sec,
                                       unsigned int tv_usec, void *data) {
  reinterpret_cast<HWEventsDRM *>(data)->event_handler_->VSync(fd, sequence, tv_sec, tv_usec, data);
}

void HWEventsDRM::PFlipHandlerCallback(int fd, unsigned int sequence, unsigned int tv_sec,
                                       unsigned int tv_usec, void *data) {
  reinterpret_cast<HWEventsDRM *>(data)->event_handler_->PFlip(fd, sequence, tv_sec, tv_usec, data);
}

void HWEventsDRM::HandleIdleTimeout(char *data) {
  event_handler_->IdleTimeout();
}

void HWEventsDRM::HandleCECMessage(char *data) {
  event_handler_->CECMessage(data);
}

void HWEventsDRM::HandleIdlePowerCollapse(char *data) {
  event_handler_->IdlePowerCollapse();
}

}  // namespace sdm
