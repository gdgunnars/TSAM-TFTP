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
const size_t BUFFER_SIZE = 512;
enum OP_CODE { RRQ = 1, DATA = 3, ERROR = 5 };

ssize_t get_message(int sockfd, char* message, size_t size, 
                    struct sockaddr_in* client, socklen_t* len) {

    ssize_t n = recvfrom(sockfd, message, size, 0, 
			 (struct sockaddr *) &client, len);
    if (n < 0) {
        fprintf(stderr, "Error! %s\n", strerror(errno));
    }

    return n;
}

void get_filename_and__mode(char* message, char* filename, char* mode)
{
    int c = 0;
    size_t i;
    for (i = 2; i < BUFFER_SIZE; i++, c++) {
        filename[c] = (char) message[i];

        if (message[i] == '\0') {
            i++;
            break;
        }
    }

    c = 0;
    for (size_t j = i; j < BUFFER_SIZE; j++, c++) {
	mode[c] = (char) message[j];

	if (message[j] == '\0') {
	    break;
	}
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

    int sockfd;
    struct sockaddr_in server, client;
    char message[512];
	
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
	size_t size = sizeof(message);
	ssize_t n = get_message(sockfd, message ,size, &client, &len);

	if (n >= 0) {
	    uint16_t opcode = message[1];
			
	    fprintf(stdout, "Received request!\n");
	    fprintf(stdout, "Number of bytes recieved: %zd\n", n);
	    fprintf(stdout, "Opcode: %d\n", opcode);
			
	    char filename[512];
	    char mode[512];
			
	    get_filename_and__mode(message, filename, mode);
		        
	    if (strstr(filename, "../") != NULL || strcmp(&filename[0], "/") == 0) {
		// TODO: Send error packet motehrfuckers! And maybe set errno?
		fprintf(stderr, "Error! Filename outside base directory! \n");
		return -1;
	    }
			
			
	    fprintf(stdout, "Filename: %s\n", filename);
	    fprintf(stdout, "Mode: %s\n", mode);
					

	    FILE *fd;
	    char* read_mode = "r";
			
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
			
			
	    uint8_t buffer[512];
	    char packet[516];
	    fd = fopen(filename, read_mode);
			
	    if (fd == NULL){
		fprintf(stderr, "Error! %s\n", strerror(errno));
		return -1;
	    }

	    int close = 0;
	    ssize_t bytes_read;
	    unsigned long block_number = 0;
	    while(!close){
		block_number++;

		printf("Preparing packet number: %d \n",(int) block_number);
		bytes_read = fread(buffer, 1, 512, fd);

		printf("Bytes read: %zd\n", bytes_read);
		/*	
			printf("\nContents of buffer:\n");
			for (size_t k = 0; k < sizeof(buffer); k++) {
			printf("%c", (char) buffer[k]);
			}
		*/	

		if (bytes_read < 512) {
		    // Breaking out of while loop
		    printf("Last packet being sent!\n");
		    close = 1;
		}
		fflush(stdout);
			    
			
		// --------- SEND DATA

		// fill reply with zeros
		memset(packet, 0, sizeof(packet));	
			    
		// Opcode
		packet[0] = (3 >> 8) & 0xff;
		packet[1] = 3 & 0xff;
		// Block code
		packet[2] = (block_number >> 8) & 0xff;
		packet[3] = block_number & 0xff;
			    
		for (int k = 0; k < bytes_read; k++) {
		    packet[k+4] = buffer[k];
		    printf("%c", (char)packet[k+4]);
		}
				
				
					
		/*
		  for(int k  = 0; k < 516; k++) {
		  printf("%c", (char)packet[k]);
		  }*/
				
		printf("\nOpcode:\n");
		printf("%d - %d\n", (int)packet[0], (int)packet[1]);
		printf("Block code:\n");
		printf("%d - %d\n", (int)packet[2], (int)packet[3]);
				
		printf("\nLast 4 bytes: %c %c %c %c", packet[512], packet[513], packet[514], packet[515]); 

		/*
		  printf("\nContents of packet:\n");
		  for (size_t k = 0; k < sizeof(packet); k++) {
		  printf("%c", packet[k]);
		  }
		  printf("\n\n");
		*/

							
		int num_sent = sendto(sockfd, packet, sizeof(packet), 0, 
				      (struct sockaddr *) &client, len);
				
		if (num_sent == -1) {
		    fprintf(stdout, "\nSending packet nr: %d\n",(int) block_number); 	
		    fprintf(stderr, "Error sending packet: %s\n", strerror(errno));
		    return -1;
		}

		printf("sent this many things: %d \n", num_sent);
				    
	    }			
			
	    fflush(stdout);


	    //sendto(sockfd, message, (size_t) n, 0,
	    //  (struct sockaddr *) &client, len);
	} else {
	    // Error or timeout. Check errno == EAGAIN or
	    // errno == EWOULDBLOCK to check whether a timeout happened
	}
    }
    return 0;
}

