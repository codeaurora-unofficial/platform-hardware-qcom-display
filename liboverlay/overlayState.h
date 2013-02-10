/*
* Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#ifndef OVERLAY_STATE_H
#define OVERLAY_STATE_H

#include "overlayUtils.h"
#include "overlayImpl.h"
#include "overlayRotator.h"
#include "pipes/overlayGenPipe.h"
#include "pipes/overlayVideoExtPipe.h"
#include "pipes/overlayUIMirrorPipe.h"
#include "pipes/overlay3DPipe.h"
#include "pipes/overlayFloatingPipe.h"

namespace overlay {

/*
* Used by Overlay class. Invokes each event
* */

class OverlayState : utils::NoCopy {
public:
    /*ctor*/
    explicit OverlayState();

    /*dtor*/
    ~OverlayState();

    /* return current state */
    utils::eOverlayState state() const;

    /* Hard reset to a new state. If the state is the same
     * as the current one, it would be a no-op */
    OverlayImplBase* reset(utils::eOverlayState s);

    /* Caller pass the state to the handleEvent function.
     * The input is the current OverlayImplBase*, and output is
     * a pointer to (possibly new) instance of OverlayImplBase
     * The eFormat can be 2D/3D etc. */
    OverlayImplBase* handleEvent(utils::eOverlayState s,
            OverlayImplBase* ov);

    /* Dump */
    void dump() const;

private:

    /* Transitions from a state to a state. Default behavior is to move from an
     * old state to CLOSED and from CLOSED to a new state. Any impl wishing to
     * copy pipes should specialize this call */
    template<utils::eOverlayState FROM_STATE, utils::eOverlayState TO_STATE>
    OverlayImplBase* handle_from_to(OverlayImplBase* ov);

    /* Just convenient intermediate functions to bring down the number of
     * combinations arising from multiple states */
    template<utils::eOverlayState FROM_STATE>
    OverlayImplBase* handle_from(utils::eOverlayState toState,
            OverlayImplBase* ov);
    OverlayImplBase* handle_CLOSED_to(utils::eOverlayState toState,
            OverlayImplBase* ov);

    /* Substitues for partially specialized templated handle functions since the
     * standard doesn't allow partial specialization of functions */
    template<utils::eOverlayState FROM_STATE>
    OverlayImplBase* handle_xxx_to_CLOSED(OverlayImplBase* ov);

    template<utils::eOverlayState TO_STATE>
    OverlayImplBase* handle_CLOSED_to_xxx(OverlayImplBase* ov);

    /* States here */
    utils::eOverlayState mState;
};

//------------------------State Traits --------------------------

// primary has nothing
template <int STATE> struct StateTraits {};

/*
 * For 3D_xxx we need channel ID besides the FBx since
 * get crop/position 3D need that to determine pos/crop
 * info.
 * */

