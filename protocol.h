#ifndef PROTOCOL_H
#define PROTOCOL_H

#define PORT "5001"

typedef enum {
    RECV_SUCCESS,
    RECV_DISCONNECTED,
    RECV_ERROR
} RecvResult;

typedef enum {
    SEND_SUCCESS,
    SEND_ERROR
} SendResult;

ssize_t send_all(int fd, void *buf, int len);
ssize_t recv_all(int fd, void *buf, int len);
SendResult send_message(int fd, char *message, ssize_t message_length);
RecvResult recv_message(int fd, char **out_buf, uint32_t *out_len);

#endif