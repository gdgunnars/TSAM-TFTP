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

// argv[0] =  program name
// argv[1] = port number
// argv[2] = directory to serve

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
    for (i = 2; i < 516; i++, c++) {
        filename[c] = (char) message[i];

        if (message[i] == '\0') {
            i++;
            break;
        }
   }

   c = 0;
   for (size_t j = i; j < 516; j++, c++) {
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
	  printf("\nWaiting for request...\n");
	  //char buffer[516];
	  //size_t op_code_length = 2;

	  // Receive up to one byte less than declared, because it will
	  // be NUL-terminated later.
	socklen_t len = (socklen_t) sizeof(client);
	size_t size = sizeof(message);
	ssize_t n = get_message(sockfd, message ,size, &client, &len);

	if (n >= 0) {
	    uint16_t opcode = message[1];
		message[n] = '\0';
		fprintf(stdout, "Message: %s\n", message);
		
        fprintf(stdout, "Received request!\n");
		fprintf(stdout, "Number of bytes recieved: %zd\n", n);
	    fprintf(stdout, "Opcode: %d\n", opcode);
        
		char filename[512];
		char mode[512];
		
		get_filename_and__mode(message, filename, mode);
		      		
		fprintf(stdout, "Filename: %s\n", filename);
		fprintf(stdout, "Mode: %s\n", mode);
		fflush(stdout);


        sendto(sockfd, message, (size_t) n, 0,
                   (struct sockaddr *) &client, len);
        } else {
            // Error or timeout. Check errno == EAGAIN or
            // errno == EWOULDBLOCK to check whether a timeout happened
        }
    }
	return 0;
}

