#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "constants.h"
#include <sys/time.h>
#include <sys/ipc.h>
#include <sqlite3.h>
#include <sys/shm.h>
#include "crypto/crypt_blowfish.c"
//#include "crypto/wrapper.c"
#include <sys/epoll.h>
#include <sys/un.h>

#include "utils.h"

#define BACKLOG 10     // how many pending connections queue will hold

void sigchld_handler(int s)
{
	// waitpid() might overwrite errno, so we save and restore it:
	int saved_errno = errno;

	while (waitpid(-1, NULL, WNOHANG) > 0);

	errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

struct playerData {
	char accountName[DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH + 1]; //+1 for the NULL char at the end
	int health;
};

void die(int exitvalue, int inetsock, sqlite3* db) {
	if (db != NULL) {
		sqlite3_close(db);
	}
	close(inetsock);
	exit(1);
}

int hashCompare(char* hash1, char* hash2, int length) { //strncmp() that avoids timing attacks
	int result = 1;
	for (int i = 0; i < length; i++) {
		if (*(hash1 + i) != *(hash2 + i)) {
			result = 0;
		}
	}
	return result;
}

void sendCriticalError(char* body) { //Copied from http://stackoverflow.com/questions/2362989/how-can-i-send-e-mail-in-c
	char cmd[100];  // to hold the command.
	char to[] = EMAIL_CRITICAL_ERROR; // email id of the recepient.
									  //char body[] = "SO rocks";    // email body.
	char tempFile[100];     // name of tempfile.

	strcpy(tempFile, tempnam("/tmp", "sendmail")); // generate temp file name.

	FILE *fp = fopen(tempFile, "w"); // open it for writing.
	fprintf(fp, "%s\n", body);        // write body to it.
	fclose(fp);             // close it.

	sprintf(cmd, "sendmail %s < %s", to, tempFile); // prepare command.
	system(cmd);     // execute it.
}

int main(void)
{
	int sockfd, inetsock;  // listen on sock_fd, new connection on inetsock
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes = 1;
	char IPString[INET6_ADDRSTRLEN];
	int rv;
	int uniq = 0; //I don't give a fuck about overflows here
	key_t key;
	struct timeval timeout = { RECV_TIMEOUT_SEC,RECV_TIMEOUT_MICROSEC };

	if ((key = ftok(FTOK_FILEPATH, 1)) == -1) {
		perror("ftok");
		exit(1);
	}

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	// loop through all the results and bind to the first we can
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
			p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
			sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	freeaddrinfo(servinfo); // all done with this structure

	if (p == NULL) {
		fprintf(stderr, "server: failed to bind\n");
		exit(1);
	}

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	printf("server: waiting for connections...\n");

	while (1) {  // main accept() loop
		sin_size = sizeof their_addr;
		inetsock = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);

		if (inetsock == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family,
			get_in_addr((struct sockaddr *)&their_addr),
			IPString, sizeof IPString);
		printf("server: got connection from %s\n", IPString);

		uniq++;
		if (!fork()) { // this is the child process
			close(sockfd); // child doesn't need the listener
			char buffer[MESSAGE_MAX_BYTESIZE];
			unsigned int bytesReceived;
			unsigned int totalBytesReceived = 0;
			if (setsockopt(inetsock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(struct timeval)) == -1) {
				perror("setsockopt");
				die(1, inetsock, NULL);
			}

			//Handshake

			uint16_t answer;
			{
				uint16_t protocolVersion;
				while (totalBytesReceived < sizeof protocolVersion) {
					if ((bytesReceived = recv(inetsock, ((char*)buffer) + totalBytesReceived, sizeof protocolVersion - totalBytesReceived, 0)) < 0) {
						perror("recv");
						die(1, inetsock, NULL);
					}
					else if (bytesReceived == 0) { //Client has closed the connection
						die(1, inetsock, NULL);
					}
					totalBytesReceived += bytesReceived;
				}
				protocolVersion = ntohs(*((uint16_t *)buffer));
				if (protocolVersion == PROTOCOL_VERSION_CURRENT) {
					answer = htons(PROTOCOL_HANDSHAKE_OK);
					sendall(inetsock, &answer, sizeof answer);
				}
				else {
					printf("socket server: Connection from %s rejected because the protocol version received (%d) does not match current (%d)\n", IPString, protocolVersion, PROTOCOL_VERSION_CURRENT);
					answer = htons(PROTOCOL_HANDSHAKE_UPGRADE);
					sendall(inetsock, &answer, sizeof answer);
					die(2, inetsock, NULL);
				}
			}

			//Login

			totalBytesReceived = 0;
			uint8_t accountNameLength = 0;
			uint8_t passwordLength = 0;
			while (totalBytesReceived < (accountNameLength + sizeof accountNameLength + passwordLength + sizeof passwordLength)) {
				if ((bytesReceived = recv(inetsock, ((char*)buffer) + totalBytesReceived, sizeof buffer - totalBytesReceived, 0)) < 0) {
					perror("recv");
					die(1, inetsock, NULL);
				}
				else if (bytesReceived == 0) {
					die(1, inetsock, NULL);
				}
				totalBytesReceived += bytesReceived;
				if (!accountNameLength) {
					accountNameLength = *((uint8_t*)buffer);
					if (accountNameLength > DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH) {
						printf("socket server: Connection from %s rejected because it has tried to send an account name %d bytes long, which is longer than the maximum allowed (%d)\n", IPString, accountNameLength, DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH);
						die(2, inetsock, NULL);
					}
				}
				if (!passwordLength && totalBytesReceived >= (sizeof accountNameLength + accountNameLength + sizeof passwordLength)) {
					passwordLength = *((uint8_t*)(buffer + sizeof accountNameLength + accountNameLength));
					if (passwordLength > DATABASE_TABLE_PLAYERS_PASSWORD_MAXLENGTH) {
						printf("socket server: Connection from %s rejected because it has tried to send a password %d bytes long, which is longer than the maximum allowed (%d)\n", IPString, passwordLength, DATABASE_TABLE_PLAYERS_PASSWORD_MAXLENGTH);
						die(2, inetsock, NULL);
					}
				}
			}
			//Null terminate both strings (account name and password) for security reasons as if a length approach was used that could potentially trigger undefined behaviour on sqlite when receiving malformed packets, assumes buffer is defined as char*. Reference: "If any NUL characters occur at byte offsets less than the value of the fourth parameter then the resulting string value will contain embedded NULs. The result of expressions involving strings with embedded NULs is undefined." from https://www.sqlite.org/c3ref/bind_blob.html
			buffer[sizeof accountNameLength + accountNameLength] = '\0';
			buffer[accountNameLength + sizeof accountNameLength + passwordLength + sizeof passwordLength] = '\0';
			char* accountName = buffer + sizeof accountNameLength;
			char* password = buffer + sizeof accountNameLength + accountNameLength + sizeof passwordLength;
			sqlite3* db;
			sqlite3_stmt* SQLStatement;
			if ((rv = sqlite3_open(DATABASE_FILEPATH, &db)) != SQLITE_OK) {
				fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
				die(1, inetsock, db);
			}
			if ((rv = sqlite3_prepare_v2(db, "SELECT * FROM " DATABASE_TABLE_PLAYERS " WHERE AccountName=?1", -1, &SQLStatement, NULL)) != SQLITE_OK) {
				perror("sqlite3_prepare_v2");
				fprintf(stderr, "sqlite3_prepare_v2 failed with error code: %d\n", rv);
				die(1, inetsock, db);
			}
			if ((rv = sqlite3_bind_text(SQLStatement, 1, accountName, -1, SQLITE_STATIC)) != SQLITE_OK) {
				perror("sqlite3_bind_text");
				fprintf(stderr, "sqlite3_bind_text failed with error code %d when trying to bind the string '%s' in the account name field (login)\n", rv, accountName);
				die(1, inetsock, db);
			}
			if ((rv = sqlite3_step(SQLStatement)) != SQLITE_ROW) {
				if (rv == SQLITE_DONE) {
					printf("socket server: Connection from %s rejected because it has tried to login into the account '%s', which doesn't exist\n", IPString, accountName);
					answer = htons(LOGIN_ACCOUNT_DOESNT_EXIST);
					sendall(inetsock, &answer, sizeof answer);
					die(2, inetsock, db);
				}
				else {
					perror("sqlite3_step");
					fprintf(stderr, "sqlite3_step failed with error code %d when trying to execute a lookup for an account which has '%s' as its account name (login)\n", rv, accountName);
					die(1, inetsock, db);
				}
			}
			char* correctPasswordHash = (char*)(sqlite3_column_text(SQLStatement, PASSWORD_COL));
			char* salt = (char*)(sqlite3_column_text(SQLStatement, SALT_COL));
			if ((correctPasswordHash != NULL) && (salt != NULL)) {
				char fullSalt[BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH + DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH + 1];
				strncpy(fullSalt, BCRYPT_SALT_PREFIX BCRYPT_SALT_ROUNDS, BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH + 1);
				strncat(fullSalt, salt, DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH);
				char passwordHash[DATABASE_TABLE_PLAYERS_PASSWORDHASH_LENGTH + BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH + DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH + 1];
				memset(passwordHash, 0, DATABASE_TABLE_PLAYERS_PASSWORDHASH_LENGTH + BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH + DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH + 1);
				_crypt_blowfish_rn(password, fullSalt, passwordHash, DATABASE_TABLE_PLAYERS_PASSWORDHASH_LENGTH + BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH + DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH + 1);

				if (hashCompare(correctPasswordHash, passwordHash + BCRYPT_SALT_PREFIX_AND_ROUNDS_CHARLENGTH + DATABASE_TABLE_PLAYERS_PASSWORDSALT_LENGTH - 1, DATABASE_TABLE_PLAYERS_PASSWORDHASH_LENGTH)) { //Does not leak info
					printf("socket server: Successful login from %s as '%s'\n", IPString, accountName);
					answer = htons(LOGIN_SUCESS);
					sendall(inetsock, &answer, sizeof answer);
				}
				else {
					printf("socket server: Connection from %s rejected because it has tried to login into the account '%s' with a wrong password\n", IPString, accountName);
					answer = htons(LOGIN_WRONG_PASSWORD);
					sendall(inetsock, &answer, sizeof answer);
					die(2, inetsock, db);
				}
			}
			else {
				perror("sqlite3_column_text");
				die(1, inetsock, db);
			}

			//Get shared memory segment for player data

			key += uniq;
			int shmid; //Shared memory id
			struct playerData* shmem;
			/* create the segment: */
			if ((shmid = shmget(key, sizeof(struct playerData), 0644 | IPC_CREAT)) == -1) {
				perror("shmget");
				die(1, inetsock, db);
			}
			/* attach to the segment to get a pointer to it: */
			shmem = (struct playerData*)shmat(shmid, (void *)0, 0);
			if ((void *)shmem == (void *)(-1)) {
				perror("shmat");
				die(1, inetsock, db);
			}

			//Put player data from the database into the segment
			strncpy(shmem->accountName, accountName, DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH + 1);
			shmem->health = sqlite3_column_int(SQLStatement, HEALTH_COL); //No es comprova de que pugui haver un error ja que el valor de l'error seria 0 i aquest és un valor vàlid per a la vida. Referència: "If a memory allocation error occurs during the evaluation of any of these routines, a default value is returned. The default value is either the integer 0" - https://sqlite.org/c3ref/column_blob.html

																		  //Set up a socket to the chat process
			int chatsock;
			if ((chatsock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
				perror("socket");
				die(1, inetsock, db);
			}
			struct sockaddr_un chatSockaddr;
			chatSockaddr.sun_family = AF_UNIX;
			strcpy(chatSockaddr.sun_path, CHAT_SOCKET_FILEPATH);
			int chatSockadrrLength = sizeof chatSockaddr.sun_family + strlen(chatSockaddr.sun_path);
			if (connect(chatsock, (struct sockaddr*)&chatSockaddr, chatSockadrrLength) < 0) {
				perror("connect");
				die(1, inetsock, db);
			}

			//Send username to the chat process
			if (send(chatsock, accountName, accountNameLength + 1, 0) < 0) {
				perror("send");
				die(1, inetsock, db);
			}

			//Set up socket monitorization
			int epollfd;
			if ((epollfd = epoll_create1(0)) == -1) {
				perror("epoll_create1");
				die(1, inetsock, db);
			}
			struct epoll_event event;
			event.events = EPOLLIN;
			{
				int sockets[] = { inetsock, chatsock }; //Add all sockets that need to be monitored (chat, game server...)
				for (int i = 0; i < (sizeof sockets / sizeof(sockets[0])); i++) {
					event.data.fd = sockets[i];
					if (epoll_ctl(epollfd, EPOLL_CTL_ADD, sockets[i], &event) < 0) {
						perror("epoll_ctl");
						die(1, inetsock, db);
					}
				}
			}

			struct epoll_event events[EPOLL_MAX_EVENTS];
			int numEvents;

			//Point of no return - Any errors from now on should be handled without calling die() nor exit() nor anything that prevents the saving of the modified data inside shmem

			//Main receive-send middleman loop
			int ded = 0;
			while (!ded) {
				if ((numEvents = epoll_wait(epollfd, events, EPOLL_MAX_EVENTS, -1)) < 0) {
					perror("epoll_wait");
					//TODO: Handle error
				}
				for (int i = 0; i < numEvents; i++) {
					switch (rv = recvall(events[i].data.fd, buffer, sizeof buffer)) {
					case 0: //Peer closed the connection
						if (events[i].data.fd == inetsock) { //Client has disconnected
							ded = 1;
							break;
						}
						//If an internal server has disconnected, try to connect again, handle it the same way an error (-1) is
					case -1: //Error
						if (events[i].data.fd == inetsock) { //Error receiving data from client
							ded = 1;
						}
						else { //Error receiving data from server
							ded = 1;
						}
						break;
					default:
						if (events[i].data.fd == inetsock) {
							//The client has sent us some data -> Recognize which server the data is intended for (chat or game server) and relay it
							//TODO: Add message recognization code
							if (sendall(chatsock, buffer, rv) < 0) {
								//Error sending: reconnect to server?, try again?
							}
						}
						else {
							//A server has sent some data -> Relay that data to the client
							if (sendall(inetsock, buffer, rv) < 0) {
								//Error sending data
								ded = 1;
							}
						}

					}
				}
			}

			close(epollfd); //socket monitoring is no longer needed, free the resources

							//Send request to remove player from Chat server and game Server (just close the sockets to them?)
			close(chatsock);

			//Store memory segment info in the database

			{
				int written = 0;
				for (int i = 0; i < DATABASE_MAX_WRITE_ATTEMPTS && !written; i++) {
					if ((rv = sqlite3_prepare_v2(db, "UPDATE " DATABASE_TABLE_PLAYERS " SET health=?2 WHERE AccountName=?1", -1, &SQLStatement, NULL)) != SQLITE_OK) {
						perror("sqlite3_prepare_v2");
					}
					else {
						if ((rv = sqlite3_bind_text(SQLStatement, 1, shmem->accountName, -1, SQLITE_STATIC)) != SQLITE_OK) {
							perror("sqlite3_bind_text");
						}
						else {
							if ((rv = sqlite3_bind_int(SQLStatement, 2, shmem->health)) != SQLITE_OK) {
								perror("sqlite3_bind_text");
							}
							else {
								if ((rv = sqlite3_step(SQLStatement)) != SQLITE_DONE) {
									perror("sqlite3_step");
								}
								else {
									written = 1;
								}
							}
						}
					}
					if (!written) {
						sleep(DATABASE_FAILED_WRITE_ATTEMPT_SLEEP_SECONDS);
					}
				}


				if (!written) {
					fprintf(stderr, "CRITICAL FAILURE!! Couldn't store modified information of account %s into the database. Error code: %d", shmem->accountName, rv);
					sendCriticalError("CRITICAL FAILURE!! Couldn't store modified information of an account into the database. Check the logs for more information.");
					die(1, inetsock, db);
				}
			}

			//All data has been saved, it's safe to call die() or any other disrupting function from now on

			//Destroy memory segment (detach from the segment)
			if (shmdt(shmem) == -1) {
				perror("shmdt");
				die(1, inetsock, db);
			}

			//End
			die(0, inetsock, db);
		}
		close(inetsock);  // parent doesn't need this
	}

	return 0;
}
