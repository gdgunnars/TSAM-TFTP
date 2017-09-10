/* $Id: tftpd.c,v 1.0 2017/09/09 20:17:00 hpa Exp $ */
/* ----------------------------------------------------------------------- *
 *   
 *   Copyright 2017 
 *		Guðjón Steinar Sverrisson
 *		Gunnar Davíð Gunnarsson
 *		Hlynur Stefánsson
  * - All Rights Reserved
 *
 *   This program is free software available under the same license
 *   as the "OpenBSD" operating system, distributed at
 *   http://www.openbsd.org/.
 *
 * ----------------------------------------------------------------------- */

/*
 * tftpd.c
 *
 * Simple TFTP server that only accepts RRQ (Reading files) but not 
 * WRQ (writing files).
 *
 */


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
int transaction_done = 0;
short opcode;

char* read_mode = "r";

char filename[512];
char mode[512];
char message[512];
char packet[516];

char error_msg[512];

int bytes_read = 0;

unsigned short current_client_port = 0;

void clear_everything() {
	fd = NULL;
	block_number = 0;
	rec_block_number = 0;
	
	last_packet = 0;
	transaction_done = 0;
	opcode = 0;
	
	read_mode = "r";
	
	memset(filename, 0, sizeof(filename));
	memset(mode, 0, sizeof(mode));
	memset(message, 0, sizeof(message));
	memset(packet, 0, sizeof(packet));
	memset(error_msg, 0, sizeof(error_msg));

	bytes_read = 0;
	current_client_port = 0;
}

void send_error_packet(int error_code, char* msg) {
	fprintf(stdout, "Sending error packet\nError Code: %d\nError Message: %s\n",
					error_code, msg);
	char error_packet[strlen(msg)+5];
	// fill packet with zeros
	memset(error_packet, 0, sizeof(error_packet));

	// Opcode
	error_packet[0] = 0;
	error_packet[1] = 5;
	// Block Number
	error_packet[2] = 0;
	error_packet[3] = error_code;
	// Error message
	size_t i;
	for (i = 0; i < strlen(msg); i++) {
		error_packet[i+4] = msg[i];
	}
	error_packet[i+4] = '\0';

	int num_sent = sendto(sockfd, error_packet, (size_t)strlen(msg) + (size_t)5, 0, 
						(struct sockaddr *) &client, (socklen_t) sizeof(client));
	if (num_sent < 0) {
		fprintf(stderr, "Error! %s\n", strerror(errno));
		clear_everything();
	}
	else {
		printf("Number of bytes sent: %d \n", num_sent);
	}
}

void send_data_packet() {
	char buffer[BUFFER_SIZE];
	
	bytes_read = 0;
	printf("Preparing packet number: %d \n",(int) block_number);
	bytes_read  = fread(buffer, 1, BUFFER_SIZE, fd);

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

	int num_sent = sendto(sockfd, packet, (size_t) bytes_read + (size_t) 4, 0, 
				(struct sockaddr *) &client, (socklen_t) sizeof(client));

	if (num_sent < 0) {
		fprintf(stderr, "Error! %s\n", strerror(errno));
		clear_everything();
	}
	else {
		printf("Number of bytes sent: %d \n", num_sent);
	}
}

void resend_last_data_packet() {
	printf("Resending packet number: %d \n",(int) block_number);
	int num_sent = sendto(sockfd, packet, (size_t) bytes_read + (size_t) 4, 0, 
				(struct sockaddr *) &client, (socklen_t) sizeof(client));
	printf("Number of bytes sent: %d \n", num_sent);
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
	// convert mode to lowercase
	for (i = 0; mode[i]; i++) {
		mode[i] = tolower((unsigned char)mode[i]);
	}
}

