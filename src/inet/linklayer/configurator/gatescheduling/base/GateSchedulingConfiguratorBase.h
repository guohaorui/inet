//
// Copyright (C) 2013 OpenSim Ltd.
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

#ifndef __INET_GATESCHEDULINGCONFIGURATORBASE_H
#define __INET_GATESCHEDULINGCONFIGURATORBASE_H

#include "inet/common/Units.h"
#include "inet/networklayer/configurator/base/NetworkConfiguratorBase.h"

namespace inet {

using namespace units::values;

class INET_API GateSchedulingConfiguratorBase : public NetworkConfiguratorBase
{
  protected:
    // input parameters for the gate scheduler
    class INET_API Input
    {
      public:
        class Port;

        // all network nodes
        class NetworkNode
        {
          public:
            std::vector<Port *> ports; // list of network interfaces
            cModule *module = nullptr; // corresponding OMNeT++ module

          public:
            virtual ~NetworkNode() {}
        };

        // an end device (network node) that runs applications
        class Device : public NetworkNode
        {
        };

        // an application that generates traffic
        class Application
        {
          public:
            cModule *module = nullptr; // corresponding OMNeT++ module
            int priority = -1; // traffic class (index of subqueue)
            b packetLength = b(-1); // packet size in bits
            simtime_t packetInterval = -1; // packet inter-arrival time in seconds
            simtime_t maxLatency = -1; // maximum allowed latency in seconds
            Device *device = nullptr; // the device where the application is running
        };

        // network interface
        class Port
        {
          public:
            cModule *module = nullptr; // corresponding OMNeT++ module
            int numPriorities = -1; // number of traffic classes (number of subqueues)
            bps datarate = bps(NaN); // transmission bitrate in bits per second
            simtime_t propagationTime = -1; // time to travel to the connected port in seconds
            b maxPacketLength = b(-1); // maximum packet size in bits
            simtime_t guardBand = -1; // guard band in seconds
            simtime_t maxCycleTime = -1; // maximum length of the cycle
            simtime_t maxSlotDuration = -1; // maximum slot duration in the cycle
            NetworkNode *startNode = nullptr; // the network node where this port is
            NetworkNode *endNode = nullptr; // the network node to which this port is connected
        };

        // an Ethernet switch
        class Switch : public NetworkNode
        {
        };

        // part of a flow's paths
        class PathFragment
        {
          public:
            std::vector<NetworkNode *> networkNodes; // list of network nodes the path goes through
        };

        // a flow of packets from the application of the start device to the end device
        class Flow
        {
          public:
            std::string name; // for user identification
            Application *startApplication = nullptr; // the application that generates the packets
            Device *endDevice = nullptr; // the device where the flow ends
            std::vector<PathFragment *> pathFragments; // list of path fragments (may use redundancy)
        };

      public:
        std::vector<Device *> devices; // list of end devices in the network
        std::vector<Application *> applications; // list of applications in the network
        std::vector<Port *> ports; // all ports of all switches in the network
        std::vector<Switch *> switches; // list of switches in the network
        std::vector<Flow *> flows; // list of traffic flows in the network
        std::vector<NetworkNode *> networkNodes; // list of all network nodes (devices and switches)

      public:
        ~Input() {
            for (auto device : devices) delete device;
            for (auto application : applications) delete application;
            for (auto port : ports) delete port;
            for (auto switch_ : switches) delete switch_;
            for (auto flow : flows) delete flow;
        }

        Device *findDevice(cModule *module) {
            auto it = std::find_if(devices.begin(), devices.end(), [&] (const Device *device) {
               return device->module == module;
            });
            return it != devices.end() ? *it : nullptr;
        }
        Device *getDevice(cModule *module) {
            auto device = findDevice(module);
            if (device == nullptr)
                throw cRuntimeError("Cannot find device");
            else
                return device;
        }

        Port *findPort(cModule *module) {
            auto it = std::find_if(ports.begin(), ports.end(), [&] (const Port *port) {
               return port->module == module;
            });
            return it != ports.end() ? *it : nullptr;
        }
        Port *getPort(cModule *module) {
            auto port = findPort(module);
            if (port == nullptr)
                throw cRuntimeError("Cannot find port");
            else
                return port;
        }

        NetworkNode *findNetworkNode(cModule *module) {
            auto it = std::find_if(networkNodes.begin(), networkNodes.end(), [&] (const NetworkNode *networkNode) {
               return networkNode->module == module;
            });
            return it != networkNodes.end() ? *it : nullptr;
        }
        NetworkNode *getNetworkNode(cModule *module) {
            auto networkNode = findNetworkNode(module);
            if (networkNode == nullptr)
                throw cRuntimeError("Cannot find network node");
            else
                return networkNode;
        }
    };

    // output parameters for the gate scheduler
    class INET_API Output
    {
      public:
        // a single slot in a schedule
        class Slot
        {
          public:
            simtime_t start; // start time in seconds
            simtime_t duration; // duration in seconds
        };

        // a gate scheduling for a specific priority (traffic class) of a specific port
        class Schedule
        {
          public:
            Input::Port *port = nullptr; // reference to the port
            int priority = -1; // index of the subqueue
            simtime_t cycleStart = -1; // start of the cycle in seconds
            simtime_t cycleDuration = -1; // duration of the cycle in seconds
            std::vector<Slot> slots; // list of slots ordered by start time
        };

      public:
        std::map<Input::Port *, std::vector<Schedule *>> gateSchedules; // maps ports to schedules per priority (traffic class)
        std::map<Input::Application *, simtime_t> applicationStartTimes; // maps applications to start times

      public:
        ~Output() {
            for (auto it : gateSchedules)
                for (auto element : it.second)
                    delete element;
        }
    };

  protected:
    // parameters
    simtime_t gateCycleDuration;
    cValueArray *configuration = nullptr;

    // state
    Input *gateSchedulingInput = nullptr;
    Output *gateSchedulingOutput = nullptr;

  protected:
    virtual void initialize(int stage) override;
    virtual void handleParameterChange(const char *name) override;
    virtual void handleMessage(cMessage *msg) override { throw cRuntimeError("this module doesn't handle messages, it runs only in initialize()"); }

    virtual void clearConfiguration();
    virtual void computeConfiguration();

    virtual Input *createGateSchedulingInput() const;
    virtual void addDevices(Input& input) const;
    virtual void addSwitches(Input& input) const;
    virtual void addPorts(Input& input) const;
    virtual void addFlows(Input& input) const;

    virtual void configureGateScheduling();
    virtual void configureGateScheduling(cModule *networkNode, cModule *gate, InterfaceInfo *interfaceInfo);
    virtual void configureApplicationOffsets();

    virtual Output *computeGateScheduling(const Input& input) const = 0;

  public:
    virtual ~GateSchedulingConfiguratorBase() { clearConfiguration(); }
};

} // namespace inet

#endif

