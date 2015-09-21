//
//  RotationAccumulator.h
//
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "RotationAccumulator.h"

#include <glm/gtx/quaternion.hpp>

void RotationAccumulator::add(glm::quat rotation) {
    // make sure both quaternions are on the same hyper-hemisphere before we add them linearly (lerp)
    _rotationSum += copysignf(1.0f, glm::dot(_rotationSum, rotation)) * rotation;
    ++_numRotations;
}

glm::quat RotationAccumulator::getAverage() {
    return (_numRotations > 0) ?  glm::normalize(_rotationSum) : glm::quat();
}

void RotationAccumulator::clear() { 
    _rotationSum *= 0.0f;
    _numRotations = 0;
}