template <> struct StateTraits<utils::OV_2D_VIDEO_ON_PANEL>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0; //prim video
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::NullPipe pipe2;   // place holder
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef Rotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_2D_VIDEO_ON_PANEL_TV>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0; //prim video
    typedef overlay::VideoExtPipe pipe1; //ext video
    typedef overlay::GenericPipe<utils::EXTERNAL> pipe2; //ext subtitle
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef Rotator rot0;
    typedef Rotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_2D_VIDEO_ON_TV>
{
    typedef overlay::NullPipe pipe0; //nothing on primary with mdp
    typedef overlay::VideoExtPipe pipe1; //ext video
    typedef overlay::GenericPipe<utils::EXTERNAL> pipe2; //ext subtitle
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef Rotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_2D_PANEL>
{
    typedef overlay::M3DPrimaryPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::NullPipe pipe2;   // place holder
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef Rotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_3D_PANEL>
{
    typedef overlay::S3DPrimaryPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::S3DPrimaryPipe<utils::OV_PIPE1> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef Rotator rot0;
    typedef Rotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_3D_TV>
{
    typedef overlay::S3DExtPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::S3DExtPipe<utils::OV_PIPE1> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>
{
    typedef overlay::M3DPrimaryPipe<utils::OV_PIPE0> pipe0;
    typedef overlay::M3DExtPipe<utils::OV_PIPE1> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef Rotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_UI_MIRROR>
{

    typedef overlay::NullPipe pipe0;   // place holder
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::UIMirrorPipe pipe2;
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot3;
    typedef Rotator rot2;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_2D_TRUE_UI_MIRROR>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::VideoExtPipe pipe1;
    typedef overlay::UIMirrorPipe pipe2;
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef Rotator rot0;
    typedef Rotator rot1;
    typedef Rotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_BYPASS_1_LAYER>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::NullPipe pipe1;   // place holder
    typedef overlay::NullPipe pipe2;   // place holder
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_BYPASS_2_LAYER>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe1;
    typedef overlay::NullPipe pipe2;   // place holder
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_BYPASS_3_LAYER>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe1;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe2;
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_BYPASS_4_LAYER>
{
    typedef overlay::GenericPipe<utils::PRIMARY> pipe0;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe1;
    typedef overlay::GenericPipe<utils::PRIMARY> pipe2;
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_DUAL_DISP>
{
    typedef overlay::GenericPipe<utils::EXTERNAL> pipe0;
    typedef overlay::NullPipe pipe1;
    typedef overlay::NullPipe pipe2;
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

template <> struct StateTraits<utils::OV_FB>
{
    typedef overlay::NullPipe pipe0;
    typedef overlay::NullPipe pipe1;
    typedef overlay::NullPipe pipe2;
    typedef overlay::FloatingPipe pipe3;   // FB pipe

    typedef NullRotator rot0;
    typedef NullRotator rot1;
    typedef NullRotator rot2;
    typedef NullRotator rot3;

    typedef overlay::OverlayImpl<pipe0, pipe1, pipe2, pipe3> ovimpl;
};

//------------------------Inlines --------------------------------


inline OverlayState::OverlayState() : mState(utils::OV_CLOSED){}
inline OverlayState::~OverlayState() {}
inline utils::eOverlayState OverlayState::state() const { return mState; }
inline OverlayImplBase* OverlayState::reset(utils::eOverlayState s) {
    return handleEvent(s, 0);
}
inline void OverlayState::dump() const {
    ALOGE("== Dump state %d start/end ==", mState);
}

inline OverlayImplBase* OverlayState::handleEvent(utils::eOverlayState toState,
        OverlayImplBase* ov)
{
    OverlayImplBase* newov = ov; // at least, we return the same
    if (mState != toState) {
        ALOGD_IF(DEBUG_OVERLAY, "%s: state changed %s-->%s",
                __FUNCTION__, getStateString(mState), getStateString(toState));
    } else {
        ALOGD_IF(DEBUG_OVERLAY, "%s: no state change, state=%s",
                __FUNCTION__, getStateString(toState));
        return newov;
    }

    switch(mState)
    {
        case utils::OV_CLOSED:
            newov = handle_CLOSED_to(toState, ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            newov = handle_from<utils::OV_2D_VIDEO_ON_PANEL>(toState, ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            newov = handle_from<utils::OV_2D_VIDEO_ON_PANEL_TV>(toState, ov);
            break;
        case utils::OV_2D_VIDEO_ON_TV:
            newov = handle_from<utils::OV_2D_VIDEO_ON_TV>(toState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            newov = handle_from<utils::OV_3D_VIDEO_ON_2D_PANEL>(toState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            newov = handle_from<utils::OV_3D_VIDEO_ON_3D_PANEL>(toState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            newov = handle_from<utils::OV_3D_VIDEO_ON_3D_TV>(toState, ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            newov = handle_from<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(toState,
                                                                      ov);
            break;
        case utils::OV_UI_MIRROR:
            newov = handle_from<utils::OV_UI_MIRROR>(toState, ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            newov = handle_from<utils::OV_2D_TRUE_UI_MIRROR>(toState, ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            newov = handle_from<utils::OV_BYPASS_1_LAYER>(toState, ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            newov = handle_from<utils::OV_BYPASS_2_LAYER>(toState, ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            newov = handle_from<utils::OV_BYPASS_3_LAYER>(toState, ov);
            break;
        case utils::OV_BYPASS_4_LAYER:
            newov = handle_from<utils::OV_BYPASS_4_LAYER>(toState, ov);
            break;
        case utils::OV_DUAL_DISP:
            newov = handle_from<utils::OV_DUAL_DISP>(toState, ov);
            break;
        case utils::OV_FB:
            newov = handle_from<utils::OV_FB>(toState, ov);
            break;
        default:
            OVASSERT(1 == 0, "%s: unknown state = %d", __FUNCTION__, mState);

    }
    return newov;
}

template <utils::eOverlayState FROM_STATE>
inline OverlayImplBase* OverlayState::handle_from(utils::eOverlayState toState,
        OverlayImplBase* ov) {

    switch(toState)
    {
        case utils::OV_CLOSED:
            ov = handle_xxx_to_CLOSED<FROM_STATE>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            ov = handle_from_to<FROM_STATE, utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            ov = handle_from_to<FROM_STATE, utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_TV:
            ov = handle_from_to<FROM_STATE, utils::OV_2D_VIDEO_ON_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            ov = handle_from_to<FROM_STATE, utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            ov = handle_from_to<FROM_STATE, utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            ov = handle_from_to<FROM_STATE, utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            ov = handle_from_to<FROM_STATE,
                        utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            ov = handle_from_to<FROM_STATE, utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            ov = handle_from_to<FROM_STATE, utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            ov = handle_from_to<FROM_STATE, utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            ov = handle_from_to<FROM_STATE, utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            ov = handle_from_to<FROM_STATE, utils::OV_BYPASS_3_LAYER>(ov);
            break;
        case utils::OV_BYPASS_4_LAYER:
            ov = handle_from_to<FROM_STATE, utils::OV_BYPASS_4_LAYER>(ov);
            break;
        case utils::OV_DUAL_DISP:
            ov = handle_from_to<FROM_STATE, utils::OV_DUAL_DISP>(ov);
            break;
        case utils::OV_FB:
            ov = handle_from_to<FROM_STATE, utils::OV_FB>(ov);
            break;
        default:
            OVASSERT(1 == 0, "%s: unknown state = %d", __FUNCTION__, toState);
    }
    mState = toState;
    return ov;
}

inline OverlayImplBase* OverlayState::handle_CLOSED_to(
        utils::eOverlayState toState, OverlayImplBase* ov) {

    switch(toState)
    {
        case utils::OV_CLOSED:
            break;
        case utils::OV_2D_VIDEO_ON_PANEL:
            ov = handle_CLOSED_to_xxx<utils::OV_2D_VIDEO_ON_PANEL>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_PANEL_TV:
            ov = handle_CLOSED_to_xxx<utils::OV_2D_VIDEO_ON_PANEL_TV>(ov);
            break;
        case utils::OV_2D_VIDEO_ON_TV:
            ov = handle_CLOSED_to_xxx<utils::OV_2D_VIDEO_ON_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL:
            ov = handle_CLOSED_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_PANEL:
            ov = handle_CLOSED_to_xxx<utils::OV_3D_VIDEO_ON_3D_PANEL>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_3D_TV:
            ov = handle_CLOSED_to_xxx<utils::OV_3D_VIDEO_ON_3D_TV>(ov);
            break;
        case utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            ov = handle_CLOSED_to_xxx<utils::OV_3D_VIDEO_ON_2D_PANEL_2D_TV>(ov);
            break;
        case utils::OV_UI_MIRROR:
            ov = handle_CLOSED_to_xxx<utils::OV_UI_MIRROR>(ov);
            break;
        case utils::OV_2D_TRUE_UI_MIRROR:
            ov = handle_CLOSED_to_xxx<utils::OV_2D_TRUE_UI_MIRROR>(ov);
            break;
        case utils::OV_BYPASS_1_LAYER:
            ov = handle_CLOSED_to_xxx<utils::OV_BYPASS_1_LAYER>(ov);
            break;
        case utils::OV_BYPASS_2_LAYER:
            ov = handle_CLOSED_to_xxx<utils::OV_BYPASS_2_LAYER>(ov);
            break;
        case utils::OV_BYPASS_3_LAYER:
            ov = handle_CLOSED_to_xxx<utils::OV_BYPASS_3_LAYER>(ov);
            break;
        case utils::OV_BYPASS_4_LAYER:
            ov = handle_CLOSED_to_xxx<utils::OV_BYPASS_4_LAYER>(ov);
            break;
        case utils::OV_DUAL_DISP:
            ov = handle_CLOSED_to_xxx<utils::OV_DUAL_DISP>(ov);
            break;
        case utils::OV_FB:
            ov = handle_CLOSED_to_xxx<utils::OV_FB>(ov);
            break;
        default:
            OVASSERT(1 == 0, "%s: unknown state = %d", __FUNCTION__, toState);
    }
    mState = toState;
    return ov;
}

/* Transition default from any to any. */
template<utils::eOverlayState FROM_STATE, utils::eOverlayState TO_STATE>
inline OverlayImplBase* OverlayState::handle_from_to(OverlayImplBase* ov) {
    // Create new ovimpl based on new state
    OverlayImplBase* newov = new typename StateTraits<TO_STATE>::ovimpl;

    //close old pipe0, create new pipe0
    ov->closePipe(utils::OV_PIPE0);
    RotatorBase* rot0 = new typename StateTraits<TO_STATE>::rot0;
    newov->initPipe(rot0, utils::OV_PIPE0);
    //close old pipe1, create new pipe1
    ov->closePipe(utils::OV_PIPE1);
    RotatorBase* rot1 = new typename StateTraits<TO_STATE>::rot1;
    newov->initPipe(rot1, utils::OV_PIPE1);
    //close old pipe2, create new pipe2
    ov->closePipe(utils::OV_PIPE2);
    RotatorBase* rot2 = new typename StateTraits<TO_STATE>::rot2;
    newov->initPipe(rot2, utils::OV_PIPE2);
    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);
    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

//---------------Specializations-------------


/* Transition from CLOSED to ANY */
template<utils::eOverlayState TO_STATE>
inline OverlayImplBase* OverlayState::handle_CLOSED_to_xxx(
            OverlayImplBase* /*ignored*/) {
    //If going from CLOSED to CLOSED, nothing to do.
    if(TO_STATE == utils::OV_CLOSED) return NULL;
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_CLOSED),
            utils::getStateString(TO_STATE));
    OverlayImplBase* ov = new typename StateTraits<TO_STATE>::ovimpl;
    RotatorBase* rot0 = new typename StateTraits<TO_STATE>::rot0;
    RotatorBase* rot1 = new typename StateTraits<TO_STATE>::rot1;
    RotatorBase* rot2 = new typename StateTraits<TO_STATE>::rot2;
    RotatorBase* rot3 = new typename StateTraits<TO_STATE>::rot3;
    if(!ov->init(rot0, rot1, rot2, rot3)) {
        ALOGE("Overlay failed to init in state %d", TO_STATE);
        return 0;
    }
    return ov;
}

/* Transition from ANY to CLOSED */
template<utils::eOverlayState FROM_STATE>
inline OverlayImplBase* OverlayState::handle_xxx_to_CLOSED(OverlayImplBase* ov)
{
    //If going from CLOSED to CLOSED, nothing to do.
    if(FROM_STATE == utils::OV_CLOSED) return NULL;
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(FROM_STATE),
            utils::getStateString(utils::OV_CLOSED));
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    if(!ov->close()) {
        ALOGE("%s: Failed to ov close", __FUNCTION__);
    }
    delete ov;
    ov = 0;
    return 0;
}

/* Transition from 2D_VIDEO_ON_PANEL to 2D_VIDEO_ON_PANEL_TV */
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_2D_VIDEO_ON_PANEL,
        utils::OV_2D_VIDEO_ON_PANEL_TV>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL),
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL_TV));
    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_VIDEO_ON_PANEL_TV> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //copy pipe0/rot0 (primary video)
    newov->copyOvPipe(ov, utils::OV_PIPE0);
    //close old pipe1, create new pipe1
    ov->closePipe(utils::OV_PIPE1);
    RotatorBase* rot1 = new NewState::rot1;
    newov->initPipe(rot1, utils::OV_PIPE1);
    //close old pipe2, create new pipe2
    ov->closePipe(utils::OV_PIPE2);
    RotatorBase* rot2 = new NewState::rot2;
    newov->initPipe(rot2, utils::OV_PIPE2);
    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);
    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

/* Transition from 2D_VIDEO_ON_PANEL_TV to 2D_VIDEO_ON_PANEL */
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_2D_VIDEO_ON_PANEL_TV,
        utils::OV_2D_VIDEO_ON_PANEL>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL_TV),
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL));

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_VIDEO_ON_PANEL> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //copy pipe0/rot0 (primary video)
    newov->copyOvPipe(ov, utils::OV_PIPE0);
    //close old pipe1, create new pipe1
    ov->closePipe(utils::OV_PIPE1);
    RotatorBase* rot1 = new NewState::rot1;
    newov->initPipe(rot1, utils::OV_PIPE1);
    //close old pipe2, create new pipe2
    ov->closePipe(utils::OV_PIPE2);
    RotatorBase* rot2 = new NewState::rot2;
    newov->initPipe(rot2, utils::OV_PIPE2);
    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);
    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

/* Transition from 2D_VIDEO_ON_PANEL_TV to 2D_VIDEO_ON_TV */
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_2D_VIDEO_ON_PANEL_TV,
        utils::OV_2D_VIDEO_ON_TV>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL_TV),
            utils::getStateString(utils::OV_2D_VIDEO_ON_TV));

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_VIDEO_ON_TV> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //close old pipe0, create new pipe0
    ov->closePipe(utils::OV_PIPE0);
    RotatorBase* rot0 = new NewState::rot0;
    newov->initPipe(rot0, utils::OV_PIPE0);
    //copy pipe1/rot1 (ext video)
    newov->copyOvPipe(ov, utils::OV_PIPE1);
    //copy pipe2/rot2 (ext cc)
    newov->copyOvPipe(ov, utils::OV_PIPE2);
    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);
    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

/* Transition from 2D_VIDEO_ON_TV to 2D_VIDEO_ON_PANEL_TV */
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_2D_VIDEO_ON_TV,
        utils::OV_2D_VIDEO_ON_PANEL_TV>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_2D_VIDEO_ON_TV),
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL_TV));

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_VIDEO_ON_PANEL_TV> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //close old pipe0, create new pipe0
    ov->closePipe(utils::OV_PIPE0);
    RotatorBase* rot0 = new NewState::rot0;
    newov->initPipe(rot0, utils::OV_PIPE0);
    //copy pipe1/rot1 (ext video)
    newov->copyOvPipe(ov, utils::OV_PIPE1);
    //copy pipe2/rot2 (ext cc)
    newov->copyOvPipe(ov, utils::OV_PIPE2);
    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);
    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

