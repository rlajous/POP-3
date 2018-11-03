//
// Created by francisco on 26/10/18.
//

#ifndef PROJECT_SPCPSERVERCREDENTIALS_H
#define PROJECT_SPCPSERVERCREDENTIALS_H

#include <stdbool.h>

struct user_info {
    char *username;
    char *password;
};

static const struct user_info USERS[] = {
        {
            .username = "admin",
            .password = "admin",
        }
};

extern bool
validate_user(char *username, char *password);

extern bool
user_present(char *username);
#endif
