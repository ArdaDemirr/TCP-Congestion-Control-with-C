#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "node.h"

/* ================================================================
   CONGESTION CONTROL FUNCTIONS (preserved from original)
   ================================================================ */

Algorithm parse_algorithm(const char *name) {
    if (strcmp(name, "tahoe") == 0) return ALG_TAHOE;
    if (strcmp(name, "reno") == 0) return ALG_RENO;
    if (strcmp(name, "newreno") == 0) return ALG_NEWRENO;
    return ALG_UNKNOWN;
}

const char *algorithm_name(Algorithm algorithm) {
    switch (algorithm) {
        case ALG_TAHOE: return "TCP Tahoe";
        case ALG_RENO: return "TCP Reno";
        case ALG_NEWRENO: return "TCP NewReno";
        default: return "Unknown";
    }
}

const char *state_name(CCState state) {
    switch (state) {
        case SLOW_START: return "Slow Start";
        case CONGESTION_AVOIDANCE: return "Congestion Avoidance";
        case FAST_RECOVERY: return "Fast Recovery";
        default: return "Unknown";
    }
}

int load_config(const char *filename, NodeConfig *config) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return 0;
    char line[MAX_LINE];
    config->neighbor_count = 0;

    /*
       First non-comment line:
       NodeID Port

       Example:
       A 5001
    */
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, " %c %d", &config->node_id, &config->port) == 2) break;
    }

    /*
       Remaining non-comment lines:
       NeighborID IP Port Cost

       Example:
       B 127.0.0.1 5002 4
    */
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (config->neighbor_count >= MAX_NEIGHBORS) { fclose(fp); return 0; }
        Neighbor *n = &config->neighbors[config->neighbor_count];
        if (sscanf(line, " %c %63s %d %d", &n->id, n->ip, &n->port, &n->cost) == 4)
            config->neighbor_count++;
    }
    fclose(fp);
    return 1;
}

/*
   Initialize TCP state

   it always begins in the Slow Start phase. We set the initial congestion window ($cwnd$) to 1.0 packet,
   and the slow-start threshold ($ssthresh$) to 16.0. Our duplicate ACK counter is at zero
*/
void init_tcp(TCPState *tcp, Algorithm algorithm) {
    tcp->algorithm = algorithm;
    tcp->state = SLOW_START;
    tcp->cwnd = 1.0;
    tcp->ssthresh = 16.0;
    tcp->dup_ack_count = 0;
    tcp->round = 0;
    tcp->recover_ack = 0; // for newReno, it is the value of the first ACK received after the timeout
}

static void print_tcp_status(TCPState *tcp, const char *event, int ack_no, const char *note) {
    printf("%5d | %-10s | ACK=%-3d | cwnd=%5.2f | ssthresh=%5.2f | %-22s | %s\n",
           tcp->round, event, ack_no, tcp->cwnd, tcp->ssthresh,
           state_name(tcp->state), note);
}

