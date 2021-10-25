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

void LineSegmentsMobilityBase::initializePosition()
{
    MobilityBase::initializePosition();
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
}

void LineSegmentsMobilityBase::processBorderPolicy()
{
    Coord dummyCoord;
    handleIfOutside(borderPolicy, dummyCoord, lastVelocity);
}

void LineSegmentsMobilityBase::move()
{
    simtime_t now = simTime();
    if (now == nextChange) {
        lastPosition = targetPosition;
        lastVelocity = segmentStartVelocity;
        processBorderPolicy();
        targetPosition = lastPosition;
        EV_INFO << "reached current target position = " << lastPosition << endl;
        doSetTargetPosition();
    }
    else if (now > lastUpdate) {
        ASSERT(nextChange == -1 || now < nextChange);
        lastPosition = segmentStartPosition + segmentStartVelocity * (now - segmentStartTime).dbl();
        lastVelocity = segmentStartVelocity;
        processBorderPolicy();
    }
}

} // namespace inet

