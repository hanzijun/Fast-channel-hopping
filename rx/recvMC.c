#include <signal.h>
#include <linux/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <tx80211.h>
#include <tx80211_packet.h>
#include "util_pay.h"
#include "iwl_connector.h"
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#define MAX_PAYLOAD 2048
#define SLOW_MSG_CNT 1
#define PAYLOAD_SIZE	2000000


FILE* out = NULL;
struct timespec test;
int now,last,diff,start_s,start_ns,end_s,end_ns;
uint32_t num_packets;
uint32_t packet_size = 100;
struct lorcon_packet *packet;
struct tx80211	tx;
struct tx80211_packet	tx_packet;
uint32_t i = 0;
int fail_i = 0;
int32_t ret, ret_0, ret_r, ret_w, ret_s;
uint32_t delay_us;
uint8_t *payload_buffer;
int flag = 0;
int sock_fd = -1;
FILE* fs;
FILE* open_file(char* filename, char* spec);							// the socket
/* Local variables */
struct sockaddr_nl proc_addr, kern_addr;	// addrs for recv, send, bind
struct cn_msg *cmsg;
char buf[4096];
char recv_buf[4096];
unsigned short l, l2;
int count = 0;
int j = 1;
int chan_list[3] = {52,56,60};		//chan_list[2] = 136, equals 136 in "sudo ./my_setup_inject.sh 136 HT40-"
char buffer[100];
char command[200];
struct timeval timeout={0,1000};//select等待1000s秒，要非阻塞就置0
void check_usage(int argc, char** argv);
FILE* open_file(char* filename, char* spec);
static void init_lorcon();
void caught_signal(int sig);
void exit_program(int code);
void exit_program_err(int code, char* func);
void SetSocket();

int64_t getCurrentTime()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

struct lorcon_packet
{
	__le16	fc;
	__le16	dur;
	u_char	addr1[6];
	u_char	addr2[6];
	u_char	addr3[6];
	__le16	seq;
	u_char	payload[0];
} __attribute__((packed));
struct rx_pkt
{
	u_char	phyheader[21];
	__le16	fc;
	__le16	dur;
	u_char	addr1[6];
	u_char	addr2[6];
	u_char	addr3[6];
	__le16	seq;
	u_char	payload[0];
} __attribute__ ((packed));


void send_ack(int seq)
{
	struct lorcon_packet *ptr = tx_packet.packet;
	ptr->seq = seq;
	ret = tx80211_txpacket(&tx, &tx_packet);	
	if (ret < 0) {
		fprintf(stderr, "Unable to transmit packet: %s\n", tx.errstr);
		exit(1);
	}
	else
	{
		printf("acked\n");
	}
}

int main(int argc, char** argv)
{
	sprintf(command,"bash /home/han/linux-80211n-csitool-supplementary/injection/my_setup_monitor_recvMC.sh wlan0 %d HT20",chan_list[0]);
	system(command);
	sleep(10);
	
	SetSocket();
	
	/* Make sure usage is correct */
	check_usage(argc, argv);
	
	/* Open and check log file */
	out = open_file(argv[1], "w");

	/* Generate packet payloads */
	printf("Generating packet payloads \n");
	payload_buffer = malloc(PAYLOAD_SIZE);
	if (payload_buffer == NULL) {
		perror("malloc payload buffer");
		exit(1);
	}
	generate_payloads(payload_buffer, PAYLOAD_SIZE);

	/* Setup the interface for lorcon */
	printf("Initializing LORCON\n");
	init_lorcon();

	/* Allocate packet */
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
	
	while (1) {
		if (flag == 1)		
			ret_0 = setsockopt(sock_fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
		/* Receive from socket with infinite timeout */
		ret_r = recv(sock_fd, recv_buf, sizeof(recv_buf), 0);
		flag = 1;
		if (ret_r != -1)
		{
			//send_ack(j);
			/* Pull out the message portion and print some stats */
			cmsg = NLMSG_DATA(recv_buf);
			if (count % SLOW_MSG_CNT == 0)
				printf("received %d: %d bytes, id: %d val: %d seq: %d clen: %d\n",count+1, cmsg->len, cmsg->id.idx, cmsg->id.val, cmsg->seq, cmsg->len);
			/* Log the data to file */
			l = (unsigned short) cmsg->len;
			l2 = htons(l);
			fwrite(&l2, 1, sizeof(unsigned short), out);
			ret_w = fwrite(cmsg->data, 1, l, out);
			fs = fopen("./time.txt", "a+");
			long int a  = getCurrentTime();
			fprintf(fs, "%ld\n", a);
			fclose(fs);
			if (ret_w != l)
				exit_program_err(1, "fwrite");
			if (j % 100 == 0)
				printf("wrote %d bytes [msgcnt=%u]\n", ret_w, j);
//channel hop
			j = count + 1;
			switch(j % 3){
				case 0:ret_s = tx80211_setchannel(&tx, chan_list[0]);break;
				case 1:ret_s = tx80211_setchannel(&tx, chan_list[1]);break;
				case 2:ret_s = tx80211_setchannel(&tx, chan_list[2]);break;
			}
			count++;						
		}

	}
	exit_program(0);
	return 0;
}
void check_usage(int argc, char** argv)
{
	if (argc != 2)
	{
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
	if (tx80211_open(&tx) < 0) {
		fprintf(stderr, "Error opening LORCON interface\n");
		exit(1);
	}

	/* Set up rate selection packet */
	tx80211_initpacket(&tx_packet);printf("end");
}



void SetSocket(){
	
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
}





void caught_signal(int sig)
{
	fprintf(stderr, "caught signal %d\n", sig);
	exit_program(0);
}

void exit_program(int code)
{
	
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
