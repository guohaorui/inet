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

#ifndef __INET_SIMPLEGATESCHEDULINGCONFIGURATOR_H
#define __INET_SIMPLEGATESCHEDULINGCONFIGURATOR_H

#include <algorithm>
#include <vector>

#include "inet/common/PatternMatcher.h"
#include "inet/common/Topology.h"
#include "inet/linklayer/configurator/Ieee8021dInterfaceData.h"
#include "inet/networklayer/contract/IInterfaceTable.h"

namespace inet {

// TODO: derive from GateSchedulingConfiguratorBase
class INET_API SimpleGateSchedulingConfigurator : public cSimpleModule
{
  protected:
    class InterfaceInfo;

    /**
     * Represents a node in the network.
     */
    class Node : public Topology::Node {
      public:
        cModule *module;
        IInterfaceTable *interfaceTable;
        std::vector<InterfaceInfo *> interfaceInfos;

      public:
        Node(cModule *module) : Topology::Node(module->getId()) { this->module = module; interfaceTable = nullptr; }
        ~Node() { for (size_t i = 0; i < interfaceInfos.size(); i++) delete interfaceInfos[i]; }
    };

    /**
     * Represents an interface in the network.
     */
    class InterfaceInfo : public cObject {
      public:
        NetworkInterface *networkInterface;
        std::vector<int> gateOpenIndices;
        std::vector<simtime_t> gateOpenTimes;
        std::vector<simtime_t> gateCloseTimes;

      public:
        InterfaceInfo(NetworkInterface *networkInterface) : networkInterface(networkInterface) {}
        virtual std::string getFullPath() const override { return networkInterface->getInterfaceFullPath(); }
    };

    class Link : public Topology::Link {
      public:
        InterfaceInfo *sourceInterfaceInfo;
        InterfaceInfo *destinationInterfaceInfo;

      public:
        Link() { sourceInterfaceInfo = nullptr; destinationInterfaceInfo = nullptr; }
    };

    class Topology : public inet::Topology {
      protected:
        virtual Node *createNode(cModule *module) override { return new SimpleGateSchedulingConfigurator::Node(module); }
        virtual Link *createLink() override { return new SimpleGateSchedulingConfigurator::Link(); }
    };

    class StreamReservation
    {
      public:
        Node *source = nullptr;
        Node *destination = nullptr;
        int priority = -1;
        b packetLength = b(-1);
        simtime_t packetInterval = -1;
        simtime_t maxLatency = -1;
        bps datarate = bps(NaN);
        simtime_t startOffset = -1;
        std::vector<std::vector<std::string>> pathFragments;
    };

  protected:
    cValueArray *configuration;
    simtime_t gateCycleDuration;

    Topology topology;
    std::vector<StreamReservation> streamReservations;

  protected:
    virtual int numInitStages() const override { return NUM_INIT_STAGES; }
    virtual void initialize(int stage) override;
    virtual void handleParameterChange(const char *name) override;
    virtual void handleMessage(cMessage *msg) override { throw cRuntimeError("this module doesn't handle messages, it runs only in initialize()"); }

    /**
     * Extracts network topology by walking through the module hierarchy.
     * Creates vertices from modules having @networkNode property.
     * Creates edges from connections (wired and wireless) between network interfaces.
     */
    virtual void extractTopology(Topology& topology);

    virtual void clearConfiguration();
    /**
     * Computes the network configuration for all nodes in the network.
     * The result of the computation is only stored in the configurator.
     */
    virtual void computeConfiguration();
    virtual void computeStreamReservations();
    virtual void computeGateScheduling();

    virtual void computeStreamStartOffset(StreamReservation& streamReservation);
    virtual simtime_t computeStartOffsetForPathFragments(StreamReservation& streamReservation, std::string startNetworkNodeName, simtime_t startTime);

    virtual void addGateScheduling(StreamReservation& streamReservation, int startIndex, int endIndex);
    virtual void addGateSchedulingForPathFragments(StreamReservation& streamReservation, std::string startNetworkNodeName, simtime_t startTime, int index);

    virtual void configureGateScheduling();
    virtual void configureGateScheduling(cModule *networkNode, cModule *gate, InterfaceInfo *interfaceInfo);
    virtual void configureApplicationOffsets();

    virtual std::vector<std::string> computePath(Node *source, Node *destination);

    virtual Link *findLinkOut(Node *node, const char *neighbor);
    virtual Topology::LinkOut *findLinkOut(Node *node, int gateId);
    virtual InterfaceInfo *findInterfaceInfo(Node *node, NetworkInterface *networkInterface);
};

} // namespace inet

#endif

