Using gPTP
==========

Goals
-----

In this example we demonstrate how to configure gPTP (Generic Precision Time
Protocol, IEEE 802.1 AS) master clocks, bridges, and end stations to achieve
reliable time synchronization throughout the whole network.

| INET version: ``4.4``
| Source files location: `inet/showcases/tsn/timesynhronization/gptp <https://github.com/inet-framework/tree/master/showcases/tsn/timesynchronization/gptp>`__

About gPTP
----------

.. so

   gptp, compared to the out of bound clock sync, is a real protocol. the sync is an emergent property of the working of the protocol.

   it works by measuring link delays and resident time of packets in the various network nodes, and uses that to sync clocks.
   the protocol assign a role to each network node: master clock, slave clock, or bridge node -> nope

   so

   gptp works by

   - measuring link delay
   - propagating the time from the master clocks to the slave clocks (and the bridge clocks which are slaves in bridge nodes)
   - so that the slave nodes know the time from the delay and the time sent by the master clock
   - sync messages propagate the time
   - delay request and response messages probe the link delay

   so they periodically send req/resp messages to probe the link delay

   the slaves and bridges periodically send 

Overview
~~~~~~~~

The Generic Precision Time Protocol (gPTP) can synchronize clocks in a network with high accuracy required by TSN protocols.
A master clock's time is synchronized in a gPTP `time domain`. A network can have multiple gPTP time domains, i.e. nodes can keep track of time using multple clocks
for redundancy (in case one of the master clocks fails or goes offline due to link break, for example). 
Each time domain contains a master clock, and any number of slave clocks. The protocol synchronizes the slave clocks to the master clock by sending `sync messages`.

.. **TODO** types of ports, nodes, etc here?

According to the IEEE 802.1AS standard, the master clock can be automatically selected by the Best Master Clock algorithm (BCMA). BMCA also determines
the clock spanning tree, i.e. the routes on which sync messages are propagated to slave clocks in the network. INET currently
doesn't support BMCA; the master clock and the spanning tree needs to be specified manually. (**TODO** each domain has its own spanning tree)

.. gPTP nodes can be one of three types, according to their location in the spanning tree: master, bridge or slave node. Master nodes (containing the master clock for the time domain) create
   gPTP sync messages, and broadcast them down-tree to bridge and slave nodes. Bridge nodes forward sync messages to slave nodes as well. **TODO** this seems to be INET specific?

   **TODO** by specifying master and slave ports (interfaces)

The operation of gPTP is summarized as follows:

- All nodes compute the residence time and up-tree link latency
- The gPTP sync messages are propagated along the spanning tree
- Nodes calculate the precise time from the sync messages, the residence time and the link latency (of the link the sync message was received from)

.. The protocol synchronizes slave clocks to a master clock. 


.. **TODO** briefly how it works; master and slave clocks, etc.

gPTP in INET
~~~~~~~~~~~~

In INET, the protocol is implemented by the :ned:`Gptp` module. The module is an application, and it is built in in several
network nodes, such as hosts and ethernet switches. The built-in :ned:`Gptp` modules can be enabled with the :par:`hasTimeSynchronization`
parameter in network nodes (this adds a :ned:`Gptp` module by default).

Each gPTP node is one of three types:

- **Master**: contains the master clock
- **Bridge**: contains a slave clock, and forwards sync messages down the tree
- **Slave**: contains a slave clock, and is a leaf in the tree

gPTP nodes can be one of three types, according to their location in the spanning tree: `master`, `bridge` or `slave`. Master nodes (containing the master clock for the time domain) create
gPTP sync messages, and broadcast them down-tree to bridge and slave nodes. Bridge nodes forward sync messages to slave nodes as well.

The node type can be selected by the :ned:`Gptp` module's :par:`gptpNodeType` parameter (either ``MASTER_NODE``, ``BRIDGE_NODE`` or ``SLAVE_NODE``)

The spanning tree is specified by labeling some of a node's ports as either master or slave, with the :par:`slavePort` and :par:`masterPorts` parameters (there is only one slave port). 
Sync messages are received via the slave port, and forwarded via master ports.

