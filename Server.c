#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/time.h>
#define max_clients 10
#define buffer_size 1024
#define max_group_mem 5
#define max_groups 200
typedef struct client client;
typedef struct group group;

struct client{
	struct sockaddr_in address;
	int sockfd;
	int uid;
};

typedef struct{
	int uid;
	char command[100];
	char msg[buffer_size];
}msg;

struct group{
	client* admin[max_group_mem];
	int admin_count;
	int gid;
	client* members[max_group_mem];
	int mem_count;
	int broadcast_group;
	int requested_mem[max_group_mem];
	int requested_admin[max_group_mem];
	int decline_request_count[max_group_mem];
};

int connected = 0;
int client_unique_id = 10000;
int group_unique_id = 100;
client *clients[max_clients];
group *groups[max_groups];
char buf[buffer_size];
int uid_to_newsockfd_map[max_clients];


void sigCHandler(int sig);
void sigZHandler(int sig);
void handleSend(int client_fd);
void handleActive(int client_fd);
void broadcast(int client_fd);
void error(char *msg);
int getEmptyClient();
void handleConnection(struct sockaddr_in cli_addr, int newfd);
void handleExceededConnections(struct sockaddr_in cli_addr, int newfd);
void removeClient(int client_fd);
int performAction(int client_fd);
void handleInvalidCommand(int client_fd);
int getUid(int client_fd);
void handleMakeGroup(int client_fd);
void handleMakeGroupReq(int client_fd);
int getFreeGroupIndex();
int getClientIndex(int client_fd);
void allotGroupMem(group* currGroup, int admin_fd);
void allotGroupMemReq(group* currGroup, int admin_fd);
void handleJoinGroup(int client_fd);
void handleDeclineGroup(int client_fd);
int getGroupIndex(int group_id);
int isGroupMem(int client_fd, int gid);
int isRequestedMem(int client_fd, int gid);
void handleSendGroup(int client_fd);
void handleMakeAdmin(int client_fd);
int isAdmin(int client_fd, int group_id);
void handleAddToGroup(int client_fd);
void handleRemoveFromGroup(int client_fd);
void setNULL(group* currGroup);
void handleMakeGroupBroadcast(int client_fd);
void handleActiveGroups(int client_fd);
void handleRemoveFromAllGroup(int client_fd);
void handleDeleteGroup(int group_id);
int isClientActive(int client_uid);
void setGroupsNull();
void setClientsNull();
void handleMakeAdminReq(int client_fd);
void handleApproveAdminReq(int client_fd);
void handleDeclineAdminReq(int client_fd);



int main(int argc, char *argv[])
{
	signal(SIGINT, sigCHandler);        
    signal(SIGTSTP, sigZHandler); 
	if (argc < 2)
	{
		fprintf(stderr, "ERROR, no port provided\n");
		exit(1);
	}

	int sockfd, newsockfd, portno, clilen, pid, client_id, flags;
	struct sockaddr_in serv_addr, cli_addr;

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0)
		error("ERROR opening socket");

	int optval = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

	if ((flags = fcntl(sockfd, F_GETFL, 0)) < 0)
	{
		error("can't get flags to SCOKET!!");
	}

	if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0)
	{
		error("fcntl failed.");
	}

	bzero((char *)&serv_addr, sizeof(serv_addr));

	portno = atoi(argv[1]);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);

	if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		error("ERROR on binding");
	}

	if(listen(sockfd, 10) != 0){
		error("Socket listen failed");
	}

	fd_set readfds, writefds, exceptfds, master;
	FD_ZERO(&readfds);
	FD_ZERO(&writefds);
	FD_ZERO(&exceptfds);
	FD_ZERO(&master);
	FD_SET(sockfd, &master);
	int fdmax = 0;
	if(fdmax < sockfd){
		fdmax = sockfd;
	}
	setGroupsNull();
	setClientsNull();
	printf("*****************Server Log*********************\n");
	while(1){
		readfds = master;
		writefds = master;
		exceptfds = master;
		int activity = select(fdmax+1, &readfds, &writefds, &exceptfds, NULL);
		for(int i=0;i<=fdmax;i++){
			if(FD_ISSET(i, &readfds)){
				if(i == sockfd){
					bzero((char *) &cli_addr, sizeof(cli_addr)); 
					clilen = sizeof(cli_addr);
					newsockfd = accept(sockfd, (struct sockaddr *) & cli_addr, &clilen);
					if(connected >= max_clients){
						handleExceededConnections(cli_addr, newsockfd);
						continue;
					}else{
						if (newsockfd == -1){
							perror("accept");
						}
						else{
							handleConnection(cli_addr, newsockfd);
							FD_SET(newsockfd, &master);
							if (newsockfd > fdmax){
								fdmax = newsockfd;
							}
						}
					}
				}else{
					int nbytes;
					if ((nbytes = recv(i, buf, sizeof(buf), 0)) > 0){
						int temp = performAction(i);
						if(temp > 0){
							FD_CLR(i, &master);
						}
					}
					else{
						removeClient(i);
						FD_CLR(i, &master);
					}
				}
			}
		}

	}
	return 0;
}

