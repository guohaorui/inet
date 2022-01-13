//
// Copyright (C) 2021 by original authors
//
// This file is copied from the following project with the explicit permission
// from the authors: https://github.com/ACassimiro/TSNsched
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

#include "inet/linklayer/configurator/gatescheduling/z3/FlowFragment.h"
#include "inet/linklayer/configurator/gatescheduling/z3/Port.h"

namespace inet {

void Port::setUpCycleRules(solver& solver, context& ctx) {

    for (FlowFragment *frag : flowFragments) {
        for (int index = 0; index < cycle->getNumOfSlots(); index++) {
            std::shared_ptr<expr> flowPriority = frag->getFragmentPriorityZ3();
            expr indexZ3 = ctx.int_val(index);

            // A slot will be somewhere between 0 and the end of the cycle minus its duration (Slot in cycle constraint)
            addAssert(solver, cycle->slotStartZ3(ctx, *flowPriority, indexZ3) >= ctx.int_val(0));
            addAssert(solver,
                cycle->slotStartZ3(ctx, *flowPriority, indexZ3) <=
                        cycle->getCycleDurationZ3() -
                        cycle->slotDurationZ3(ctx, *flowPriority, indexZ3));

            // Every slot duration is greater or equal 0 and lower or equal than the maximum (Slot duration constraint)
            addAssert(solver, cycle->slotDurationZ3(ctx, *flowPriority, indexZ3) >= ctx.int_val(0));
            addAssert(solver, cycle->slotDurationZ3(ctx, *flowPriority, indexZ3) <= cycle->getMaximumSlotDurationZ3());

            //Every slot must fit inside a cycle
            addAssert(solver,
                    cycle->getCycleDurationZ3() >=
                        cycle->slotStartZ3(ctx, *flowPriority, indexZ3) +
                        cycle->slotDurationZ3(ctx, *flowPriority, indexZ3));

            /*
             * If the priority of the fragments are the same, then the start and duration
             * of a slot is also the same (needs to be specified due to z3 variable naming
             * properties) (Same priority, same slot constraint)
             */

            for (FlowFragment *auxFrag : flowFragments) {
                addAssert(solver,
                    implies(*frag->getFragmentPriorityZ3() == *auxFrag->getFragmentPriorityZ3(),
                            cycle->slotStartZ3(ctx, *frag->getFragmentPriorityZ3(), indexZ3) ==
                            cycle->slotStartZ3(ctx, *auxFrag->getFragmentPriorityZ3(), indexZ3) &&
                            cycle->slotDurationZ3(ctx, *frag->getFragmentPriorityZ3(), indexZ3) ==
                            cycle->slotDurationZ3(ctx, *auxFrag->getFragmentPriorityZ3(), indexZ3)));
            }

            // No two slots can overlap (No overlapping slots constraint)
            for (FlowFragment *auxFrag : flowFragments) {
                if (auxFrag == frag) {
                    continue;
                }

                std::shared_ptr<expr> auxFlowPriority = auxFrag->getFragmentPriorityZ3();

                addAssert(solver,
                    implies(!(*flowPriority == *auxFlowPriority),
                                cycle->slotStartZ3(ctx, *flowPriority, indexZ3) >=
                                    cycle->slotStartZ3(ctx, *auxFlowPriority, indexZ3) +
                                    cycle->slotDurationZ3(ctx, *auxFlowPriority, indexZ3) ||
                                    cycle->slotStartZ3(ctx, *flowPriority, indexZ3) +
                                    cycle->slotDurationZ3(ctx, *flowPriority, indexZ3) <=
                                cycle->slotStartZ3(ctx, *auxFlowPriority, indexZ3)));
            }


            if (index < cycle->getNumOfSlots() - 1) {
                addAssert(solver,
                        cycle->slotStartZ3(ctx, *flowPriority, indexZ3) <
                        cycle->slotStartZ3(ctx, *flowPriority, ctx.int_val(index + 1)));
            }



            /*
             * If 2 slots are not consecutive, then there must be a space
             * of at least gbSize (the size of the guard band) between them
             * (guard band constraint).
             *
            for (FlowFragment auxFrag : flowFragments) {
                for (int auxIndex = 0; auxIndex < cycle->getNumOfSlots(); auxIndex++) {
                    std::shared_ptr<expr> auxIndexZ3 = ctx.int_val(auxIndex);

                    if (auxFrag->equals(frag)) {
                        continue;
                    }

                    std::shared_ptr<expr> auxFlowPriority = auxFrag->getFlowPriority();

                    addAssert(solver,
                        implies(
                            mkAnd(
                                mkNot(
                                    mkAnd(
                                        mkEq(flowPriority, auxFlowPriority),
                                        mkEq(indexZ3, auxIndexZ3))),
                                mkNot(
                                    mkEq(
                                        cycle->slotStartZ3(ctx, flowPriority, indexZ3),
                                        mkAdd(
                                            cycle->slotDurationZ3(ctx, auxFlowPriority, auxIndexZ3),
                                            cycle->slotStartZ3(ctx, auxFlowPriority, auxIndexZ3)))),
                                mkGt(
                                    cycle->slotStartZ3(ctx, flowPriority, indexZ3),
                                    cycle->slotStartZ3(ctx, auxFlowPriority, auxIndexZ3))),
                            mkGe(
                                cycle->slotStartZ3(ctx, flowPriority, indexZ3),
                                mkAdd(
                                    cycle->slotStartZ3(ctx, auxFlowPriority, auxIndexZ3),
                                    cycle->slotDurationZ3(ctx, auxFlowPriority, auxIndexZ3),
                                    gbSizeZ3))));
                }
            }
            */
        }

    }
}

void Port::setupTimeSlots(solver& solver, context& ctx, FlowFragment *flowFrag) {
    // If there is a flow assigned to the slot, slotDuration must be greater than transmission time
    for (int index = 0; index < cycle->getNumOfSlots(); index++) {
        addAssert(solver,
                cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), ctx.int_val(index+1)) >=
                        cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), ctx.int_val(index)) +
                        cycle->slotDurationZ3(ctx, *flowFrag->getFragmentPriorityZ3(), ctx.int_val(index)));
    }

    for (int index = 0; index < cycle->getNumOfSlots(); index++) {
        expr indexZ3 = ctx.int_val(index);

        // addAssert(solver, mkGe(cycle->slotDurationZ3(ctx, flowFrag->getFlowPriority(), indexZ3), transmissionTimeZ3));

        // Every flow must have a priority (Priority assignment constraint)
        addAssert(solver, flowFrag->getFragmentPriorityZ3() >= ctx.int_val(0));
        addAssert(solver, *flowFrag->getFragmentPriorityZ3() < ctx.int_val(cycle->getNumOfPrts()));

        // Slot start must be <= cycle time - slot duration
        addAssert(solver,
                    cycle->slotDurationZ3(ctx, *flowFrag->getFragmentPriorityZ3(), indexZ3) +
                    cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), indexZ3) <=
                cycle->getCycleDurationZ3());

    }
}

