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

#include "inet/linklayer/configurator/MacAddressTableConfigurator.h"

#include "inet/common/MatchableObject.h"
#include "inet/common/ModuleAccess.h"
#include "inet/linklayer/configurator/StreamRedundancyConfigurator.h"
#include "inet/networklayer/arp/ipv4/GlobalArp.h"
#include "inet/networklayer/ipv4/Ipv4InterfaceData.h"

namespace inet {

Define_Module(MacAddressTableConfigurator);

MacAddressTableConfigurator::~MacAddressTableConfigurator()
{
    for (auto it : configurations)
        delete it.second;
}

void MacAddressTableConfigurator::initialize(int stage)
{
    NetworkConfiguratorBase::initialize(stage);
    if (stage == INITSTAGE_NETWORK_CONFIGURATION) {
        computeConfiguration();
        configureMacAddressTables();
        getParentModule()->subscribe(ipv4MulticastChangeSignal, this);
    }
}

void MacAddressTableConfigurator::computeConfiguration()
{
    long initializeStartTime = clock();
    delete topology;
    topology = new Topology();
    TIME(extractTopology(*topology))
    TIME(computeMacAddressTables());
    printElapsedTime("initialize", initializeStartTime);
}

void MacAddressTableConfigurator::computeMacAddressTables()
{
    // for each network node as destination
    //   for each network interface of destination
    //   find shortest path from all sources to destination passing through the network interface
    //     for each switch as source
    //       store first outgoing interface of the switch in the MAC forwarding table of the switch
    //       towards the MAC address of the network interface of the destination
    for (int i = 0; i < topology->getNumNodes(); i++) {
        Node *destinationNode = (Node *)topology->getNode(i);
        if (!isBridgeNode(destinationNode)) {
            for (auto destinationInterface : destinationNode->interfaces) {
                if (!destinationInterface->networkInterface->isLoopback() &&
                    !destinationInterface->networkInterface->getMacAddress().isUnspecified())
                {
                    extendConfiguration(destinationNode, destinationInterface, destinationInterface->networkInterface->getMacAddress());
                }
            }
        }
    }
}

void MacAddressTableConfigurator::extendConfiguration(Node *destinationNode, Interface *destinationInterface, MacAddress macAddress)
{
    for (int j = 0; j < destinationNode->getNumInLinks(); j++) {
        auto link = (Link *)destinationNode->getLinkIn(j);
        link->setWeight(link->destinationInterface != destinationInterface ? std::numeric_limits<double>::infinity() : 1);
    }
    topology->calculateWeightedSingleShortestPathsTo(destinationNode);
    for (int j = 0; j < topology->getNumNodes(); j++) {
        Node *sourceNode = (Node *)topology->getNode(j);
        if (sourceNode != destinationNode && isBridgeNode(sourceNode) && sourceNode->getNumPaths() != 0) {
            auto firstLink = (Link *)sourceNode->getPath(0);
            auto firstInterface = static_cast<Interface *>(firstLink->sourceInterface);
            auto moduleId = sourceNode->module->getSubmodule("macTable")->getId();
            auto it = configurations.find(moduleId);
            if (it == configurations.end())
                configurations[moduleId] = new cValueArray();
            auto rule = new cValueMap();
            rule->set("address", macAddress.str());
            rule->set("interface", firstInterface->networkInterface->getInterfaceName());
            configurations[moduleId]->add(rule);
        }
    }
}

void MacAddressTableConfigurator::configureMacAddressTables() const
{
    for (auto it : configurations) {
        auto macAddressTable = getSimulation()->getModule(it.first);
        macAddressTable->par("addressTable") = it.second->dup();
    }
}

void MacAddressTableConfigurator::receiveSignal(cComponent *source, simsignal_t signal, cObject *object, cObject *details)
{
    if (signal == ipv4MulticastChangeSignal) {
        auto sourceInfo = check_and_cast<Ipv4MulticastGroupSourceInfo *>(object);
        auto node = static_cast<Node *>(topology->getNodeFor(getContainingNode(sourceInfo->ie)));
        auto interface = findInterface(node, sourceInfo->ie);
        extendConfiguration(node, interface, GlobalArp::toMulticastMacAddress(sourceInfo->groupAddress));
        configureMacAddressTables();
    }
}

} // namespace inet