void sigCHandler(int sig)
{
    fflush(stdout);
    bzero(buf, buffer_size);
    sprintf(buf, "Server Closing");
	broadcast(-1);
	bzero(buf, buffer_size);
	sprintf(buf, "quit");
	broadcast(-1);
	printf("\nServer Closing\n");
	exit(0);
}

/*Function to handle ^Z*/
void sigZHandler(int sig)
{
    fflush(stdout);
    bzero(buf, buffer_size);
    sprintf(buf, "Server Closing");
	broadcast(-1);
	bzero(buf, buffer_size);
	sprintf(buf, "quit");
	broadcast(-1);
	printf("\nServer Closing\n");
	exit(0);
}

void error(char *msg)
{
	printf("%s\n", msg);
}


int getEmptyClient()
{
	for(int i=0;i<max_clients;i++){
		if(!clients[i]) return i;
	}
}

void handleConnection(struct sockaddr_in cli_addr, int newfd){
	bzero(buf, buffer_size);
	sprintf(buf, "Server : Connection successfull and your Unique ID is %d\n", client_unique_id);
	printf("Client %d has connected\n", client_unique_id);
	int activity = send(newfd, buf, sizeof(buf), 0);
	bzero(buf, buffer_size);
	if(activity < 0){
		error("ERROR sending message");
	}
	client *cli = (client *)malloc(sizeof(client));
	cli->address = cli_addr;
	cli->sockfd = newfd;
	cli->uid = client_unique_id;
	int index = getEmptyClient();
	clients[index] = cli;
	uid_to_newsockfd_map[index] = client_unique_id;
	client_unique_id++;
	connected++;
}

void handleExceededConnections(struct sockaddr_in cli_addr, int newfd){
	sprintf(buf, "Server : Connection Limit Exceeded Try after some time\n");
	int activity = send(newfd, buf, sizeof(buf), 0);
	bzero(buf, buffer_size);
	sprintf(buf, "quit");
	activity = send(newfd, buf, sizeof(buf), 0);
	bzero(buf, buffer_size);
	close(newfd);
}

void removeClient(int client_fd){
	int uid = 0;
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->sockfd == client_fd){
			uid = clients[i]->uid;
			break;
		}
	}
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->sockfd != client_fd){
			bzero(buf, buffer_size);
			sprintf(buf, "Server : client %d is going to exit!!\n", uid);
			int activity = send(clients[i]->sockfd, buf, sizeof(buf), 0);
			bzero(buf, buffer_size);
		}
	}
	printf("Client %d has left the chat\n", uid);
	handleRemoveFromAllGroup(client_fd);
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->sockfd == client_fd){
			uid_to_newsockfd_map[i] = 0;
			bzero(buf, buffer_size);
			sprintf(buf, "quit");
			int activity = send(clients[i]->sockfd, buf, sizeof(buf), 0);
			bzero(buf, buffer_size);
			clients[i] = NULL;
			close(client_fd);
			connected--;
			break;
		}
	}
}

void setGroupsNull(){
	for(int i=0;i<max_groups;i++) groups[i] = NULL;
}

void setClientsNull(){
	for(int i=0;i<max_clients;i++) clients[i] = NULL;
}

void handleRemoveFromAllGroup(int client_fd){
	for(int i=0;i<max_groups;i++){
		if(groups[i] == NULL) continue;
		int is_mem = isGroupMem(client_fd, groups[i]->gid);
		if(is_mem == 1){
			for(int j=0;j<max_group_mem;j++){
				if(groups[i]->admin[j] != NULL && groups[i]->admin[j]->sockfd == client_fd){
					groups[i]->admin[j] = NULL;
					groups[i]->admin_count--;
					if(groups[i]->admin_count == 0){
						handleDeleteGroup(groups[i]->gid);
						break;
					}
				}
				if(groups[i]->members[j] != NULL && groups[i]->members[j]->sockfd == client_fd){
					groups[i]->members[j] = NULL;
					groups[i]->mem_count--;
				}
			}
		}
	}
}

void handleDeleteGroup(int group_id){
	int group_index = getGroupIndex(group_id);
	char msg_send[buffer_size];
	for(int i=0;i<max_group_mem;i++){
		if(groups[group_index] != NULL && groups[group_index]->members[i] != NULL){
			bzero(msg_send, sizeof(msg_send));
			sprintf(msg_send, "Server : Group %d have been deleted\n", group_id);
			int activity = send(groups[group_index]->members[i]->sockfd, msg_send, sizeof(msg_send), 0);
			groups[group_index]->members[i] = NULL;
		}
	}
	groups[group_index] = NULL;
}

void broadcast(int client_fd)
{
	int uid = 0;
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->sockfd != client_fd){
			int activity = send(clients[i]->sockfd, buf, sizeof(buf), 0);
		}else if(clients[i] != NULL){
			uid = clients[i]->uid;
		}
	}
	bzero(buf, buffer_size);
}