/*
   Handle ACK event

   - Slow Start:
     cwnd increases by 1 for each new ACK
     When cwnd >= ssthresh, switch to Congestion Avoidance

   - Congestion Avoidance:
     cwnd increases by 1/cwnd for each new ACK (additive increase)

   - Fast Recovery:
     Reno: full ACK exits Fast Recovery, cwnd = ssthresh

*/
void on_ack(TCPState *tcp, int ack_no) {
    tcp->round++;
    tcp->dup_ack_count = 0;

    if (tcp->state == SLOW_START) {
        tcp->cwnd += 1.0;
        if (tcp->cwnd >= tcp->ssthresh) tcp->state = CONGESTION_AVOIDANCE;
        print_tcp_status(tcp, "ACK", ack_no, "new ACK received; cwnd increased");
    }
    else if (tcp->state == CONGESTION_AVOIDANCE) {
        tcp->cwnd += 1.0 / tcp->cwnd;
        print_tcp_status(tcp, "ACK", ack_no, "new ACK received; additive increase");
    }

    /*
    In Fast Recovery, we are already in a "semi-congested" state, so we don't want to reduce the window
    aggressively. Instead, we keep the window at $ssthresh + 3.0$ and wait for the next ACK.
    If we receive a full ACK (one that acknowledges all packets up to the one that was lost),
    it means the congestion has cleared. At this point, we exit Fast Recovery and return to Congestion Avoidance.
    */
    else if (tcp->state == FAST_RECOVERY) {
        /*
           Reno:
           A full ACK exits Fast Recovery.

           NewReno:
           Students may extend this part:
           - partial ACK: retransmit next missing segment and stay in Fast Recovery
           - full ACK: exit Fast Recovery
        */
        if (tcp->algorithm == ALG_NEWRENO && ack_no < tcp->recover_ack) {
            tcp->cwnd += 1.0;
            print_tcp_status(tcp, "ACK", ack_no, "partial ACK; stay in Fast Recovery");
        } else {
            tcp->cwnd = tcp->ssthresh;
            tcp->state = CONGESTION_AVOIDANCE;
            print_tcp_status(tcp, "ACK", ack_no, "full ACK; exit Fast Recovery");
        }
    }
}

/*
   Handle duplicate ACK event

   - Three duplicate ACKs indicate likely packet loss.
   - Reno: sets cwnd to ssthresh + 3 and enters Fast Recovery

   If we get 1 or 2 duplicate ACKs, we do nothing and wait. But if we receive exactly 3 duplicate ACKs,
   TCP Reno knows a packet was dropped. First, it cuts the threshold in half ($ssthresh = \frac{cwnd}{2}$). Then,
   instead of dropping the window back to 1 like Tahoe, Reno sets $cwnd = ssthresh + 3.0$ and enters Fast Recovery.
   This makes Reno much faster at recovering from single packet losses
*/
void on_duplicate_ack(TCPState *tcp, int ack_no) {
    tcp->round++;
    tcp->dup_ack_count++;

    if (tcp->dup_ack_count < 3) {
        print_tcp_status(tcp, "DUPACK", ack_no, "duplicate ACK received; waiting");
        return; // wait for more duplicate ACKs
    }

    /*
       Three duplicate ACKs indicate likely packet loss.
    */
    tcp->ssthresh = tcp->cwnd / 2.0;
    if (tcp->ssthresh < 2.0) tcp->ssthresh = 2.0;


    if (tcp->algorithm == ALG_TAHOE) {
        tcp->cwnd = 1.0;
        tcp->state = SLOW_START;
        print_tcp_status(tcp, "DUPACK", ack_no, "3 duplicate ACKs; Tahoe resets cwnd");
    }
    else if (tcp->algorithm == ALG_RENO) {
        tcp->cwnd = tcp->ssthresh + 3.0;
        tcp->state = FAST_RECOVERY;
        print_tcp_status(tcp, "DUPACK", ack_no, "3 duplicate ACKs; Reno Fast Recovery");
    }
    else if (tcp->algorithm == ALG_NEWRENO) {
        // Estimate highest sent packet based on current window
        tcp->recover_ack = ack_no + (int)tcp->cwnd;
        tcp->cwnd = tcp->ssthresh + 3.0;
        tcp->state = FAST_RECOVERY;
        print_tcp_status(tcp, "DUPACK", ack_no, "3 duplicate ACKs; NewReno Fast Recovery");
    }
}

/*
   Handle timeout event

   - Reno reset cwnd to 1 and enter Slow Start
   - ssthresh is set to half of the previous cwnd

   If a severe network traffic jam happens and we get a Timeout, 
   it cuts the threshold in half, drops the window all the way down to 1.0,
   and restarts the entire process from Slow Start.
*/
void on_timeout(TCPState *tcp) {
    tcp->round++;

    tcp->ssthresh = tcp->cwnd / 2.0;
    if (tcp->ssthresh < 2.0) tcp->ssthresh = 2.0;

    tcp->cwnd = 1.0;
    tcp->dup_ack_count = 0;
    tcp->state = SLOW_START;

    print_tcp_status(tcp, "TIMEOUT", 0, "timeout; cwnd reset to 1");
}

