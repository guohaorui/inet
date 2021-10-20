//
// Copyright (C) 2005 Emin Ilker Cetinbas, Andras Varga
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// Author: Emin Ilker Cetinbas (niw3_at_yahoo_d0t_com)
// Generalization: Andras Varga
//

#include "inet/mobility/single/MassMobility.h"

#include "inet/common/INETMath.h"

namespace inet {

Define_Module(MassMobility);

MassMobility::MassMobility()
{
    borderPolicy = REFLECT;
}

void MassMobility::initialize(int stage)
{
    LineSegmentsMobilityBase::initialize(stage);

    EV_TRACE << "initializing MassMobility stage " << stage << endl;
    if (stage == INITSTAGE_LOCAL) {
        rad heading = deg(par("initialMovementHeading"));
        rad elevation = deg(par("initialMovementElevation"));
        changeIntervalParameter = &par("changeInterval");
        angleDeltaParameter = &par("angleDelta");
        rotationAxisAngleParameter = &par("rotationAxisAngle");
        speedParameter = &par("speed");
        quaternion = Quaternion(EulerAngles(heading, -elevation, rad(0)));
        WATCH(quaternion);
    }
}

void MassMobility::orient()
{
    if (faceForward)
        lastOrientation = quaternion;
}

void MassMobility::setTargetPosition()
{
    rad angleDelta = deg(angleDeltaParameter->doubleValue());
    rad rotationAxisAngle = deg(rotationAxisAngleParameter->doubleValue());
    Quaternion dQ = Quaternion(Coord::X_AXIS, rotationAxisAngle.get()) * Quaternion(Coord::Z_AXIS, angleDelta.get());
    quaternion = quaternion * dQ;
    quaternion.normalize();
    Coord direction = quaternion.rotate(Coord::X_AXIS);

    simtime_t nextChangeInterval = *changeIntervalParameter;
    EV_DEBUG << "interval: " << nextChangeInterval << endl;
    targetPosition = lastPosition + direction * (*speedParameter) * nextChangeInterval.dbl();
    nextChange = segmentStartTime + nextChangeInterval;
}

void MassMobility::processBorderPolicy()
{
    Coord dummyCoord;
    rad dummyAngle;
    if (simTime() == nextChange) {
        handleIfOutside(borderPolicy, targetPosition, lastVelocity, dummyAngle, dummyAngle, quaternion);
        orient();
    }
    else {
        handleIfOutside(borderPolicy, dummyCoord, lastVelocity, dummyAngle, dummyAngle, lastOrientation);
    }
}

void MassMobility::move()
{
    simtime_t now = simTime();
    if (now == nextChange) {
        lastPosition = targetPosition;
        processBorderPolicy();
        targetPosition = lastPosition;
        EV_INFO << "reached current target position = " << lastPosition << endl;
        doSetTargetPosition();
    }
    else if (now > lastUpdate) {
        ASSERT(nextChange == -1 || now < nextChange);
        double alpha = (now - segmentStartTime) / (nextChange - segmentStartTime);
        lastPosition = segmentStartPosition * (1.0 - alpha) + targetPosition * alpha;
        processBorderPolicy();
    }
}

double MassMobility::getMaxSpeed() const
{
    return speedParameter->isExpression() ? NaN : speedParameter->doubleValue();
}

} // namespace inet

