#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <tx80211.h>
#include <tx80211_packet.h>
#include "util_pay.h"
#include "iwl_connector.h"
#include <string.h>

#include <sys/socket.h>
#include <linux/netlink.h>

#include "udp.c"

struct lorcon_packet
{
	__le16	fc;
	__le16	dur;
	u_char	addr1[6];
	u_char	addr2[6];
	u_char	addr3[6];
	__le16	seq;
	u_char	payload[0];
} __attribute__ ((packed));

struct lorcon_packet *packet;
struct tx80211	tx;
struct tx80211_packet	tx_packet;

#define MAX_PAYLOAD 2048
#define SLOW_MSG_CNT 1
#define PAYLOAD_SIZE 2000000

uint8_t *payload_buffer;
int sock_fd = -1;
uint32_t packet_size = 100;
char command[200]; // run setup_monitor.sh
int j = 0; //for channel selected  e.g., j % 16
int32_t rett;	// return the channel hopping result
FILE* out = NULL;
int chan_list[16] = {100,104,108,112,116,120,124,128,132,136,140,149,153,157,161,165}; // channel list
//int chan_list[11] = {100,104,108,112,116,120,124,128,132,136,140};
void check_usage(int argc, char** argv);
static void init_lorcon();
FILE* open_file(char* filename, char* spec);
void caught_signal(int sig);
void exit_program(int code);
void exit_program_err(int code, char* func);

int main(int argc, char** argv)
{
	/* Local variables */
	struct sockaddr_nl proc_addr, kern_addr;	// addrs for recv, send, bind
	struct cn_msg *cmsg;

	char buf[4096];
	char ack[4]; // send the number of next channel to TX 
	int ret, channel;
	unsigned short l, l2;
	int count = 0;
	int cfd; //for udp
	char strptr[] = "10.112.250.82"; //server's ip
        unsigned short portnum = 0x1031; //server's port
	strcpy(strptr, argv[1]);
	portnum =(unsigned short) atoi(argv[2]);
	cfd = init_udp(strptr, portnum);
	if (-1 == cfd){
		printf("socket init error!\n");
		return 0;
	}

	/* Make sure usage is correct */
	check_usage(argc, argv);
	/* Open and check log file */
	out = open_file(argv[3], "w");

	sprintf(command,"bash /home/han/linux-80211n-csitool-supplementary/splice_rx/my_setup_monitor_csi.sh %d HT20",chan_list[0]);
	system(command);
	/* Setup the socket */
	sock_fd = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	if (sock_fd == -1)
		exit_program_err(-1, "socket");

	/* Initialize the address structs */
	memset(&proc_addr, 0, sizeof(struct sockaddr_nl));
	proc_addr.nl_family = AF_NETLINK;
	proc_addr.nl_pid = getpid();			// this process' PID
	proc_addr.nl_groups = CN_IDX_IWLAGN;
	memset(&kern_addr, 0, sizeof(struct sockaddr_nl));
	kern_addr.nl_family = AF_NETLINK;
	kern_addr.nl_pid = 0;					// kernel
	kern_addr.nl_groups = CN_IDX_IWLAGN;

	/* Now bind the socket */
	if (bind(sock_fd, (struct sockaddr *)&proc_addr, sizeof(struct sockaddr_nl)) == -1)
		exit_program_err(-1, "bind");

	/* And subscribe to netlink group */
	{
		int on = proc_addr.nl_groups;
		ret = setsockopt(sock_fd, 270, NETLINK_ADD_MEMBERSHIP, &on, sizeof(on));
		if (ret)
			exit_program_err(-1, "setsockopt");
	}

	/* Set up the "caught_signal" function as this program's sig handler */
	signal(SIGINT, caught_signal);

	payload_buffer = malloc(PAYLOAD_SIZE);
	if (payload_buffer == NULL){
		perror("malloc payload buffer error");
		exit(1);
	}
	printf("Generating packet payloads \n");	
	generate_payloads(payload_buffer, PAYLOAD_SIZE);
	
	printf("Initializing Lorcon\n");
	init_lorcon();

	packet = malloc(sizeof(*packet) + packet_size);
	if (!packet) {
		perror("malloc packet");
		exit(1);
	}
	packet->fc = (0x08 /* Data frame */
				| (0x0 << 8) /* Not To-DS */);
	packet->dur = 0xffff;
	memcpy(packet->addr1, "\x00\x16\xea\x12\x34\x56", 6);
	memcpy(packet->addr2, "\x00\x16\xea\x12\x34\x56", 6);
	memcpy(packet->addr3, "\xff\xff\xff\xff\xff\xff", 6);
	packet->seq = 0;
	tx_packet.packet = (uint8_t *)packet;
	tx_packet.plen = sizeof(*packet) + packet_size;
	/* Poll socket forever waiting for a message */
	while (1)
	{
		/* Receive from socket with infinite timeout */
		ret = recv(sock_fd, buf, sizeof(buf), 0);
		if (ret == -1)
			exit_program_err(-1, "recv");
		/* Pull out the message portion and print some stats */
		cmsg = NLMSG_DATA(buf);
		if (count % SLOW_MSG_CNT == 0)
			printf("received %d bytes: id: %d val: %d seq: %d clen: %d\n", cmsg->len, cmsg->id.idx, cmsg->id.val, cmsg->seq, cmsg->len);
		/* Log the data to file */
		l = (unsigned short) cmsg->len;
		l2 = htons(l);
		fwrite(&l2, 1, sizeof(unsigned short), out);
		ret = fwrite(cmsg->data, 1, l, out);
		++j;
		sprintf(ack, "%d", chan_list[j % 16]);
		channel = atoi(ack);
		write(cfd, ack, sizeof(ack)); //send the number of next channel (ack) to TX through UDP
		printf("next channel: %d, UDP succeed!\n", channel);
		
		rett = tx80211_setchannel(&tx, channel); //channel hopping
		//sleep(0.00001);
		if (rett < 0) {
			printf("Error setting channel\n");
		}	
		if (count % 100 == 0)
			printf("wrote %d bytes [msgcnt=%u]\n", ret, count);
		if (ret != l)
			exit_program_err(1, "fwrite");		
		++count;
	}
	exit_program(0);
	return 0;
}

