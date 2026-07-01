#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "typedef.h"
#include "protocol.h"

void read_input(InputBuffer *input_buffer) {
    // getline takes the buffer and the size of it and updates what is provided by the user to the buffer and updates length
    ssize_t bytes_read = getline(&(input_buffer->buffer), &(input_buffer->buffer_length), stdin);
    if(bytes_read <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    } 

    input_buffer->input_length = bytes_read - 1; // Remove the newline from user pressing enter, .exit\n\0 -> .exit\n
    input_buffer->buffer[bytes_read - 1] = 0; // Replace \n with \0, to \n
}

void free_input_buffer(InputBuffer* input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

MetaCommandResult meta_command(InputBuffer* input_buffer, int fd) {
    // Meta commands are in the form .x 
    if(strcmp(input_buffer->buffer, ".exit") == 0) {
        free_input_buffer(input_buffer);
        //db_close(table);
        close(fd);
        exit(EXIT_SUCCESS);
    } else {
        return META_COMMAND_UNRECOGNISED;
    }
}

InputBuffer* new_input_buffer() {
    // A new buffer would assume everything being empty
    InputBuffer* input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

void print_prompt() {
    printf("db > ");
}

int main() {
    // Child process
    InputBuffer* input_buffer = new_input_buffer();
    
    struct addrinfo client_hints;
    struct addrinfo *client_ptr, *client_res;

    memset(&client_hints, 0, sizeof(client_hints));

    client_hints.ai_family = AF_INET;
    client_hints.ai_socktype = SOCK_STREAM;
    client_hints.ai_protocol = IPPROTO_TCP;

    // This is the only different part here, the IP and port are pointing to the server.
    // this port is the loopback address pointing back to this machine
    int client_status = getaddrinfo("127.0.0.1", PORT, &client_hints, &client_res);
    if(client_status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(client_status));
        exit(EXIT_FAILURE);
    }
        
    int client_fd;

    for(client_ptr = client_res; client_ptr != NULL; client_ptr = client_ptr->ai_next) {
        client_fd = socket(client_ptr->ai_family, client_ptr->ai_socktype, client_ptr->ai_protocol);
        if(client_fd == -1) continue;

        // We are testing for an address that allows us to connect to the server, so this is not bind() but connect() instead
        int client_connect = connect(client_fd, client_ptr->ai_addr, client_ptr->ai_addrlen);

        if(client_connect == -1) {
            perror("bind");
            continue;
        } else if (client_connect == 0) {
            break;
        }

        close(client_fd);
    }

    freeaddrinfo(client_res);

    while(true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.') {
            switch(meta_command(input_buffer, client_fd)) {
                case(META_COMMAND_SUCCESS):
                    continue;
                case(META_COMMAND_UNRECOGNISED):
                    printf("Unrecognised command '%s'.\n", input_buffer->buffer);
                    continue;
            }
        }

        SendResult send_result = send_message(client_fd, input_buffer->buffer, input_buffer->input_length);

        if(send_result != SEND_SUCCESS) break;

        char *buf;
        uint32_t len;

        // send_message() is 
        RecvResult recv_result = recv_message(client_fd, &buf, &len);
        if(recv_result != RECV_SUCCESS) break;

        printf("%s", buf);
    }

    close(client_fd);
}