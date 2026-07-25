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
    if (bytes_read <= 0) {
        printf("Error reading input\n");
        exit(EXIT_FAILURE);
    }

    input_buffer->input_length = bytes_read - 1; // Remove the newline from user pressing enter, .exit\n\0 -> .exit\n
    input_buffer->buffer[bytes_read - 1] = 0;    // Replace \n with \0, to \n
}

void free_input_buffer(InputBuffer *input_buffer) {
    free(input_buffer->buffer);
    free(input_buffer);
}

MetaCommandResult meta_command(InputBuffer *input_buffer, int fd) {
    // Meta commands are in the form .x
    if (strcmp(input_buffer->buffer, ".exit") == 0) {
        free_input_buffer(input_buffer);
        // db_close(table);
        close(fd);
        exit(EXIT_SUCCESS);
    } else {
        return META_COMMAND_UNRECOGNISED;
    }
}

InputBuffer *new_input_buffer() {
    // A new buffer would assume everything being empty
    InputBuffer *input_buffer = malloc(sizeof(InputBuffer));
    input_buffer->buffer = NULL;
    input_buffer->buffer_length = 0;
    input_buffer->input_length = 0;

    return input_buffer;
}

void print_prompt() {
    printf("db > ");
}

// Keep trying to connect to the server until it comes back up (e.g. after a recompile).
// Returns a connected client_fd.
int connect_to_server() {
    struct addrinfo client_hints;
    struct addrinfo *client_ptr, *client_res;

    memset(&client_hints, 0, sizeof(client_hints));

    client_hints.ai_family = AF_INET;
    client_hints.ai_socktype = SOCK_STREAM;
    client_hints.ai_protocol = IPPROTO_TCP;

    // This is the only different part here, the IP and port are pointing to the server.
    // this port is the loopback address pointing back to this machine
    int client_status = getaddrinfo("127.0.0.1", PORT, &client_hints, &client_res);
    if (client_status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(client_status));
        exit(EXIT_FAILURE);
    }

    while (true) {
        for (client_ptr = client_res; client_ptr != NULL; client_ptr = client_ptr->ai_next) {
            int client_fd = socket(client_ptr->ai_family, client_ptr->ai_socktype, client_ptr->ai_protocol);
            if (client_fd == -1)
                continue;

            // We are testing for an address that allows us to connect to the server, so this is not bind() but connect() instead
            int client_connect = connect(client_fd, client_ptr->ai_addr, client_ptr->ai_addrlen);

            if (client_connect == 0) {
                freeaddrinfo(client_res);
                return client_fd;
            }

            close(client_fd);
        }

        // No candidate accepted the connection, server is probably down/recompiling. Wait and retry.
        printf("Could not reach server, retrying...\n");
        sleep(1);
    }
}

int main() {
    // Child process
    InputBuffer *input_buffer = new_input_buffer();

    int client_fd = connect_to_server();

    for (int i = 0; i < 50; i++) {
        char str[20];
        char *pos = str;

        memcpy(pos, "insert ", 7);
        pos += 7;
        // snprintf() writes down the given integer i into the buffer str and when it is done it would put
        // a \0 for us, so we can get the length of it using strlen() since strlen() waits for \0
        snprintf(pos, sizeof(str), "%d", i);
        int len = strlen(pos);

        pos += len;

        memcpy(pos, " foo foo", 8);

        pos += 8;

        *pos = '\0';

        SendResult send_result = send_message(client_fd, str, len + 15);
        if (send_result != SEND_SUCCESS)
            break;
    }

    while (true) {
        print_prompt();
        read_input(input_buffer);

        if (input_buffer->buffer[0] == '.') {
            switch (meta_command(input_buffer, client_fd)) {
            case (META_COMMAND_SUCCESS):
                continue;
            case (META_COMMAND_UNRECOGNISED):
                printf("Unrecognised command '%s'.\n", input_buffer->buffer);
                continue;
            }
        }

        SendResult send_result = send_message(client_fd, input_buffer->buffer, input_buffer->input_length);

        // Server went away (e.g. recompiled). Reconnect and re-prompt.
        if (send_result != SEND_SUCCESS) {
            printf("Lost connection to server, reconnecting...\n");
            close(client_fd);
            client_fd = connect_to_server();
            continue;
        }

        char *buf;
        uint32_t len;

        // send_message() is
        RecvResult recv_result = recv_message(client_fd, &buf, &len);
        if (recv_result != RECV_SUCCESS) {
            printf("Lost connection to server, reconnecting...\n");
            close(client_fd);
            client_fd = connect_to_server();
            continue;
        }

        printf("%s", buf);
    }

    close(client_fd);
}