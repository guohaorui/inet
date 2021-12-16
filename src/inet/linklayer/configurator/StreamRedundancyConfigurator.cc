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

#include "inet/linklayer/configurator/StreamRedundancyConfigurator.h"

namespace inet {

Define_Module(StreamRedundancyConfigurator);

void StreamRedundancyConfigurator::initialize(int stage)
{
    NetworkConfiguratorBase::initialize(stage);
    if (stage == INITSTAGE_LOCAL) {
        minVlanId = par("minVlanId");
        maxVlanId = par("maxVlanId");
        configuration = check_and_cast<cValueArray *>(par("configuration").objectValue());
    }
    else if (stage == INITSTAGE_NETWORK_CONFIGURATION) {
        computeConfiguration();
        configureStreams();
    }
}

void StreamRedundancyConfigurator::handleParameterChange(const char *name)
{
    if (name != nullptr) {
        if (!strcmp(name, "configuration")) {
            configuration = check_and_cast<cValueArray *>(par("configuration").objectValue());
            clearConfiguration();
            computeConfiguration();
            configureStreams();
        }
    }
}

void StreamRedundancyConfigurator::clearConfiguration()
{
    streams.clear();
    nextVlanIds.clear();
    assignedVlanIds.clear();
    if (topology != nullptr)
        topology->clear();
}

void StreamRedundancyConfigurator::computeConfiguration()
{
    long initializeStartTime = clock();
    delete topology;
    topology = new Topology();
    TIME(extractTopology(*topology));
    TIME(computeStreams());
    printElapsedTime("initialize", initializeStartTime);
}

void StreamRedundancyConfigurator::computeStreams()
{
    for (int i = 0; i < configuration->size(); i++) {
        cValueMap *streamConfiguration = check_and_cast<cValueMap *>(configuration->get(i).objectValue());
        computeStreamSendersAndReceivers(streamConfiguration);
        computeStreamEncodings(streamConfiguration);
        computeStreamPolicyConfigurations(streamConfiguration);
    }
}

static std::string getNodeName(std::string name) {
    auto pos = name.find('.');
    return pos == std::string::npos ? name : name.substr(0, pos);
}

void StreamRedundancyConfigurator::computeStreamSendersAndReceivers(cValueMap *streamConfiguration)
{
    std::string streamName = streamConfiguration->get("name").stringValue();
    auto& stream = streams[streamName];
    for (int i = 0; i < topology->getNumNodes(); i++) {
        auto node = (Node *)topology->getNode(i);
        auto networkNode = node->module;
        auto networkNodeName = networkNode->getFullName();
        std::string sourceNetworkNodeName = streamConfiguration->get("source");
        cValueArray *trees = check_and_cast<cValueArray *>(streamConfiguration->get("trees").objectValue());
        stream.streamNodes[networkNodeName].senders.resize(trees->size());
        stream.streamNodes[networkNodeName].receivers.resize(trees->size());
        stream.streamNodes[networkNodeName].interfaces.resize(trees->size());
        std::vector<std::string> senderNetworkNodeNames;
        for (int j = 0; j < trees->size(); j++) {
            std::vector<std::string> receiverNetworkNodeNames;
            std::vector<NetworkInterface *> interfaces;
            cValueArray *tree = check_and_cast<cValueArray*>(trees->get(j).objectValue());
            for (int k = 0; k < tree->size(); k++) {
                cValueArray *path = check_and_cast<cValueArray*>(tree->get(k).objectValue());
                for (int l = 0; l < path->size(); l++) {
                    std::string name = path->get(l).stringValue();
                    auto pos = name.find('.');
                    auto nodeName = pos == std::string::npos ? name : name.substr(0, pos);
                    auto interfaceName = pos == std::string::npos ? "" : name.substr(pos + 1);
                    if (nodeName == networkNode->getFullName()) {
                        auto senderName = l != 0 ? path->get(l - 1).stringValue() : nullptr;
                        auto receiverName = l != path->size() - 1 ? path->get(l + 1).stringValue() : nullptr;
                        auto senderNetworkNodeName = senderName != nullptr ? getNodeName(senderName) : "";
                        auto receiverNetworkNodeName = receiverName != nullptr ? getNodeName(receiverName) : "";
                        if (senderName != nullptr && std::find(senderNetworkNodeNames.begin(), senderNetworkNodeNames.end(), senderNetworkNodeName) == senderNetworkNodeNames.end())
                            senderNetworkNodeNames.push_back(senderNetworkNodeName);
                        if (receiverName != nullptr && std::find(receiverNetworkNodeNames.begin(), receiverNetworkNodeNames.end(), receiverNetworkNodeName) == receiverNetworkNodeNames.end()) {
                            Interface *interface = nullptr;
                            if (interfaceName.empty())
                                interface = findLinkOut(node, receiverNetworkNodeName.c_str())->sourceInterface;
                            else
                                interface = *std::find_if(node->interfaces.begin(), node->interfaces.end(), [&] (auto interface) {
                                    return interface->networkInterface->getInterfaceName() == interfaceName;
                                });
                            receiverNetworkNodeNames.push_back(receiverNetworkNodeName);
                            interfaces.push_back(interface->networkInterface);
                        }
                    }
                }
            }
            stream.streamNodes[networkNodeName].receivers[j] = receiverNetworkNodeNames;
            stream.streamNodes[networkNodeName].interfaces[j] = interfaces;
        }
        stream.streamNodes[networkNodeName].senders = senderNetworkNodeNames;
    }
    for (auto& it : stream.streamNodes) {
        auto& streamNode = it.second;
        for (auto& r : streamNode.receivers) {
            if (!r.empty()) {
                if (std::find(streamNode.distinctReceivers.begin(), streamNode.distinctReceivers.end(), r) == streamNode.distinctReceivers.end())
                    streamNode.distinctReceivers.push_back(r);
            }
        }
    }
    EV_DEBUG << "Stream " << streamName << std::endl;
    for (auto& it : stream.streamNodes) {
        EV_DEBUG << "  Node " << it.first << std::endl;
        auto& streamNode = it.second;
        for (int i = 0; i < streamNode.senders.size(); i++) {
            auto& e = streamNode.senders[i];
            EV_DEBUG << "    Sender for tree " << i << " = " << e << std::endl;
        }
        for (int i = 0; i < streamNode.receivers.size(); i++) {
            auto& receiverNodes = streamNode.receivers[i];
            EV_DEBUG << "    Receivers for tree " << i << " = [";
            for (int j = 0; j < receiverNodes.size(); j++) {
                auto& e = receiverNodes[j];
                if (j != 0)
                    EV_DEBUG << ", ";
                EV_DEBUG << e;
            }
            EV_DEBUG << "]" << std::endl;
        }
    }
}

void StreamRedundancyConfigurator::computeStreamEncodings(cValueMap *streamConfiguration)
{
    for (int i = 0; i < topology->getNumNodes(); i++) {
        auto node = (Node *)topology->getNode(i);
        auto networkNode = node->module;
        auto networkNodeName = networkNode->getFullName();
        std::string destinationAddress = streamConfiguration->containsKey("destinationAddress") ? streamConfiguration->get("destinationAddress") : streamConfiguration->get("destination");
        std::string streamName = streamConfiguration->get("name").stringValue();
        // encoding configuration
        auto& stream = streams[streamName];
        auto& streamNode = stream.streamNodes[networkNodeName];
        for (int j = 0; j < streamNode.receivers.size(); j++) {
            auto& receiverNetworkNodeNames = streamNode.receivers[j];
            if (!receiverNetworkNodeNames.empty()) {
                std::string streamNameSuffix;
                for (auto receiverNetworkNodeName : receiverNetworkNodeNames)
                    streamNameSuffix += "_" + receiverNetworkNodeName;
                auto outputStreamName = streamNode.distinctReceivers.size() == 1 ? streamName : streamName + streamNameSuffix;
                auto it = std::find_if(node->streamEncodings.begin(), node->streamEncodings.end(), [&] (const StreamEncoding& streamEncoding) {
                    return streamEncoding.name == outputStreamName;
                });
                if (it != node->streamEncodings.end())
                    continue;
                auto jt = nextVlanIds.emplace(std::pair<std::string, std::string>{networkNodeName, destinationAddress}, 0);
                int vlanId = jt.first->second++;
                if (vlanId > maxVlanId)
                    throw cRuntimeError("Cannot assign VLAN ID in the available range");
                for (int k = 0; k < receiverNetworkNodeNames.size(); k++) {
                    auto receiverNetworkNodeName = receiverNetworkNodeNames[k];
                    EV_DEBUG << "Assigning VLAN id" << EV_FIELD(streamName) << EV_FIELD(networkNodeName) << EV_FIELD(receiverNetworkNodeName) << EV_FIELD(destinationAddress) << EV_FIELD(vlanId) << EV_ENDL;
                    assignedVlanIds[{networkNodeName, receiverNetworkNodeName, destinationAddress, streamName}] = vlanId;
                    StreamEncoding streamEncoding;
                    streamEncoding.name = outputStreamName;
                    streamEncoding.networkInterface = streamNode.interfaces[j][k];
                    streamEncoding.vlanId = vlanId;
                    streamEncoding.destination = destinationAddress;
                    node->streamEncodings.push_back(streamEncoding);
                }
            }
        }
    }
}

void StreamRedundancyConfigurator::computeStreamPolicyConfigurations(cValueMap *streamConfiguration)
{
    std::string sourceNetworkNodeName = streamConfiguration->get("source");
    std::string destinationAddress = streamConfiguration->containsKey("destinationAddress") ? streamConfiguration->get("destinationAddress") : streamConfiguration->get("destination");
    std::string streamName = streamConfiguration->get("name").stringValue();
    auto& stream = streams[streamName];
    for (int i = 0; i < topology->getNumNodes(); i++) {
        auto node = (Node *)topology->getNode(i);
        auto networkNodeName = node->module->getFullName();
        auto& streamNode = stream.streamNodes[networkNodeName];
        // identification configuration
        if (networkNodeName == sourceNetworkNodeName) {
            StreamIdentification streamIdentification;
            streamIdentification.stream = streamConfiguration->get("name").stringValue();
            streamIdentification.packetFilter = streamConfiguration->get("packetFilter").stringValue();
            node->streamIdentifications.push_back(streamIdentification);
        }
        // decoding configuration
        for (auto senderNetworkNodeName : streamNode.senders) {
            auto inputStreamName = streamNode.senders.size() == 1 ? streamName : streamName + "_" + senderNetworkNodeName;
            auto linkIn = findLinkIn(node, senderNetworkNodeName.c_str());
            auto vlanId = assignedVlanIds[{senderNetworkNodeName, networkNodeName, destinationAddress, streamName}];
            StreamDecoding streamDecoding;
            streamDecoding.name = inputStreamName;
            streamDecoding.networkInterface = linkIn->destinationInterface->networkInterface;
            streamDecoding.vlanId = vlanId;
            if (streamDecoding.vlanId > maxVlanId)
                throw cRuntimeError("Cannot assign VLAN ID in the available range");
            node->streamDecodings.push_back(streamDecoding);
        }
        // merging configuration
        if (streamNode.senders.size() > 1) {
            StreamMerging streamMerging;
            streamMerging.outputStream = streamName;
            for (auto senderNetworkNodeName : streamNode.senders) {
                auto inputStreamName = streamName + "_" + senderNetworkNodeName;
                streamMerging.inputStreams.push_back(inputStreamName);
            }
            node->streamMergings.push_back(streamMerging);
        }
        // splitting configuration
        if (streamNode.distinctReceivers.size() != 1) {
            StreamSplitting streamSplitting;
            streamSplitting.inputStream = streamName;
            for (auto& receiverNetworkNodeNames : streamNode.distinctReceivers) {
                std::string streamNameSuffix;
                for (auto receiverNetworkNodeName : receiverNetworkNodeNames)
                    streamNameSuffix += "_" + receiverNetworkNodeName;
                auto outputStreamName = streamName + streamNameSuffix;
                streamSplitting.outputStreams.push_back(outputStreamName);
            }
            node->streamSplittings.push_back(streamSplitting);
        }
    }
}

void StreamRedundancyConfigurator::configureStreams()
{
    for (int i = 0; i < topology->getNumNodes(); i++) {
        auto node = (Node *)topology->getNode(i);
        configureStreams(node);
    }
}

void StreamRedundancyConfigurator::configureStreams(Node *node)
{
    auto networkNode = node->module;
    auto macAddressTable = networkNode->findModuleByPath(".macTable");
    auto ieee8021qTagHeaderChecker = networkNode->findModuleByPath(".ieee8021q.qTagHeaderChecker");
    auto streamRelay = networkNode->findModuleByPath(".bridging.streamRelay");
    if (streamRelay == nullptr)
        streamRelay = networkNode->findModuleByPath(".ieee8021r.policy.streamRelay");
    auto streamIdentifierLayer = networkNode->findModuleByPath(".bridging.streamIdentifier");
    if (streamIdentifierLayer == nullptr)
        streamIdentifierLayer = networkNode->findModuleByPath(".ieee8021r.policy.streamIdentifier");
    auto streamIdentifier = streamIdentifierLayer->getSubmodule("identifier");
    auto streamMerger = streamRelay->getSubmodule("merger");
    auto streamSplitter = streamRelay->getSubmodule("splitter");
    auto streamCoder = networkNode->findModuleByPath(".bridging.streamCoder");
    if (streamCoder == nullptr)
        streamCoder = networkNode->findModuleByPath(".ieee8021r.policy.streamCoder");
    auto streamDecoder = streamCoder->getSubmodule("decoder");
    auto streamEncoder = streamCoder->getSubmodule("encoder");
    if (streamIdentifier != nullptr && !node->streamIdentifications.empty()) {
        cValueArray *parameterValue = new cValueArray();
        for (auto& streamIdentification : node->streamIdentifications) {
            cValueMap *value = new cValueMap();
            value->set("packetFilter", streamIdentification.packetFilter);
            value->set("stream", streamIdentification.stream);
            parameterValue->add(value);
        }
        EV_INFO << "Configuring stream merging" << EV_FIELD(networkNode) << EV_FIELD(streamIdentifier) << EV_FIELD(parameterValue) << EV_ENDL;
        streamIdentifier->par("mapping") = parameterValue;
    }
    if (streamDecoder != nullptr && !node->streamDecodings.empty()) {
        cValueArray *parameterValue = new cValueArray();
        for (auto& streamDecoding : node->streamDecodings) {
            cValueMap *value = new cValueMap();
            value->set("interface", streamDecoding.networkInterface->getInterfaceName());
            value->set("vlan", streamDecoding.vlanId);
            value->set("stream", streamDecoding.name.c_str());
            parameterValue->add(value);
        }
        EV_INFO << "Configuring stream decoding" << EV_FIELD(networkNode) << EV_FIELD(streamDecoder) << EV_FIELD(parameterValue) << EV_ENDL;
        streamDecoder->par("mapping") = parameterValue;
    }
    if (streamMerger != nullptr && !node->streamMergings.empty()) {
        cValueMap *parameterValue = new cValueMap();
        for (auto& streamMerging : node->streamMergings) {
            for (auto inputStream : streamMerging.inputStreams)
                parameterValue->set(inputStream.c_str(), streamMerging.outputStream.c_str());
        }
        EV_INFO << "Configuring stream merging" << EV_FIELD(networkNode) << EV_FIELD(streamMerger) << EV_FIELD(parameterValue) << EV_ENDL;
        streamMerger->par("mapping") = parameterValue;
    }
    if (streamSplitter != nullptr && !node->streamSplittings.empty()) {
        cValueMap *parameterValue = new cValueMap();
        for (auto& streamSplitting : node->streamSplittings) {
            cValueArray *value = new cValueArray();
            for (auto outputStream : streamSplitting.outputStreams)
                value->add(outputStream.c_str());
            parameterValue->set(streamSplitting.inputStream.c_str(), value);
        }
        EV_INFO << "Configuring stream splitting" << EV_FIELD(networkNode) << EV_FIELD(streamSplitter) << EV_FIELD(parameterValue) << EV_ENDL;
        streamSplitter->par("mapping") = parameterValue;
    }
    if (streamEncoder != nullptr && !node->streamEncodings.empty()) {
        cValueMap *parameterValue = new cValueMap();
        for (auto& streamEncoding : node->streamEncodings)
            parameterValue->set(streamEncoding.name.c_str(), streamEncoding.vlanId);
        EV_INFO << "Configuring stream encoding" << EV_FIELD(networkNode) << EV_FIELD(streamEncoder) << EV_FIELD(parameterValue) << EV_ENDL;
        streamEncoder->par("mapping") = parameterValue;
    }
    if (macAddressTable != nullptr && !node->streamEncodings.empty()) {
        cValueArray *parameterValue = new cValueArray();
        for (auto& streamEncoding : node->streamEncodings) {
            cValueMap *value = new cValueMap();
            value->set("address", streamEncoding.destination.c_str());
            value->set("vlan", streamEncoding.vlanId);
            value->set("interface", streamEncoding.networkInterface->getInterfaceName());
            parameterValue->add(value);
        }
        EV_INFO << "Configuring MAC address table" << EV_FIELD(networkNode) << EV_FIELD(macAddressTable) << EV_FIELD(parameterValue) << EV_ENDL;
        macAddressTable->par("addressTable") = parameterValue;
    }
    if (ieee8021qTagHeaderChecker != nullptr) {
        std::set<int> vlanIds;
        for (auto& streamDecoding : node->streamDecodings)
            vlanIds.insert(streamDecoding.vlanId);
        cValueArray *parameterValue = new cValueArray();
        for (int vlanId : vlanIds)
            parameterValue->add(vlanId);
        EV_INFO << "Configuring VLAN filter" << EV_FIELD(networkNode) << EV_FIELD(ieee8021qTagHeaderChecker) << EV_FIELD(parameterValue) << EV_ENDL;
        ieee8021qTagHeaderChecker->par("vlanIdFilter") = parameterValue;
    }
}

std::vector<std::string> StreamRedundancyConfigurator::getStreamNames()
{
    std::vector<std::string> result;
    for (auto& it : streams)
        result.push_back(it.first);
    return result;
}

std::vector<std::vector<std::string>> StreamRedundancyConfigurator::getPathFragments(const char *stream)
{
    for (int i = 0; i < configuration->size(); i++) {
        cValueMap *streamConfiguration = check_and_cast<cValueMap *>(configuration->get(i).objectValue());
        if (!strcmp(streamConfiguration->get("name").stringValue(), stream)) {
            std::vector<std::vector<std::string>> memberStreams;
            std::string streamName = streamConfiguration->get("name").stringValue();
            cValueArray *trees = check_and_cast<cValueArray *>(streamConfiguration->get("trees").objectValue());
            for (int j = 0; j < trees->size(); j++) {
                cValueArray *tree = check_and_cast<cValueArray*>(trees->get(j).objectValue());
                for (int k = 0; k < tree->size(); k++) {
                    cValueArray *path = check_and_cast<cValueArray*>(tree->get(k).objectValue());
                    std::vector<std::string> memberStream;
                    for (int l = 0; l < path->size(); l++) {
                        std::string name = path->get(l).stringValue();
                        auto pos = name.find('.');
                        auto nodeName = pos == std::string::npos ? name : name.substr(0, pos);
                        auto module = findModuleByPath(nodeName.c_str());
                        Node *node = (Node *)topology->getNodeFor(module);
                        bool isMerging = false;
                        for (auto streamMerging : node->streamMergings)
                            if (streamMerging.outputStream == streamName)
                                isMerging = true;
                        bool isSplitting = false;
                        for (auto streamSplitting : node->streamSplittings)
                            if (streamSplitting.inputStream == streamName)
                                isSplitting = true;
                        memberStream.push_back(name);
                        if (isMerging || isSplitting) {
                            if (memberStream.size() > 1 && std::find(memberStreams.begin(), memberStreams.end(), memberStream) == memberStreams.end())
                                memberStreams.push_back(memberStream);
                            memberStream.clear();
                            memberStream.push_back(name);
                        }
                    }
                    if (memberStream.size() > 1 && std::find(memberStreams.begin(), memberStreams.end(), memberStream) == memberStreams.end())
                        memberStreams.push_back(memberStream);
                }
            }
            return memberStreams;
        }
    }
    throw cRuntimeError("Stream not found");
}

} // namespace inet