static void print_config(NodeConfig *config) {
    int i;
    printf("Node %c listening on port %d\n", config->node_id, config->port);
    printf("Neighbors:\n");
    for (i = 0; i < config->neighbor_count; i++)
        printf("  %c %s %d cost=%d\n", config->neighbors[i].id,
               config->neighbors[i].ip, config->neighbors[i].port,
               config->neighbors[i].cost);
}

static int run_event_file(const char *event_file, TCPState *tcp) {
    FILE *fp = fopen(event_file, "r");
    if (!fp) { printf("Could not open event file: %s\n", event_file); return 0; }
    char line[MAX_LINE], event[32]; int ack_no;
    printf("\nCongestion-control trace\n");
    printf("Round | Event      | ACK    | cwnd       | ssthresh   | State                  | Explanation\n");
    printf("------------------------------------------------------------------------------------------------\n");
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "%31s", event) != 1) continue;
        if (strcmp(event, "ACK") == 0) {
            if (sscanf(line + 3, "%d", &ack_no) != 1) continue;
            on_ack(tcp, ack_no);
        } else if (strcmp(event, "DUPACK") == 0) {
            if (sscanf(line + 6, "%d", &ack_no) != 1) continue;
            on_duplicate_ack(tcp, ack_no);
        } else if (strcmp(event, "TIMEOUT") == 0) {
            on_timeout(tcp);
        }
    }
    fclose(fp);
    return 1;
}

/* ================================================================
   ROUTING FUNCTIONS (Dijkstra shortest path)
   ================================================================ */

/*
   Loads the global network topology from a file.
   The file should contain lines in the format: NodeA NodeB Cost
   Returns 1 on success, 0 on failure.
*/

int load_topology(const char *filename, Edge *edges, int *edge_count) {
    FILE *fp = fopen(filename, "r");
    if (!fp) return 0;
    char line[MAX_LINE];
    *edge_count = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char a, b; int c;
        if (sscanf(line, " %c %c %d", &a, &b, &c) == 3 && *edge_count < MAX_EDGES) {
            edges[*edge_count].node_a = a;
            edges[*edge_count].node_b = b;
            edges[*edge_count].cost = c;
            (*edge_count)++;
        }
    }
    fclose(fp);
    return 1;
}

/*
   Discovers all unique nodes present in the loaded edges.
   Populates the nodes array and updates the node_count.
   Returns the number of unique nodes found.
*/
int discover_nodes(Edge *edges, int edge_count, char *nodes, int *node_count) {
    int present[26] = {0};
    int i;
    for (i = 0; i < edge_count; i++) {
        present[edges[i].node_a - 'A'] = 1;
        present[edges[i].node_b - 'A'] = 1;
    }
    *node_count = 0;
    for (i = 0; i < 26; i++) {
        if (present[i]) nodes[(*node_count)++] = (char)('A' + i);
    }
    return *node_count;
}

