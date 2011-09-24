#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/queue.h>
#include <ctype.h>

#include "proxy.h"
#include "net.h"
#include "list.h"
#include "http_message.h"

#define PORT "8080"

// TODO: Test all the edge cases
char *read_line(int sockfd)
{
	int buffer_size = 2; 
	char *line = (char*)malloc(sizeof(char)*buffer_size+1); 
	char c;
	int length = 0;	
	int counter = 0;

	while(1) 
	{
		length = recv(sockfd, &c, 1, 0); 
		line[counter++] = c; 

		if(c == '\n')
		{
			line[counter] = '\0'; 
			return line; 
		}

		// reallocate the buffer 
		if(counter == buffer_size)
		{
			buffer_size *= 2; 

			// TODO: should probably allocate +1 for the null terminator,
			// but not sure.
			line = (char*)realloc(line, sizeof(char)*buffer_size); 
		}

	}

	return NULL;
}

int containing_forbidden_words(char str[]){

    // Forbidden words
    char *words[] = {"SpongeBob", "Britney Spears", "Paris Hilton", "Norrkӧping", "Norrk&ouml;ping", "Norrk%C3%B6ping"};
    int hits[] = {0, 0, 0, 0, 0, 0}; // Every forbidden word need to have a zero in this array to be able to count number of char hits.
    int numb_words = 6; // Number of forbidden words

    int str_length = strlen(str);
    int c, w;   // Index for char in str, and index for word in words

    // Search for forbidden words
    for (c = 0; c < str_length; c++)
    {
        for (w = 0; w < numb_words; w++)
        {
            if (tolower(words[w][ hits[w] ]) == tolower(str[c])){
                if(++hits[w] == strlen(words[w]))
                    return 1;
            }
            else
                hits[w] = 0;
        }
    }

    return 0;
}

int send_to_client(int client_sockfd, char data[], int packages_size)
{
    // if packages_size is set to 0, then the function will try to send all data as one package.
    int length = strlen(data);
    if(packages_size < 1){
        if(send(client_sockfd, data, length, 0) == -1)
	    {
		    perror("Couldn't send data to the client.");
		    return -1;
	    }
    }
    else
    {
        int p;
        for(p = 0; p*packages_size + packages_size < length; p++){
            if(send(client_sockfd, (data + p*packages_size), packages_size, 0) == -1)
	        {
		        perror("Couldn't send any or just some data to the client. (loop)\n");
		        return -1;
            }
        }

        if (p*packages_size < length)
        {
            if(send(client_sockfd, (data + p*packages_size), length - p*packages_size, 0) == -1)
	        {
		        perror("Couldn't send any or just some data to the client.\n");
		        return -1;
            }
        }
    }

    return 0;
}

int http_request_send(int sockfd, http_request *req)
{
	LOG(LOG_TRACE, "Requesting ");
	printf("%s\n", req->search_path);

	char *request_buffer = http_build_request(req); 

	// send the http request to the web server 
	if(send(sockfd, request_buffer, strlen(request_buffer), 0) == -1)
	{
		perror("send"); 
		return 1; 
	}

	LOG(LOG_TRACE, "Sent HTTP header to web server\n");
	
	return 0;
}

void handle_client(int client_sockfd)
{
	char *line; 
	int server_sockfd; 
	http_request *req; 

	req = http_read_header(client_sockfd);
	if(req == NULL)
	{
		LOG(LOG_ERROR, "Failed to parse the header\n");
		return;
	}

	server_sockfd = http_connect(req);
	if(server_sockfd == -1) 
	{
		LOG(LOG_ERROR, "Failed to connect to host\n");
		return; 
	}

	LOG(LOG_TRACE, "Connected to host\n");

	http_request_send(server_sockfd, req); 

	LOG(LOG_TRACE, "Beginning to retrieve the response header\n");
	while(1)
	{
		line = read_line(server_sockfd); 
		send_to_client(client_sockfd, line, 0); 
		if(line[0] == '\r' && line[1] == '\n') 
		{
			// We received the end of the HTTP header 
			LOG(LOG_TRACE, "Received the end of the HTTP response header\n");
		}

		free(line); 
	}

	LOG(LOG_TRACE, "Beginning to retrieve content\n");
	char *temp = http_read_chunk(server_sockfd);
	LOG(LOG_TRACE, "Received the content\n");

	send_to_client(client_sockfd, temp, 1337);
	close(server_sockfd);
}

void start_server(unsigned int port)
{
	printf("Starting server\n"); 

	int sockfd, new_fd; 
	struct addrinfo hints, *servinfo, *p; 
	struct sockaddr_storage their_addr; 
	socklen_t sin_size; 
	int rv; 
	int yes = 1; 

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; 
	hints.ai_socktype = SOCK_STREAM; 
	hints.ai_flags = AI_PASSIVE; 

	if((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0)
	{
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return; 
	}

	for(p = servinfo; p != NULL; p = p->ai_next)
	{
		if((sockfd = socket(p->ai_family, p->ai_socktype, 
			p->ai_protocol)) == -1) 
		{
			perror("server: socket"); 
			continue; 
		}

		if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, 
			sizeof(int)) == -1) 
		{
			perror("setsockopt"); 
			exit(1); 
		}

		if(bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) 
		{
			close(sockfd); 
			perror("server: bind"); 
			continue; 
		}

		break; 
	}

	if(p == NULL)
	{
		fprintf(stderr, "server: failed to bind\n"); 
		return; 
	}

	freeaddrinfo(servinfo); 

	if(listen(sockfd, 10) == -1)
	{
		perror("listen"); 
		exit(1); 
	}

	printf("server: waiting for connections..\n"); 
	while(1) 
	{
		sin_size = sizeof(their_addr); 
		new_fd = accept(sockfd, (struct sockaddr*)&their_addr, &sin_size); 
		if(new_fd == -1)
		{
			perror("accept"); 
			continue; 
		}
		
		printf("Receieved connection\n"); 

		pid_t child_pid = fork();
		if(!child_pid) 
		{
			handle_client(new_fd); 

			close(new_fd); 
			exit(0); 
		}
		close(new_fd); 
	}
}

int main(int argc, char *argv[])
{
	start_server(8080); 
	return 0; 
}

