#include <pthread.h>

#include "dllist.h"
#include "jrb.h"

/* Client struct */
typedef struct client {
    char *user_name; 
    
    // The socket that a client uses for I/O
    int fd;

    // The streams the client recieves I/O on 
    FILE *fout; 
    FILE *fin; 

    // A red-black tree containing a Room struct 
    // for each of the server's rooms 
    JRB room_tree; 
} * Client; 

/* Room struct */
typedef struct room {
    char *room_name;

    // Used as a boolean; the conditional variable's predicate
    int client_input; 

    // The conditional variable that is used to wait until there 
    // is a message for the server to send to each client  
    pthread_cond_t cond; 

    // The mutex used to lock a respective room struct to do I/O 
    // on its lists 
    pthread_mutex_t lock; 

    // Contains a Client for each user in the room 
    Dllist member_list; 

    // The list of messages that need to be sent to each client 
    Dllist message_queue; 
} * Room; 

/*
 * @name ClientThread 
 * @brief This function is run in its own thread to implement 
 *        the functionality of the client in the chat app. It 
 *        handles a client joining/leaving a room and reading 
 *        messages to be sent into a chat room. 
 * @param[in] A Client struct (see Client struct declaration for details) 
 * @returns A null pointer will always be returned from this function.
 */
void *
ClientThread(void *arg); 

/* 
 * @name RoomThread 
 * @brief This function is run in its own thread to implement the 
 *        functionality of a room server in the chat app. Each room 
 *        will have its own thread running this function. It is used 
 *        to send messages to all the clients in the room. 
 * @param[in] A Room struct (see Room struct declaration for details)
 * @returns A null pointer will always be returned from this function. 
 */
void *
RoomThread(void *arg); 

/* 
 * @name CloseClient
 * @brief Closes the file descriptors and pointers associated with a client
 *        and frees the memory allocated for the client. Used when a client 
 *        ends their session. 
 * @param[in] The client that terminated their session. 
 */
void
CloseClient(Client); 

/*
 * @name CopyStr
 * @brief Allocates a new string and copies the contents of a 
 *        fixed size buffer into that string.
 * @param[in] The string to be allocated 
 * @param[in] The buffer being copied from 
 */
void
CopyStr(char **, char *); 

/*
 * @name PrintRooms
 * @brief Prints the server's rooms and any users currently in 
 *        those rooms. 
 * @param[in] A Client struct (see Client struct declaration for details)
 */
void 
PrintRooms(Client);