#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <math.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#define BACKLOG_SIZE 256
#define NET_BUFFER_SIZE 131072
#define TOTAL_ROOMS 10
#define INIT_STATS 100

using namespace std;

#pragma pack(push, 1)

struct Character {
	char name[32];
	uint8_t flags;
	uint16_t attack;
	uint16_t defense;
	uint16_t regen;
	int16_t health;
	uint16_t gold;
	uint16_t current_room;
	uint16_t desc_len;
	char desc[UINT16_MAX];
};

struct Room {
	uint16_t room_num;
	char name[32];
	uint16_t desc_len;
	char desc[UINT16_MAX];
};

struct ChatMessage {
	uint16_t msglen;
	char recip_name[32];
	char sender_name[32];
	char msg[UINT16_MAX];
};

#pragma pack(pop)

void initServer(SOCKET*);
void initGame(vector<Room>*, vector<Character>*, bool[][TOTAL_ROOMS]);
void acceptConnections(SOCKET*, vector<Room>*, vector<Character>*, bool[][TOTAL_ROOMS]);
void clientThread(SOCKET, map<string, SOCKET>*, mutex*, vector<Room>*, vector<Character>*, bool[][TOTAL_ROOMS]);
void changeRoom(SOCKET, char*, map<string, SOCKET>*, Character*, vector<Room>*, vector<Character>*, bool[][TOTAL_ROOMS], uint16_t next_room);
void runFight(char*, map<string, SOCKET>*, Character*, vector<Character>*);
void setBytes(char*, initializer_list<uint8_t>);
void clearBuffer(char*);
void sendVersion(SOCKET, char*);
void sendGame(SOCKET, char*);
void sendError(SOCKET, char*, uint8_t, string);
void sendAccept(SOCKET, char*, uint8_t);
void sendCharacter(SOCKET, char*, Character);
void sendRoom(SOCKET, char*, Room);
void sendConnection(SOCKET, char*, Room);
void sendMessage(SOCKET, char*, char*, map<string, SOCKET>*);
void parseCharacter(char*, Character*, unsigned int);

int main() {
	SOCKET server;
	bool connections[TOTAL_ROOMS][TOTAL_ROOMS];
	vector<Room> rooms;
	vector<Character> entities;

	cout << "-- Zac's LURK Server v2.00 --" << endl;
	cout << endl;

	initServer(&server);
	initGame(&rooms, &entities, connections);
	acceptConnections(&server, &rooms, &entities, connections);

	return 0;
}

void initServer(SOCKET* server) {
	unsigned int port;
	WSADATA wsa_data;
	sockaddr_in local;
	int wsaret = WSAStartup(0x101, &wsa_data);

	cout << "Starting server..." << endl;
	cout << "Enter port number:  ";

	cin >> port;

	cout << "Starting server on port " << port << "." << endl;

	local.sin_family = AF_INET;
	local.sin_addr.s_addr = INADDR_ANY;
	local.sin_port = htons((u_short)port);

	*server = socket(AF_INET, SOCK_STREAM, 0);

	bind(*server, (sockaddr*)&local, sizeof(local));

	cout << "Server initialized." << endl;
	cout << endl;
}

void initGame(vector<Room>* rooms, vector<Character>* entities, bool connections[][TOTAL_ROOMS]) {
	Room temp_room;
	Character temp_character;

	memset(connections, false, pow(TOTAL_ROOMS, 2));

	for(int i = 0; i < TOTAL_ROOMS; i++) {
		if(i == TOTAL_ROOMS - 1) {
			connections[i][0] = true;
		} else {
			connections[i][i + 1] = true;
		}

		memset(&temp_room, 0, sizeof(Room));
		temp_room.room_num = i;
		strncpy(temp_room.name, ("Test Room " + to_string(i)).c_str(), 11);
		temp_room.desc_len = 28;
		strncpy(temp_room.desc, "Placeholder room for testing", 28);

		memset(&temp_character, 0, sizeof(Character));
		strncpy(temp_character.name, ("Test Monster " + to_string(i)).c_str(), 14);
		temp_character.flags = 255;
		temp_character.attack = 20;
		temp_character.defense = 20;
		temp_character.regen = 0;
		temp_character.health = 50;
		temp_character.gold = 1;
		temp_character.current_room = i;
		temp_character.desc_len = 31;
		strncpy(temp_character.desc, "Placeholder monster for testing", 31);

		rooms->push_back(temp_room);
		entities->push_back(temp_character);
	}
}

