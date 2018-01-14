#pragma once
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>

int recvall(int fd, void* buffer, int buffersize) { //Return values: n(number of bytes received >0) - success, 0 - connection closed by peer, -1 - system call failure(error)
													//Assumeix que buffersize > sizeof msgsize ja que sinó no tindria cap sentit
	int bytesReceived;
	int totalBytesReceived = 0;
	uint16_t msgsize;
	do {
		switch (bytesReceived = recv(fd, ((char*)buffer) + totalBytesReceived, buffersize - totalBytesReceived, 0)) {
		case 0:
			//close(fd);
			return 0;
		case -1:
			perror("recv");
			return -1;
		}
		totalBytesReceived += bytesReceived;
	} while (totalBytesReceived < sizeof(uint16_t));

	msgsize = ntohs(*((uint16_t*)buffer));
	if (msgsize > buffersize) {
		return -1;
	}

	while (msgsize > totalBytesReceived) {
		switch (bytesReceived = recv(fd, ((char*)buffer) + totalBytesReceived, buffersize - totalBytesReceived, 0)) {
		case 0:
			//close(fd);
			return 0;
		case -1:
			perror("recv");
			return -1;
		}
		totalBytesReceived += bytesReceived;
	}
	//totalBytesReceived -= msgsize;
	//memcpy(buffer, ((char*)buffer) + msgsize, totalBytesReceived);
	return msgsize;
}

int sendall(int fd, void* buffer, int msglen) { //Returns 0 on success and -1 on error
	unsigned int byteSent, totalByteSent = 0;
	while (totalByteSent < msglen) {
		if ((byteSent = send(fd, ((char*)buffer) + totalByteSent, msglen - totalByteSent, 0)) < 0) {
			perror("send");
			return -1;
		}
		totalByteSent += byteSent;
	}
	return 0;
}