The spanning tree is created by labeling nodes' interfaces (called `ports`) as either a master or a slave, with the :par:`slavePort` and :par:`masterPorts` parameters.
Sync messages are sent on master ports, and received on slave ports. (Master nodes only have master ports, and slave nodes only slave ports.)

.. The protocol has two distinct mechanisms:

  - Peer delay measurement: initiated by slave and bridge nodes up-tree by sending peer delay request messages (pDelayReq) and receiving peer delay responses (pDelayResp)
  - Time synchronization: initiated by the master node by sending ``gptpSync`` messages down-tree; the messages are forwarded by bridge nodes to slave nodes

  The INET implementation currently only features the two-step time synchronization process. The pDelayResp and gptpSync messages are immediatelly followed by a follow up message (pDelayFollowUp and gptpFollowUp), which contains the precise time of when the previous message
  was sent. After receiving the follow up message, a node sets its clock time. (In the one-step process, the precise time is included in the original packet, so no follow-ups are necessary.)

  **TODO** no BMC algorithm

The :ned:`Gptp` has two distinct mechanisms:

- peer delay measurement: slave and bridge nodes periodically measure link delay by sending peer delay request messages (pDelayReq) up-tree; 
  they receive peer delay response messages (pDelayResp)
- time synchronization: master nodes periodically broadcast gPTP sync messages with the correct time that propagate through-out the tree

.. note:: - Currently, only two-step synchronization is supported, i.e. pDelayResp and gptpSync messages are immediatelly followed by follow-up 
            messages which contain the precise time of sending the original pDelayResp/gptpSync message.
          - Nodes can have multiple gPTP time domains. In this case, each time domain has a corresponding :ned:`Gptp` module. The :ned:`MultiGptp` module makes
            this convenient, as it contains multiple :ned:`Gptp` modules. Also, each domain needs to have a corresponding clock module. The :ned:`MultiClock` module
            can be used for this purpose.

The period and offset of sync and peer delay measurement messages can be specified by parameters (:par:`syncInterval`, :par:`pDelayInterval`, 
:par:`syncInitialOffset`, :par:`pDelayInitialOffset`).

.. **TODO** multiple domains

For more information on the parameters of the :ned:`Gptp` module, check the NED documentation.