void Port::setupDevPacketTimes(solver& solver, context& ctx, FlowFragment *flowFrag) {

    // For the specified range of packets defined by [0, upperBoundRange],
    // apply the scheduling rules.

    //System.out.println(std::string("Setting up rules for " + flowFrag->getName() + std::string(" - Num of packets: ")) + flowFrag->getParent().getNumOfPacketsSent());
    for (int i = 0; i < flowFrag->getParent()->getNumOfPacketsSent(); i++) {
        // Make t3 > t2 + transmissionTime
        addAssert(solver,  // Time to Transmit constraint.
                scheduledTime(ctx, i, flowFrag) >=
                arrivalTime(ctx, i, flowFrag) + flowFrag->getPacketSizeZ3() / portSpeedZ3);

    }

    std::shared_ptr<expr> indexZ3 = nullptr;
    std::shared_ptr<expr> auxExp = nullptr;
    std::shared_ptr<expr> auxExp2 = std::make_shared<expr>(ctx.bool_val(true));
    std::shared_ptr<expr> exp = nullptr;

    for (FlowFragment *auxFragment : flowFragments) {

        /*
        System.out.println(std::string("Num de pacotes escalonados:") + auxFragment->getNumOfPacketsSent());

        for (int i = 0; i < auxFragment->getNumOfPacketsSent(); i++) {
            addAssert(solver,
                mkEq(
                    arrivalTime(ctx, i, auxFragment),
                    mkAdd(
                            departureTime(ctx, i, auxFragment),
                            timeToTravelZ3)));

            addAssert(solver,
                mkGe(
                    scheduledTime(ctx, i, auxFragment),
                    mkAdd(
                            arrivalTime(ctx, i, auxFragment),
                            transmissionTimeZ3)));
        }
        */


        for (int i = 0; i < flowFrag->getNumOfPacketsSent(); i++) {
            for (int j = 0; j < auxFragment->getNumOfPacketsSent(); j++) {
                if (auxExp == nullptr) {
                    auxExp = std::make_shared<expr>(ctx.bool_val(false));
                }

                if (auxFragment == flowFrag && i == j) {
                    continue;
                }


                /*****************************************************
                 *
                 * Packet A must be transfered after packet B or
                 * fit one of the three base cases.
                 *
                 *****************************************************/
                auxExp = std::make_shared<expr>(auxExp ||
                            (auxFragment->getFragmentPriorityZ3() == *flowFrag->getFragmentPriorityZ3() &&
                                arrivalTime(ctx, i, flowFrag) <= arrivalTime(ctx, j, auxFragment)) &&
                                scheduledTime(ctx, j, auxFragment) ==
                                    scheduledTime(ctx, i, flowFrag) +
                                    flowFrag->getPacketSizeZ3() / portSpeedZ3);



            }


            for (int j = 0; j < cycleUpperBoundRange; j++) {

                /*
                T2 IS INSIDE SLOT, HAS ENOUGH TIME TO TRANSMIT
                ; **************************************
                ; |------------------------------------|
                ; CS       S    t2-------t3    E       CE
                ;               transmission
                ; **************************************
                */

                for (int index = 0; index < cycle->getNumOfSlots(); index++) {
                    indexZ3 = std::make_shared<expr>(ctx.int_val(index));

                    auxExp2 = std::make_shared<expr>(auxExp2 && // Arrived during a time slot predicate
                            implies(
                                        arrivalTime(ctx, i, flowFrag) <=
                                                cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                                    (cycle->slotDurationZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                                    cycle->cycleStartZ3(ctx, ctx.int_val(j))) -
                                            flowFrag->getPacketSizeZ3() / portSpeedZ3 &&
                                        arrivalTime(ctx, i, flowFrag) >=
                                            cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                            cycle->cycleStartZ3(ctx, j),
                                    scheduledTime(ctx, i, flowFrag) ==
                                        arrivalTime(ctx, i, flowFrag) +
                                        flowFrag->getPacketSizeZ3() / portSpeedZ3));


                    /*
                    ; T2 IS BEFORE THE SLOT
                    ; **************************************
                    ; |------------------------------------|
                    ; CS     t2      S-------t3    E       CE
                    ;               transmission
                    ; **************************************
                    */

                    if (index == 0) {
                        auxExp2 = std::make_shared<expr>(auxExp2 && // Arrived before slot start constraint
                                implies(
                                        arrivalTime(ctx, i, flowFrag) <
                                            cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                            cycle->cycleStartZ3(ctx, j) &&
                                        arrivalTime(ctx, i, flowFrag) >=
                                        cycle->cycleStartZ3(ctx, j),
                                        scheduledTime(ctx, i, flowFrag) ==
                                                (cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                                cycle->cycleStartZ3(ctx, j) +
                                            flowFrag->getPacketSizeZ3() / portSpeedZ3)));
                    } else if (index < cycle->getNumOfSlots()) {
                        auxExp2 = std::make_shared<expr>(auxExp2 &&
                                implies(
                                        arrivalTime(ctx, i, flowFrag) <
                                            cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                            cycle->cycleStartZ3(ctx, j) &&
                                        arrivalTime(ctx, i, flowFrag) >
                                                cycle->cycleStartZ3(ctx, j) +
                                                    (cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), ctx.int_val(index - 1)) +
                                                    cycle->slotDurationZ3(ctx, *flowFrag->getFragmentPriorityZ3(), ctx.int_val(index - 1))) -
                                            flowFrag->getPacketSizeZ3() / portSpeedZ3,
                                        scheduledTime(ctx, i, flowFrag) ==
                                                (cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                                cycle->cycleStartZ3(ctx, j) +
                                            flowFrag->getPacketSizeZ3() / portSpeedZ3)));
                    }

                    /*
                    ; T2 IS AFTER THE SLOT OR INSIDE WITHOUT ENOUGH TIME. The packet won't be trans-
                    ; mitted. This happens due to the usage of hyper and micro-cycles.
                    ; ****************************************************************************
                    ; |------------------------------------|------------------------------------|
                    ; CS        S        t2     E        CE/CS       S----------t3   E         CE
                    ;                                                transmission
                    ; ****************************************************************************
                    */

                    if (index == cycle->getNumOfSlots() - 1) {
                        auxExp2 = std::make_shared<expr>(auxExp2 && // Arrived after slot end constraint
                                implies(
                                        arrivalTime(ctx, i, flowFrag) >=
                                        cycle->cycleStartZ3(ctx, j) &&
                                            arrivalTime(ctx, i, flowFrag) <=
                                                cycle->getCycleDurationZ3() +
                                                cycle->cycleStartZ3(ctx, j),
                                        scheduledTime(ctx, i, flowFrag) <=
                                            cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                                (cycle->slotDurationZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                                cycle->cycleStartZ3(ctx, j))));
                    }

                    /**
                    * THE CODE BELLOW HAS ISSUES REGARDING NOT COVERING ALL CASES (ALLOWS DELAY).
                    * REVIEW LATER.

                    if (j < cycleUpperBoundRange - 1 && index == cycle->getNumOfSlots() - 1) {
                        auxExp2 = mkAnd((expr) auxExp2, // Arrived after slot end constraint
                                implies(
                                    mkAnd(
                                        mkGt(
                                            arrivalTime(ctx, i, flowFrag),
                                            mkSub(
                                                mkAdd(
                                                    cycle->slotStartZ3(ctx, flowFrag->getFlowPriority(), indexZ3),
                                                    cycle->slotDurationZ3(ctx, flowFrag->getFlowPriority(), indexZ3),
                                                    cycle->cycleStartZ3(ctx, j)),
                                                transmissionTimeZ3)),
                                        mkLe(
                                            arrivalTime(ctx, i, flowFrag),
                                            mkAdd(
                                                cycle->cycleStartZ3(ctx, j),
                                                cycle->getCycleDurationZ3()))),

                                    mkEq(
                                        scheduledTime(ctx, i, flowFrag),
                                        mkAdd(
                                            mkAdd(
                                                cycle->slotStartZ3(ctx, flowFrag->getFlowPriority(), ctx.int_val(0)),
                                                cycle->cycleStartZ3(ctx, j + 1)),
                                            transmissionTimeZ3))));
                    } else if (j == cycleUpperBoundRange - 1 && index == cycle->getNumOfSlots() - 1) {
                        auxExp2 = mkAnd((expr) auxExp2,
                                implies(
                                    mkAnd(
                                        mkGt(
                                            arrivalTime(ctx, i, flowFrag),
                                            mkSub(
                                                mkAdd(
                                                    cycle->slotStartZ3(ctx, flowFrag->getFlowPriority(), indexZ3),
                                                    cycle->slotDurationZ3(ctx, flowFrag->getFlowPriority(), indexZ3),
                                                    cycle->cycleStartZ3(ctx, j)),
                                                transmissionTimeZ3)),
                                        mkLe(
                                            arrivalTime(ctx, i, flowFrag),
                                            mkAdd(
                                                cycle->cycleStartZ3(ctx, j),
                                                cycle->getCycleDurationZ3()))),

                                    mkEq(
                                        scheduledTime(ctx, i, flowFrag),
                                        arrivalTime(ctx, i, flowFrag))
));
                    }
                    */
                }
            }

            //auxExp = mkOr((expr)ctx.bool_val(false)(), (expr)auxExp2);
            auxExp = std::make_shared<expr>(auxExp || auxExp2);


            if (exp == nullptr) {
                exp = auxExp;
            } else {
                exp = std::make_shared<expr>(exp && auxExp);
            }
            auxExp = std::make_shared<expr>(ctx.bool_val(false));
            auxExp2 = std::make_shared<expr>(ctx.bool_val(true));
        }
    }

    addAssert(solver, *exp);

    auxExp = nullptr;
    exp = std::make_shared<expr>(ctx.bool_val(false));

    //Every packet must be transmitted inside a timeslot (transmit inside a time slot constraint)
    for (int i = 0; i < flowFrag->getNumOfPacketsSent(); i++) {
        for (int j = 0; j < cycleUpperBoundRange; j++) {
            for (int index = 0; index < cycle->getNumOfSlots(); index++) {
                indexZ3 = std::make_shared<expr>(ctx.int_val(index));
                auxExp = std::make_shared<expr>(
                            scheduledTime(ctx, i, flowFrag) >=
                                cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                    (cycle->cycleStartZ3(ctx, j) +
                                    flowFrag->getPacketSizeZ3() / portSpeedZ3) &&
                            scheduledTime(ctx, i, flowFrag) <=
                                cycle->slotStartZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                    (cycle->slotDurationZ3(ctx, *flowFrag->getFragmentPriorityZ3(), *indexZ3) +
                                    cycle->cycleStartZ3(ctx, j)));

                exp = std::make_shared<expr>(exp || auxExp);
            }
        }
        addAssert(solver, *exp);
        exp = std::make_shared<expr>(ctx.bool_val(false));
    }

    for (int i = 0; i < flowFrag->getNumOfPacketsSent() - 1; i++) {
        addAssert(solver,
                scheduledTime(ctx, i + 1, flowFrag) >=
                        scheduledTime(ctx, i, flowFrag) +
                        flowFrag->getPacketSizeZ3() / portSpeedZ3);
    }

    for (int i = 0; i < flowFrag->getNumOfPacketsSent(); i++) {
        for (FlowFragment *auxFlowFrag : flowFragments) {
            for (int j = 0; j < auxFlowFrag->getNumOfPacketsSent(); j++) {

               if (auxFlowFrag == flowFrag) {
                   continue;
               }



               /*
                * Given that packets from two different flows have
                * the same priority in this switch, they must not
                * be transmitted at the same time
                *
                * OBS: This constraint might not be needed due to the
                * FIFO constraint.
                *
               addAssert(solver,
                   implies(
                       mkEq(
                           auxFlowFrag->getFlowPriority(),
                           flowFrag->getFlowPriority()),
                       mkOr(
                           mkLe(
                               scheduledTime(ctx, i, flowFrag),
                               mkSub(
                                   scheduledTime(ctx, j, auxFlowFrag),
                                   transmissionTimeZ3)),
                           mkGe(
                               scheduledTime(ctx, i, flowFrag),
                               mkAdd(
                                   scheduledTime(ctx, j, auxFlowFrag),
                                   transmissionTimeZ3)))));
               */

               /*
                * Frame isolation constraint as specified by:
                *   Craciunas, Silviu S., et al. "Scheduling real-time communication in IEEE
                *   802.1 Qbv time sensitive networks." Proceedings of the 24th International
                *   Conference on Real-Time Networks and Systems. ACM, 2016.
                *
                * Two packets from different flows cannot be in the same priority queue
                * at the same time
                *

               addAssert(solver,
                   implies(
                      mkEq(
                          flowFrag->getFlowPriority(),
                           auxFlowFrag->getFlowPriority()),
                       mkOr(
                           mkLt(
                               scheduledTime(ctx, i, flowFrag),
                               arrivalTime(ctx, j, auxFlowFrag)),
                           mkGt(
                               arrivalTime(ctx, i, flowFrag),
                               scheduledTime(ctx, j, auxFlowFrag)))));
               */

            }
        }
    }

    /*
     * If two packets are from the same priority, the first one to arrive
     * should be transmitted first (FIFO priority queue constraint)
     */
    for (int i = 0; i < flowFrag->getNumOfPacketsSent(); i++) {
        for (FlowFragment *auxFlowFrag : flowFragments) {
            for (int j = 0; j < auxFlowFrag->getNumOfPacketsSent(); j++) {

                if ((flowFrag == auxFlowFrag && i == j)) {
                    continue;
                }

                addAssert(solver,  // Packet transmission order constraint
                    implies(
                                arrivalTime(ctx, i, flowFrag) <=
                                arrivalTime(ctx, j, auxFlowFrag) &&
                                flowFrag->getFragmentPriorityZ3() ==
                                auxFlowFrag->getFragmentPriorityZ3(),
                            scheduledTime(ctx, i, flowFrag) <=
                            scheduledTime(ctx, j, auxFlowFrag) - auxFlowFrag->getPacketSizeZ3() / portSpeedZ3));

                /*
                if (!(flowFrag->equals(auxFlowFrag) && i == j)) {
                    addAssert(solver,
                        mkNot(
                            mkEq(
                                arrivalTime(ctx, i, flowFrag),
                                arrivalTime(ctx, j, auxFlowFrag))));
                }
                */

            }
        }
    }

}

