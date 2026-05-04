CSE 320 — TCP Congestion Control Algorithms Comparison

GROUP INFORMATION:
- Student 1: Abdurrahman Arda Demir, ID: 20250808628
- Student 2: Rasul Can Çulğatay, ID: 20250808602

ALGORITHM SELECTION:
According to the assignment formula: (20250808628 + 20250808602) % 3 = 2
Assigned Algorithm: TCP NewReno

================================================================================
IMPLEMENTATION DETAILS
================================================================================

We implemented TCP NewReno with full support for:
  - Slow Start: cwnd increases by 1 per ACK until reaching ssthresh
  - Congestion Avoidance: cwnd increases by 1/cwnd per ACK (additive increase)
  - Fast Retransmit: triggered on 3 duplicate ACKs
  - Fast Recovery with Partial ACK handling (NewReno improvement):
    A 'recover_ack' variable tracks the highest sequence number sent before
    loss. During Fast Recovery, ACKs below this target are treated as partial
    ACKs (window inflated, stay in FR). Only a full ACK exits Fast Recovery.

The executable also retains the original Tahoe and Reno logic for comparison.

Additionally, we implemented:
  - Dijkstra shortest-path routing from a topology file
  - UDP socket-based multi-node communication
  - Multi-hop message forwarding across 6 nodes
  - Interactive console for sending messages and running simulations

================================================================================
HOW TO COMPILE
================================================================================

Using GCC (MinGW) on Windows:
  gcc node.c -o node.exe -lws2_32

Using Clang on Windows:
  clang node.c -o node.exe -lws2_32

On Linux/macOS:
  gcc node.c -o node -lpthread

================================================================================
HOW TO RUN
================================================================================

The program has TWO modes:

--- Mode 1: Simulation Mode (event file) ---

  node <config_file> <algorithm> <event_file>

  Examples:
    node A.conf newreno events/comparison_test.txt
    node A.conf reno events/duplicate_ack_test.txt
    node A.conf tahoe events/timeout_test.txt

--- Mode 2: Interactive Network Mode (6 nodes in 6 terminals) ---

  node <config_file> <algorithm>

  Open 6 separate terminals and run:
    Terminal 1: node A.conf newreno
    Terminal 2: node B.conf newreno
    Terminal 3: node C.conf newreno
    Terminal 4: node D.conf newreno
    Terminal 5: node E.conf newreno
    Terminal 6: node F.conf newreno

  Interactive commands:
    send <dest> <message>  - send message to destination node
    run <event_file>       - run congestion control simulation
    table                  - show routing table
    quit                   - exit

================================================================================
NETWORK TOPOLOGY
================================================================================

Nodes: A, B, C, D, E, F
Ports: A=5001, B=5002, C=5003, D=5004, E=5005, F=5006

Edges (from topology.conf):
  A-B: 4,  A-C: 7,  A-D: 13,  A-F: 5
  B-D: 8,  B-E: 3,  C-D: 9,   C-E: 12

Routing table for Node A (computed by Dijkstra):
  Dest | Next Hop | Cost | Path
  A    | -        | 0    | A
  B    | B        | 4    | A -> B
  C    | C        | 7    | A -> C
  D    | B        | 12   | A -> B -> D    (NOT direct A->D at cost 13)
  E    | B        | 7    | A -> B -> E
  F    | F        | 5    | A -> F

================================================================================
MESSAGE FORWARDING EXAMPLES
================================================================================

From Node A terminal:
  > send D hello_from_A_to_D

  [A] Destination D, next hop B
  [B] Forwarding message from A to D, next hop D
  [D] Received message from A: hello_from_A_to_D

From Node F terminal:
  > send E hello_from_F_to_E

  [F] Destination E, next hop A
  [A] Forwarding message from F to E, next hop B
  [B] Forwarding message from F to E, next hop E
  [E] Received message from F: hello_from_F_to_E

================================================================================
EVENT FILES
================================================================================

  events/example_events.txt        - Basic test with ACK, DUPACK, TIMEOUT
  events/duplicate_ack_test.txt    - Triple duplicate ACK scenario
  events/timeout_test.txt          - Timeout loss scenario
  events/comparison_test.txt       - Full comparison across all 3 algorithms
  events/newreno_partial_ack_test.txt - NewReno partial ACK advantage demo
  events/long_session_test.txt     - Extended session with multiple losses

================================================================================
ALGORITHM COMPARISON (using comparison_test.txt)
================================================================================

Key difference at triple DUPACK (Round 20):
  Tahoe:   cwnd = 1.00,  state = Slow Start       (full reset)
  Reno:    cwnd = 11.06, state = Fast Recovery     (enters FR)
  NewReno: cwnd = 11.06, state = Fast Recovery     (enters FR)

Key difference at partial ACK (Round 21, ACK 18):
  Tahoe:   cwnd = 2.00,  state = Slow Start       (still recovering slowly)
  Reno:    cwnd = 8.06,  state = Cong. Avoidance   (exits FR immediately)
  NewReno: cwnd = 12.06, state = Fast Recovery     (stays in FR, inflates cwnd)

Summary:
  - Tahoe is most conservative: resets cwnd to 1 on any loss
  - Reno is faster: uses Fast Recovery but exits on first new ACK
  - NewReno is best for multiple losses: stays in Fast Recovery on partial ACKs