void handleSend(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int rec_uid = atoi(ptr);
	int send_uid = 0;
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->sockfd == client_fd){
			send_uid = clients[i]->uid;
			break;
		}
	}
	char temp[buffer_size];
	char msg[buffer_size];
	ptr = strtok(NULL, " ");
	int uid = getUid(client_fd);
	sprintf(temp, "Client %d : ", uid);
	while(ptr != NULL){
		strcat(temp, ptr);
		strcat(temp, " ");
		strcat(msg, ptr);
		strcat(msg, " ");
		ptr = strtok(NULL, " ");
		if(ptr == "\n") break;
	}
	int ind = -1;
	for(int i=0;i<max_clients;i++){
		if(uid_to_newsockfd_map[i] == rec_uid){
			ind = i;
			break;
		}
	}
	if(ind == -1){
		bzero(buf, buffer_size);
		sprintf(buf, "Server : Client ID not found\n");
		int activity = send(client_fd, buf, sizeof(buf), 0);
	}else{
		int activity = send(clients[ind]->sockfd, temp, sizeof(temp), 0);
		printf("Message from %d to %d : %s\n", send_uid, rec_uid, msg);
	}
	bzero(temp, buffer_size);
	bzero(buf, buffer_size);
}

void handleActive(int client_fd){
	bzero(buf, buffer_size);
	sprintf(buf, "active_clients\n");
	int activity = send(client_fd, buf, sizeof(buf), 0);
	bzero(buf, buffer_size);
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL){
			bzero(buf, buffer_size);
			sprintf(buf, "%d", uid_to_newsockfd_map[i]);
			activity = send(client_fd, buf, sizeof(buf), 0);
			bzero(buf, buffer_size);
		}
	}
	sprintf(buf, "active_clients_end\n");
	activity = send(client_fd, buf, sizeof(buf), 0);
	bzero(buf, buffer_size);
}

void handleInvalidCommand(int client_fd){
	bzero(buf, buffer_size);
	sprintf(buf, "Server : Invalid Command\n");
	int activity = send(client_fd, buf, sizeof(buf), 0);
	bzero(buf, buffer_size);
}

int getUid(int client_fd){
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->sockfd == client_fd){
			return clients[i]->uid;
		}
	}
	return -1;
}

int getFreeGroupIndex(){
	for(int i=0;i<max_groups;i++){
		if(groups[i] == NULL) return i;
	}
	return -1;
}

int isClientActive(int client_uid){
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->uid == client_uid) return 1;
	}
	return -1;
}

int getClientIndex(int client_uid){
	for(int i=0;i<max_clients;i++){
		if(clients[i] != NULL && clients[i]->uid == client_uid) return i;
	}
	return -1;
}

int isGroupMem(int client_fd, int gid){
	int g_index = getGroupIndex(gid);
	for(int i=0;i<max_group_mem;i++){
		if(groups[g_index]->members[i] != NULL && groups[g_index]->members[i]->sockfd == client_fd){
			return 1;
		}
	}
	return -1;
}

int isRequestedMem(int client_fd, int gid){
	int g_index = getGroupIndex(gid);
	for(int i=0;i<max_group_mem;i++){
		if(groups[g_index]->requested_mem[i] == client_fd){
			groups[g_index]->requested_mem[i] = 0;
			return 1;
		}
	}
	return -1;
}

void allotGroupMem(group* currGroup, int admin_fd){
	int admin_uid = getUid(admin_fd);
	char* ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int temp = 1;
	while(ptr != NULL){
		int curr_uid = atoi(ptr);
		if(admin_uid == curr_uid){
			ptr = strtok(NULL, " ");
			continue;
		}
		if(currGroup->mem_count >= max_group_mem){
			char msg_group[buffer_size];
			sprintf(msg_group, "Server : Can't add Client %d to gruop %d, Maximum limit reached\n", curr_uid, currGroup->gid);
			int activity = send(admin_fd, msg_group, sizeof(msg_group), 0);
			bzero(msg_group, sizeof(msg_group));
		}else{
			int is_active = isClientActive(curr_uid);
			if(is_active == 1){
				int client_index = getClientIndex(curr_uid);
				int is_mem = isGroupMem(clients[client_index]->sockfd, currGroup->gid);
				if(is_mem == -1){
					currGroup->members[temp] = clients[client_index];
					char msg_group[buffer_size];
					sprintf(msg_group, "Server : You have been added to the group with group ID %d\n", currGroup->gid);
					int activity = send(clients[client_index]->sockfd, msg_group, sizeof(msg_group), 0);
					bzero(msg_group, sizeof(msg_group));
					temp++;
					currGroup->mem_count++;
				}else{
					char msg_group[buffer_size];
					sprintf(msg_group, "Server : Client %d is already member of the gruop %d\n", curr_uid,currGroup->gid);
					int activity = send(admin_fd, msg_group, sizeof(msg_group), 0);
					bzero(msg_group, sizeof(msg_group));
				}
			}else{
				char msg_group[buffer_size];
				sprintf(msg_group, "Server : Client %d is inactive can't add to gruop %d\n", curr_uid,currGroup->gid);
				int activity = send(admin_fd, msg_group, sizeof(msg_group), 0);
				bzero(msg_group, sizeof(msg_group));
			}
		}
		ptr = strtok(NULL, " ");
	}
}