.. **what does it do/how does it work**

   .. In reality, the master clock is selected by the Best Master Clock algorithm. This algorithm is not available in the INET gPTP
      implementation, so the master clocks and the tree is selected manually.

   **inet implementation**

   - implemented as apps (sync modules)
   - clocks and multiclocks
   - parameters of gptp (some of them)

   so

   - two distinct mechanisms: peer delay measurement and time sync
   - peer delay measurement: initiated by slave nodes and bridge nodes up-tree
   - pDelayReq -> pDelayResp -> pDelayFollowUp
   - time sync: initiated by master clock, forwarded down-tree by bridge nodes (its updated to use the bridge node's time)
   - gptpSync -> gptpFollowUp

The Model
---------

In this showcase, we demonstrate the setup and operation of gPTP in three simulations:

- **One Master Clock**: Simple setup with one time domain and one master clock.
- **Primary and Hot-Standby Master Clocks**: More complex setup with two time domains for a primary and a hot-standby master clock. If the primary master node goes offline,
  the stand-by clock can continue time synchronization.
- **Two Master Clocks Exploiting Network Redundancy**: Two master nodes with two master clocks each. Time synchronization is protected against the failure of any link in the network.

.. Here is the ``General`` configuration:

.. Here is part of the configuration shared by all three simulations:

In the ``General`` configuration, we enable :ned:`Gptp` modules in all network nodes, and configure a random, constant clock drift for all clocks in the network.

.. literalinclude:: ../omnetpp.ini
   :language: ini
   :end-before: OneMasterClock

We detail each simulation in the following sections.

One Master Clock
----------------

In this configuration the network topology is a simple tree. The network contains
one master clock, one bridge and two end stations (:ned:`TsnDevice`), connected via
an :ned:`EthernetSwitch`:

.. figure:: media/OneMasterClockNetwork.png
   :align: center

We configure the spanning tree by settings the master ports in ``tsnClock`` and ``tsnSwitch``:

.. **TODO** the slave ports dont need to be set? -> no because its set by default in TsnSwitch (and TsnDevice and TsnClock)

.. Here is the configuration:

.. literalinclude:: ../omnetpp.ini
   :language: ini
   :start-at: OneMasterClock
   :end-at: tsnSwitch

.. note:: The slave ports are set to ``eth0`` by default in :ned:`TsnDevice` and :ned:`TsnSwitch`.

**TODO** video

.. Here are the results for one master clock:

We examine clock drift of all clocks by plotting the clock time difference to simulation time:

.. figure:: media/OneMasterClock.png
   :align: center

The difference increases for the master clock, and the other clocks are periodically synchronized to that/to the master clock's time (so they keep drfiting together).

Primary and Hot-Standby Master Clocks
-------------------------------------

In this configuration the tree network topology is further extended. The network
contains one primary master clock and one hot-standby master clock. Both master
clocks have their own time synchronization domain and they do their synchronization
separately. The only connection between the two is in the hot-standby master clock
which is also synchronized to the primary master clock. This connection effectively
causes the two time domains to be totally synchronized and allows seamless failover
in the case of the master clock failure.

Here is the network:

.. figure:: media/PrimaryAndHotStandbyNetwork.png
   :align: center

Here is the configuration:

.. literalinclude:: ../omnetpp.ini
   :language: ini
   :start-at: PrimaryAndHotStandbyMasterClocks
   :end-at: clock[1].referenceClock = "tsnClock2.clock

.. Here are the results for the primary and hot standby clocks:

   .. figure:: media/PrimaryAndHotStandby2.svg
      :align: center

   Here is the clock time difference to simulation time plot (displaying the active clock in case there are multiple clocks in a node):

   .. figure:: media/PrimaryAndHotStandby.png
      :align: center

Instead of plotting clock drift for all clocks in one chart, let's use three charts so they are easier to understand. Here is the 
clock drift (clock time differencet to simulation time) of the two `master clocks`:

.. figure:: media/PrimaryAndHotStandBy_masterclocks.png
   :align: center

The two master clocks have a different drift rate, and the hot-standby master clock is periodically synchronized to the primary.

Here is the clock drift of all clocks in `time domain 0` (primary master):

.. figure:: media/PrimaryAndHotStandBy_timedomain0.png
   :align: center

The each clock has a different drift rate, but they are periodically synchronized to the primary master clock.

Let's see the clock drift for all clocks in `time domain 1` (hot stand-by master):

.. figure:: media/PrimaryAndHotStandBy_timedomain1.png
   :align: center

The clocks has different drift rates, and they are periodically synchronized to the hot stand-by master clock (which itself drifts from the primary master,
and gets synchronized periodically; displayed with the think line).

.. And here are the time domains of the primary and hot standby clocks:

   .. figure:: media/TimeDomainsPrimaryAndHotStandby2.svg
      :align: center

Two Master Clocks Exploiting Network Redundancy
-----------------------------------------------

In this configuration the network topology is a ring. Each of the primary master
clock and the hot-standby master clock has two separate time domains. One time
domain uses the clockwise and another one uses the counter-clockwise direction
in the ring topology to disseminate the clock time in the network. This approach
provides protection against a single link failure in the ring topology because
all bridges can be reached in both directions by one of the time synchronization
domains of both master clocks.

Here is the network:

.. figure:: media/TwoMasterClocksNetwork.png
   :align: center

Here is the configuration:

.. literalinclude:: ../omnetpp.ini
   :language: ini
   :start-at: TwoMasterClocksExploitingNetworkRedundancy

Here are the results for two master clocks:

.. figure:: media/TwoMasterClocks.svg
   :align: center
   :width: 100%

And here are the time domains of the two master clocks:

.. figure:: media/TimeDomainsTwoMasterClocks.svg
   :align: center
   :width: 100%

.. Results
   -------

   The following video shows the behavior in Qtenv:

   .. video:: media/behavior.mp4
      :align: center
      :width: 90%

   Here are the simulation results:

   .. .. figure:: media/results.png
      :align: center
      :width: 100%


Sources: :download:`omnetpp.ini <../omnetpp.ini>`, :download:`GptpShowcase.ned <../GptpShowcase.ned>`

Discussion
----------

Use `this <https://github.com/inet-framework/inet/discussions/TODO>`__ page in the GitHub issue tracker for commenting on this showcase.