/* Transition from OV_UI_MIRROR to OV_2D_TRUE_UI_MIRROR */
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_UI_MIRROR,
        utils::OV_2D_TRUE_UI_MIRROR>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_UI_MIRROR),
            utils::getStateString(utils::OV_2D_TRUE_UI_MIRROR));

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_TRUE_UI_MIRROR> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //copy pipe2/rot2 (UIMirroringPipe)
    newov->copyOvPipe(ov, utils::OV_PIPE2);
    //close old pipes 0/1
    ov->closePipe(utils::OV_PIPE0);
    ov->closePipe(utils::OV_PIPE1);

    //create new pipes 0/1
    RotatorBase* rot0 = new NewState::rot0;
    RotatorBase* rot1 = new NewState::rot1;

    newov->initPipe(rot0, utils::OV_PIPE0);
    newov->initPipe(rot1, utils::OV_PIPE1);

    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

/* Transition from OV_2D_TRUE_UI_MIRROR to OV_UI_MIRROR */
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_2D_TRUE_UI_MIRROR,
        utils::OV_UI_MIRROR>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGD_IF(DEBUG_OVERLAY, "FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_2D_TRUE_UI_MIRROR),
            utils::getStateString(utils::OV_UI_MIRROR));

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_UI_MIRROR> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //copy pipe2/rot2 (UIMirroringPipe)
    newov->copyOvPipe(ov, utils::OV_PIPE2);
    //close old pipes 0/1
    ov->closePipe(utils::OV_PIPE0);
    ov->closePipe(utils::OV_PIPE1);

    //create new pipes 0/1
    RotatorBase* rot0 = new NewState::rot0;
    RotatorBase* rot1 = new NewState::rot1;

    newov->initPipe(rot0, utils::OV_PIPE0);
    newov->initPipe(rot1, utils::OV_PIPE1);

    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