void allotGroupMemReq(group* currGroup, int admin_fd){
	int admin_uid = getUid(admin_fd);
	char* ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int requested_mem_index = 0;
	while(ptr != NULL){
		int curr_uid = atoi(ptr);
		if(admin_uid == curr_uid){
			ptr = strtok(NULL, " ");
			continue;
		}
		int is_active = isClientActive(curr_uid);
		if(is_active == 1){
			int client_index = getClientIndex(curr_uid);
			int is_mem = isGroupMem(clients[client_index]->sockfd, currGroup->gid);
			if(is_mem == -1){
				char msg_group[buffer_size];
				sprintf(msg_group, "Server : You are requested to join the group with Group ID %d\n", currGroup->gid);
				int activity = send(clients[client_index]->sockfd, msg_group, sizeof(msg_group), 0);
				currGroup->requested_mem[requested_mem_index] = clients[client_index]->sockfd;
				requested_mem_index++;
			}else{
				char msg_group[buffer_size];
				sprintf(msg_group, "Server : Client %d is already member of the gruop %d\n", curr_uid,currGroup->gid);
				int activity = send(admin_fd, msg_group, sizeof(msg_group), 0);
				bzero(msg_group, sizeof(msg_group));
			}
		}else{
			char msg_group[buffer_size];
			sprintf(msg_group, "Server : Client %d is inactive can't add to gruop %d\n", curr_uid,currGroup->gid);
			int activity = send(admin_fd, msg_group, sizeof(msg_group), 0);
			bzero(msg_group, sizeof(msg_group));
		}
		ptr = strtok(NULL, " ");
	}
}


void handleMakeGroup(int client_fd){
	group* currGroup = (group*)malloc(sizeof(group));
	int ind = getFreeGroupIndex();
	if(ind == -1){
		char temp[buffer_size];
		sprintf(temp, "Server : Can't Create Group maximum limit reached\n");
		int activity = send(client_fd, temp, sizeof(temp), 0);
	}else{
		setNULL(currGroup);
		currGroup->admin_count = 1;
		int admin_index = getClientIndex(getUid(client_fd));
		currGroup->admin[0] = clients[admin_index];
		currGroup->gid = group_unique_id;
		currGroup->members[0] = clients[admin_index];
		char temp[buffer_size];
		sprintf(temp, "Server : Group Created with group ID %d\n", currGroup->gid);
		int activity = send(client_fd, temp, sizeof(temp), 0);
		bzero(temp, sizeof(temp));
		currGroup->mem_count = 1;
		group_unique_id++;
		currGroup->broadcast_group = 0;
		groups[ind] = currGroup;
		allotGroupMem(currGroup, client_fd);
	}
}

void setNULL(group* currGroup){
	for(int i=0;i<max_group_mem;i++){
		currGroup->admin[i] = NULL;
		currGroup->members[i] = NULL;
		currGroup->decline_request_count[i] = 0;
		currGroup->requested_admin[i] = 0;
		currGroup->requested_mem[i] = 0;
	}
}

void handleMakeGroupReq(int client_fd){
	group* currGroup = (group*)malloc(sizeof(group));
	int ind = getFreeGroupIndex();
	if(ind == -1){
		char temp[buffer_size];
		sprintf(temp, "Server : Can't Create Group maximum limit reached\n");
		int activity = send(client_fd, temp, sizeof(temp), 0);
	}else{
		setNULL(currGroup);
		currGroup->admin_count = 1;
		int admin_index = getClientIndex(getUid(client_fd));
		currGroup->admin[0] = clients[admin_index];
		currGroup->gid = group_unique_id;
		currGroup->members[0] = clients[admin_index];
		char temp[buffer_size];
		sprintf(temp, "Server : Group Created with group ID %d\n", currGroup->gid);
		int activity = send(client_fd, temp, sizeof(temp), 0);
		bzero(temp, sizeof(temp));
		currGroup->mem_count = 1;
		group_unique_id++;
		currGroup->broadcast_group = 0;
		groups[ind] = currGroup;
		allotGroupMemReq(currGroup, client_fd);
	}
}

int getGroupIndex(int group_id){
	for(int i=0;i<max_groups;i++){
		if(groups[i] != NULL && groups[i]->gid == group_id) return i;
	}
	return -1;
}

void handleJoinGroup(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	if(ptr == NULL){
		handleInvalidCommand(client_fd);
	}else{
		int group_id = atoi(ptr);
		int g_exists = getGroupIndex(group_id);
		if(g_exists == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : No Such Group ID exists\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int group_index = getGroupIndex(group_id);
			if(groups[group_index]->mem_count >= max_group_mem){
				char msg_group[buffer_size];
				sprintf(msg_group, "Server : Can't join group %d, group limit reached\n", groups[group_index]->gid);
				int activity = send(client_fd, msg_group, sizeof(msg_group), 0);
				bzero(msg_group, sizeof(msg_group));
			}else{
				int request_exists = isRequestedMem(client_fd, group_id);
				if(request_exists == -1){
					char msg_mem[buffer_size];
					sprintf(msg_mem, "Server : You were not requested to join the group with Group ID %d\n", groups[group_index]->gid);
					int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
				}else{
					for(int j=0;j<max_group_mem;j++){
						if(groups[group_index] != NULL && groups[group_index]->members[j] == NULL){
							int client_index = getClientIndex(getUid(client_fd));
							groups[group_index]->members[j] = clients[client_index];
							char msg_mem[buffer_size];
							sprintf(msg_mem, "Server : You were added to the group with Group ID %d\n", groups[group_index]->gid);
							int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
							groups[group_index]->mem_count++;
							break;
						}
					}
				}
			}
		}
	}
}

