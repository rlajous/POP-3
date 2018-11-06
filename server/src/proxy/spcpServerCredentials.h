//
// Created by francisco on 26/10/18.
//

#ifndef PROJECT_SPCPSERVERCREDENTIALS_H
#define PROJECT_SPCPSERVERCREDENTIALS_H

#include <stdbool.h>

/** Estructura que contiene la informacion de
 * autenticación de los usuarios*/
struct user_info {
    char *username;
    char *password;
};

/** Lista de todos los usuarios que pueden
 * acceder al servidor SPCP*/
static const struct user_info USERS[] = {
        {
            .username = "admin",
            .password = "admin",
        }
};

/** Valida que el par usuario contraseña pertenezca
 * a un usuario valido del servidor*/
extern bool
validate_user(char *username, char *password);

/** Valida que exista el nombre de usuario para
 * algún usuario del servidor SPCP*/
extern bool
user_present(char *username);
#endif
