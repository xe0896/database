#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "protocol.h"
#include "typedef.h"

// Given a buffer and the length, it would send
ssize_t send_all(int fd, void *buf, int len) {
    // The amount of we sent must be tracked so we can return it
    ssize_t sent = 0;
    // Cast the buffer to be in terms of bytes since pointer arithmetic
    // on void isn't allowed
    uint8_t *buf_byte = (uint8_t *)buf;

    // We would continuously decrement len until it becomes 0 meaning
    // we are finished
    while (len > 0) {
        // Write to the socket
        ssize_t written = write(fd, buf_byte, len);
        if (written == -1) {
            perror("write");
            return -1;
        }
        // write() would return the amount written, so we can track it by doing this
        sent += written;
        // Move the buffer pointer
        buf_byte += written;

        // Tracks so we know when to end
        len = len - written;
    }

    return sent;
}

// Mirrored function of the one above but if read() returns 0 then it
// means the connection has closed, so we should bail out and the caller
// can request this read again
ssize_t recv_all(int fd, void *buf, int len) {
    ssize_t sent = 0;
    uint8_t *buf_byte = (uint8_t *)buf;

    while (len > 0) {
        // The read here is blocking, meaning that it will wait here until it receive some bytes, once it does
        // it will continue for while loop until it receives the requested amount by len, as it consumes the
        // kernel buffer would track how much has been read and move the pointer, meaning future calls to recv_all
        // would be correctly pointed to the next item whether it be a payload or new message
        ssize_t readed = read(fd, buf_byte, len);
        if (readed == -1) {
            perror("read");
            return -1;
        } else if (readed == 0) {
            return sent;
        }
        sent += readed;
        buf_byte += readed;

        len = len - readed;
    }

    return sent;
}

SendResult send_message(int fd, char *message, ssize_t message_length) {
    // The input buffer stores it as ssize_t, so we cast to 4 bytes and get
    // the big endian version before we send the length to make it the network length
    uint32_t network_length = htonl((uint32_t)message_length);

    // Send the length and the size is a fixed 4 bytes so we can send the sizeof()
    ssize_t length_field = send_all(fd, &network_length, sizeof(network_length));

    if (length_field == -1)
        return SEND_ERROR;

    ssize_t payload_field = send_all(fd, message, message_length);

    if (payload_field == -1)
        return SEND_ERROR;

    return SEND_SUCCESS;
}

RecvResult recv_message(int fd, char **out_buf, uint32_t *out_len) {
    // Stores the size that was provided, we dont create a pointer via malloc
    // so we do not need to free it in the end
    uint32_t given_size;

    // Grab the length field and pass the buffer as the above and the size
    // would always be 4 bytes
    ssize_t length_field = recv_all(fd, &given_size, sizeof(uint32_t));

    if (length_field == -1)
        return RECV_ERROR;

    // If it wasn't -1 and wasn't 4 bytes returned then it means the user disconnected
    // halfway so this is a different type of error that can be handled
    if (length_field != sizeof(uint32_t))
        return RECV_DISCONNECTED;

    // Create it back to the devices intepretation of MSB
    uint32_t network_length = ntohl(given_size);
    // Malloc the buffer using the length that we found out
    void *buf = malloc(network_length + 1); // + 1 so we can add null terminator

    // Grab the payload
    ssize_t payload_field = recv_all(fd, buf, network_length);

    // Given that something went wrong then we must also free the buffer
    if (payload_field == -1) {
        free(buf);
        return RECV_ERROR;
    }

    // Same idea with the payload
    if (payload_field != network_length) {
        free(buf);
        return RECV_DISCONNECTED;
    }

    ((char *)buf)[network_length] = '\0';

    // Dereference the pointers provided by the caller to let it know the values
    *out_len = network_length;
    *out_buf = buf;

    return RECV_SUCCESS;
}