void handleDeclineGroup(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	if(ptr == NULL){
		handleInvalidCommand(client_fd);
	}else{
		int group_id = atoi(ptr);
		int g_exists = getGroupIndex(group_id);
		if(g_exists == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : No Such Group ID exists\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int group_index = getGroupIndex(group_id);
			int request_exists = isRequestedMem(client_fd, group_id);
			if(request_exists == -1){
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : You were not requested to join the group with Group ID %d\n", groups[group_index]->gid);
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}else{
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : You declined to join the Group with ID %d\n", groups[group_index]->gid);
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}
		}
	}
}

void handleSendGroup(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int group_id = atoi(ptr);
	char msg_send[buffer_size];
	ptr = strtok(NULL, " ");
	sprintf(msg_send, "Server : Group %d : Client %d : ", group_id, getUid(client_fd));
	while(ptr != NULL){
		strcat(msg_send, ptr);
		strcat(msg_send, " ");
		ptr = strtok(NULL, " ");
	}
	int g_index = getGroupIndex(group_id);
	if(g_index == -1){
		char msg_mem[buffer_size];
		sprintf(msg_mem, "Server : No Such Group ID exists\n");
		int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
	}else{
		int is_mem = isGroupMem(client_fd, group_id);
		if(is_mem == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : You are not part of the group you can't send message to the group\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int is_admin = isAdmin(client_fd, group_id);
			if(groups[g_index]->broadcast_group == 1){
				if(is_admin == 1){
					for(int i=0;i<max_group_mem;i++){
						if(groups[g_index] && groups[g_index]->members[i] != NULL && groups[g_index]->members[i]->sockfd != client_fd){
							int activity = send(groups[g_index]->members[i]->sockfd, msg_send, sizeof(msg_send), 0);
						}
					}
				}else{
					bzero(msg_send, sizeof(msg_send));
					sprintf(msg_send, "Server : Group %d is Broadcast group and you are not the admin so you cannot send messages\n", group_id);
					int activity = send(client_fd, msg_send, sizeof(msg_send), 0);
				}
			}else{
				for(int i=0;i<max_group_mem;i++){
					if(groups[g_index] != NULL && groups[g_index]->members[i] != NULL && groups[g_index]->members[i]->sockfd != client_fd){
						int activity = send(groups[g_index]->members[i]->sockfd, msg_send, sizeof(msg_send), 0);
					}
				}
			}
		}
	}
}

int isAdmin(int client_fd, int group_id){
	int group_index = getGroupIndex(group_id);
	for(int i=0;i<max_group_mem;i++){
		if(groups[group_index] != NULL && groups[group_index]->admin[i] != NULL && groups[group_index]->admin[i]->sockfd == client_fd) return 1;
	}
	return -1;
}

void handleMakeAdmin(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int g_id = atoi(ptr);
	ptr = strtok(NULL, " ");
	int mem_uid = atoi(ptr);
	int is_admin = isAdmin(client_fd, g_id);
	if(is_admin == -1){
		char msg_mem[buffer_size];
		sprintf(msg_mem, "Server : You are not the admin so can't make anyone admin\n");
		int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
	}else{
		int is_mem = isGroupMem(clients[getClientIndex(mem_uid)]->sockfd, g_id);
		if(is_mem == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : The member you are trying to make admin is not part of the group\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			is_admin = isAdmin(clients[getClientIndex(mem_uid)]->sockfd, g_id);
			if(is_admin == -1){
				int group_index = getGroupIndex(g_id);
				for(int i=0;i<max_group_mem;i++){
					if(groups[group_index] != NULL && groups[group_index]->admin[i] == NULL){
						groups[group_index]->admin[i] = clients[getClientIndex(mem_uid)];
						char msg_mem[buffer_size];
						sprintf(msg_mem, "Server : You have been made the admin of the group %d\n", g_id);
						int activity = send(clients[getClientIndex(mem_uid)]->sockfd, msg_mem, sizeof(msg_mem), 0);
						bzero(msg_mem, sizeof(msg_mem));
						sprintf(msg_mem, "Server : Successfully appointed %d as the admin of the group %d\n", mem_uid, g_id);
						activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
						groups[group_index]->admin_count++;
						printf("Client %d is now admin of the group %d", mem_uid, g_id);
						break;
					}
				}
			}else{
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : The member you are trying to make admin is already an admin of the group\n");
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}
		}
	}
}