void acceptConnections(SOCKET* server, vector<Room>* rooms, vector<Character>* entities, bool connections[][TOTAL_ROOMS]) {
	vector<thread> clients;
	map<string, SOCKET> all_clients;
	mutex client_mutex;
	bool server_should_stop = false;
	SOCKET client;
	sockaddr_in from;
	int fromlen = sizeof(from);

	listen(*server, BACKLOG_SIZE);

	cout << "Listening for client connections..." << endl;

	while(!server_should_stop) {
		client = accept(*server, (struct sockaddr*)&from, &fromlen);
		clients.push_back(thread(clientThread, client, &all_clients, &client_mutex, rooms, entities, connections));

		cout << "New connection from " << inet_ntoa(from.sin_addr) << "."  << endl;
	}
}

void clientThread(SOCKET client, map<string, SOCKET>* all_clients, mutex* client_mutex, vector<Room>* rooms, vector<Character>* entities, bool connections[][TOTAL_ROOMS]) {
	char writebuf[NET_BUFFER_SIZE];
	char readbuf[NET_BUFFER_SIZE];
	unsigned int readlen;
	bool client_is_connected = true;
	bool all_monsters_dead;
	bool character_exists = false;
	bool started = false;
	Character proposed_character;

	client_mutex->lock();
	sendVersion(client, writebuf);
	sendGame(client, writebuf);
	entities->push_back(Character{});
	memset(&entities->back(), sizeof(Character), 0);

	const unsigned int char_index = size(*entities) - 1;

	client_mutex->unlock();

	while(client_is_connected) {
		readlen = recv(client, readbuf, NET_BUFFER_SIZE, 0);

		if(readlen == SOCKET_ERROR) {
			break;
		}

		client_mutex->lock();

		switch (readbuf[0]) {
			case 12:
				all_clients->erase((*entities)[char_index].name);
				client_is_connected = false;

				break;

			case 10:
				if(!character_exists) {
					parseCharacter(readbuf, &proposed_character, readlen);

					if(proposed_character.attack + proposed_character.defense + proposed_character.regen > INIT_STATS) {
						sendError(client, writebuf, 4, "Proposed stats exceed initial stat limit");

						break;
					}

					sendAccept(client, writebuf, 10);
					proposed_character.current_room = 0;
					proposed_character.gold = 0;
					proposed_character.health = 100;
					proposed_character.flags &= 192;
					proposed_character.flags |= 128;
					memcpy(&(*entities)[char_index], &proposed_character, sizeof(proposed_character));
					character_exists = true;
					(*all_clients)[(*entities)[char_index].name] = client;
					sendCharacter(client, writebuf, (*entities)[char_index]);
					started = false;
				} else {
					sendError(client, writebuf, 2, "Character already exists for this client");
				}

				break;

			case 6:
				if(!character_exists) {
					sendError(client, writebuf, 5, "Character has not been created yet");

					break;
				}

				if(started) {
					sendError(client, writebuf, 0, "Already started");

					break;
				}

				started = true;
				(*entities)[char_index].flags |= 24;
				sendCharacter(client, writebuf, (*entities)[char_index]);
				changeRoom(client, writebuf, all_clients, &(*entities)[char_index], rooms, entities, connections, 0);

				break;

			case 5:
				sendError(client, writebuf, 0, "Loot is distributed automatically by fights");

				break;

			case 4:
				sendError(client, writebuf, 8, "Server does not support PVPFight");

				break;

			case 3:
				all_monsters_dead = true;

				if(!started) {
					sendError(client, writebuf, 5, "Character must be started to fight");

					break;
				}

				if(!((*entities)[char_index].flags & 128)) {
					sendError(client, writebuf, 5, "Character must be alive to fight");

					break;
				}

				for(int i = 0; i < size(*entities); i++) {
					if(((*entities)[i].flags & 128) && ((*entities)[i].flags & 32) && (*entities)[i].current_room == (*entities)[char_index].current_room) {
						all_monsters_dead = false;

						break;
					}
				}

				if(!all_monsters_dead) {
					runFight(writebuf, all_clients, &(*entities)[char_index], entities);
				} else {
					sendError(client, writebuf, 7, "There are no living monsters in this room");
				}

				break;

			case 2:
				if(started && ((*entities)[char_index].flags & 128)) {
					if((((uint16_t)readbuf[2] << 8) | readbuf[1]) >= TOTAL_ROOMS) {
						sendError(client, writebuf, 1, "Room does not exist");

						break;
					}

					if(connections[(*entities)[char_index].current_room][((uint16_t)readbuf[2] << 8) | readbuf[1]]) {
						changeRoom(client, writebuf, all_clients, &(*entities)[char_index], rooms, entities, connections, ((uint16_t)readbuf[2] << 8) | readbuf[1]);
					} else {
						sendError(client, writebuf, 1, "No connection to specified room");
					}
				} else {
					sendError(client, writebuf, 5, "Player character has not started or is not alive");
				}

				break;

			case 1:
				if(character_exists) {
					sendMessage(client, readbuf, writebuf, all_clients);
				} else {
					sendError(client, writebuf, 5, "Character must be created before messages can be sent or received");
				}

				break;

			default:
				sendError(client, writebuf, 0, "Unknown message type");

				break;
		}

		client_mutex->unlock();
	}
}

