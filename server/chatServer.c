#include "constants.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/epoll.h>

#define CHAT_BACKLOG 10

struct client {
	char accountName[DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH + 1];
	int fd;
	struct client* next;
};

struct client* addNode(struct client** listBegin, int fd) {
	struct client* newNode = malloc(sizeof(struct client));
	if (newNode == (struct client*)-1) {
		perror("malloc");
		return NULL;
	}
	newNode->fd = fd;
	newNode->next = *listBegin;
	*listBegin = newNode;
	return newNode;
}

void removeNode(struct client** listBegin, struct client* nodeToRemove) {
	close(nodeToRemove->fd);
	if (*listBegin == nodeToRemove) {
		*listBegin = nodeToRemove->next;
	}
	else {
		struct client* p = *listBegin;
		while (p->next != nodeToRemove) {
			p = p->next;
		}
		p->next = nodeToRemove->next;
	}
	free(nodeToRemove);
}

int recvAccountName(int fd, char* accountNameHolder, int accountNameHolderLength) { //returns -1 on failure
	int recvBytes = recv(fd, accountNameHolder, accountNameHolderLength, 0);
	if (recvBytes < 0) {
		perror("send");
		return -1;
	}
	if (*(accountNameHolder + recvBytes - 1)) {
		return -1;
	}
	else {
		return recvBytes;
	}
}

int main(void)
{
	char recvBuffer[MESSAGE_MAX_BYTESIZE];
	char sendBuffer[MESSAGE_MAX_BYTESIZE];
	int listenSock, newSock;
	struct sockaddr_un local, remote;

	if ((listenSock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}

	local.sun_family = AF_UNIX;
	strcpy(local.sun_path, CHAT_SOCKET_FILEPATH);
	unlink(local.sun_path);
	{
		int len = strlen(local.sun_path) + sizeof(local.sun_family);
		if (bind(listenSock, (struct sockaddr *)&local, len) == -1) {
			perror("bind");
			exit(1);
		}
	}

	if (listen(listenSock, CHAT_BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	printf("Chat server: Waiting for connections...\n");

	//Set up epoll
	int epollfd;
	if ((epollfd = epoll_create1(0)) == -1) {
		perror("epoll_create1");
		exit(1);
	}
	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = NULL; //NULL represents the listening socket (listenSock)
	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, listenSock, &event) < 0) {
		perror("epoll_ctl");
		exit(1);
	}
	struct epoll_event events[EPOLL_MAX_EVENTS];
	int numEvents;

	struct client* listBegin = NULL;
	struct client* pNode;

	int sockaddrLength;
	uint16_t recvMsgLength, sendMsgLength;
	for (;;) {
		if ((numEvents = epoll_wait(epollfd, events, EPOLL_MAX_EVENTS, -1)) < 0) {
			perror("epoll_wait");
			continue;
		}
		for (int i = 0; i < numEvents; i++) {
			if (events[i].data.ptr == NULL) { //New connection
				sockaddrLength = sizeof(remote);
				if ((newSock = accept(listenSock, (struct sockaddr *)&remote, (socklen_t*)&sockaddrLength)) == -1) {
					perror("accept");
				}
				else {
					printf("Connected.\n");
					if (addNode(&listBegin, newSock) == NULL) {
						close(newSock);
					}
					else{
						//Get the account name and add it to the struct
						if (recvAccountName(newSock, listBegin->accountName, DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH) == -1) {
							removeNode(&listBegin, listBegin);
						}
						else {
							printf("new account connected: %s\n", listBegin->accountName);
							event.data.ptr = listBegin;
							if (epoll_ctl(epollfd, EPOLL_CTL_ADD, newSock, &event) < 0) {
								perror("epoll_ctl");
								//Revert the node addition to the linked list
								removeNode(&listBegin, listBegin);
							}
						}
					}
				}
			}
			else {
				//Receive the message
				recvMsgLength = sizeof recvBuffer;
				if ((recvMsgLength = recvall(((struct client*)(events[i].data.ptr))->fd, recvBuffer, recvMsgLength)) <= 0) {
					removeNode(&listBegin, events[i].data.ptr);
					continue;
				}
				
				printf("received %d bytes\n", recvMsgLength);

				strncpy(sendBuffer + sizeof sendMsgLength, ((struct client*)(events[i].data.ptr))->accountName, sizeof ((struct client*)(events[i].data.ptr))->accountName);
				strncat(sendBuffer + sizeof sendMsgLength + strlen(sendBuffer + sizeof sendMsgLength) + 1, recvBuffer + sizeof recvMsgLength, recvMsgLength - sizeof recvMsgLength);
				sendMsgLength = sizeof sendMsgLength + strlen(sendBuffer + sizeof sendMsgLength) + 1 + recvMsgLength - sizeof recvMsgLength;
				*((uint16_t*)sendBuffer) = htons(sendMsgLength);

				for (pNode = listBegin; pNode != NULL; pNode = pNode->next) {
					if (pNode != events[i].data.ptr) {
						if (sendall(pNode->fd, sendBuffer, sendMsgLength) < 0) {
							//Remove conflicting node
							if (epoll_ctl(epollfd, EPOLL_CTL_DEL, pNode->fd, NULL) == -1) {
								perror("epoll_ctl");
								fprintf(stderr, "Couldn't deregister a file descriptor from epoll before deleting the struct associated with it");
							}
							removeNode(&listBegin, pNode);
						}
					}
				}
			}
		}
	}

	return 0;
}