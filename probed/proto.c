/*
 * UDP PING
 * Author: Anders Berggren
 *
 * Protocol code for both client and server. 
 */

#include "sla-ng.h"

struct sockaddr_in my;

void proto() {
	struct packet_ping pp;
	struct timespec tx;
	struct timeval tv;
	int seq, i, r;
	char addr[50];
	char pps[10];

	seq = i = 0;
	tv.tv_sec = 0;
	tv.tv_usec = 1;
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		perror("setsockopt: SO_RCVTIMEO");
	while (1) {
		p.data[0] = 0;
		data_recv(0); /* wait for ping/pong/ts */
		if (p.data[0] == TYPE_PONG) proto_client();
		if (p.data[0] == TYPE_PING) proto_server();
		if (p.data[0] == TYPE_TIME) proto_timestamp();
		/* send ping (client) */
		if (i % 100 == 0) {
			r = config_getkey("/config/ping[1]/address", addr, 50);
			if (r < 0) continue;
			r = config_getkey("/config/ping[1]/pps", pps, 10);
			if (r < 0) continue;
			them.sin_family = AF_INET;
			them.sin_port = htons(c.port);
			if (inet_aton(addr, &them.sin_addr) < 0)
				perror("inet_aton: Check the IP address");
			pp.type = TYPE_PING;
			pp.seq = seq;
			data_send((char*)&pp, sizeof(pp));
			tx = p.ts; /* save timestamp */
			seq++;
		}
		i++;
		//if (i == 1000-USLEEP) i = 0;

		/*
		   int i;
		   char str[250];
		   for (i = 0; i<10000; i++){
		   config_getkey("/config/ping[address='130.244.97.210']/type", str);
		   }
		   printf("FISK %s\n", str);
		 */

	}
}

void proto_server() {
	struct packet_ping *pp;
	struct packet_time pt;
	struct timespec rx, tx, di;

	rx = p.ts; /* save timestamp */
	pp = (struct packet_ping*)&p.data;
	pp->type = TYPE_PONG;
	printf("* PING %d\n", pp->seq);
	data_send((char*)pp, sizeof(struct packet_ping)); /* send pong */
	tx = p.ts; /* save timestamp */

	diff_ts(&di, &tx, &rx);
	if (c.debug) printf("DI %010ld.%09ld\n", di.tv_sec, di.tv_nsec);
	pt.type = TYPE_TIME;
	pt.seq1 = pp->seq;
	pt.rx1 = rx;
	pt.tx1 = tx;
	data_send((char*)&pt, sizeof(pt)); /* send diff */
}

void proto_client() {
	struct packet_ping *pp;
	struct timespec rx;

	rx = p.ts; /* save timestamp */
	pp = (struct packet_ping*)&p.data;
	printf("* PONG %d\n", pp->seq);
	
	/*data_recv(0);
	diff_ts(&di, &rx, &tx);
	if (c.debug) printf("DI %010ld.%09ld\n", di.tv_sec, di.tv_nsec);
	data = p.data;
	data += 11;
	sdi = atoi(data);
	if (c.debug) printf("SD %d\n", sdi);
	rtt = di.tv_nsec - sdi;
	printf("RT %d\n", rtt); */

}

void proto_timestamp() {
	struct packet_time *pt;
	struct timespec di;

	pt = (struct packet_time*)&p.data;
	diff_ts(&di, &(pt->tx1), &(pt->rx1));
	printf("* TIME %d %09ld\n", pt->seq1, di.tv_nsec);
	
	/*data_recv(0);
	diff_ts(&di, &rx, &tx);
	if (c.debug) printf("DI %010ld.%09ld\n", di.tv_sec, di.tv_nsec);
	data = p.data;
	data += 11;
	sdi = atoi(data);
	if (c.debug) printf("SD %d\n", sdi);
	rtt = di.tv_nsec - sdi;
	printf("RT %d\n", rtt); */

}

void proto_bind(int port) {
	if (port == c.port) return;
	printf("Binding port %d\n", port);
	c.port = port;
	my.sin_family = AF_INET;
	my.sin_addr.s_addr = htonl(INADDR_ANY);
	my.sin_port = htons(port);
	if (bind(s, (struct sockaddr *)&my, slen) < 0) perror("bind");
}