void changeRoom(SOCKET recip, char* writebuf, map<string, SOCKET>* all_clients, Character* character, vector<Room>* rooms, vector<Character>* entities, bool connections[][TOTAL_ROOMS], uint16_t next_room) {
	uint16_t room_leaving = character->current_room;

	character->current_room = next_room;
	sendCharacter(recip, writebuf, *character);

	for(int i = 0; i < size(*entities); i++) {
		if(all_clients->count((*entities)[i].name) > 0 && (*entities)[i].current_room == room_leaving) {
			sendCharacter((*all_clients)[(*entities)[i].name], writebuf, *character);
		}
	}

	sendRoom(recip, writebuf, (*rooms)[character->current_room]);

	for (int i = 0; i < size(*entities); i++) {
		if (all_clients->count((*entities)[i].name) > 0 && (*entities)[i].current_room == character->current_room) {
			sendCharacter((*all_clients)[(*entities)[i].name], writebuf, *character);
		}
	}

	for (int i = 0; i < TOTAL_ROOMS; i++) {
		if (connections[character->current_room][i]) {
			sendConnection(recip, writebuf, (*rooms)[i]);
		}
	}

	for (int i = 0; i < size(*entities); i++) {
		if ((*entities)[i].current_room == character->current_room) {
			sendCharacter(recip, writebuf, (*entities)[i]);
		}
	}
}

void runFight(char* writebuf, map<string, SOCKET>* all_clients, Character* character, vector<Character>* entities) {
	uint16_t fight_room = character->current_room;
	vector<uint16_t> monster_attacks;
	vector<uint16_t> player_attacks;

	for(int i = 0; i < size(*entities); i++) {
		if(((*entities)[i].current_room == fight_room) && ((*entities)[i].flags & 128) && (((*entities)[i].flags & 64) || (strcmp((*entities)[i].name, character->name) == 0))) {
			if((*entities)[i].flags & 32) {
				monster_attacks.push_back((*entities)[i].attack);
			} else {
				player_attacks.push_back((*entities)[i].attack);
			}
		}
	}

	for(int i = 0; i < size(*entities); i++) {
		if(((*entities)[i].current_room == fight_room) && ((*entities)[i].flags & 128) && (((*entities)[i].flags & 64) || (strcmp((*entities)[i].name, character->name) == 0))) {
			if((*entities)[i].flags & 32) {
				for(int j = 0; j < size(player_attacks); j++) {
					if((*entities)[i].defense <= player_attacks[j] * 2) {
						(*entities)[i].health = max((*entities)[i].health - max(player_attacks[j] - (*entities)[i].regen, 0), 0);
					}
				}
			} else {
				for(int j = 0; j < size(monster_attacks); j++) {
					if((*entities)[i].defense <= monster_attacks[j] * 2) {
						(*entities)[i].health = max((*entities)[i].health - max(monster_attacks[j] - (*entities)[i].regen, 0), 0);
					}
				}
			}
		}
	}

	monster_attacks.clear();
	player_attacks.clear();

	for(int i = 0; i < size(*entities); i++) {
		if((*entities)[i].current_room == fight_room && (*entities)[i].health <= 0) {
			(*entities)[i].flags &= 32;
		}
	}

	for(int i = 0; i < size(*entities); i++) {
		if((*entities)[i].current_room == fight_room && ((*entities)[i].flags & 32) && (!((*entities)[i].flags & 128))) {
			for(int j = 0; j < size(*entities); j++) {
				if((!((*entities)[j].flags & 32)) && (*entities)[j].current_room == fight_room && ((*entities)[j].flags & 128)) {
					(*entities)[j].gold += (*entities)[i].gold;
					(*entities)[i].gold = 0;
				}
			}
		}
	}

	for(int i = 0; i < size(*entities); i++) {
		if((!((*entities)[i].flags & 32)) && ((*entities)[i].current_room == fight_room)) {
			for(int j = 0; j < size(*entities); j++) {
				if((*entities)[j].current_room == fight_room) {
					sendCharacter((*all_clients)[(*entities)[i].name], writebuf, (*entities)[j]);
				}
			}
		}
	}
}

