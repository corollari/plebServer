#include "socket.h"
//#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#pragma comment (lib, "Ws2_32.lib")
#include "chat.h"
#include "map.h"

SOCKET sock; //Quiero ponerlo como variable privada de la clase pero por un problema con los includes no puedo

//sendall and recvall have been copied from plebServer/plebServer/plebServer/utils.h
int sendall(void* buffer, int msglen) { //Returns 0 on success and -1 on error
	unsigned int byteSent, totalByteSent = 0;
	while (totalByteSent < msglen) {
		if ((byteSent = send(sock, ((char*)buffer) + totalByteSent, msglen - totalByteSent, 0)) < 0) {
			perror("send");
			return -1;
		}
		totalByteSent += byteSent;
	}
	return 0;
}

int recvall(SOCKET fd, void* buffer, int buffersize) { //Return values: n(number of bytes received >0) - success, 0 - connection closed by peer, -1 - system call failure(error)
													//Assumeix que buffersize > sizeof msgsize ja que sinó no tindria cap sentit
	int bytesReceived;
	static int totalBytesReceived = 0;
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
	totalBytesReceived -= msgsize;
	memcpy_s(buffer, sizeof buffer, ((char*)buffer) + msgsize, totalBytesReceived);
	return msgsize;
}

uint16_t getServerReply(SOCKET sock, char* buffer) { //Returns 0 on failure
	uint16_t serverReply;
	unsigned int bytesReceived;
	unsigned int totalBytesReceived = 0;
	while (totalBytesReceived < sizeof(serverReply)) {
		if ((bytesReceived = recv(sock, ((char*)buffer) + totalBytesReceived, sizeof(uint16_t) - totalBytesReceived, 0)) < 0) {
			perror("recv");
			return 0;
			//sendErrorMsg("getServerReply: failed in recv");
		}
		else if (bytesReceived == 0) {
			return 0;
			//sendErrorMsg("getServerReply: server closed connection");
		}
		else {
			totalBytesReceived += bytesReceived;
		}
	}
	serverReply = ntohs(*((uint16_t *)buffer));
	return serverReply;
}

Socket::Socket(Chat* chat, Map** mapa) {
	m_chat = chat;
	m_mapa = mapa;
}

const char* Socket::startUp(){
	WSADATA wsa;

	//Initialising Winsock...
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		string error = "setup: Failed initializing WSA. Error Code : " + WSAGetLastError();
		return error.c_str();
	}
	return "";
}

const char* Socket::Login(const char* username, const char* password){ //Returns an empty string on success and a C string with the error message on failure

	struct addrinfo hints, *servinfo, *p;
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(SERVER_HOST, PORT, &hints, &servinfo)) != 0) {
		printf("getaddrinfo: %s", gai_strerror(rv));
		return "setup: getaddrinfo failure";
	}

	// loop through all the results and connect to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sock = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sock, p->ai_addr, p->ai_addrlen) == -1) {
			closesocket(sock);
			perror("client: connect");
			continue;
		}

		break;
	}

	if (p == NULL) {
		return "setup: failed to connect";
	}

	freeaddrinfo(servinfo); // all done with this structure

	uint16_t protocolVersion = htons(PROTOCOL_VERSION_CURRENT);
	if (sendall(&protocolVersion, sizeof protocolVersion) < 0) {
		closesocket(sock);
		return "handshake: failed to send";
	}

	if ((rv = getServerReply(sock, buffer)) == 0) {
		return "handshake: coulnd't get the server's reply";
	}
	else if (rv != PROTOCOL_HANDSHAKE_OK) {
		closesocket(sock);
		return "handshake: protocol is not up to date";
	}

	uint8_t accountNameLength = strlen(username);
	uint8_t passwordLength = strlen(password);
	buffer[0] = accountNameLength;
	strncpy_s(buffer + sizeof accountNameLength, sizeof buffer, username, accountNameLength);
	buffer[sizeof accountNameLength + accountNameLength] = passwordLength;
	strncpy_s(buffer + sizeof accountNameLength + accountNameLength + sizeof passwordLength, sizeof buffer, password, passwordLength);
	if (sendall(buffer, sizeof accountNameLength + accountNameLength + sizeof passwordLength + passwordLength) < 0) {
		closesocket(sock);
		return "login: failed to send credentials";
	}

	if ((rv = getServerReply(sock, buffer)) == 0) {
		return "login: coulnd't get the server's reply";
	}
	else if (rv == LOGIN_WRONG_PASSWORD) {
		closesocket(sock);
		return "login: rejected by the server - incorrect credentials";
	}
	else if (rv == LOGIN_ACCOUNT_DOESNT_EXIST) {
		closesocket(sock);
		return "login: rejected by the server - account doesn't exist";
	}
	else if (rv == LOGIN_SUCESS) {
		strcpy_s(m_username, username);
		u_long yes = 1;
		ioctlsocket(sock, FIONBIO, &yes);
		return "";
	}
	else {
		closesocket(sock);
		return "login: rejected by the server";
	}
}

void Socket::sendChatMessage(const char* message) {
	m_chat->addMessage(m_username, message);
	/*uint16_t msgCode = htons(MSGCODE_CHAT_BROADCAST_MESSAGE);
	uint16_t msgLength = sizeof msgLength + sizeof msgCode + strlen(message);
	((uint16_t*)buffer)[0] = htons(msgLength);
	((uint16_t*)buffer)[1] = msgCode;
	strcpy_s(buffer + 2 * sizeof(uint16_t), sizeof buffer - 2 * sizeof(uint16_t), message);
	if (sendall(buffer, msgLength) < 0) {
		//ERROR
	}*/
	
	uint16_t msgLength = sizeof msgLength + strlen(message);
	((uint16_t*)buffer)[0] = htons(msgLength);
	strcpy_s(buffer + 1 * sizeof(uint16_t), sizeof buffer - 1 * sizeof(uint16_t), message);
	if (sendall(buffer, msgLength) < 0) {
		//ERROR
	}
}

void Socket::update() {
	int receivedBytes;
	if ((receivedBytes=recvall(sock, buffer, sizeof buffer)) <= 0) {
		//error
	}
	else {
		buffer[receivedBytes] = '\0';
		m_chat->addMessage(buffer + sizeof(uint16_t), buffer + sizeof(uint16_t) + strlen(buffer + sizeof(uint16_t)) + 1);
	}
}