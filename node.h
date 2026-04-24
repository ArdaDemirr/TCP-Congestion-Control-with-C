#ifndef NODE_H
#define NODE_H

#define MAX_NEIGHBORS 16
#define MAX_LINE 256


// algorithm enums
typedef enum {
    ALG_TAHOE = 0,
    ALG_RENO,
    ALG_NEWRENO,
    ALG_UNKNOWN
} Algorithm;

// congestion control state enums
typedef enum {
    SLOW_START = 0,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY
} CCState;

// neighbor struct
typedef struct {
    char id;
    char ip[64];
    int port;
    int cost;
} Neighbor;

// node config struct
typedef struct {
    char node_id;
    int port;
    Neighbor neighbors[MAX_NEIGHBORS];
    int neighbor_count;
} NodeConfig;

// tcp state struct
typedef struct {
    Algorithm algorithm;
    CCState state;
    double cwnd;
    double ssthresh;
    int dup_ack_count;
    int round;
} TCPState;

// load config function
int load_config(const char *filename, NodeConfig *config);

// parse algorithm function
Algorithm parse_algorithm(const char *name);

// algorithm name function
const char *algorithm_name(Algorithm algorithm);

// state name function
const char *state_name(CCState state);

// init tcp function
void init_tcp(TCPState *tcp, Algorithm algorithm);

// on ack function
void on_ack(TCPState *tcp, int ack_no);

// on duplicate ack function
void on_duplicate_ack(TCPState *tcp, int ack_no);

// on timeout function
void on_timeout(TCPState *tcp);

#endif
