#ifndef NODE_H
#define NODE_H

/* Maximum number of neighbors a node can have */
#define MAX_NEIGHBORS 16
/* Maximum length of a line when reading config or topology files */
#define MAX_LINE 256
/* Maximum number of nodes in the network topology (A-Z) */
#define MAX_NODES 26
/* Maximum number of edges (links) in the network topology */
#define MAX_EDGES 64
/* Buffer size for sending and receiving UDP messages */
#define MSG_BUF 1024

/* ========== Platform Abstraction ========== */
/* This section provides platform-independent socket and thread definitions,
   allowing the code to compile on both Windows (Winsock2) and Linux/macOS (POSIX). */
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    typedef SOCKET socket_t;
    #define CLOSE_SOCKET closesocket
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <pthread.h>
    typedef int socket_t;
    #define CLOSE_SOCKET close
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

/* ========== Congestion Control Enums ========== */

/* Supported TCP Congestion Control Algorithms */
typedef enum {
    ALG_TAHOE = 0,   /* TCP Tahoe: resets to slow start on 3 dup ACKs */
    ALG_RENO,        /* TCP Reno: enters fast recovery on 3 dup ACKs */
    ALG_NEWRENO,     /* TCP NewReno: stays in fast recovery on partial ACKs */
    ALG_UNKNOWN      /* Invalid algorithm fallback */
} Algorithm;

/* Congestion Control State Machine Phases */
typedef enum {
    SLOW_START = 0,          /* Exponential growth phase */
    CONGESTION_AVOIDANCE,    /* Additive increase phase */
    FAST_RECOVERY            /* Fast recovery after 3 duplicate ACKs */
} CCState;

/* ========== Network Topology Structs ========== */

/* Represents a direct neighbor of this node */
typedef struct {
    char id;        /* Node ID of the neighbor (e.g., 'B') */
    char ip[64];    /* IP address of the neighbor */
    int port;       /* Port number the neighbor is listening on */
    int cost;       /* Link cost to reach this neighbor */
} Neighbor;

/* Represents the configuration of this node */
typedef struct {
    char node_id;                      /* ID of this node (e.g., 'A') */
    int port;                          /* Port this node listens on */
    Neighbor neighbors[MAX_NEIGHBORS]; /* Array of direct neighbors */
    int neighbor_count;                /* Number of direct neighbors */
} NodeConfig;

/* Represents the state of the TCP congestion control algorithm */
typedef struct {
    Algorithm algorithm;   /* The selected algorithm (Tahoe, Reno, NewReno) */
    CCState state;         /* Current state (Slow Start, Cong. Avoidance, Fast Recovery) */
    double cwnd;           /* Congestion window size */
    double ssthresh;       /* Slow start threshold */
    int dup_ack_count;     /* Number of consecutive duplicate ACKs received */
    int round;             /* Current event round counter */
    int recover_ack;       /* For NewReno: the ACK sequence number that exits Fast Recovery */
} TCPState;

/* ========== Routing Structs ========== */

/* Represents a single undirected link (edge) in the global topology */
typedef struct {
    char node_a;    /* First node of the link */
    char node_b;    /* Second node of the link */
    int cost;       /* Cost of the link */
} Edge;

/* Represents an entry in the computed routing table */
typedef struct {
    char dest;      /* Destination node ID */
    char next_hop;  /* Next hop node ID to reach the destination */
    int cost;       /* Total shortest-path cost to the destination */
    char path[256]; /* String representation of the full path */
} RoutingEntry;

/* Context structure passed to the UDP receiver background thread */
typedef struct {
    socket_t sock;         /* UDP socket to listen on */
    NodeConfig *config;    /* Pointer to the node's configuration */
    RoutingEntry *routes;  /* Pointer to the node's routing table */
    int route_count;       /* Number of entries in the routing table */
    volatile int running;  /* Flag to control thread termination (1=run, 0=stop) */
} NetContext;

/* ========== Function Declarations ========== */

/* Congestion control and state management */
int load_config(const char *filename, NodeConfig *config);
Algorithm parse_algorithm(const char *name);
const char *algorithm_name(Algorithm algorithm);
const char *state_name(CCState state);
void init_tcp(TCPState *tcp, Algorithm algorithm);

/* Event handlers for TCP congestion control simulation */
void on_ack(TCPState *tcp, int ack_no);
void on_duplicate_ack(TCPState *tcp, int ack_no);
void on_timeout(TCPState *tcp);

/* Routing functions for building shortest-path routes via Dijkstra */
int load_topology(const char *filename, Edge *edges, int *edge_count);
int discover_nodes(Edge *edges, int edge_count, char *nodes, int *node_count);
void build_routing_table(char source, Edge *edges, int edge_count,
                         char *nodes, int node_count,
                         RoutingEntry *table, int *route_count);
void print_routing_table(RoutingEntry *table, int route_count);
RoutingEntry *find_route(char dest, RoutingEntry *table, int route_count);

/* Networking functions for sending and receiving messages */
int find_neighbor(NodeConfig *config, char id, Neighbor **out);
void send_udp(socket_t sock, const char *ip, int port, const char *msg);

#endif
