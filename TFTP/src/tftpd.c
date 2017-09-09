#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

// argv[0] =  program name
// argv[1] = port number
// argv[2] = directory to serve

enum OP_CODE { 
	RRQ = 1,
	WRQ,
	DATA,
	ACK, 
	ERROR 
};

const int BUFFER_SIZE = 512;

FILE *fd;
unsigned short block_number;
unsigned short rec_block_number;

int sockfd;
struct sockaddr_in server, client;
int last_packet = 0;
short opcode;

char* read_mode = "r";

char filename[512];
char mode[512];
char message[512];


void send_data_packet() {
	char buffer[BUFFER_SIZE];
	char packet[BUFFER_SIZE+4];

	printf("Preparing packet number: %d \n",(int) block_number);
	int bytes_read = fread(buffer, 1, BUFFER_SIZE, fd);

	if (bytes_read < 0) {
		// something bad happened
		fprintf(stdout, "Error reading file!");
		return;
	}

	printf("Number of bytes read: %d\n", bytes_read);

	if (bytes_read < BUFFER_SIZE) {
		// Breaking out of while loop
		printf("Last packet being sent!\n");
		last_packet = 1;
		// close the file stream
		fclose(fd);
	}

	// fill packet with zeros
	memset(packet, 0, sizeof(packet));	
			
	// Opcode
	packet[0] = 0 & 0xff;
	packet[1] = 3 & 0xff;
	// Block Number
	packet[2] =(block_number >> 8) & 0xff;
	packet[3] = block_number & 0xff;
	

	for (int k = 0; k < bytes_read; k++) {
		packet[k+4] = buffer[k];
	}

	fprintf(stdout, "\nSending packet nr: %d\n",(int) block_number); 
	printf("\nOpcode: |%d|%d|", (int)packet[0], (int)packet[1]);
	printf("Block code: |%d|%d|\n\n", (int)packet[2], (int)packet[3]);
	

	int num_sent = sendto(sockfd, packet, (size_t) bytes_read + (size_t) 4, 0, 
				(struct sockaddr *) &client, (socklen_t) sizeof(client));

	printf("sent this many things: %d \n", num_sent);
}

void get_filename_and__mode(char* message, char* filename, char* mode)
{
    int c = 0;
	int i;
    for (i = 2; i < BUFFER_SIZE; i++, c++) {
        filename[c] = (char) message[i];

        if (message[i] == '\0') {
            i++;
            break;
        }
    }

    c = 0;
    for (int j = i; j < BUFFER_SIZE; j++, c++) {
		mode[c] = (char) message[j];

		if (message[j] == '\0') {
			break;
		}
    }
}

void read_request() {
	fprintf(stdout, "Received Read Request!\n");
	
	get_filename_and__mode(message, filename, mode);

	fprintf(stdout, "Filename: %s\n", filename);
	fprintf(stdout, "Mode: %s\n", mode);
	
	// Check if file is outside directory
	if (strstr(filename, "../") != NULL || strcmp(&filename[0], "/") == 0) {
		// TODO: Send error packet motehrfuckers! And maybe set errno?
		fprintf(stderr, "Error! Filename outside base directory! \n");
		return;
	}
	
	// check if we read with mode "r" or "rb"
	if (strcmp(mode, "netascii") == 0) {
		read_mode = "r";
	}
	else if (strcmp(mode, "octet") == 0) {
		read_mode = "rb";
	}
	else {
		// TODO: Send Error packet here
		fprintf(stdout, "Incorrect Mode");
	}

	fd = fopen(filename, "rb");
	
	if (fd == NULL){
		fprintf(stderr, "Error! %s\n", strerror(errno));
		return;
	}


	block_number = 1;
	send_data_packet();
}

int main(int argc, char **argv)
{	
    struct stat s;
    // Check if number of arguments are correct
    if(argc != 3) {
		fprintf(stderr, "Usage: %s <port> <directory>\n", argv[0]);
		return -1;
    }
	
    // Check if valid directory
    if (stat(argv[2], &s) == -1 || !(s.st_mode & S_IFDIR)) {
		fprintf(stderr, "No such directory: %s\n", argv[2]);
		return -1;
    }
	
    char *base_directory = argv[2];	
	
    // Change working directory to base directory
    if (chdir(base_directory) < 0) {
		fprintf(stderr, "Error! %s\n", strerror(errno));
		return -1;
    }

    // Get the port number from the command line
    int port = atoi(argv[1]);

    // Create and bind a UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    // Receives should timeout after 30 seconds.
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    // Network functions need arguments in network byte order instead
    // of host byte order. The macros htonl, htons convert the
    // values.
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons(port);

    // On success, zero is returned.  On error, -1 is returned, and errno is
    // set appropriately.
    int bindStatus = bind(sockfd, (struct sockaddr *) &server, (socklen_t) sizeof(server));
	
    // Check if bind worked.
    if (bindStatus == -1) {
		fprintf(stderr, "Error! %s\n", strerror(errno));
		return -1;
    }
    printf("Listening on port %d...\n", port);

    for (;;) {
		printf("\n--------------------------\n");
		printf("| Waiting for request... | \n");
		printf("--------------------------\n\n");

		// Receive up to one byte less than declared, because it will
		// be NUL-terminated later.
		socklen_t len = (socklen_t) sizeof(client);
		ssize_t n = recvfrom(sockfd, message, sizeof(message) - 1,
					0, (struct sockaddr *) &client, &len);
		if (n < 0) {
			fprintf(stderr, "Error! %s\n", strerror(errno));
		}
		// we got a message
		if (n >= 0) {
			// Get opcode
			opcode = message[1];
			fprintf(stdout, "Opcode: %d\n", opcode);
			
			switch(opcode) {
				case RRQ:
					read_request();
					break;
				case WRQ:
					fprintf(stdout, "Write requests are not allowed!");
					break;
				case DATA:
					fprintf(stdout, "Data upload not allowed!");
					break;
				case ACK:
					// TODO: implement
					printf("Mode is: %s \n", mode);
					printf("\nBlocknumber recieved: |%d|%d|\n", (int)message[2], (int)message[3]);
					rec_block_number = ((((unsigned char*)message)[2] << 8) + ((unsigned char*)message)[3]);
					fprintf(stdout, "I got an acknowledgement for block number: %d\n", rec_block_number);

					if (block_number == rec_block_number && !last_packet) {
						block_number++;
						send_data_packet();
					}
					else {
						// TODO: send last packet again.
					}
					

					break;
				case ERROR:
					fprintf(stdout, "I got sent an error message!");
					break;
			}
		} else {
			fprintf(stdout, "Error when receiving message");
			// Error or timeout. Check errno == EAGAIN or
			// errno == EWOULDBLOCK to check whether a timeout happened
		}
	}
    return 0;
}

