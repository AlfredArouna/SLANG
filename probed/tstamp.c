/*
 * HARDWARE TIMESTAMPING ACTIVATION
 * Author: Anders Berggren
 *
 * Function tstamp_hw()
 * Contains commands for activating hardware timestamping on both socket "s"
 * and on network interface "iface". Has to be run synchroniously.
 * - Hardware timestamps require SO_TIMESTAMPING on socket and ioctl on device.
 * - Kernel   timestamps require SO_TIMESTAMPING on socket.
 * - Software timestamps require SO_TIMESTAMPNS on socket. 
 * 
 * Function tstamp_sw()
 * Enable software timestamping, disable kernel and hardware timestamping.
 * 
 * Function tstamp_get(msg)
 * Extracts the timestamp from a message.
 */ 

#include "probed.h"

void tstamp_hw() {
	struct ifreq dev; /* request to ioctl */
	struct hwtstamp_config hwcfg; /* hw tstamp cfg to ioctl req */
	int f = 0; /* flags to setsockopt for socket request */
	if (c.ts == 'h') return; /* check if it has already been run */
	/* STEP 1: ENABLE HW TIMESTAMP ON IFACE IN IOCTL */
	memset(&dev, 0, sizeof(dev));
	/* get interface by iface name */
	strncpy(dev.ifr_name, c.iface, sizeof(dev.ifr_name));
	/* now that we have ioctl, check for ip address :) */
	if (ioctl(s, SIOCGIFADDR, &dev) < 0)
		syslog(LOG_ERR, "SIOCGIFADDR: no IP: %s", strerror(errno));
	/* point ioctl req data at hw tstamp cfg, reset tstamp cfg */
	dev.ifr_data = (void *)&hwcfg;
	memset(&hwcfg, 0, sizeof(&hwcfg)); 
	/* enable tx hw tstamp, ptp style, i82580 limit */
	hwcfg.tx_type = HWTSTAMP_TX_ON; 
	/* enable rx hw tstamp, all packets, yey! */
	hwcfg.rx_filter = HWTSTAMP_FILTER_ALL; 
	/* apply by sending to ioctl */
	if (ioctl(s, SIOCSHWTSTAMP, &dev) < 0) {
		syslog(LOG_ERR, "ioctl: SIOCSHWTSTAMP: %s", strerror(errno));
		syslog(LOG_ERR, "Check %s, and that you're root.\n", c.iface);
		/* otherwise, try kernel timestamps (socket only) */ 
		syslog(LOG_INFO, "Falling back to kernel timestamps.");
		tstamp_kernel();
		return;
	}
	/* STEP 2: ENABLE NANOSEC TIMESTAMPING ON SOCKET */
	f |= SOF_TIMESTAMPING_TX_HARDWARE;
	f |= SOF_TIMESTAMPING_RX_HARDWARE;
	f |= SOF_TIMESTAMPING_RAW_HARDWARE;
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &f, sizeof(f)) < 0) {
		/* try software timestamps (socket only) */ 
		syslog(LOG_ERR, "SO_TIMESTAMPING: %s", strerror(errno));
		syslog(LOG_INFO, "Falling back to software timestamps.");
		tstamp_sw();
		return;
	}
	syslog(LOG_INFO, "Using hardware timestamps.");
	c.ts = 'h'; /* remember hw is used */
}

void tstamp_kernel() {
	int f = 0; /* flags to setsockopt for socket request */

	if (c.ts == 'k') return; /* check if it has already been run */
	f |= SOF_TIMESTAMPING_TX_SOFTWARE;
	f |= SOF_TIMESTAMPING_RX_SOFTWARE;
	f |= SOF_TIMESTAMPING_SOFTWARE;
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &f, sizeof(f)) < 0) {
		syslog(LOG_ERR, "SO_TIMESTAMPING: %s", strerror(errno));
		syslog(LOG_INFO, "Falling back to software timestamps.");
		tstamp_sw();
		return;
	}
	syslog(LOG_INFO, "Using kernel timestamps.");
	c.ts = 'k'; /* remember kernel is used */
}

void tstamp_sw() {
	if (c.ts == 's') return; /* check if it has already been run */
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(yes)) < 0)
		syslog(LOG_ERR, "SO_TIMESTAMP: %s", strerror(errno));
	syslog(LOG_INFO, "Using software timestamps.");
	c.ts = 's';
}

int tstamp_get(struct msghdr *msg) {
	struct cmsghdr *cmsg;
	struct scm_timestamping *t;
	struct timespec *t2;
	//puts("NEW");
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		//printf("LEVL: %d sol %d ip %d\n", cmsg->cmsg_level, SOL_SOCKET, IPPROTO_IP);
		//printf("TYPE: %d ts %d err %d pkt %d\n", cmsg->cmsg_type, SO_TIMESTAMPING, IP_RECVERR, IP_PKTINFO);
		if (cmsg->cmsg_level == SOL_SOCKET) {
			/* if hw/kernel timestamps, check SO_TIMESTAMPING */
			if (c.ts != 's' && cmsg->cmsg_type == SO_TIMESTAMPING) {
				t = (struct scm_timestamping *)CMSG_DATA(cmsg);
				if (c.ts == 'h') p.ts = t->hwtimeraw;
				if (c.ts == 'k') p.ts = t->systime;
				return 0;
			}
			/* if software timestamps, check SO_TIMESTAMPNS */
			if (c.ts == 's' && cmsg->cmsg_type == SO_TIMESTAMPNS) {
				t2 = (struct timespec *)CMSG_DATA(cmsg);
				p.ts = *t2;
				return 0;
			}
		}
	}
	return -1;
}

/*
 * Function that waits for a TX timestamp on the error queue, used for 
 * the kernel (and hardware) timestamping mode. It does so by select()ing 
 * reads, and checking for approx 1 second. Note that normal packets will
 * trigger the select() as well, therefore the while-loop is needed. 
 * As usual, the timestamp is stored by data_recv() calling tstamp_get()
 * in the global variable ts.
 */
void tstamp_recv() {
	fd_set fs; /* select fd set for hw tstamps */
	struct timeval tv, now, last; /* timeout for select and while */

	tv.tv_sec = 1; /* wait for nic tx tstamp during at least 1sec */ 
	tv.tv_usec = 0;
	gettimeofday(&last, 0);
	gettimeofday(&now, 0);
	while (now.tv_sec - last.tv_sec < 2) {
		FD_ZERO(&fs);
		FD_SET(s, &fs);
		if (select(s + 1, &fs, 0, 0, &tv) > 0) 
			if (data_recv(MSG_ERRQUEUE) == 0)  /* get tx ts */
				return;
		gettimeofday(&now, 0);
	}
	syslog(LOG_ERR, "Kernel TX timestamp error.");
	p.ts.tv_sec = 0;
	p.ts.tv_nsec = 0;
}