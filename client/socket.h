#pragma once
#include "/Users/core/Source/Repos/plebServer/plebServer/plebServer/server/constants.h"


//CONSTANTS
#define SERVER_HOST "albert.sh"

class Map;
class Chat;

class Socket {
public:
	Socket(Chat* chat, Map** mapa);
	const char* startUp();
	const char* Login(const char* username, const char* pwd);
	void sendChatMessage(const char* message);
	void update();
private:
	Chat* m_chat;
	Map** m_mapa;
	char buffer[MESSAGE_MAX_BYTESIZE];
	char m_username[DATABASE_TABLE_PLAYERS_ACCOUNTNAME_MAXLENGTH + 1];
};