void check_usage(int argc, char** argv)
{
	if (argc != 4)
	{
		printf("/*   Usage 3: recv_csi <ip> <port> <output_file>*/\n");		
		fprintf(stderr, "Usage: %s <output_file>\n", argv[0]);
		exit_program(1);
	}
}

FILE* open_file(char* filename, char* spec)
{
	FILE* fp = fopen(filename, spec);
	if (!fp)
	{
		perror("fopen");
		exit_program(1);
	}
	return fp;
}

static void init_lorcon()
{
	/* Parameters for LORCON */
	int drivertype = tx80211_resolvecard("iwlwifi");

	/* Initialize LORCON tx struct */
	if (tx80211_init(&tx, "mon0", drivertype) < 0) {
		fprintf(stderr, "Error initializing LORCON: %s\n",
				tx80211_geterrstr(&tx));
		exit(1);
	}
	if (tx80211_open(&tx) < 0 ) {
		fprintf(stderr, "Error opening LORCON interface\n");
		exit(1);
	}

	/* Set up rate selection packet */
	tx80211_initpacket(&tx_packet);
}


void caught_signal(int sig)
{
	fprintf(stderr, "Caught signal %d\n", sig);
	exit_program(0);
}

void exit_program(int code)
{
	if (out)
	{
		fclose(out);
		out = NULL;
	}
	if (sock_fd != -1)
	{
		close(sock_fd);
		sock_fd = -1;
	}
	exit(code);
}

void exit_program_err(int code, char* func)
{
	perror(func);
	exit_program(code);
}