void Port::setupBestEffort(solver& solver, context& ctx) {
    std::shared_ptr<expr> slotStart[8];
    std::shared_ptr<expr> slotDuration[8];
    // expr guardBandTime = nullptr;

    std::shared_ptr<expr> firstPartOfImplication = nullptr;
    std::shared_ptr<expr> sumOfPrtTime = nullptr;

    for (int i = 0; i < 8; i++) {
        slotStart[i] = std::make_shared<expr>(ctx.real_const((name + std::string("SlotStart") + std::to_string(i)).c_str()));
        slotDuration[i] = std::make_shared<expr>(ctx.real_const((name + std::string("SlotDuration") + std::to_string(i)).c_str()));
    }

    for (FlowFragment f : flowFragments) {
        // expr sumOfSlotsStart = ctx.real_val(0);
        std::shared_ptr<expr> sumOfSlotsDuration = std::make_shared<expr>(ctx.real_val(0));

        for (int i = 0; i < cycle->getNumOfSlots(); i++) {
            sumOfSlotsDuration = std::make_shared<expr>(sumOfSlotsDuration + cycle->slotDurationZ3(ctx, *f.getFragmentPriorityZ3(), ctx.int_val(i)));
        }

        for (int i = 1; i <= 8; i++) {
            addAssert(solver,
                implies(*f.getFragmentPriorityZ3() == ctx.int_val(i),
                        slotDuration[i-1] == *sumOfSlotsDuration));
        }
    }

    for (int i = 1; i<=8; i++) {
        firstPartOfImplication = nullptr;

        for (FlowFragment f : flowFragments) {
            if (firstPartOfImplication == nullptr) {
                firstPartOfImplication = std::make_shared<expr>(!(*f.getFragmentPriorityZ3() == ctx.int_val(i)));
            } else {
                firstPartOfImplication = std::make_shared<expr>(firstPartOfImplication &&
                                             !(*f.getFragmentPriorityZ3() == ctx.int_val(i)));
            }
        }

        addAssert(solver,  // Queue not used constraint
            implies(
                *firstPartOfImplication,
                slotStart[i-1] == ctx.real_val(0) &&
                slotDuration[i-1] == ctx.real_val(0)));
    }

    for (std::shared_ptr<expr> slotDr : slotDuration) {
        if (sumOfPrtTime == nullptr) {
            sumOfPrtTime = slotDr;
        } else {
            sumOfPrtTime = std::make_shared<expr>(sumOfPrtTime + slotDr);
        }
    }


    addAssert(solver,  // Best-effort bandwidth reservation constraint
            sumOfPrtTime <= (ctx.real_val(1) - bestEffortPercentZ3) * cycle->getCycleDurationZ3());

    /*
    addAssert(solver,
            mkLe(
                sumOfPrtTime,
                mkMul(bestEffortPercentZ3, cycle->getCycleDurationZ3())));

    addAssert(solver,
        mkGe(
            bestEffortPercentZ3,
            mkDiv(sumOfPrtTime, cycle->getCycleDurationZ3())));
    */

}