void sendVersion(SOCKET recip, char* writebuf) {
	setBytes(writebuf, {14, 2, 3, 0, 0});
	send(recip, writebuf, 5, 0);
	clearBuffer(writebuf);
}
void sendGame(SOCKET recip, char* writebuf) {
	uint16_t initial_stats = INIT_STATS;

	setBytes(writebuf, {11});
	memcpy(writebuf + 1, &initial_stats, 2);
	setBytes(writebuf + 3, {55, 255, 15, 0});
	strncpy(writebuf + 7, "Zac's LURK Game", 15);
	send(recip, writebuf, 22, 0);
	clearBuffer(writebuf);
}

void sendError(SOCKET recip, char* writebuf, uint8_t code, string msg) {
	uint16_t msglen = strlen(msg.c_str());

	setBytes(writebuf, {7, code});
	memcpy(writebuf + 2, &msglen, 2);
	strncpy(writebuf + 4, msg.c_str(), msglen);
	send(recip, writebuf, 4 + msglen, 0);
	clearBuffer(writebuf);
}

void sendAccept(SOCKET recip, char* writebuf, uint8_t type) {
	setBytes(writebuf, {8, type});
	send(recip, writebuf, 2, 0);
	clearBuffer(writebuf);
}

void sendCharacter(SOCKET recip, char* writebuf, Character character) {
	setBytes(writebuf, {10});
	memcpy(writebuf + 1, &character, sizeof(character));
	send(recip, writebuf, 1 + sizeof(character) - (UINT16_MAX - character.desc_len), 0);
	clearBuffer(writebuf);
}

void sendRoom(SOCKET recip, char* writebuf, Room room) {
	setBytes(writebuf, {9});
	memcpy(writebuf + 1, &room, sizeof(room));
	send(recip, writebuf, 1 + sizeof(room) - (UINT16_MAX - room.desc_len), 0);
	clearBuffer(writebuf);
}

void sendConnection(SOCKET recip, char* writebuf, Room room) {
	setBytes(writebuf, {13});
	memcpy(writebuf + 1, &room, sizeof(room));
	send(recip, writebuf, 1 + sizeof(room) - (UINT16_MAX - room.desc_len), 0);
	clearBuffer(writebuf);
}

void sendMessage(SOCKET client, char* readbuf, char* writebuf, map<string, SOCKET>* all_clients) {
	ChatMessage chatmessage;
	SOCKET recip;

	memset(&chatmessage, 0, sizeof(ChatMessage));
	chatmessage.msglen = ((uint16_t)readbuf[2] << 8) | readbuf[1];
	strncpy(chatmessage.recip_name, readbuf + 3, 32);
	strncpy(chatmessage.sender_name, readbuf + 35, 32);
	strncpy(chatmessage.msg, readbuf + 67, chatmessage.msglen);

	if(all_clients->count(chatmessage.recip_name) > 0) {
		recip = (*all_clients)[chatmessage.recip_name];
		setBytes(writebuf, {1});
		memcpy(writebuf + 1, &chatmessage, sizeof(chatmessage));
		send(recip, writebuf, 1 + sizeof(chatmessage) - (UINT16_MAX - chatmessage.msglen), 0);
		clearBuffer(writebuf);
	} else {
		sendError(client, writebuf, 6, "Recipient of message does not exist");
	}
}

void parseCharacter(char* readbuf, Character* character, unsigned int readlen) {
	memcpy(character, readbuf + 1, readlen - 1);
}

void setBytes(char* dest, initializer_list<uint8_t> source) {
	unsigned int index = 0;

	for(uint8_t i : source) {
		dest[index] = i;
		index++;
	}
}

void clearBuffer(char* buffer) {
	memset(buffer, 0, NET_BUFFER_SIZE);
}