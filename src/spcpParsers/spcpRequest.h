#ifndef PROJECT_REQUEST_H
#define PROJECT_REQUEST_H

enum spcp_response_status {
    success             = 0x00,
    auth_err            = 0x01,
    invalid_command     = 0x02,
    invalid_arguments   = 0x03,
    err                 = 0x04,
};


enum spcp_request_cmd {
    spcp_user,
    spcp_pass,
    spcp_concurrent_connections,
    spcp_transfered_bytes,
    spcp_historical_accesses,
    spcp_active_transformation,
    spcp_set_buffer_size,
    spcp_change_transformation,
    spcp_set_timeouts,
    spcp_quit,
};

struct spcp_request {
    enum spcp_request_cmd cmd;
    char *arg1;
    char *arg2
};

struct spcp_request_parser {
    struct spcp_request request;
};

#endif