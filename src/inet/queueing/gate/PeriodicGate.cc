//
// Copyright (C) 2020 OpenSim Ltd.
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

#include "inet/queueing/gate/PeriodicGate.h"

#include "inet/common/ModuleAccess.h"

namespace inet {
namespace queueing {

Define_Module(PeriodicGate);

void PeriodicGate::initialize(int stage)
{
    ClockUserModuleMixin::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        isOpen_ = par("initiallyOpen");
        offset = par("offset");
        durations = check_and_cast<cValueArray *>(par("durations").objectValue());
        scheduleForAbsoluteTime = par("scheduleForAbsoluteTime");
        changeTimer = new ClockEvent("ChangeTimer");
    }
    else if (stage == INITSTAGE_QUEUEING)
        initializeGating();
}

void PeriodicGate::handleParameterChange(const char *name)
{
    if (name != nullptr) {
        if (!strcmp(name, "offset"))
            offset = par("offset");
        else if (!strcmp(name, "initiallyOpen"))
            isOpen_ = par("initiallyOpen");
        else if (!strcmp(name, "durations")) {
            durations = check_and_cast<cValueArray *>(par("durations").objectValue());
            initializeGating();
        }
    }
}

void PeriodicGate::handleMessage(cMessage *message)
{
    if (message == changeTimer) {
        scheduleChangeTimer();
        processChangeTimer();
    }
    else
        throw cRuntimeError("Unknown message");
}

void PeriodicGate::initializeGating()
{
    if (durations->size() % 2 != 0)
        throw cRuntimeError("The duration parameter must contain an even number of values");
    index = 0;
    while (offset > 0) {
        clocktime_t duration = durations->get(index).doubleValueInUnit("s");
        if (offset > duration) {
            isOpen_ = !isOpen_;
            offset -= duration;
            index = (index + 1) % durations->size();
        }
        else
            break;
    }
    if (index < (int)durations->size()) {
        if (changeTimer->isScheduled())
            cancelClockEvent(changeTimer);
        scheduleChangeTimer();
    }
}

void PeriodicGate::scheduleChangeTimer()
{
    ASSERT(0 <= index && index < (int)durations->size());
    clocktime_t duration = durations->get(index).doubleValueInUnit("s");
    if (scheduleForAbsoluteTime)
        scheduleClockEventAt(getClockTime() + duration - offset, changeTimer);
    else
        scheduleClockEventAfter(duration - offset, changeTimer);
    index = (index + 1) % durations->size();
    offset = 0;
}

void PeriodicGate::processChangeTimer()
{
    if (isOpen_)
        close();
    else
        open();
}

bool PeriodicGate::canPacketFlowThrough(Packet *packet) const
{
    if (std::isnan(bitrate.get()))
        return PacketGateBase::canPacketFlowThrough(packet);
    else if (packet == nullptr)
        return false;
    else {
        clocktime_t flowEndTime = getClockTime() + s((packet->getTotalLength() + extraLength) / bitrate).get() + SIMTIME_AS_CLOCKTIME(extraDuration);
        return !changeTimer->isScheduled() || flowEndTime <= getArrivalClockTime(changeTimer);
    }
}

} // namespace queueing
} // namespace inet