void handleAddToGroup(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int g_id = atoi(ptr);
	ptr = strtok(NULL, " ");
	while(ptr != NULL){
		int mem_uid = atoi(ptr);
		int is_admin = isAdmin(client_fd, g_id);
		if(is_admin == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : You can't make anyone admin as you are not the admin of the group\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int is_mem = isGroupMem(clients[getClientIndex(mem_uid)]->sockfd, g_id);
			if(is_mem == -1){
				int group_index = getGroupIndex(g_id);
				if(groups[group_index] != NULL && groups[group_index]->mem_count >= max_group_mem){
					char msg_mem[buffer_size];
					sprintf(msg_mem, "Server : Can't add any new member to the group %d, member limit reached\n",g_id);
					int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
					break;
				}else{
					for(int i=0;i<max_group_mem;i++){
						if(groups[group_index] != NULL && groups[group_index]->members[i] == NULL){
							groups[group_index]->members[i] = clients[getClientIndex(mem_uid)];
							groups[group_index]->mem_count++;
							int client_sockfd = clients[getClientIndex(mem_uid)]->sockfd;
							char msg_mem[buffer_size];
							sprintf(msg_mem, "Server : You have been added to the group %d\n",g_id);
							int activity = send(client_sockfd, msg_mem, sizeof(msg_mem), 0);
							bzero(msg_mem, sizeof(msg_mem));
							sprintf(msg_mem, "Server : Successfully added Client %d to the group %d", mem_uid, g_id);
							activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
							printf("Client %d is added to the group %d\n", mem_uid, g_id);
							break;
						}
					}
				}
			}else{
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : Client %d is already member of the group %d\n", mem_uid, g_id);
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}
		}
		ptr = strtok(NULL, " ");
	}
}

void handleRemoveFromGroup(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int g_id = atoi(ptr);
	ptr = strtok(NULL, " ");
	while(ptr != NULL){
		int mem_uid = atoi(ptr);
		int is_admin = isAdmin(client_fd, g_id);
		if(is_admin == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : You are not the admin of the group so can't remove anyone\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int is_mem = isGroupMem(clients[getClientIndex(mem_uid)]->sockfd, g_id);
			if(is_mem == -1){
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : Client %d is not the member of the group %d so can't remove him\n", mem_uid, g_id);
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}else{
				int group_index = getGroupIndex(g_id);
				for(int i=0;i<max_group_mem;i++){
					if(groups[group_index] != NULL && groups[group_index]->members[i] != NULL && groups[group_index]->members[i]->uid == mem_uid){
						is_admin = isAdmin(clients[getClientIndex(mem_uid)]->sockfd, g_id);
						if(is_admin == 1){
							for(int j=0;j<max_group_mem;j++){
								if(groups[group_index]->admin[j] != NULL && groups[group_index]->admin[j]->uid == mem_uid){
									groups[group_index]->admin[j] = NULL;
									groups[group_index]->admin_count--;
									if(groups[group_index]->admin_count == 0){
										handleDeleteGroup(groups[group_index]->gid);
									}
									break;
								}
							}
						}
						if(groups[group_index] == NULL) break;
						groups[group_index]->members[i] = NULL;
						groups[group_index]->mem_count--;
						char msg_mem[buffer_size];
						sprintf(msg_mem, "Server : You are removed from the group %d\n", g_id);
						int activity = send(clients[getClientIndex(mem_uid)]->sockfd, msg_mem, sizeof(msg_mem), 0);
						bzero(msg_mem, sizeof(msg_mem));
						sprintf(msg_mem, "Server : Successfully removed Client %d from the group %d", mem_uid, g_id);
						activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
						break;
					}
				}
			}
		}
		ptr = strtok(NULL, " ");
	}
}

void handleMakeGroupBroadcast(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int g_id = atoi(ptr);
	int group_index = getGroupIndex(g_id);
	if(group_index == -1){
		char msg_mem[buffer_size];
		sprintf(msg_mem, "Server : No Such Group ID exists\n");
		int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
	}
	else{
		int is_mem = isGroupMem(client_fd, g_id);
		char msg_send[buffer_size];
		if(is_mem == -1){
			sprintf(msg_send, "Server : You are not the member of the group %d so you cannot make it Broadcast Group\n", g_id);
			int activity = send(client_fd, msg_send, sizeof(msg_send), 0);
		}
		else{
			int is_admin = isAdmin(client_fd, g_id);
			if(is_admin == -1){
				sprintf(msg_send, "Server : You are not the admin of the group %d so you cannot make it Broadcast Group\n", g_id);
				int activity = send(client_fd, msg_send, sizeof(msg_send), 0);
			}
			else{
				groups[group_index]->broadcast_group = 1;
				sprintf(msg_send, "Server : Successfully made group %d as a broadcast group", g_id);
				int activity = send(client_fd, msg_send, sizeof(msg_send), 0);
				bzero(buf, sizeof(buf));
				sprintf(buf, "/sendgroup %d Group %d is now a broadcast group only admins can send messages\n", g_id, g_id);
				handleSendGroup(client_fd);
			}
		}
	}
}

void handleActiveGroups(int client_fd){
	char msg_send[buffer_size];
	sprintf(msg_send, "active_groups_start");
	int activity = send(client_fd, msg_send, sizeof(msg_send), 0);
	for(int i=0;i<max_groups;i++){
		if(groups[i] == NULL) continue;
		int is_mem = isGroupMem(client_fd, groups[i]->gid);
		if(is_mem == 1){
			bzero(msg_send, sizeof(msg_send));
			sprintf(msg_send , "Group %d : ", groups[i]->gid);
			for(int j=0;j<max_group_mem;j++){
				if(groups[i] != NULL && groups[i]->members[j] != NULL){
					char temp[20];
					sprintf(temp, "%d ", groups[i]->members[j]->uid);
					strcat(msg_send, temp);
				}
			}
			activity = send(client_fd, msg_send, sizeof(msg_send), 0);
			bzero(msg_send, sizeof(msg_send));
		}
	}
	bzero(msg_send, sizeof(msg_send));
	sprintf(msg_send, "active_groups_end");
	activity = send(client_fd, msg_send, sizeof(msg_send), 0);
}