void Port::zeroOutNonUsedSlots(solver& solver, context& ctx)
{
    std::shared_ptr<expr> exp1;
    std::shared_ptr<expr> exp2;
    std::shared_ptr<expr> indexZ3;

    for (int prtIndex = 0; prtIndex < cycle->getNumOfPrts(); prtIndex++) {
        for (FlowFragment *frag : flowFragments) {
            for (int slotIndex = 0; slotIndex < cycle->getNumOfSlots(); slotIndex++) {
                addAssert(solver,
                    implies(*frag->getFragmentPriorityZ3() == ctx.int_val(prtIndex),
                            cycle->slotStartZ3(ctx, *frag->getFragmentPriorityZ3(), ctx.int_val(slotIndex)) ==
                            cycle->slotStartZ3(ctx, ctx.int_val(prtIndex), ctx.int_val(slotIndex)) &&
                            cycle->slotDurationZ3(ctx, *frag->getFragmentPriorityZ3(), ctx.int_val(slotIndex)) ==
                            cycle->slotDurationZ3(ctx, ctx.int_val(prtIndex), ctx.int_val(slotIndex))));
            }
        }
    }


    for (int prtIndex = 0; prtIndex < cycle->getNumOfPrts(); prtIndex++) {
        for (int cycleNum = 0; cycleNum < cycleUpperBoundRange; cycleNum++) {
            for (int indexNum = 0; indexNum < cycle->getNumOfSlots(); indexNum++) {
                indexZ3 = std::make_shared<expr>(ctx.int_val(indexNum));
                exp1 = std::make_shared<expr>(ctx.bool_val(true));
                for (FlowFragment *frag : flowFragments) {
                    for (int packetNum = 0; packetNum < frag->getNumOfPacketsSent(); packetNum++) {
                        exp1 = std::make_shared<expr>(
                                exp1 &&
                                (!(scheduledTime(ctx, packetNum, frag) >=
                                   cycle->slotStartZ3(ctx, ctx.int_val(prtIndex), *indexZ3) +
                                   cycle->cycleStartZ3(ctx, ctx.int_val(cycleNum)) &&
                                   scheduledTime(ctx, packetNum, frag) <=
                                   cycle->slotStartZ3(ctx, ctx.int_val(prtIndex), *indexZ3) +
                                   (cycle->slotDurationZ3(ctx, ctx.int_val(prtIndex), *indexZ3) +
                                   cycle->cycleStartZ3(ctx, ctx.int_val(cycleNum)))) &&
                                ctx.int_val(prtIndex) == *frag->getFragmentPriorityZ3()));
                    }

                }

                addAssert(solver,
                    implies(
                        *exp1,
                        *cycle->slotDurationZ3(ctx, ctx.int_val(prtIndex), *indexZ3) == ctx.int_val(0)));
            }

        }

    }
}

