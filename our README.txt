CSE 320 — Node-Style Congestion Control Template

GROUP INFORMATION:
- Student 1: Abdurrahman Arda Demir, ID: 20250808628
- Student 2: Rasul Can Çulğatay, ID: 20250808602

ALGORITHM SELECTION:
According to the assignment formula: (20250808628 + 20250808602) % 3 = 2
Assigned Algorithm: TCP NewReno

IMPLEMENTATION DETAILS:
We successfully implemented TCP NewReno by modifying the provided node.c template. We added a 'recover_ack' variable to the TCPState struct to track the highest sequence number sent before packet loss. During the FAST_RECOVERY state, our code evaluates incoming ACKs against this target to successfully identify "Partial ACKs" and inflate the congestion window, preventing premature exit from Fast Recovery. 

The executable also retains the original Tahoe and Reno logic for comparison during our video demonstration.

HOW TO COMPILE:
Using Clang or GCC on Windows:
clang node.c -o node.exe
(or)
gcc node.c -o node.exe

HOW TO RUN:
Run format: node <config_file> <algorithm> <event_file>

Examples to test NewReno:
node A.conf newreno events/duplicate_ack_test.txt
node B.conf newreno events/timeout_test.txt
node C.conf newreno events/example_events.txt

Examples to compare against baseline Reno:
node A.conf reno events/duplicate_ack_test.txt

Example A.conf:

A 5001
B 127.0.0.1 5002 4
C 127.0.0.1 5003 7
D 127.0.0.1 5004 13
F 127.0.0.1 5006 5

Event file format:

ACK <number>
DUPACK <number>
TIMEOUT

Example:

ACK 1
ACK 2
ACK 3
DUPACK 3
DUPACK 3
DUPACK 3
ACK 4
TIMEOUT

What should be visible in console?

The program prints:
- node ID
- node port
- neighbors
- selected congestion control algorithm
- ACK events
- duplicate ACK events
- timeout events
- cwnd changes
- ssthresh changes
- current congestion-control state

Expected style of output:

Node A listening on port 5001
Neighbors:
  B 127.0.0.1 5002 cost=4
  C 127.0.0.1 5003 cost=7

Selected congestion control algorithm: TCP Reno
Initial cwnd=1.00, ssthresh=16.00

Round | Event      | ACK    | cwnd       | ssthresh   | State                  | Explanation
------------------------------------------------------------------------------------------------
1     | ACK        | ACK=1   | cwnd=2.00  | ssthresh=16.00 | Slow Start            | new ACK received; cwnd increased
2     | ACK        | ACK=2   | cwnd=3.00  | ssthresh=16.00 | Slow Start            | new ACK received; cwnd increased
3     | DUPACK     | ACK=3   | cwnd=3.00  | ssthresh=16.00 | Slow Start            | duplicate ACK received; waiting
4     | DUPACK     | ACK=3   | cwnd=3.00  | ssthresh=16.00 | Slow Start            | duplicate ACK received; waiting
5     | DUPACK     | ACK=3   | cwnd=4.50  | ssthresh=1.50  | Fast Recovery         | 3 duplicate ACKs; Reno Fast Recovery

Important:
- Tahoe must reset cwnd to 1 after three duplicate ACKs.
- Reno must enter Fast Recovery after three duplicate ACKs.
- NewReno must behave like Reno at first, but students should improve partial ACK behavior.