void handleMakeAdminReq(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int g_id = atoi(ptr);
	int group_index = getGroupIndex(g_id);
	int is_admin = isAdmin(client_fd, g_id);
	int mem_uid = getUid(client_fd);
	if(group_index == -1){
		char msg_mem[buffer_size];
		sprintf(msg_mem, "Server : No such group ID exists\n");
		int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
	}else{
		if(is_admin == 1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : You are already an admin of the group %d\n", g_id);
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int is_mem = isGroupMem(clients[getClientIndex(mem_uid)]->sockfd, g_id);
			if(is_mem == -1){
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : You are not the member of the group %d so can't request to be an admin\n", g_id);
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}else{
				int group_index = getGroupIndex(g_id);
				for(int i=0;i<max_group_mem;i++){
					if(groups[group_index] != NULL && groups[group_index]->requested_admin[i] == 0){
						groups[group_index]->requested_admin[i] = mem_uid;
						break;
					}
				}
				for(int i=0;i<max_group_mem;i++){
					if(groups[group_index] != NULL && groups[group_index]->admin[i] != NULL){
						char msg_mem[buffer_size];
						sprintf(msg_mem, "Server : Client %d has requested to be an admin of the group %d\n", mem_uid, g_id);
						int activity = send(groups[group_index]->admin[i]->sockfd, msg_mem, sizeof(msg_mem), 0);
						bzero(msg_mem, sizeof(msg_mem));
					}
				}
			}
		}
	}
}

void handleApproveAdminReq(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int g_id = atoi(ptr);
	int group_index = getGroupIndex(g_id);
	ptr = strtok(NULL, " ");
	int mem_uid = atoi(ptr);
	int is_admin = isAdmin(client_fd, g_id);
	if(group_index == -1){
		char msg_mem[buffer_size];
		sprintf(msg_mem, "Server : No such group ID exists\n");
		int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
	}else{
		if(is_admin == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : You are not the admin so can't make anyone admin\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int is_mem = isGroupMem(clients[getClientIndex(mem_uid)]->sockfd, g_id);
			if(is_mem == -1){
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : The member you are trying to make admin is not part of the group\n");
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}else{
				is_admin = isAdmin(clients[getClientIndex(mem_uid)]->sockfd, g_id);
				if(is_admin == -1){
					int group_index = getGroupIndex(g_id);
					int has_requested_admin = -1;
					for(int i=0;i<max_group_mem;i++){
						if(groups[group_index] != NULL && groups[group_index]->requested_admin[i] == mem_uid){
							has_requested_admin = 1;
							groups[group_index]->requested_admin[i] = 0;
							break;
						}
					}
					if(has_requested_admin == 1){
						for(int i=0;i<max_group_mem;i++){
							if(groups[group_index] != NULL && groups[group_index]->admin[i] == NULL){
								groups[group_index]->admin[i] = clients[getClientIndex(mem_uid)];
								char msg_mem[buffer_size];
								sprintf(msg_mem, "Server : You have been made the admin of the group %d\n", g_id);
								int activity = send(clients[getClientIndex(mem_uid)]->sockfd, msg_mem, sizeof(msg_mem), 0);
								bzero(msg_mem, sizeof(msg_mem));
								sprintf(msg_mem, "Server : Successfully appointed %d as the admin of the group %d\n", mem_uid, g_id);
								activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
								groups[group_index]->admin_count++;
								printf("Client %d is now admin of the group %d", mem_uid, g_id);
								break;
							}
						}
					}else{
						char msg_mem[buffer_size];
						sprintf(msg_mem, "Server : The member you are trying to make admin has not requested to be an admin of the group %d\n", g_id);
						int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
					}
				}else{
					char msg_mem[buffer_size];
					sprintf(msg_mem, "Server : The member you are trying to make admin is already an admin of the group %d\n", g_id);
					int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
				}
			}
		}
	}
}