/*
   Implements Dijkstra's Shortest Path Algorithm to compute routes.
   It calculates the shortest path from the 'source' node to all other nodes
   and populates the routing table with the destination, next hop, cost, and path string.
*/
void build_routing_table(char source, Edge *edges, int edge_count,
                         char *nodes, int node_count,
                         RoutingEntry *table, int *route_count) {
    int dist[26], prev[26], visited[26];
    int i, j, u, src = source - 'A';
    
    /* Initialize distances to infinity (999999) and visited to 0 */
    for (i = 0; i < 26; i++) { dist[i] = 999999; prev[i] = -1; visited[i] = 0; }
    dist[src] = 0;

    /* Find shortest paths to all nodes */
    for (i = 0; i < node_count; i++) {
        /* Pick the minimum distance node from the set of unvisited nodes */
        u = -1;
        for (j = 0; j < 26; j++)
            if (!visited[j] && dist[j] < (u == -1 ? 999999 : dist[u])) u = j;
        if (u == -1) break; /* No reachable unvisited nodes left */
        visited[u] = 1;
        
        /* Update distance value of adjacent nodes of the picked node */
        for (j = 0; j < edge_count; j++) {
            int a = edges[j].node_a - 'A', b = edges[j].node_b - 'A', c = edges[j].cost;
            /* If the current node is 'a', check neighbors 'b', and vice versa */
            if (a == u && !visited[b] && dist[u] + c < dist[b]) { dist[b] = dist[u] + c; prev[b] = u; }
            if (b == u && !visited[a] && dist[u] + c < dist[a]) { dist[a] = dist[u] + c; prev[a] = u; }
        }
    }

    /* Build the final routing table entries from the computed paths */
    *route_count = 0;
    for (i = 0; i < node_count; i++) {
        int idx = nodes[i] - 'A';
        RoutingEntry *r = &table[*route_count];
        r->dest = nodes[i];
        r->cost = dist[idx];
        if (idx == src) {
            r->next_hop = '-';
            sprintf(r->path, "%c", source);
        } else if (dist[idx] >= 999999) {
            r->next_hop = '?';
            sprintf(r->path, "unreachable");
        } else {
            /* Trace back the path from destination to source to find the next hop */
            int cur = idx;
            while (prev[cur] != src && prev[cur] != -1) cur = prev[cur];
            r->next_hop = (char)(cur + 'A');
            
            /* Build the full path string by tracing backwards then reversing */
            char pn[26]; int pl = 0, n = idx;
            while (n != -1) { pn[pl++] = (char)(n + 'A'); n = prev[n]; }
            r->path[0] = '\0';
            for (j = pl - 1; j >= 0; j--) {
                char tmp[8];
                sprintf(tmp, j == pl - 1 ? "%c" : " -> %c", pn[j]);
                strcat(r->path, tmp);
            }
        }
        (*route_count)++;
    }
}

/*
   Prints the calculated routing table in a formatted way.
*/
void print_routing_table(RoutingEntry *table, int route_count) {
    int i;
    printf("\n%-12s %-10s %-6s %s\n", "Destination", "Next Hop", "Cost", "Path");
    printf("------------------------------------------------\n");
    for (i = 0; i < route_count; i++)
        printf("%-12c %-10c %-6d %s\n", table[i].dest, table[i].next_hop,
               table[i].cost, table[i].path);
    printf("\n");
}

/*
   Finds a route to a specific destination node in the routing table.
   Returns a pointer to the routing entry or NULL if not found.
*/
RoutingEntry *find_route(char dest, RoutingEntry *table, int route_count) {
    int i;
    for (i = 0; i < route_count; i++)
        if (table[i].dest == dest) return &table[i];
    return NULL;
}

/*
   Finds a direct neighbor in the node's configuration.
   Returns 1 and sets 'out' if found, returns 0 otherwise.
*/
int find_neighbor(NodeConfig *config, char id, Neighbor **out) {
    int i;
    for (i = 0; i < config->neighbor_count; i++) {
        if (config->neighbors[i].id == id) { *out = &config->neighbors[i]; return 1; }
    }
    return 0;
}

/* ================================================================
   NETWORKING FUNCTIONS (UDP sockets)
   ================================================================ */

/*
   Sends a string message over UDP to the specified IP address and port.
*/
void send_udp(socket_t sock, const char *ip, int port, const char *msg) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr(ip);
    sendto(sock, msg, (int)strlen(msg), 0, (struct sockaddr *)&addr, sizeof(addr));
}

