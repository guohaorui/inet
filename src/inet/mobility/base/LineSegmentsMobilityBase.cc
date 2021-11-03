//
// Copyright (C) 2005 OpenSim Ltd.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

#include "inet/mobility/base/LineSegmentsMobilityBase.h"

#include "inet/common/INETMath.h"

namespace inet {

LineSegmentsMobilityBase::LineSegmentsMobilityBase()
{
    targetPosition = Coord::ZERO;
}

void LineSegmentsMobilityBase::initializeMobilityData()
{
    MobilityBase::initializeMobilityData();
    doSetTargetPosition();
    lastUpdate = simTime();
    scheduleUpdate();
}

void LineSegmentsMobilityBase::doSetTargetPosition()
{
    segmentStartPosition = lastPosition;
    segmentStartTime = simTime();
    setTargetPosition();
    EV_INFO << "new target position = " << targetPosition << ", next change = " << nextChange << endl;
    lastVelocity = segmentStartVelocity = stationary ? Coord::ZERO : (targetPosition - segmentStartPosition) / (nextChange - segmentStartTime).dbl();
    if (faceForward && (lastVelocity != Coord::ZERO))
        lastOrientation = segmentStartOrientation = getOrientOfVelocity(segmentStartVelocity);
}

void LineSegmentsMobilityBase::processBorderPolicy()
{
    Coord dummyCoord;
    rad dummyAngle;
    handleIfOutside(borderPolicy, dummyCoord, lastVelocity, dummyAngle, dummyAngle, lastOrientation);
}

void LineSegmentsMobilityBase::move()
{
    simtime_t now = simTime();
    lastVelocity = segmentStartVelocity;
    lastOrientation = segmentStartOrientation;
    if (now == nextChange) {
        lastPosition = targetPosition;
        processBorderPolicy();
        targetPosition = lastPosition;
        segmentStartVelocity = lastVelocity;
        segmentStartOrientation = lastOrientation;
        EV_INFO << "reached current target position = " << lastPosition << endl;
        doSetTargetPosition();
    }
    else if (now > lastUpdate) {
        ASSERT(nextChange == -1 || now < nextChange);
        lastPosition = segmentStartPosition + segmentStartVelocity * (now - segmentStartTime).dbl();
        processBorderPolicy();
    }
}

} // namespace inet

