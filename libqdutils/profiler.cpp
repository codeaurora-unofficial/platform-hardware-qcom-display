/*
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#define LOG_NDDEBUG 0
#include "profiler.h"

#ifdef DEBUG_CALC_FPS


ANDROID_SINGLETON_STATIC_INSTANCE(qdutils::CalcFps) ;

namespace qdutils {

CalcFps::CalcFps() {
    debug_fps_level = 0;
    Init();
}

CalcFps::~CalcFps() {
}

void CalcFps::Init() {
    char prop[PROPERTY_VALUE_MAX];
    property_get("debug.gr.calcfps", prop, "0");
    debug_fps_level = atoi(prop);
    if (debug_fps_level > MAX_DEBUG_FPS_LEVEL) {
        ALOGW("out of range value for debug.gr.calcfps, using 0");
        debug_fps_level = 0;
    }

    ALOGD("DEBUG_CALC_FPS: %d", debug_fps_level);
    populate_debug_fps_metadata();
}

void CalcFps::Fps() {
    if (debug_fps_level > 0)
        calc_fps(ns2us(systemTime()));
}

void CalcFps::populate_debug_fps_metadata(void)
{
    char prop[PROPERTY_VALUE_MAX];

    /*defaults calculation of fps to based on number of frames*/
    property_get("debug.gr.calcfps.type", prop, "0");
    debug_fps_metadata.type = (debug_fps_metadata_t::DfmType) atoi(prop);

    /*defaults to 1000ms*/
    property_get("debug.gr.calcfps.timeperiod", prop, "1000");
    debug_fps_metadata.time_period = atoi(prop);

    property_get("debug.gr.calcfps.period", prop, "10");
    debug_fps_metadata.period = atoi(prop);

    if (debug_fps_metadata.period > MAX_FPS_CALC_PERIOD_IN_FRAMES) {
        debug_fps_metadata.period = MAX_FPS_CALC_PERIOD_IN_FRAMES;
    }

    /* default ignorethresh_us: 500 milli seconds */
    property_get("debug.gr.calcfps.ignorethresh_us", prop, "500000");
    debug_fps_metadata.ignorethresh_us = atoi(prop);

    debug_fps_metadata.framearrival_steps =
        (debug_fps_metadata.ignorethresh_us / 16666);

    if (debug_fps_metadata.framearrival_steps > MAX_FRAMEARRIVAL_STEPS) {
        debug_fps_metadata.framearrival_steps = MAX_FRAMEARRIVAL_STEPS;
        debug_fps_metadata.ignorethresh_us =
            debug_fps_metadata.framearrival_steps * 16666;
    }

    /* 2ms margin of error for the gettimeofday */
    debug_fps_metadata.margin_us = 2000;

    for (unsigned int i = 0; i < MAX_FRAMEARRIVAL_STEPS; i++)
        debug_fps_metadata.accum_framearrivals[i] = 0;

    ALOGD("period: %d", debug_fps_metadata.period);
    ALOGD("ignorethresh_us: %lld", debug_fps_metadata.ignorethresh_us);
}

void CalcFps::print_fps(float fps)
{
    if (debug_fps_metadata_t::DFM_FRAMES == debug_fps_metadata.type)
        ALOGD("FPS for last %d frames: %3.2f", debug_fps_metadata.period, fps);
    else
        ALOGD("FPS for last (%f ms, %d frames): %3.2f",
              debug_fps_metadata.time_elapsed,
              debug_fps_metadata.curr_frame, fps);

    debug_fps_metadata.curr_frame = 0;
    debug_fps_metadata.time_elapsed = 0.0;

    if (debug_fps_level > 1) {
        ALOGD("Frame Arrival Distribution:");
        for (unsigned int i = 0;
             i < ((debug_fps_metadata.framearrival_steps / 6) + 1);
             i++) {
            ALOGD("%lld %lld %lld %lld %lld %lld",
                  debug_fps_metadata.accum_framearrivals[i*6],
                  debug_fps_metadata.accum_framearrivals[i*6+1],
                  debug_fps_metadata.accum_framearrivals[i*6+2],
                  debug_fps_metadata.accum_framearrivals[i*6+3],
                  debug_fps_metadata.accum_framearrivals[i*6+4],
                  debug_fps_metadata.accum_framearrivals[i*6+5]);
        }

        /* We are done with displaying, now clear the stats */
        for (unsigned int i = 0;
             i < debug_fps_metadata.framearrival_steps;
             i++)
            debug_fps_metadata.accum_framearrivals[i] = 0;
    }
    return;
}

void CalcFps::calc_fps(nsecs_t currtime_us)
{
    static nsecs_t oldtime_us = 0;

    nsecs_t diff = currtime_us - oldtime_us;

    oldtime_us = currtime_us;

    if (debug_fps_metadata_t::DFM_FRAMES == debug_fps_metadata.type &&
        diff > debug_fps_metadata.ignorethresh_us) {
        return;
    }

    if (debug_fps_metadata.curr_frame < MAX_FPS_CALC_PERIOD_IN_FRAMES) {
        debug_fps_metadata.framearrivals[debug_fps_metadata.curr_frame] = diff;
    }

    debug_fps_metadata.curr_frame++;

    if (debug_fps_level > 1) {
        unsigned int currstep = (diff + debug_fps_metadata.margin_us) / 16666;

        if (currstep < debug_fps_metadata.framearrival_steps) {
            debug_fps_metadata.accum_framearrivals[currstep-1]++;
        }
    }

    if (debug_fps_metadata_t::DFM_FRAMES == debug_fps_metadata.type) {
        if (debug_fps_metadata.curr_frame == debug_fps_metadata.period) {
            /* time to calculate and display FPS */
            nsecs_t sum = 0;
            for (unsigned int i = 0; i < debug_fps_metadata.period; i++)
                sum += debug_fps_metadata.framearrivals[i];
            print_fps((debug_fps_metadata.period * float(1000000))/float(sum));
        }
    }
    else if (debug_fps_metadata_t::DFM_TIME == debug_fps_metadata.type) {
        debug_fps_metadata.time_elapsed += ((float)diff/1000.0);
        if (debug_fps_metadata.time_elapsed >= debug_fps_metadata.time_period) {
            float fps = (1000.0 * debug_fps_metadata.curr_frame)/
                (float)debug_fps_metadata.time_elapsed;
            print_fps(fps);
        }
    }
    return;
}
};//namespace qomutils
#endif

