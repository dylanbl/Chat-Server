// Dylan Lewis 
// December 13, 2022 

// This program implements a chat application 
// that has rooms which are joinable, and clients 
// in these rooms can send messages to all the other 
// clients in the room 

// Note: Descriptions of the functions and structs used 
//		 by this program are in src/chat_server.h 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/select.h>

#include "sockettome.h"
#include "chat_server.h"
#include "dllist.h"
#include "jval.h"
#include "jrb.h"

void *
ClientThread(void *arg); 

void *
RoomThread(void *arg); 

void
CloseNewClient(Client, FILE *, FILE *); 

int main(int argc, char** argv) {
	int sock, port, fd; 
	pthread_t rooms[argc - 2]; 
	JRB room_tree; 
	Room room; 
	Client client; 

	if (argc < 3) {
		fprintf(stderr, "usage: ./chat-server port room-names\n"); 
		exit(1); 
	}

	/* Create a Room struct and thread for each room */
	room_tree = make_jrb(); 
	for (int i = 2; argv[i] != NULL; i++) {
		room = malloc(sizeof(struct room)); 

		room->room_name = malloc(sizeof(char) * (strlen(argv[i]) + 1)); 
		memcpy(room->room_name, argv[i], strlen(argv[i]) + 1); 

		room->member_list = new_dllist(); 
		room->message_queue = new_dllist();
		room->client_input = 0; 

		pthread_mutex_init(&room->lock, NULL); 
		pthread_cond_init(&room->cond, NULL); 

		if (pthread_create(&rooms[i - 2], NULL, &RoomThread, (void *) room) != 0) {
			perror("pthread_create"); 
			exit(1); 
		}

		jrb_insert_str(room_tree, strdup(room->room_name), new_jval_v(room)); 
	}

	/* Loop infinitely and accept connections from new clients */
	port = atoi(argv[1]); 
	sock = serve_socket(port); 

	while (1) {
		pthread_t tid; 
		fd = accept_connection(sock);

		client = malloc(sizeof(struct client)); 
		client->fd = fd; 
		client->room_tree = room_tree; 
		client->user_name = NULL;

		if (pthread_create(&tid, NULL, &ClientThread, (void *) client) != 0) {
			perror("pthread_create"); 
			exit(1); 
		}
	}

	return 0;
}

void *
RoomThread(void *arg) {
	Room room; 
	Client client; 
	Dllist msg_ptr, client_ptr; 

	room = (Room) arg; 

	while (1) {
		/* Wait for there to be a message from a client */
		pthread_mutex_lock(&room->lock); 
		while (!room->client_input) {
			pthread_cond_wait(&room->cond, &room->lock); 
		}

		/* Send all messages on the message list to all clients in the room */
		dll_traverse(msg_ptr, room->message_queue) { 
			dll_traverse(client_ptr, room->member_list) {
				client = (Client) client_ptr->val.v;
				
				if (fputs(msg_ptr->val.s, client->fout) > 0) {
					fflush(client->fout); 
				}
			}

			free(msg_ptr->val.s);
		}

		/* Reset the message list and conditional variable's predicate */
		free_dllist(room->message_queue); 
		room->message_queue = new_dllist(); 
		room->client_input = 0; 

		pthread_mutex_unlock(&room->lock); 
	}

	/* Never reached, fix compiler warning */
	return NULL; 
}