/*
   Handles forwarding a message that is destined for another node.
   Uses the routing table to find the next hop and sends the message via UDP.
*/
static void forward_message(socket_t sock, NodeConfig *config,
                             RoutingEntry *routes, int route_count,
                             char src, char dest, const char *payload) {
    RoutingEntry *r = find_route(dest, routes, route_count);
    if (!r || r->next_hop == '?' || r->next_hop == '-') {
        printf("[%c] No route to %c\n", config->node_id, dest);
        return;
    }
    Neighbor *nb = NULL;
    if (!find_neighbor(config, r->next_hop, &nb)) {
        printf("[%c] Next hop %c is not a direct neighbor\n", config->node_id, r->next_hop);
        return;
    }
    printf("[%c] Forwarding message from %c to %c, next hop %c\n",
           config->node_id, src, dest, r->next_hop);
    char buf[MSG_BUF];
    sprintf(buf, "MSG|%c|%c|%s", src, dest, payload);
    send_udp(sock, nb->ip, nb->port, buf);
}

/*
   Processes an incoming UDP message buffer.
   If the message is for this node, it displays it.
   If it is meant for someone else, it initiates forwarding.
*/
static void handle_incoming(socket_t sock, NodeConfig *config,
                             RoutingEntry *routes, int route_count,
                             const char *buffer) {
    char src, dest, payload[512];
    if (sscanf(buffer, "MSG|%c|%c|%511[^\n]", &src, &dest, payload) != 3) return;
    if (dest == config->node_id) {
        printf("\n[%c] Received message from %c: %s\n> ", config->node_id, src, payload);
        fflush(stdout);
    } else {
        printf("\n");
        forward_message(sock, config, routes, route_count, src, dest, payload);
        printf("> ");
        fflush(stdout);
    }
}