void handleDeclineAdminReq(int client_fd){
	char *ptr = strtok(buf, " ");
	ptr = strtok(NULL, " ");
	int g_id = atoi(ptr);
	int group_index = getGroupIndex(g_id);
	ptr = strtok(NULL, " ");
	int mem_uid = atoi(ptr);
	int is_admin = isAdmin(client_fd, g_id);
	if(group_index == -1){
		char msg_mem[buffer_size];
		sprintf(msg_mem, "Server : No such group ID exists\n");
		int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
	}else{
		if(is_admin == -1){
			char msg_mem[buffer_size];
			sprintf(msg_mem, "Server : You are not the admin so can't decline admin request\n");
			int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
		}else{
			int is_mem = isGroupMem(clients[getClientIndex(mem_uid)]->sockfd, g_id);
			if(is_mem == -1){
				char msg_mem[buffer_size];
				sprintf(msg_mem, "Server : The member you are trying to decline admin request of is not the member of the group %d\n", g_id);
				int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
			}else{
				is_admin = isAdmin(clients[getClientIndex(mem_uid)]->sockfd, g_id);
				if(is_admin == -1){
					int group_index = getGroupIndex(g_id);
					int has_requested_admin = -1;
					int request_index = -1;
					for(int i=0;i<max_group_mem;i++){
						if(groups[group_index] != NULL && groups[group_index]->requested_admin[i] == mem_uid){
							has_requested_admin = 1;
							request_index = i;
							break;
						}
					}
					if(has_requested_admin == 1){
						for(int i=0;i<max_group_mem;i++){
							if(groups[group_index] != NULL && groups[group_index]->admin[i] == NULL){
								char msg_mem[buffer_size];
								sprintf(msg_mem, "Server : You have declined the request of client %d to be an admin of the group %d\n", mem_uid, g_id);
								int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
								groups[group_index]->decline_request_count[request_index]++;
								if(groups[group_index]->decline_request_count[request_index] == groups[group_index]->admin_count){
									bzero(msg_mem, sizeof(msg_mem));
									sprintf(msg_mem, "Server : All the admins of the group %d have declined your request to be admin of the group\n", g_id);
									int activity = send(clients[getClientIndex(mem_uid)]->sockfd, msg_mem, sizeof(msg_mem), 0);
								}
								break;
							}
						}
					}else{
						char msg_mem[buffer_size];
						sprintf(msg_mem, "Server : The member you are trying to decline admin request has not requested to be an admin of the group %d\n", g_id);
						int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
					}
				}else{
					char msg_mem[buffer_size];
					sprintf(msg_mem, "Server : The member you are trying to decline admin request of is already an admin of the group %d\n", g_id);
					int activity = send(client_fd, msg_mem, sizeof(msg_mem), 0);
				}
			}
		}
	}
}

int performAction(int client_fd){
	if(strncmp(buf, "/quit", 5) == 0){
		if(buf[5] != '\n'){
			handleInvalidCommand(client_fd);
		}
		else{
			removeClient(client_fd);
			return client_fd;
		}
	}
	else if(strncmp(buf, "/activegroups", 13) == 0){
		if(buf[13] != '\n'){
			handleInvalidCommand(client_fd);
		}else{
			handleActiveGroups(client_fd);
		}
	}
	else if(strncmp(buf, "/active", 7) == 0){
		if(buf[7] != '\n'){
			handleInvalidCommand(client_fd);
		}else{
			handleActive(client_fd);
		}
	}
	else if(strncmp(buf, "/broadcast", 10) == 0){
		if(buf[10] != ' '){
			handleInvalidCommand(client_fd);
		}
		else{
			char *ptr = strtok(buf, " ");
			ptr = strtok(NULL, " ");
			char temp[buffer_size];
			char msg[buffer_size];
			int uid = getUid(client_fd);
			sprintf(temp, "Client %d : ", uid);
			while(ptr != NULL){
				strcat(temp, ptr);
				strcat(temp, " ");
				strcat(msg, ptr);
				strcat(msg, " ");
				ptr = strtok(NULL, " ");
				if(ptr == "\n") break;
			}
			printf("Broadcast message from %d : %s\n", uid, msg);
			bzero(buf, buffer_size);
			strcpy(buf, temp);
			broadcast(client_fd);
		}
	}
	else if(strncmp(buf, "/sendgroup", 10) == 0){
		if(buf[10] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleSendGroup(client_fd);
		}
	}
	else if(strncmp(buf, "/send", 5) == 0){
		if(buf[5] != ' '){
			handleInvalidCommand(client_fd);
		}else if(buf[5] == ' '){
			handleSend(client_fd);
		}
	}
	else if(strncmp(buf, "/makegroupbroadcast", 19) == 0){
		if(buf[19] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleMakeGroupBroadcast(client_fd);
		}
	}
	else if(strncmp(buf, "/makegroupreq", 13) == 0){
		if(buf[13] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleMakeGroupReq(client_fd);
		}
	}
	else if(strncmp(buf, "/makegroup", 10) == 0){
		if(buf[10] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleMakeGroup(client_fd);
		}
	}
	else if(strncmp(buf, "/joingroup", 10) == 0){
		if(buf[10] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleJoinGroup(client_fd);
		}
	}
	else if(strncmp(buf, "/declineadminreq", 16) == 0){
		if(buf[16] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleDeclineAdminReq(client_fd);
		}
	}
	else if(strncmp(buf, "/declinegroup", 13) == 0){
		if(buf[13] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleDeclineGroup(client_fd);
		}
	}
	else if(strncmp(buf, "/makeadminreq", 13) == 0){
		if(buf[13] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleMakeAdminReq(client_fd);
		}
	}
	else if(strncmp(buf, "/approveadminreq", 16) == 0){
		if(buf[16] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleApproveAdminReq(client_fd);
		}
	}
	else if(strncmp(buf, "/makeadmin", 10) == 0){
		if(buf[10] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleMakeAdmin(client_fd);
		}
	}
	else if(strncmp(buf, "/addtogroup", 11) == 0){
		if(buf[11] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleAddToGroup(client_fd);
		}
	}
	else if(strncmp(buf, "/removefromgroup", 16) == 0){
		if(buf[16] != ' '){
			handleInvalidCommand(client_fd);
		}else{
			handleRemoveFromGroup(client_fd);
		}
	}
	else{
		handleInvalidCommand(client_fd);
	}
		
	return 0;
}