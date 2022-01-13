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

#include "inet/common/packet/PacketFilter.h"
#include "inet/linklayer/common/MacAddress.h"
#include "inet/networklayer/common/L3Address.h"
#include "inet/networklayer/contract/ipv4/Ipv4Address.h"
#include "inet/networklayer/contract/ipv6/Ipv6Address.h"

namespace inet {

PacketFilter::PacketFilter()
{
    packetDissectorCallback = new PacketDissectorCallback(this);
}

PacketFilter::~PacketFilter()
{
    delete filterExpression;
    delete matchExpression;
    delete packetDissectorCallback;
}


void PacketFilter::setPattern(const char *pattern)
{
    delete matchExpression;
    matchExpression = new cMatchExpression();
    matchExpression->setPattern(pattern, false, true, true);
}

void PacketFilter::setExpression(const char *expression)
{
    delete filterExpression;
    filterExpression = new cDynamicExpression();
    filterExpression->parse(expression, new DynamicExpressionResolver(this));
}

void PacketFilter::setExpression(cDynamicExpression *expression)
{
    filterExpression = expression;
    filterExpression->setResolver(new DynamicExpressionResolver(this));
}

void PacketFilter::setExpression(cOwnedDynamicExpression *expression)
{
    filterExpression = expression->dup();
    filterExpression->setResolver(new DynamicExpressionResolver(this));
}

void PacketFilter::setExpression(cValueHolder *expression)
{
    auto& value = expression->get();
    if (value.getType() == cValue::POINTER)
        setExpression(check_and_cast<cDynamicExpression *>(value.objectValue())->dup());
    else
        setPattern(value.stringValue());
}

void PacketFilter::setExpression(cObject *expression)
{
    if (auto ownedDynamicExpression = dynamic_cast<cOwnedDynamicExpression *>(expression))
        setExpression(ownedDynamicExpression);
    else if (auto dynamicExpression = dynamic_cast<cDynamicExpression *>(expression))
        setExpression(dynamicExpression);
    else if (auto valueHolder = dynamic_cast<cValueHolder *>(expression))
        setExpression(valueHolder);
    else
        throw cRuntimeError("Unknown expression type");
}

void PacketFilter::setExpression(cValue& expression)
{
    if (expression.getType() == cValue::POINTER)
        setExpression(expression.objectValue());
    else
        setPattern(expression.stringValue());
}

bool PacketFilter::matches(const cPacket *cpacket) const
{
    this->cpacket = cpacket;
    protocolToChunkMap.clear();
    classNameToChunkMap.clear();
    if (auto packet = dynamic_cast<const Packet *>(cpacket)) {
        PacketDissector packetDissector(ProtocolDissectorRegistry::globalRegistry, *packetDissectorCallback);
        packetDissector.dissectPacket(const_cast<Packet *>(packet));
    }
    bool result = true;
    if (matchExpression != nullptr) {
        cMatchableString matchableString(cpacket->getFullName());
        result &= matchExpression->matches(&matchableString);
    }
    if (filterExpression != nullptr)
        result &= filterExpression->evaluate().boolValue();
    return result;
}

void PacketFilter::PacketDissectorCallback::visitChunk(const Ptr<const Chunk>& chunk, const Protocol *protocol)
{
    packetFilter->protocolToChunkMap.insert({protocol, const_cast<Chunk *>(chunk.get())});
    auto className = chunk->getClassName();
    const char *colon = strrchr(className, ':');
    if (colon != nullptr)
        className = colon + 1;
    packetFilter->classNameToChunkMap.insert({className, const_cast<Chunk *>(chunk.get())});
}

cValue PacketFilter::DynamicExpressionResolver::readVariable(cExpression::Context *context, const char *name)
{
    if (!strcmp(name, "pk"))
        return const_cast<cPacket *>(packetFilter->cpacket);
    else {
        bool isClassName = isupper(name[0]);
        if (isClassName) {
            auto it = packetFilter->classNameToChunkMap.find(name);
            if (it != packetFilter->classNameToChunkMap.end())
                return it->second;
            else
                return cValue(any_ptr(nullptr));
        }
        else {
            auto protocol = Protocol::findProtocol(name);
            if (protocol != nullptr) {
                auto it = packetFilter->protocolToChunkMap.find(protocol);
                if (it != packetFilter->protocolToChunkMap.end())
                    return it->second;
                else
                    return cValue(any_ptr(nullptr));
            }
        }
        auto classDescriptor = packetFilter->cpacket->getDescriptor();
        int field = classDescriptor->findField(name);
        if (field != -1) {
            auto fieldValue = classDescriptor->getFieldValueAsString(toAnyPtr(packetFilter->cpacket), field, 0);
            return classDescriptor->getFieldValue(toAnyPtr(packetFilter->cpacket), field, 0);
        }
        else
            return IResolver::readVariable(context, name);
    }
}

cValue PacketFilter::DynamicExpressionResolver::readVariable(cExpression::Context *context, const char *name, intval_t index)
{
    bool isClassName = isupper(name[0]);
    if (isClassName) {
        if (index < packetFilter->classNameToChunkMap.count(name)) {
            auto it = packetFilter->classNameToChunkMap.lower_bound(name);
            while (index-- > 0)
                it++;
            return it->second;
        }
        else
            return cValue(any_ptr(nullptr));
    }
    else {
        auto protocol = Protocol::findProtocol(name);
        if (protocol != nullptr) {
            if (index < packetFilter->protocolToChunkMap.count(protocol)) {
                auto it = packetFilter->protocolToChunkMap.lower_bound(protocol);
                while (index-- > 0)
                    it++;
                return it->second;
            }
            else
                return cValue(any_ptr(nullptr));
        }
    }
    return IResolver::readVariable(context, name, index);
}

cValue PacketFilter::DynamicExpressionResolver::readMember(cExpression::Context *context, const cValue &object, const char *name)
{
    if (object.getType() == cValue::OBJECT) {
        if (dynamic_cast<Packet *>(object.objectValue())) {
            bool isClassName = isupper(name[0]);
            if (isClassName) {
                auto it = packetFilter->classNameToChunkMap.find(name);
                if (it != packetFilter->classNameToChunkMap.end())
                    return it->second;
                else
                    return cValue(any_ptr(nullptr));
            }
            else {
                auto protocol = Protocol::findProtocol(name);
                if (protocol != nullptr) {
                    auto it = packetFilter->protocolToChunkMap.find(protocol);
                    if (it != packetFilter->protocolToChunkMap.end())
                        return it->second;
                    else
                        return cValue(any_ptr(nullptr));
                }
            }
        }
        auto cobject = object.objectValue();
        if (cobject != nullptr) {
            auto classDescriptor = cobject->getDescriptor();
            int field = classDescriptor->findField(name);
            if (field != -1) {
                auto fieldValue = classDescriptor->getFieldValueAsString(toAnyPtr(cobject), field, 0);
                return classDescriptor->getFieldValue(toAnyPtr(cobject), field, 0);
            }
        }
        return IResolver::readMember(context, object, name);
    }
    else
        return IResolver::readMember(context, object, name);
}

cValue PacketFilter::DynamicExpressionResolver::readMember(cExpression::Context *context, const cValue &object, const char *name, intval_t index)
{
    if (object.getType() == cValue::OBJECT) {
        if (dynamic_cast<Packet *>(object.objectValue())) {
            bool isClassName = isupper(name[0]);
            if (isClassName) {
                if (index < packetFilter->classNameToChunkMap.count(name)) {
                    auto it = packetFilter->classNameToChunkMap.lower_bound(name);
                    while (index-- > 0)
                        it++;
                    return it->second;
                }
                else
                    return cValue(any_ptr(nullptr));
            }
            else {
                auto protocol = Protocol::findProtocol(name);
                if (protocol != nullptr) {
                    if (index < packetFilter->protocolToChunkMap.count(protocol)) {
                        auto it = packetFilter->protocolToChunkMap.lower_bound(protocol);
                        while (index-- > 0)
                            it++;
                        return it->second;
                    }
                    else
                        return cValue(any_ptr(nullptr));
                }
            }
        }
        auto cobject = object.objectValue();
        if (cobject != nullptr) {
            auto classDescriptor = cobject->getDescriptor();
            int field = classDescriptor->findField(name);
            if (field != -1) {
                auto fieldValue = classDescriptor->getFieldValueAsString(toAnyPtr(cobject), field, 0);
                return classDescriptor->getFieldValue(toAnyPtr(cobject), field, 0);
            }
        }
        return IResolver::readMember(context, object, name, index);
    }
    else
        return IResolver::readMember(context, object, name, index);
}

cValue PacketFilter::DynamicExpressionResolver::callMethod(cExpression::Context *context, const cValue& object, const char *name, cValue argv[], int argc)
{
    if (object.getType() == cValue::POINTER && object.pointerValue() != nullptr) {
        // TODO this dispatch isn't ideal
        const auto& type = object.pointerValue().pointerType();
        if (type == typeid(MacAddress *)) {
            auto macAddress = object.pointerValue().get<MacAddress>();
            if (!strcmp("str", name))
                return macAddress->str();
            else if (!strcmp("getInt", name))
                return (intval_t)macAddress->getInt();
        }
        else if (type == typeid(Ipv4Address *)) {
            auto ipv4Address = object.pointerValue().get<Ipv4Address>();
            if (!strcmp("str", name))
                return ipv4Address->str();
            else if (!strcmp("getInt", name))
                return (intval_t)ipv4Address->getInt();
        }
        else if (type == typeid(Ipv6Address *)) {
            auto ipv6Address = object.pointerValue().get<Ipv6Address>();
            if (!strcmp("str", name))
                return ipv6Address->str();
        }
        else if (type == typeid(L3Address *)) {
            auto l3Address = object.pointerValue().get<L3Address>();
            if (!strcmp("str", name))
                return l3Address->str();
        }
    }
    return IResolver::callMethod(context, object, name, argv, argc);
}

cValue PacketFilter::DynamicExpressionResolver::callFunction(cExpression::Context *context, const char *name, cValue argv[], int argc)
{
    if (!strcmp("has", name))
        return argv[0].objectValue() != nullptr;
    else
        return IResolver::callFunction(context, name, argv, argc);
}

} // namespace inet

