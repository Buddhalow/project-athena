//
//  AnimUtil.h
//
//  Created by Anthony J. Thibault on 9/2/15.
//  Copyright (c) 2015 High Fidelity, Inc. All rights reserved.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_AnimUtil_h
#define hifi_AnimUtil_h

#include "AnimNode.h"

// this is where the magic happens
void blend(size_t numPoses, const AnimPose* a, const AnimPose* b, float alpha, AnimPose* result);

glm::quat averageQuats(size_t numQuats, const glm::quat* quats);

float accumulateTime(float startFrame, float endFrame, float timeScale, float currentFrame, float dt, bool loopFlag,
                     const QString& id, AnimVariantMap& triggersOut);

inline glm::quat safeLerp(const glm::quat& a, const glm::quat& b, float alpha) {
    // adjust signs if necessary
    glm::quat bTemp = b;
    float dot = glm::dot(a, bTemp);
    if (dot < 0.0f) {
        bTemp = -bTemp;
    }
    return glm::normalize(glm::lerp(a, bTemp, alpha));
}

AnimPose boneLookAt(const glm::vec3& target, const AnimPose& bone);

// This will attempt to determine the proper body facing of a characters body
// assumes headRot is z-forward and y-up.
// and returns a bodyRot that is also z-forward and y-up
glm::quat computeBodyFacingFromHead(const glm::quat& headRot, const glm::vec3& up);


// Uses a approximation of a critically damped spring to smooth full AnimPoses.
// It provides seperate timescales for horizontal, vertical and rotation components.
// The timescale is roughly how much time it will take the spring will reach halfway toward it's target.
class CriticallyDampedSpringPoseHelper {
public:
    CriticallyDampedSpringPoseHelper() : _prevPoseValid(false) {}

    void setHorizontalTranslationTimescale(float timescale) {
        _horizontalTranslationTimescale = timescale;
    }
    void setVerticalTranslationTimescale(float timescale) {
        _verticalTranslationTimescale = timescale;
    }
    void setRotationTimescale(float timescale) {
        _rotationTimescale = timescale;
    }

    AnimPose update(const AnimPose& pose, float deltaTime) {
        if (!_prevPoseValid) {
            _prevPose = pose;
            _prevPoseValid = true;
        }

        const float horizontalTranslationAlpha = glm::min(deltaTime / _horizontalTranslationTimescale, 1.0f);
        const float verticalTranslationAlpha = glm::min(deltaTime / _verticalTranslationTimescale, 1.0f);
        const float rotationAlpha = glm::min(deltaTime / _rotationTimescale, 1.0f);

        const float poseY = pose.trans().y;
        AnimPose newPose = _prevPose;
        newPose.trans() = lerp(_prevPose.trans(), pose.trans(), horizontalTranslationAlpha);
        newPose.trans().y = lerp(_prevPose.trans().y, poseY, verticalTranslationAlpha);
        newPose.rot() = safeLerp(_prevPose.rot(), pose.rot(), rotationAlpha);

        _prevPose = newPose;
        _prevPoseValid = true;

        return newPose;
    }

    void teleport(const AnimPose& pose) {
        _prevPoseValid = true;
        _prevPose = pose;
    }

protected:
    AnimPose _prevPose;
    float _horizontalTranslationTimescale { 0.15f };
    float _verticalTranslationTimescale { 0.15f };
    float _rotationTimescale { 0.15f };
    bool _prevPoseValid;
};

#endif
