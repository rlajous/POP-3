//
// Created by francisco on 26/10/18.
//

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "spcpServerCredentials.h"

#define N(x) (sizeof(x)/sizeof((x)[0]))

extern bool
validate_user(char *username, char *password){
    for(int i = 0; i <  N(USERS); i++) {

        if(     0 == strcmp(username, USERS[i].username) &&
                0 == strcmp(password, USERS[i].password)
                ) {
            return true;
        }
    }
    return false;
}

extern bool
user_present(char *username){
    for(int i = 0; i <  N(USERS); i++) {
        if(0 == strcmp(username, USERS[i].username))
            return true;
    }
    return false;
}