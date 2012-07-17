/*
 * Copyright (c) 2012, Code Aurora Forum. All rights reserved.

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

#ifndef INCLUDE_LIBQCOM_COMPTYPES
#define INCLUDE_LIBQCOM_COMPTYPES

#include <utils/Singleton.h>
#include <cutils/properties.h>

/* This class caches the composition type
 */
class QCCompositionType :public Singleton<QCCompositionType> {
    public:
        QCCompositionType();
        ~QCCompositionType() { }
        int getCompositionType() {return mCompositionType;}
    private:
        int mCompositionType;

};

ANDROID_SINGLETON_STATIC_INSTANCE(QCCompositionType);
inline QCCompositionType::QCCompositionType()
{
    char property[PROPERTY_VALUE_MAX], mdp_property[PROPERTY_VALUE_MAX];
    mCompositionType = 0;
    if (property_get("debug.sf.hw", property, NULL) > 0) {
        if(atoi(property) == 0) {
            mCompositionType = COMPOSITION_TYPE_CPU;
        } else { //debug.sf.hw = 1
            property_get("debug.composition.type", property, NULL);
            if (property == NULL) {
                mCompositionType = COMPOSITION_TYPE_GPU;
            } else if ((strncmp(property, "mdp", 3)) == 0) {
                mCompositionType = COMPOSITION_TYPE_MDP;
            } else if ((strncmp(property, "c2d", 3)) == 0) {
                mCompositionType = COMPOSITION_TYPE_C2D;
            } else if ((strncmp(property, "dyn", 3)) == 0) {
                if ( (property_get("debug.mdpversion", mdp_property, NULL) > 0)
                        and (strncmp(mdp_property,"mdp3",4) == 0)) {
                        mCompositionType = COMPOSITION_TYPE_DYN | COMPOSITION_TYPE_MDP;
                } else {
                    mCompositionType = COMPOSITION_TYPE_DYN | COMPOSITION_TYPE_C2D;
                }
            } else {
                mCompositionType = COMPOSITION_TYPE_GPU;
            }
        }
    } else { //debug.sf.hw is not set. Use cpu composition
        mCompositionType = COMPOSITION_TYPE_CPU;
    }

}
#endif //INCLUDE_LIBQCOM_COMPTYPES