void *
ClientThread(void *arg) {
	pthread_detach(pthread_self()); 

	FILE *fin, *fout; 
	Client client;
	Room room; 
	JRB tmp; 
	Dllist ptr; 
	char buf[1024], msg[2048]; 
	char *room_name, *allocd_msg; 

	client = (Client) arg;

	fin = fdopen(client->fd, "r"); 
	fout = fdopen(client->fd, "w"); 
	client->fout = fout; 	
	client->fin = fin; 

	PrintRooms(client);

	if (fputs("Enter your chat name (no spaces):\n", fout) < 0) {
		CloseClient(client);
		pthread_exit(NULL);
	}

	if (fflush(fout) != 0) {
		CloseClient(client);
		pthread_exit(NULL); 
	}

	memset(buf, 0, 1024); 
	if (fgets(buf, 1024, fin) == NULL) {
		CloseClient(client);
		pthread_exit(NULL); 
	}

	buf[strcspn(buf, "\n")] = '\0'; 
	CopyStr(&client->user_name, buf); 

	if (fputs("Enter chat room:\n", fout) < 0) {
		CloseClient(client);
		pthread_exit(NULL);
	}

	if (fflush(fout) != 0) {
		CloseClient(client);
		pthread_exit(NULL); 
	}

	memset(buf, 0, 1024); 
	if (fgets(buf, 1024, fin) == NULL) {
		CloseClient(client); 
		pthread_exit(NULL); 
	}

	buf[strcspn(buf, "\n")] = '\0'; 
	CopyStr(&room_name, buf);

	tmp = jrb_find_str(client->room_tree, room_name); 
	room = (Room) tmp->val.v; 

	/* Add the new client to the room */
	pthread_mutex_lock(&room->lock); 

	dll_append(room->member_list, new_jval_v(client)); 

	memset(buf, 0, 1024); 
	sprintf(buf, "%s has joined\n", client->user_name);
	CopyStr(&allocd_msg, buf); 
	dll_append(room->message_queue, new_jval_s(strdup(allocd_msg))); 
	free(allocd_msg); 

	room->client_input = 1;
	pthread_cond_signal(&room->cond);
	pthread_mutex_unlock(&room->lock); 

	/* Loop and recieve messages from the client */
	while (1) {
		memset(buf, 0, 1024);

		if (fgets(buf, 1024, fin) == NULL) {
			/* Remove a user from their room */
			pthread_mutex_lock(&room->lock); 

			dll_traverse(ptr, room->member_list) {

				if (!strcmp(((Client) ptr->val.v)->user_name, client->user_name)) {
					dll_delete_node(ptr);
					break; 
				}
			}

			memset(msg, 0, 2048); 
			sprintf(msg, "%s has left\n", client->user_name); 
			CopyStr(&allocd_msg, msg); 

			dll_append(room->message_queue, new_jval_s(strdup(allocd_msg))); 
			room->client_input = 1; 
			pthread_cond_signal(&room->cond); 

			pthread_mutex_unlock(&room->lock); 

			free(allocd_msg); 
			free(room_name); 
			CloseClient(client);

			pthread_exit(NULL);
		}

		/* Add a client's message to their room's message list */
		buf[strcspn(buf, "\n") + 1] = '\0';
		sprintf(msg, "%s: %s", client->user_name, buf); 
		CopyStr(&allocd_msg, msg); 

		pthread_mutex_lock(&room->lock); 

		dll_append(room->message_queue, new_jval_s(strdup(allocd_msg))); 
		room->client_input = 1; 

		pthread_cond_signal(&room->cond); 
		pthread_mutex_unlock(&room->lock); 

		free(allocd_msg);
	}

	/* Never reached, fix compiler warning */
	return NULL; 
}

void 
PrintRooms(Client client) {
	char msg[256];
	Room room; 
	JRB jrb_ptr; 
	Dllist dll_ptr; 	

	/* Traverse the chat rooms and print out any members */
	fputs("Chat Rooms:\n\n", client->fout); 
	fflush(client->fout);
	jrb_traverse(jrb_ptr, client->room_tree) {
		room = (Room) jrb_ptr->val.v; 

		memset(msg, 0, 256); 
		sprintf(msg, "%s:", jrb_ptr->key.s); 
		fputs(msg, client->fout); 
		fflush(client->fout); 
		
		/* Print out members in a room */
		pthread_mutex_lock(&room->lock);

		dll_traverse(dll_ptr, room->member_list) {
			memset(msg, 0, 256); 
			sprintf(msg, " %s", ((Client) dll_ptr->val.v)->user_name); 
			fputs(msg, client->fout); 
			fflush(client->fout);
		}

		pthread_mutex_unlock(&room->lock);

		fputs("\n", client->fout); 
		fflush(client->fout);
	}
	fputs("\n", client->fout);
	fflush(client->fout);
}

void
CopyStr(char **dest, char *buf) {
	int str_len; 
	
	str_len = strlen(buf) + 1; 
	*dest = malloc(sizeof(char) * str_len);
	memcpy(*dest, buf, str_len); 
}

void
CloseClient(Client client) {

	fclose(client->fin);
	fclose(client->fout);
	close(client->fd); 

	free(client->user_name);
	free(client);
}
