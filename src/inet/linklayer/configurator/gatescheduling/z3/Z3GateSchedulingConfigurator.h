//
// Copyright (C) 2021 OpenSim Ltd. and the original authors
//
// This file is partly copied from the following project with the explicit
// permission from the authors: https://github.com/ACassimiro/TSNsched
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

#ifndef __INET_Z3GATESCHEDULINGCONFIGURATOR_H
#define __INET_Z3GATESCHEDULINGCONFIGURATOR_H

#include "inet/linklayer/configurator/gatescheduling/base/GateSchedulingConfiguratorBase.h"
#include "inet/linklayer/configurator/gatescheduling/z3/Cycle.h"
#include "inet/linklayer/configurator/gatescheduling/z3/Device.h"
#include "inet/linklayer/configurator/gatescheduling/z3/Flow.h"
#include "inet/linklayer/configurator/gatescheduling/z3/FlowFragment.h"
#include "inet/linklayer/configurator/gatescheduling/z3/Network.h"
#include "inet/linklayer/configurator/gatescheduling/z3/Switch.h"

namespace inet {

using namespace z3;

class INET_API Z3GateSchedulingConfigurator : public GateSchedulingConfiguratorBase
{
  protected:
    virtual Output *computeGateScheduling(const Input& input) const override;

    virtual void configureNetwork(Network *net, context& ctx, solver& solver) const;

    /**
     * After creating a network, setting up the
     * flows and switches, the user now can call this
     * function in order calculate the schedule values
     * using z3
     *
     * @param net   Network used as base to generate the schedule
     */
    virtual void generateSchedule(Network *net) const;

    /**
     * After evaluating the model, z3 allows the
     * user to retrieve the values of variables. This
     * values are stored as strings, which are converted
     * by this function in order to be stored in the
     * classes variables. Often these z3 variables are
     * also on fraction form, which is also handled by
     * this function.
     *
     * @param str   std::string containing value to convert to double
     * @return      double value of the given string str
     */
    virtual double stringToFloat(std::string str) const;

    /**
     * This is a recursive function used to
     * navigate through the pathTree, storing information
     * about the switches and flowFramengts in the nodes
     * and printing data in the log.
     *
     * @param pathNode  Current node of pathTree (should start with root)
     * @param model     Output model generated by z3
     * @param ctx       z3 context used to generate the model
     * @param out       PrintWriter stream to output log file
     */
    virtual void writePathTree(PathNode *pathNode, model& model, context& ctx) const;
};

} // namespace inet

#endif