std::shared_ptr<expr> Port::departureTime(context& ctx, int auxIndex, FlowFragment *flowFrag)
{
    std::shared_ptr<expr> index = nullptr;
    std::shared_ptr<expr> departureTime;
    int cycleNum = 0;


    if (auxIndex + 1 > flowFrag->getNumOfPacketsSent()) {
        cycleNum = (auxIndex - (auxIndex % flowFrag->getNumOfPacketsSent()))/flowFrag->getNumOfPacketsSent();

        auxIndex = (auxIndex % flowFrag->getNumOfPacketsSent());

        departureTime = std::make_shared<expr>(
                        flowFrag->getDepartureTimeZ3(auxIndex) +
                    ctx.real_val(cycleNum) * cycle->getCycleDurationZ3());


        return departureTime;
    }

    departureTime = flowFrag->getDepartureTimeZ3(auxIndex);

    return departureTime;

}

std::shared_ptr<expr> Port::scheduledTime(context& ctx, int auxIndex, FlowFragment *flowFrag)
{
    std::shared_ptr<expr> index = nullptr;
    std::shared_ptr<expr> scheduledTime;
    int cycleNum = 0;

    if (auxIndex + 1 > flowFrag->getNumOfPacketsSent()) {
        cycleNum = (auxIndex - (auxIndex % flowFrag->getNumOfPacketsSent()))/flowFrag->getNumOfPacketsSent();

        auxIndex = (auxIndex % flowFrag->getNumOfPacketsSent());
        index = std::make_shared<expr>(ctx.int_val(auxIndex));

        scheduledTime = std::make_shared<expr>(
                    ctx.real_const((flowFrag->getName() + std::string("ScheduledTime") + index->to_string()).c_str()) +
                    ctx.real_val(cycleNum) * cycle->getCycleDurationZ3());

        return scheduledTime;
    }

    index = std::make_shared<expr>(ctx.int_val(auxIndex));


    scheduledTime = std::make_shared<expr>(ctx.real_const((flowFrag->getName() + std::string("ScheduledTime") + index->to_string()).c_str()));

    return scheduledTime;
}