/*
   Background thread function that continuously listens for incoming UDP messages.
   Calls handle_incoming for every received packet.
*/
/* Receiver thread function */
#ifdef _WIN32
static DWORD WINAPI recv_thread(LPVOID param) {
#else
static void *recv_thread(void *param) {
#endif
    NetContext *ctx = (NetContext *)param;
    char buf[MSG_BUF];
    struct sockaddr_in from_addr;
    while (ctx->running) {
#ifdef _WIN32
        int from_len = sizeof(from_addr);
#else
        socklen_t from_len = sizeof(from_addr);
#endif
        int n = recvfrom(ctx->sock, buf, sizeof(buf) - 1, 0,
                         (struct sockaddr *)&from_addr, &from_len);
        if (n > 0) {
            buf[n] = '\0';
            handle_incoming(ctx->sock, ctx->config, ctx->routes, ctx->route_count, buf);
        }
    }
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/* ================================================================
   MAIN
   ================================================================ */

/*
   The main entry point for the node.
   Handles command-line arguments to determine whether to run in
   simulation mode (reading events from a file) or interactive
   network mode (running a UDP server and reading from stdin).
*/
int main(int argc, char *argv[]) {
    NodeConfig config;
    TCPState tcp;

    /*
       Usage:
       ./node <config> <algorithm>                -> interactive network mode
       ./node <config> <algorithm> <event_file>   -> simulation-only mode
    */
    if (argc < 3 || argc > 4) {
        printf("Usage:\n");
        printf("  node <config> <algorithm>              (interactive network mode)\n");
        printf("  node <config> <algorithm> <event_file> (simulation mode)\n");
        printf("Algorithms: tahoe, reno, newreno\n");
        return 1;
    }

    Algorithm selected = parse_algorithm(argv[2]);
    if (selected == ALG_UNKNOWN) { printf("Invalid algorithm.\n"); return 1; }
    if (!load_config(argv[1], &config)) { printf("Cannot load %s\n", argv[1]); return 1; }

    print_config(&config);
    init_tcp(&tcp, selected);
    printf("\nSelected congestion control algorithm: %s\n", algorithm_name(selected));
    printf("Initial cwnd=%.2f, ssthresh=%.2f\n", tcp.cwnd, tcp.ssthresh);

    /* --- Simulation-only mode (original behavior) --- */
    if (argc == 4) {
        run_event_file(argv[3], &tcp);
        return 0;
    }

    /* --- Interactive network mode --- */
    Edge edges[MAX_EDGES];
    int edge_count = 0;
    char nodes[MAX_NODES];
    int node_count = 0;
    RoutingEntry routes[MAX_NODES];
    int route_count = 0;

    if (!load_topology("topology.conf", edges, &edge_count)) {
        printf("Cannot load topology.conf\n"); return 1;
    }
    discover_nodes(edges, edge_count, nodes, &node_count);
    build_routing_table(config.node_id, edges, edge_count, nodes, node_count,
                        routes, &route_count);
    print_routing_table(routes, route_count);

    /* Socket setup */
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { printf("WSAStartup failed\n"); return 1; }
#endif
    socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == INVALID_SOCKET) { printf("Socket creation failed\n"); return 1; }

    struct sockaddr_in my_addr;
    memset(&my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons((unsigned short)config.port);
    if (bind(sock, (struct sockaddr *)&my_addr, sizeof(my_addr)) == SOCKET_ERROR) {
        printf("Bind failed on port %d\n", config.port);
        CLOSE_SOCKET(sock);
        return 1;
    }

    /* Start receiver thread */
    NetContext ctx;
    ctx.sock = sock;
    ctx.config = &config;
    ctx.routes = routes;
    ctx.route_count = route_count;
    ctx.running = 1;

#ifdef _WIN32
    HANDLE hThread = CreateThread(NULL, 0, recv_thread, &ctx, 0, NULL);
#else
    pthread_t tid;
    pthread_create(&tid, NULL, recv_thread, &ctx);
#endif

    /* Interactive console */
    printf("[%c] Node ready. Commands:\n", config.node_id);
    printf("  send <dest> <message>  - send message to destination node\n");
    printf("  run <event_file>       - run congestion control simulation\n");
    printf("  table                  - show routing table\n");
    printf("  quit                   - exit\n\n> ");
    fflush(stdout);

    char cmd[512];
    while (fgets(cmd, sizeof(cmd), stdin)) {
        cmd[strcspn(cmd, "\r\n")] = '\0';
        if (cmd[0] == '\0') { printf("> "); fflush(stdout); continue; }

        if (strncmp(cmd, "send ", 5) == 0) {
            char dest_c; char msg[400];
            if (sscanf(cmd + 5, " %c %399[^\n]", &dest_c, msg) == 2) {
                RoutingEntry *r = find_route(dest_c, routes, route_count);
                if (!r || r->next_hop == '?') {
                    printf("[%c] No route to %c\n", config.node_id, dest_c);
                } else if (dest_c == config.node_id) {
                    printf("[%c] Received message from %c: %s\n", config.node_id, config.node_id, msg);
                } else {
                    printf("[%c] Destination %c, next hop %c\n", config.node_id, dest_c, r->next_hop);
                    Neighbor *nb = NULL;
                    if (find_neighbor(&config, r->next_hop, &nb)) {
                        char buf[MSG_BUF];
                        sprintf(buf, "MSG|%c|%c|%s", config.node_id, dest_c, msg);
                        send_udp(sock, nb->ip, nb->port, buf);
                    }
                }
            } else {
                printf("Usage: send <dest> <message>\n");
            }
        }
        else if (strncmp(cmd, "run ", 4) == 0) {
            init_tcp(&tcp, selected);
            run_event_file(cmd + 4, &tcp);
        }
        else if (strcmp(cmd, "table") == 0) {
            print_routing_table(routes, route_count);
        }
        else if (strcmp(cmd, "quit") == 0) {
            break;
        }
        else {
            printf("Unknown command: %s\n", cmd);
        }
        printf("> ");
        fflush(stdout);
    }

    ctx.running = 0;
    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WaitForSingleObject(hThread, 1000);
    WSACleanup();
#else
    pthread_cancel(tid);
    pthread_join(tid, NULL);
#endif
    printf("[%c] Node stopped.\n", config.node_id);
    return 0;
}
