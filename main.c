#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <string.h>
#include <netdb.h>

#define ECHO_REQUEST 8
#define ECHO_REPLY 0
#define IPV4 0b0100
#define CODE 0
#define CHEKS_N 0x0000
#define TIMEOUT 5
#define SCALE 1000000.0

typedef struct Icmp{
	uint8_t type;
	uint8_t code;
	uint16_t checksum;
	uint16_t id;
	uint16_t seq;
	//data
	struct timespec timestamp;
	
} Icmp;

int main(int argc, char *argv[]){
	//no argument provided
	if(argc==1){
		printf("Please enter an IP address or a hostname\n");
	}
	
	struct sockaddr_in s_addr; //holds the address to send to
	const char* host_or_ip = argv[1];
	
	//gets the IP address and copies it in the right format into the structure
	getHostAddress(&s_addr, host_or_ip);
	printf("Attempting to ping: %s\n", host_or_ip);
	
	//creates the necessary socket
	int raw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if(raw_socket<0){
		printf("Can't create socket!\n", raw_socket);
	}
	
	//while loop to send pings indefinitly
	uint16_t seq=0x0000; 
	while(1)
	{
		ping(raw_socket, &s_addr, seq);
		seq++;
		sleep(1);
	}
	return 0;
}

void prepareIcmpReq(struct Icmp *e, int seq)
{
	e->type = ECHO_REQUEST; //echo message
	e->code= CODE;
	e->id = getpid();
	e->seq = seq;
	e->checksum = CHEKS_N;
	clock_gettime(CLOCK_REALTIME, &(e->timestamp));
}

void decodeIcmpRep(struct Icmp *e, uint8_t *buff)
{
	memcpy(e,buff, sizeof(Icmp));
}

void getHostAddress(struct sockaddr_in *s_addr,char *args)
{		
	struct sockaddr temp;
	struct addrinfo shintAddr;
	struct addrinfo *res, *rf;
	
	memset(&shintAddr, 0, sizeof shintAddr);
	shintAddr.ai_family = AF_INET;
	
	if(getaddrinfo(args, NULL, &shintAddr, &res)<0) 
	{
		printf("Error in resolving hostname or IP address\n");
		exit(1);
	}
	temp = *(res->ai_addr);
	memcpy(s_addr, (struct sockaddr_in*) &temp, sizeof(struct sockaddr_in));
}

int checksum(uint16_t *in, int size)
{	
	uint16_t *addrptr=in;
	uint32_t sum = 0;
	while(size>1)
	{
		sum += *addrptr++;
		size -= sizeof(uint16_t);
	}
	
	if(size==1)
	{
		sum += *(unsigned char*) addrptr;
	}
	while(sum>>16)
	{
		sum = (sum>>16)+(sum & 0xffff);
	}
	return ~(sum);
}

void ping(int raw_socket, struct sockaddr_in *s_addr, uint16_t seq){
	//create the ICMP packet to be send
	struct Icmp e; 
	prepareIcmpReq(&e, seq);
	e.checksum = checksum((uint16_t *)&e, sizeof(e));
	
	//set the structure that calculates the round trip time 
	struct timespec rtt;
	struct timeval timeout;
	
	int s = sendto(raw_socket, &e, sizeof(e), 0, (struct sockaddr*)s_addr, sizeof(*s_addr));
	
	if(s<0)
	{
		printf("Error while trying to send: %d\n", s);	
		return;
	}
	
	uint8_t* buff = (uint8_t*)calloc(64, sizeof(uint8_t)); //allocate buffer for receiving
	struct sockaddr_in f_addr; //address storage for the address that is received from
	int addr_len = sizeof(f_addr);
	

	// create the timeout and set
	timeout.tv_sec=TIMEOUT;
	timeout.tv_usec=0;
	
	//create a file descriptor set and add the raw socket
	fd_set readfd;
	FD_ZERO(&readfd);
	FD_SET(raw_socket, &readfd);
	
	//select the sockets and set the timeout
	if(select(raw_socket+1, &readfd, NULL, NULL, &timeout)<0)
	{
		printf("Error on setting select\n");
		exit(1);
	}
	
	if(FD_ISSET(raw_socket, &readfd))
	{
		int r = recvfrom(raw_socket, buff, sizeof(uint8_t)*128, 0, (struct sockaddr*)&f_addr,&addr_len);
		clock_gettime(CLOCK_REALTIME, &rtt);
		
		char rec_from[sizeof(f_addr)]; //buffer for the address
		inet_ntop(AF_INET, &(f_addr.sin_addr), rec_from, sizeof(f_addr));
		
		struct Icmp rec;	
		uint8_t iphdrsize;
		memcpy(&iphdrsize, (uint8_t *)buff, sizeof(uint8_t));
		iphdrsize = iphdrsize<<2;
		
		decodeIcmpRep(&rec,((char *)buff+iphdrsize));
		
		if(rec.code>0&&!(r<0))
		{	
			printf("Echo reply: ICMP type:%i ICMP Code:%i\n", rec.type,rec.code); //get a response, but not from the host
			return;
		}
		
		if(rec.id!=e.id)
		{
			return;
		}
		
		uint16_t tempCh = rec.checksum; //keep checksum and set to null for comparison
		rec.checksum = CHEKS_N;
		uint16_t compareCh = checksum((uint16_t *)&rec, sizeof(rec));
		double rttime = (rtt.tv_nsec-rec.timestamp.tv_nsec)/SCALE;
		
		if(compareCh != tempCh)
		{
			printf("Receive checksum error!\n");
		}
		else
		{
			printf("Echo reply with id:%d, seq:%d, from %s with a round-trip-time of %.2f ms\n", rec.id, rec.seq,rec_from, rttime);
		}
	}
	else
	{
		printf("Echo reply timeout try: %d\n", seq);
		return;
	}
}