void Port::loadZ3(context& ctx, solver& solver)
{

    this->cycle->loadZ3(ctx, solver);

    for (FlowFragment *frag : flowFragments) {

        frag->setFragmentPriorityZ3(
            ctx.int_val(
                    frag->getFragmentPriority()));

        /*
        addAssert(solver,
            mkEq(
                frag->getFragmentPriorityZ3(),
                ctx.int_val(frag->getFragmentPriority())));
        */

        for (int index = 0; index < cycle->getNumOfSlots(); index++) {
            addAssert(solver,
                    cycle->slotDurationZ3(ctx, *frag->getFragmentPriorityZ3(), ctx.int_val(index)) ==
                    ctx.real_val(
                        std::to_string(
                            cycle->getSlotDuration(frag->getFragmentPriority(), index)).c_str()));

            addAssert(solver,
                    cycle->slotStartZ3(ctx, *frag->getFragmentPriorityZ3(), ctx.int_val(index)) ==
                    ctx.real_val(
                        std::to_string(
                            cycle->getSlotStart(frag->getFragmentPriority(), index)).c_str()));

        }

        for (int i = 0; i < frag->getNumOfPacketsSent(); i++) {
            /*
            addAssert(solver,
                mkEq(
                    departureTime(ctx, i, frag),
                    ctx.real_val(std::to_string(frag->getDepartureTime(i)))));
            */
            if (i > 0)
                frag->addDepartureTimeZ3(ctx.real_val(std::to_string(frag->getDepartureTime(i)).c_str()));

            addAssert(solver,
                    arrivalTime(ctx, i, frag) ==
                    ctx.real_val(std::to_string(frag->getArrivalTime(i)).c_str()));

            addAssert(solver,
                    scheduledTime(ctx, i, frag) ==
                    ctx.real_val(std::to_string(frag->getScheduledTime(i)).c_str()));

        }

    }
}

std::shared_ptr<expr> Port::getScheduledTimeOfPreviousPacket(context& ctx, FlowFragment *f, int index)
{
    std::shared_ptr<expr> prevPacketST = std::make_shared<expr>(ctx.real_val(0));

    for (FlowFragment *auxFrag : flowFragments) {

        for (int i = 0; i < f->getNumOfPacketsSent(); i++) {
            prevPacketST = std::make_shared<expr>(
                            auxFrag->getFragmentPriorityZ3() == *f->getFragmentPriorityZ3() &&
                            (scheduledTime(ctx, i, auxFrag) <
                             scheduledTime(ctx, index, f) &&
                             scheduledTime(ctx, i, auxFrag) >
                             prevPacketST) ?
                        *scheduledTime(ctx, i, auxFrag) :
                        *prevPacketST);

        }

    }

    return std::make_shared<expr>(
                prevPacketST == ctx.real_val(0) ?
                *scheduledTime(ctx, index, f) :
                *prevPacketST);
}

}