/* Transition from OV_2D_VIDEO_ON_PANEL to OV_2D_TRUE_UI_MIRROR */
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_2D_VIDEO_ON_PANEL,
        utils::OV_2D_TRUE_UI_MIRROR>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("TRUE MIRROR FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL),
            utils::getStateString(utils::OV_2D_TRUE_UI_MIRROR));

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_TRUE_UI_MIRROR> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //copy pipe0/rot0 (PrimaryVideo pipe)
    newov->copyOvPipe(ov, utils::OV_PIPE0);
    //close old pipes 1/2
    ov->closePipe(utils::OV_PIPE1);
    ov->closePipe(utils::OV_PIPE2);

    //create new pipes 1/2
    RotatorBase* rot1 = new NewState::rot1;
    RotatorBase* rot2 = new NewState::rot2;

    newov->initPipe(rot1, utils::OV_PIPE1);
    newov->initPipe(rot2, utils::OV_PIPE2);

    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

/* Transition from OV_2D_TRUE_UI_MIRROR to OV_2D_VIDEO_ON_PANEL*/
template<>
inline OverlayImplBase* OverlayState::handle_from_to<
        utils::OV_2D_TRUE_UI_MIRROR,
        utils::OV_2D_VIDEO_ON_PANEL>(
        OverlayImplBase* ov) {
    OVASSERT(ov, "%s: ov is null", __FUNCTION__);
    ALOGE("FROM_STATE = %s TO_STATE = %s",
            utils::getStateString(utils::OV_2D_TRUE_UI_MIRROR),
            utils::getStateString(utils::OV_2D_VIDEO_ON_PANEL));

    // Create new ovimpl based on new state
    typedef StateTraits<utils::OV_2D_VIDEO_ON_PANEL> NewState;
    OverlayImplBase* newov = new NewState::ovimpl;

    //copy pipe0/rot0 (Primary Video pipe)
    newov->copyOvPipe(ov, utils::OV_PIPE0);
    //close old pipes 1/2
    ov->closePipe(utils::OV_PIPE1);
    ov->closePipe(utils::OV_PIPE2);

    //create new pipes 0/1
    RotatorBase* rot1 = new NewState::rot1;
    RotatorBase* rot2 = new NewState::rot2;

    newov->initPipe(rot1, utils::OV_PIPE1);
    newov->initPipe(rot2, utils::OV_PIPE2);

    //copy pipe3/rot3 (FB)
    newov->copyOvPipe(ov, utils::OV_PIPE3);

    // All pipes are copied or deleted so no more need for previous ovimpl
    delete ov;
    ov = 0;
    return newov;
}

} // overlay

#endif // OVERLAY_STATE_H