ANDROID_SINGLETON_STATIC_INSTANCE(qdutils::VsyncMiss) ;

namespace qdutils {
VsyncMiss::VsyncMiss() {
    /*
     * property debug.vsyncmiss:
     *     purpose: To enable disable vsync miss prints
     *     values: 0 - disable, 1 - enable
     *     default value: 0
     * property debug.vsynctol:
     *     purpose: To specify tolerance value above vsync limit
     *     values: integer
     *     default balue: 8
     * property debug.vsynctime:
     *     purpose: To specify vsync pulse time
     *     values: integer
     *     default: 16
     * property debug.vsyncframes:
     *     purpose: To specify min number of frames over which
     *                  statics will be printed
     *     values: integer
     *     default - 10
     */
    char prop[PROPERTY_VALUE_MAX];
    property_get("debug.vsyncmiss", prop, "0");
    mEnableVsyncMissData = atoi(prop);
    property_get("debug.vsynctol", prop, "8");
    mVsyncMissTolerance = atoi(prop);
    property_get("debug.vsynctime", prop, "16");
    mVsyncTime = atoi(prop);
    property_get("debug.vsyncframes", prop, "10");
    mNumFrames = atoi(prop);
    mLastTimeStamp = 0;
    mCurrentFrameNum = 0;
    mNumVsyncCompMiss = 0;
}

VsyncMiss::~VsyncMiss() {
    mLayerMap.clear();
}

void VsyncMiss::printVsyncMiss() {
    if (!isVsyncMissPrintEnabled())
        return;
    mCurrentFrameNum++;

    nsecs_t now = systemTime();
    if (mLastTimeStamp) {
        int diff = ns2ms(now - mLastTimeStamp);
        if (diff > (mVsyncMissTolerance + mVsyncTime))
            mNumVsyncCompMiss++;
    }
    mLastTimeStamp = systemTime();

    if (mCurrentFrameNum >= mNumFrames) {
        mCurrentFrameNum = 0;
        if (mNumVsyncCompMiss > 0)
            ALOGD("Composition Vsync Miss %d in last %d frames",
                                       mNumVsyncCompMiss, mNumFrames);
        mNumVsyncCompMiss = 0;
        Mutex::Autolock _l(mLock);
        const unsigned int mapSize = mLayerMap.size();
        for (unsigned int i = 0; i < mapSize; i++) {
            LayerInfo* layer = mLayerMap.valueAt(i);
            if (layer->mNumVsyncMiss > 0) {
                const char* name = layer->mLayerName.string();
                ALOGD("LayerIdentity:: %d LayerName:: %s, for last %d frames, NumVsyncMiss:: %d",
                          layer->mLayerNum, ((name == NULL) ? "NULL" : name),
                          mNumFrames, layer->mNumVsyncMiss);
            }
            layer->clearVsyncInfo();
        }
    }
}

void VsyncMiss::addLayer(int32_t num, const String8& name) {
    if (!isVsyncMissPrintEnabled())
        return;
    if (isLayerPresent(num)) {
        ALOGE("Trying to add duplicate key-value for identity %d and name %s",
                             num, name.string());
        return;
    }
    Mutex::Autolock _l(mLock);
    LayerInfo* layer = new LayerInfo(num, name);
    mLayerMap.add(num, layer);
}

void VsyncMiss::removeLayer(int32_t num) {
    if (!isVsyncMissPrintEnabled())
        return;
    Mutex::Autolock _l(mLock);
    if (!isLayerPresent(num)) {
        ALOGE("Trying to remove non-existent item");
        return;
    }
    LayerInfo* layer = mLayerMap.valueFor(num);
    mLayerMap.removeItem(num);
    delete layer;
}

void VsyncMiss::pageFlipInfo(int32_t num) {
    if (!isVsyncMissPrintEnabled())
        return;
    Mutex::Autolock _l(mLock);
    if (!isLayerPresent(num)) {
        ALOGE("No layer added for identity.. %d", num);
        return;
    }
    LayerInfo* layer = mLayerMap.valueFor(num);
    nsecs_t now = systemTime();
    if (layer->mLastTimeStamp) {
        int diff = ns2ms(now - layer->mLastTimeStamp);
        if (diff > (mVsyncMissTolerance + mVsyncTime))
            layer->mNumVsyncMiss++;
    }
    layer->mLastTimeStamp = now;
}

void VsyncMiss::onFrameQInfo(int32_t num) {
    if (!isVsyncMissPrintEnabled())
        return;
    Mutex::Autolock _l(mLock);
    if (!isLayerPresent(num)) {
        ALOGE("No layer added for identity.. %d", num);
        return;
    }
    LayerInfo* layer = mLayerMap.valueFor(num);
    nsecs_t now = systemTime();
    if (layer->mLastFrameQ) {
        int diff = ns2ms(now - layer->mLastFrameQ);
        if ((diff > (mVsyncMissTolerance + mVsyncTime)) ||
                                            diff < mVsyncTime)
            layer->mNumQVsyncDiff++;
    }
    layer->mLastFrameQ = now;
}
};//namespace qomutils
