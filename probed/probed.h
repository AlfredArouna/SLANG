#include <sys/socket.h>
#include <netinet/in.h>
#include "msess.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <syslog.h>

#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#define SCM_TIMESTAMPING SO_TIMESTAMPING
#endif

#define APP_AND_VERSION "SLA-NG probed 0.1"
#define TYPE_PING 'i'
#define TYPE_PONG 'o'
#define TYPE_TIME 't'
#define USLEEP 1 /* the read timeout resolution, sets max pps */ 
#define TMPLEN 512
#define DATALEN 48
#define MSESS_NODE_NAME "probe"
#define NUM_CLIENT_RES 2

#define STATE_PING 'i'
#define STATE_GOT_TS 't' /* Because of Intel RX timestamp bug */
#define STATE_GOT_PONG 'o' /* Because of Intel RX timestamp bug */
#define STATE_READY 'r'

/* operation modes */
#define OPMODE_SERVER 's'
#define OPMODE_CLIENT 'c'
#define OPMODE_DAEMON 'd'

extern struct config cfg;

typedef struct timespec ts_t;
typedef struct sockaddr_in6 addr_t;
typedef uint32_t num_t;

struct config {
	char ts; /* timestamping type (u)ser (k)ern (h)w */
};

LIST_HEAD(res_listhead, res) res_head;
struct res {
	ts_t created;
	char state;
	struct in6_addr addr;
	num_t id;
	num_t seq;
	ts_t ts[4];
	LIST_ENTRY(res) res_list;
};

struct packet {
	addr_t addr; 
	char data[DATALEN];
	ts_t ts;
};
typedef struct packet pkt_t;
struct packet_data {
	char type;
	num_t seq;
	num_t id;
	ts_t t2;
	ts_t t3;
};
typedef struct packet_data data_t;

struct packet_rpm {
	int32_t t1_sec;
	int32_t t1_usec;
	int32_t t4_sec;
	int32_t t4_usec;
	int16_t version;
	int16_t magic;
	int32_t reserved;
	int32_t t2_sec;
	int32_t t2_usec;
	int32_t t3_sec;
	int32_t t3_usec;
};
struct scm_timestamping {
	struct timespec systime;
	struct timespec hwtimesys;
	struct timespec hwtimeraw;
};

/*enum TS_TYPES {
	T1,
	T2,
	T3,
	T4
};*/

int main(int argc, char *argv[]);
void help_and_die(void);
void reload(xmlDoc **cfgdoc, char *cfgpath);

void debug(int enabled);
void p(char *str);
void diff_ts (struct timespec *r, struct timespec *end, struct timespec *beg);
void diff_tv (struct timeval *r, struct timeval *end, struct timeval *beg);
int cmp_ts(struct timespec *t1, struct timespec *t2);
int cmp_tv(struct timeval *t1, struct timeval *t2);
int addr2str(addr_t *a, /*@out@*/ char *s);

void bind_or_die(/*@out@*/ int *s_udp, /*@out@*/ int *s_tcp, uint16_t port);
void loop_or_die(int s_udp, int s_tcp, /*@null@*/ char *addr, char *port);
int server_find_peer_fd(int fd_first, int fd_max, addr_t *peer);
pid_t client_fork(int pipe, struct sockaddr_in6 *server);

void tstamp_mode_hardware(int sock, char *iface);
void tstamp_mode_kernel(int sock);
void tstamp_mode_userland(int sock);
int tstamp_extract(struct msghdr *msg, /*@out@*/ ts_t *ts);
int tstamp_fetch_tx(int sock, /*@out@*/ ts_t *ts);

int recv_w_ts(int sock, int flags, /*@out@*/ struct packet *pkt);
int send_w_ts(int sock, addr_t *addr, char *data, /*@out@*/ ts_t *ts);

int config_read(xmlDoc **doc, char *cfgpath);
int config_getkey(xmlDoc *doc, char *xpath, char *str, size_t bytes);
int config_msess(xmlDoc *doc);

void client_res_init(void);
void client_res_insert(struct in6_addr *addr, data_t *data, ts_t *ts);
void client_res_update(struct in6_addr *addr, data_t *data, ts_t *ts);
void client_res_summary(int sig);

void unix_fd_set(int sock, /*@out@*/ fd_set *fs);
void unix_fd_zero(/*@out@*/ fd_set *fs);
int unix_fd_isset(int sock, /*@out@*/ fd_set *fs);