void read_request() {
	get_filename_and__mode(message, filename, mode);


	printf("file \"%s\" requested from %d.%d.%d.%d:%d\n", 
			filename,
			client.sin_addr.s_addr&0xff,
			(client.sin_addr.s_addr>>8)&0xff,
			(client.sin_addr.s_addr>>16)&0xff,
			(client.sin_addr.s_addr>>24)&0xff,
			client.sin_port);
	
	fprintf(stdout, "Mode: %s\n", mode);
	
	// Check if file is outside directory
	if (strstr(filename, "../") != NULL || (&filename[0])[0] == '/') {
		send_error_packet(2, "Access violation.");
		clear_everything();
		return;
	}
	
	// "r" reads a text file, used with netascii mode
	// "rb" reads a binary file, used with octed mode
	if (strcmp(mode, "netascii") == 0) {
		read_mode = "r";
	}
	else if (strcmp(mode, "octet") == 0) {
		read_mode = "rb";
	}
	// Else the mode is not recognized.
	else {
		send_error_packet(4, "Illegal TFTP operation.");
		clear_everything();
		return;
	}

	fd = fopen(filename, "rb");
	
	if (fd == NULL){
		send_error_packet(0, strerror(errno));
		clear_everything();
		return;
	}

	block_number = 1;
	send_data_packet();
}

void acknowledgement() {
	rec_block_number = ((((unsigned char*)message)[2] << 8) + ((unsigned char*)message)[3]);
	fprintf(stdout, "Received acknowledgement for block number: %d\n", rec_block_number);

	if (block_number == rec_block_number) {
		if (!last_packet) {
			block_number++;
			send_data_packet();
		}
		else {
			fprintf(stdout, "Transaction is done!\n");
			transaction_done = 1;
			last_packet = 0;
		}
	}
	else if (rec_block_number == block_number-1) {
		resend_last_data_packet();
	}
	else {
		fprintf(stdout, "Received an unexpected block number.");
	}
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
		printf("| Waiting for request... |\n");
		printf("--------------------------\n\n");

		// Receive up to one byte less than declared, because it will
		// be NUL-terminated later.
		socklen_t len = (socklen_t) sizeof(client);
		ssize_t n = recvfrom(sockfd, message, sizeof(message) - 1,
					0, (struct sockaddr *) &client, &len);
		

		/*
		// Check if first client connecting or if a new client is connecting.
		// Save their port number.
		if (current_client_port == 0 || (current_client_port != client.sin_port && transaction_done)) {
			current_client_port = client.sin_port;
		}
		// Check if unknown TID is trying to connect while still transfering to another TID.
		// In this case we want to keep the connection alive with the previous TID.
		else if (current_client_port != client.sin_port && !transaction_done) {
			send_error_packet(5, "Unknown transfer ID.");
			continue;
		}
		*/

		// Check if there was an error getting the message from the client.
		if (n <= 0) {
			fprintf(stderr, "Error! %s\n", strerror(errno));
			clear_everything();
			continue;
		}
		// Check if illegal message
		else if(n < 4) {
			send_error_packet(4, "Illegal TFTP operation.");
			clear_everything();
			continue;
		}

		// we got a message
		if (n >= 0) {
			// Get opcode
			opcode = message[1];
			fprintf(stdout, "Opcode: %d\n", opcode);
			
			// Check the opcode
			switch(opcode) {
				case RRQ:
					read_request();
					break;
				case WRQ:
					fprintf(stdout, "Write requests are not allowed!");
					send_error_packet(2, "Access violation.");
					break;
				case DATA:
					fprintf(stdout, "Data upload not allowed!");
					send_error_packet(4, "Illegal TFTP operation.");
					break;
				case ACK:
					acknowledgement();
					break;
				case ERROR:
					fprintf(stdout, "I got sent an error message!");
					break;
			}
		} else {
			// Error or timeout. Check errno == EAGAIN or
			// errno == EWOULDBLOCK to check whether a timeout happened
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				send_error_packet(0, strerror(errno));
			}
			else {
				send_error_packet(0, "Unknown error occurred.");
			}
			clear_everything();
		}
	}
    return 0;
}

