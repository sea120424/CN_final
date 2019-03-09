#include "TimeUtils.hpp"
#include "option.h"
#include "Command.hpp"
#include "State.hpp"
#include "CreateAccountResult.hpp"
#include "LoginResult.hpp"
#include "DataType.hpp"

#include <inttypes.h>
#include <stdint.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <memory.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <map>
#include <string>
#include <algorithm>
#include <queue>
#include "SignUpUtils.hpp"
#include "common.hpp"
#include <tuple>
#include <dirent.h>
const char *del = "-";
using namespace std;

#include "SocketAddr.hpp"
static Option options[] = {
	{ OPT_UINT, "port", &port, "port number"}
};

char TimeUtils::time_cstr[32] = "";
char TimeUtils::buffer[32] = "";

template<typename KeyType, typename ValueType>
pair<KeyType,ValueType> maxValue(const map<KeyType,ValueType> &x)
{
	return *max_element(x.begin(), x.end(),
	[] (const pair<KeyType,ValueType> & p1, const pair<KeyType,ValueType> & p2)
	{
		return p1.second < p2.second;
	});
}

void safe_exit(int signo)
{
	fprintf(stderr,
		(signo == SIGTERM) ?	"SIGTERM\n":
		(signo == SIGINT) ?		"SIGINT\n":
		(signo == SIGTSTP) ?		"SIGTSTP\n":
		"unexpected signal %d\n", signo
					);
	exit(0);
	/* by calling exit(0), buffer would be flushed out, all files would be closed */
}

class Server {
	public:
		static const int maxAccount = 1000;
		static const int maxConnection = 256;
		static const int maxLoginUsers = 30;

		static const string usersFileName;
		static const string historyFolder;
		static const string offlineFolder;
		static const string fileUploadFolder;

		FILE *usersFile;

		fd_set readFds, copy;
		int listenFd;
		unsigned listenPort;

		map < SocketAddr, int > connections;	/* SocketAddr -> connFd */

		map < string, int > onlineUsers;		/* username -> connFd */
		map < int, State> states;				/* connFd -> State */

		map < string, string > credentials;		/* username -> password */

		Server();
		Server(unsigned listenPort);

		class CommandHandler {
			public:
				Server &server;
				CommandHandler(Server &ref): server(ref) {}
				void handleSignUpRequest(int connFd);
				void checkUsernameTaken(int connFd);
				void createAccount(int connFd);
				void acceptCancelSignUp(int connFd);
				void acceptQuit(const SocketAddr &socketAddr, int connFd);
				void handleLogin(int connFd);
				void handleLogout(int connFd);
				void handleListUsers(int connFd);
				void handleSendMessage(int connFd);
				void handleSendFile(int connFd);
				void handleHistoryRequest(int connFd);
				void handleDownloadRequest(int connFd);
		};

		void init() {
			char username[64];
			char password[33];
			FILE *fp = fopen(usersFileName.c_str(), "r");
			if (fp != NULL) {
				while (fscanf(fp, "%s%s", username, password) != EOF) {
					credentials[string(username)] = string(password);
				}
				fclose(fp);
			}

			usersFile = fopen(usersFileName.c_str(), "a");
			if (usersFile == NULL) {
				perror(usersFileName.c_str());
				exit(-1);
			}
		}

		void run() {
			CommandHandler handler(*this);
			FD_SET(listenFd, &readFds);

			while (true) {
				struct timeval timeout;
				memcpy(&copy, &readFds, sizeof(fd_set));
				unsigned maxFd = getMaxFd();
				timeout.tv_sec = 0; timeout.tv_usec = 200000;
				int ret = select(maxFd + 1, &copy, NULL, NULL, &timeout);
				if (ret < 0) {
					perror("select");
					continue;
				}

				if (FD_ISSET(listenFd, &copy)) {
					addConnection();
					continue;
				}

				/* connections: socketAddr -> connFd */
				/* onlineUsers:  username -> socketAddr */

				checkConnections(handler);
			}
		}

	private:
		unsigned getMaxFd();
		void addConnection();
		void removeUserFromOnlineList(int connFd);
		void closeConnection(int connFd);
		void checkConnections(CommandHandler &handler);
		void saveHistory
		(
			string srcUser,
			string dstUser,
			string content,
			DataType type,
			string timeStr
		);
		void sendOfflineData(string username);
};




const string Server::usersFileName = "../data/server/users/user.txt";
const string Server::historyFolder = "../data/server/history/";
const string Server::offlineFolder = "../data/server/offline/";
const string Server::fileUploadFolder = "../data/server/files/";





int main(int argc, char *argv[])
{
	struct sigaction sa;
	sa.sa_handler = &safe_exit;
	sa.sa_flags = SA_RESTART;
	sigfillset(&sa.sa_mask);

	bool sig = true;
	if (sigaction(SIGINT, &sa, NULL) == -1)
		fprintf(stderr, "can't catch SIGINT\n"), sig = false;
	if (sigaction(SIGTERM, &sa, NULL) == -1)
		fprintf(stderr, "can't catch SIGTERM\n"), sig = false;
	if (sigaction(SIGTSTP, &sa, NULL) == -1)
		fprintf(stderr, "can't catch SIGSTOP\n"), sig = false;

	if (!sig) exit(0);

	Opt_Parse(argc, argv, options, Opt_Number(options), 0);
	Server server(port);
	server.init();
	server.run();
	return 0;
}

void Server::closeConnection(int connFd)
{
	if (states[connFd] == ::ONLINE)
		removeUserFromOnlineList(connFd);
	states.erase(connFd);

	map < SocketAddr, int >::iterator c_it;
	for (c_it = connections.begin(); c_it != connections.end(); c_it++) {
		if (connFd == c_it->second) {
			fprintf(stderr, "remove connection to %s:%u\n",
					c_it->first.host().c_str(),
					c_it->first.port()
					);
			connections.erase(c_it);
		}
	}

	close(connFd);
	FD_CLR(connFd, &readFds);
}

void Server::removeUserFromOnlineList(int connFd)
{
	map < string, int >::iterator it;
	for (it = onlineUsers.begin(); it != onlineUsers.end(); it++) {
		if (connFd == it->second) {
			fprintf(stderr, "remove %s from online user list\n", it->first.c_str());
			onlineUsers.erase(it);
			return;
		}
	}
}


void Server::checkConnections(CommandHandler &handler)
{
	map < SocketAddr, int >::iterator c_it; /* c: connection */
	for (c_it = connections.begin(); c_it != connections.end(); c_it++) {
		if (FD_ISSET(c_it->second, &copy)) {
			int command = 0;
			int connFd = c_it->second;
			int ret = recv(connFd, &command, sizeof(int), 0);
			if (ret == 0) {
				/* connection close */
				if (states[connFd] == ::ONLINE)
					removeUserFromOnlineList(connFd);
				/* remove states */
				states.erase(connFd);

				/* remove connection */
				fprintf(stderr, "remove connection to %s:%u\n",
					c_it->first.host().c_str(),
					c_it->first.port()
					);

				connections.erase(c_it);
				close(connFd);
				FD_CLR(connFd, &readFds);
				return;
			}

			switch (command) {
				case ::signUp:
					handler.handleSignUpRequest(connFd);
					return;
				case ::checkUsernameTaken:
					handler.checkUsernameTaken(connFd);
					return;
				case ::createAccount:
					handler.createAccount(connFd);
					return;
				case ::cancelSignUp:
					handler.acceptCancelSignUp(connFd);
					return;
				case ::quit:
					handler.acceptQuit(c_it->first, connFd);
					return;
				case ::login:
					handler.handleLogin(connFd);
					return;
				case ::logout:
					handler.handleLogout(connFd);
					return;
				case ::listUsers:
					handler.handleListUsers(connFd);
					return;
				case ::sendMessage:
					handler.handleSendMessage(connFd);
					return;
				case ::sendFile:
					handler.handleSendFile(connFd);
					return;
				case ::history:
					handler.handleHistoryRequest(connFd);
					return;
				case ::download:
					handler.handleDownloadRequest(connFd);
					return;
			}
		}
	}
}

Server::Server()
{
	listenFd = 0;
	listenPort = 0;
	FD_ZERO(&readFds);
	FD_ZERO(&copy);
}

Server::Server(unsigned listenPort)
{
	this->listenPort = listenPort;

	listenFd = socket(PF_INET, SOCK_STREAM, 0);
	if (listenFd < 0) {
		fprintf(stderr, "socket open error\n");
		exit(-1);
	}

	struct sockaddr_in addr;
 	addr.sin_family = AF_INET;
 	addr.sin_addr.s_addr = htonl(INADDR_ANY);
 	addr.sin_port = htons(port);

 	if (bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
 		close(listenFd);
 		fprintf(stderr, "port bind error\n");
    	exit(-1);
 	}

	if (listen(listenFd, 1024) < 0) {
		close(listenFd);
		fprintf(stderr, "port listen error\n");
		exit(-1);
	}

	FD_ZERO(&readFds);
	FD_ZERO(&copy);

	fprintf(stderr, "server init success\n");
}

void Server::addConnection()
{
	if (connections.size() == maxConnection) {
		fprintf(stderr, "connection full\n");
		return;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	const size_t connLen = sizeof(addr);

	int connFd = accept(listenFd,
					(struct sockaddr*)&addr,
					(socklen_t*)&connLen);
	if (connFd < 0) {
		if (errno == ENFILE) {
			fprintf(stderr, "out of file descriptor table\n");
			return;
		}

		if (errno == EINTR) return;

		fprintf(stderr, "accept err\n");
		fprintf(stderr, "code: %s\n", strerror(errno));
		return;
	}

	char ip[40];

	if (inet_ntop(addr.sin_family, &(addr.sin_addr), ip, 40) == NULL) {
		close(connFd);
		fprintf(stderr, "inet_ntop: %s\n", strerror(errno));
	}

	FD_SET(connFd, &readFds);
	SocketAddr socketAddr(string(ip), addr.sin_port);
	fprintf(stderr, "add %s:%u to connection\n", ip, addr.sin_port);
	connections[socketAddr] = connFd;
	states[connFd] = ::HOME;
}

unsigned Server::getMaxFd()
{
	if (connections.empty()) return listenFd;
	auto max = maxValue(connections);/* max in client's fd */
	int maxFd = listenFd;			/* server's listen fd */
	if (max.second > maxFd) maxFd = max.second;
	return maxFd;
}

void Server::CommandHandler::handleSignUpRequest(int connFd)
{
	map < string, string > &credentials = server.credentials;
	char permit = (credentials.size() < maxAccount) ? 1: 0;
	send(connFd, &permit, 1, 0);
	if (permit) server.states[connFd] = ::REGISTER;
}

void Server::CommandHandler::checkUsernameTaken(int connFd)
{
	char username[64];
	int usernameLen = -1;
	if (recv(connFd, &usernameLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, username, usernameLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}

	username[usernameLen] = '\0';
	map < string, string > &credentials = server.credentials;
	char usernameTaken = credentials.find(string(username)) != credentials.end() ? 1: 0;
	send (connFd, &usernameTaken, 1, 0);
}

void Server::CommandHandler::createAccount(int connFd)
{
	CreateAccountResult result = OK;
	SignUpUtils utils;

	map < int, State > &states = server.states;

	char username[64];
	char password[256];
	int usernameLen = -1;
	if (recv(connFd, &usernameLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, username, usernameLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	username[usernameLen] = '\0';
	map < string, string > &credentials = server.credentials;

	int passwordLen = -1;
	if (recv(connFd, &passwordLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, password, passwordLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	password[passwordLen] = '\0';

	utils.setUsername(string(username));
	utils.setPassword(string(password));

	if (!utils.usernameValid()) {
		result = UsernameInvalid;
	} else {
		if (!utils.passwordValid()) {
			result = PasswordInvalid;
		} else {
			if (credentials.find(utils.getUsername()) != credentials.end())
				result = UsernameTaken;
			else {
				if (credentials.size() >= maxAccount) {
					result = FullAccount;
					states[connFd] = ::HOME;
				}
			}
		}
	}

	if (result == OK) {
		states[connFd] = ::HOME;
		string hashedPassword = md5Hash(utils.getPassword());
		credentials[utils.getUsername()] = hashedPassword;
		fprintf(stderr, "create account <%s, %s>\n",
			username,
			hashedPassword.c_str()
			);
		fprintf(server.usersFile, "%s %s\n",
			username,
			hashedPassword.c_str());
	}

	send(connFd, &result, sizeof(int), 0);
}

void Server::CommandHandler::acceptCancelSignUp(int connFd)
{
	char ack = true;
	send(connFd, &ack, 1, 0);
	server.states[connFd] = ::HOME;
}

void Server::CommandHandler::acceptQuit(const SocketAddr &socketAddr, int connFd)
{
	char ack = true;
	send(connFd, &ack, 1, 0);
	fprintf(stderr, "accept quit from %s:%u\n",
		socketAddr.host().c_str(),
		socketAddr.port()
		);

	/* remove states */
	server.states.erase(connFd);

	/* remove connection */
	server.connections.erase(socketAddr);
	close(connFd);
	FD_CLR(connFd, &server.readFds);
}

void Server::CommandHandler::handleLogin(int connFd)
{
	/* AlreadyOnline */
	/* UsernameDoesNotExist */
	/* PasswordIncorrectt */
	/* ChatroomFull */
	map < int, State > &states = server.states;

	char username[20];
	char password[256];
	int usernameLen = -1;
	if (recv(connFd, &usernameLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, username, usernameLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	username[usernameLen] = '\0';

	map < string, string > &credentials = server.credentials;
	map < string, int > &onlineUsers = server.onlineUsers;

	int passwordLen = -1;
	if (recv(connFd, &passwordLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, password, passwordLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	password[passwordLen] = '\0';

	map < string, string >::iterator it = credentials.find(string(username));
	LoginResult result = Login;

	if (it == credentials.end()) {
		result = UsernameDoesNotExist;
	} else {
		if (string(md5Hash(string(password))) != it->second)
			result = PasswordIncorrect;
		else {
			if (states[connFd] == ::ONLINE)
				result = AlreadyOnline;
			else {
				if (onlineUsers.find(string(username)) != onlineUsers.end())
					result = LoginByAnotherProcess;
				else {
					if (onlineUsers.size() >= maxLoginUsers)
						result = ChatroomFull;
				}
			}
		}
	}

	send(connFd, &result, sizeof(LoginResult), 0);
	if (result == Login) {
		server.states[connFd] = ::ONLINE;
		server.onlineUsers[string(username)] = connFd;
		fprintf(stderr, "%s login on %d\n", username, connFd);
		server.sendOfflineData(string(username));
	}
}

void Server::CommandHandler::handleLogout(int connFd)
{
	char username[64];
	int usernameLen = -1;
	if (recv(connFd, &usernameLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}

	if (usernameLen > 0) {
		if (recv(connFd, username, usernameLen, 0) == 0) {
			server.closeConnection(connFd);
			return;
		}
		username[usernameLen] = '\0';
	} else {
		strcpy(username, "[anonymous]");
	}

	map < string, int > &onlineUsers = server.onlineUsers;
	map < int, State >  &states = server.states;
	map < string, int >::iterator it = onlineUsers.find(string(username));
	char ack = true;

	if (states[connFd] == ::ONLINE) {
		if (it == onlineUsers.end() || it->second != connFd)
			ack = false;
	}

	send(connFd, &ack, 1, 0);
	if (ack) {
		fprintf(stderr, "%s logout\n", username);
		server.removeUserFromOnlineList(connFd);
		states[connFd] = ::HOME;
	}
}

void Server::CommandHandler::handleListUsers(int connFd)
{
	/* only allow when state is ::ONLINE */
	map < int, State >  &states = server.states;
	map < string, int > &onlineUsers = server.onlineUsers;
	char permit = states[connFd] == ::ONLINE ? 1: 0;
	send(connFd, &permit, 1, 0);
	if (!permit) return;

	map < string, string > &credentials = server.credentials;
	int usersMapSize = credentials.size();

	send(connFd, &usersMapSize, sizeof(int), 0);

	map < string, string >::iterator it;
	for (it = credentials.begin(); it != credentials.end(); it++) {
		char isOnline = (onlineUsers.find(it->first) != onlineUsers.end()) ? 1: 0;
		int usernameLen = it->first.length();
		send(connFd, &isOnline, 1, 0);
		send(connFd, &usernameLen, sizeof(int), 0);
		send(connFd, it->first.c_str(), usernameLen, 0);
	}
}

void Server::CommandHandler::handleSendMessage(int connFd)
{
	char srcUser[64], dstUser[64], msg[256];
	int srcLen, dstLen, msgLen;
	map < string, int > &onlineUsers = server.onlineUsers;
	map < string, string > &credentials = server.credentials;

	if (recv(connFd, &srcLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, srcUser, srcLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	srcUser[srcLen] = '\0';

	char a1 = (connFd == onlineUsers[string(srcUser)]) ? 1: 0;
	send(connFd, &a1, 1, 0);

	if (!a1) return;

	if (recv(connFd, &dstLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, dstUser, dstLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	dstUser[dstLen] = '\0';

	char a2 = (credentials.find(string(dstUser)) != credentials.end()) ? 1:-0;
	send(connFd, &a2, 1, 0);

	if (!a2) return;

	if (recv(connFd, &msgLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, msg, msgLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}

	msg[msgLen] = '\0';

	map < string, int >::iterator it = onlineUsers.find(string(dstUser));

	char *time_cstr = TimeUtils::get_time_cstr(time(NULL));
	if (time_cstr == NULL) {
		TimeUtils::showError();
		exit(-1);
	}

	int time_len = strlen(time_cstr);
	DataType type = Message;

	string offlineDataPath = offlineFolder + string(dstUser);
	FILE *fp = fopen(offlineDataPath.c_str(), "a");
	if (fp == NULL) {
		perror(offlineDataPath.c_str());
		return;
	}

	fprintf(fp, "%s %s %d\t%s\n", srcUser, msg, (int)type, time_cstr);
	fclose(fp);

	server.saveHistory
	(
		string(srcUser),
		string(dstUser),
		string(msg),
		type,
		string(time_cstr)
	);

	if (it != onlineUsers.end())
		server.sendOfflineData(string(dstUser));
}

void Server::CommandHandler::handleSendFile(int connFd)
{
	static char buf[IOBufSize];
	char srcUser[64], dstUser[64], fileName[256];
	int srcLen, dstLen, fileNameLen;
	map < string, int > &onlineUsers = server.onlineUsers;
	map < string, string > &credentials = server.credentials;

	if (recv(connFd, &srcLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, srcUser, srcLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	srcUser[srcLen] = '\0';

	char a1 = (connFd == onlineUsers[string(srcUser)]) ? 1: 0;
	send(connFd, &a1, 1, 0);
	if (!a1) return;

	if (recv(connFd, &dstLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, dstUser, dstLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	dstUser[dstLen] = '\0';

	char a2 = (credentials.find(string(dstUser)) != credentials.end()) ? 1: 0;
	send(connFd, &a2, 1, 0);
	if (!a2) return;

	int64_t sz = 0;
	while (true) {
		if (recv(connFd, &sz, 8, 0) == 0) {
			server.closeConnection(connFd);
			return;
		}

		if (sz == 0) {
			if (onlineUsers.find(string(dstUser)) != onlineUsers.end())
				server.sendOfflineData(string(dstUser));
			return;
		}

		if (sz > MaxFile) {
			char permit = false;
			fprintf(stderr, "file size too large!\n");
			send(connFd, &permit, 1, 0);
			continue;
		}

		char permit = true;
		send(connFd, &permit, 1, 0);

		if (recv(connFd, &fileNameLen, sizeof(int), 0) == 0) {
			server.closeConnection(connFd);
			return;
		}
		if (recv(connFd, fileName, fileNameLen, 0) == 0) {
			server.closeConnection(connFd);
			return;
		}

		fileName[fileNameLen] = '\0';

		char *time_cstr = TimeUtils::get_time_cstr(time(NULL));
		if (time_cstr == NULL) {
			TimeUtils::showError();
			return;
		}

		int time_len = strlen(time_cstr);
		DataType type = File;

		string offlineDataPath = offlineFolder + string(dstUser);
		FILE *async = fopen(offlineDataPath.c_str(), "a");
		if (async == NULL) {
			perror(offlineDataPath.c_str());
			return;
		}

		fprintf(async, "%s %s %d\t%s\n", srcUser, fileName, (int)type, time_cstr);
		fclose(async);

		server.saveHistory
		(
			string(srcUser),
			string(dstUser),
			string(fileName),
			type,
			string(time_cstr)
		);

		string dstUserDownloadFolder = fileUploadFolder + string(dstUser) + "/";

		mkdirIfNotExist(dstUserDownloadFolder.c_str());

		string baseName = string(TimeUtils::encode_time_str(time_cstr)) + del + string(fileName);
		string path = dstUserDownloadFolder + baseName;

		FILE *fp = fopen(path.c_str(), "w");

		if (fp == NULL) {
			perror(path.c_str());
			return;
		}

		int64_t _sz = sz;
		while (sz > 0) {
			int32_t size = (sz < IOBufSize) ? sz: IOBufSize;
			int ret = recv(connFd, buf, size, 0);
			if (ret < 0) {
				perror("recv");
				return;
			}

			if (ret == 0) {
				fprintf(stderr, "error: connection close when receiving file\n");
				fclose(fp);
				server.closeConnection(connFd);
				return;
			}

			fwrite(buf, 1, ret, fp);
			sz -= ret;
		}

		fclose(fp);

		fprintf(stderr, "receive file %s uploaded by %s, send it to %s (size %" PRId64 " bytes)\n",
			fileName,
			srcUser,
			dstUser,
			_sz
			);
	}
}

void Server::saveHistory
(
	string srcUser,
	string dstUser,
	string content,
	DataType type,
	string timeStr
)
{
	string srcUserPath = historyFolder + srcUser;
	string dstUserPath = historyFolder + dstUser;

	FILE *fp1 = fopen(srcUserPath.c_str(), "a");
	if (fp1 == NULL) {
		perror(srcUserPath.c_str());
		return;
	}

	fprintf(fp1, "%s %s %s %d\t%s\n",
		srcUser.c_str(),
		dstUser.c_str(),
		content.c_str(),
		(int) type,
		timeStr.c_str()
		);

	fclose(fp1);

	FILE *fp2 = fopen(dstUserPath.c_str(), "a");
	if (fp2 == NULL) {
		perror(dstUserPath.c_str());
		return;
	}

	fprintf(fp2, "%s %s %s %d\t%s\n",
		srcUser.c_str(),
		dstUser.c_str(),
		content.c_str(),
		(int) type,
		timeStr.c_str()
		);

	fclose(fp2);
}

void Server::CommandHandler::handleHistoryRequest(int connFd)
{
	map < string, int > &onlineUsers = server.onlineUsers;
	char user[64];
	int userLen = -1;
	if (recv(connFd, &userLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, user, userLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	user[userLen] = '\0';

	char a1 = (connFd == onlineUsers[string(user)]) ? 1: 0;
	send(connFd, &a1, 1, 0);
	if (!a1) return;

	int bufSize = -1;
	if (recv(connFd, &bufSize, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	fprintf(stderr, "bufSize = %d\n", bufSize);
	char permit = (bufSize > 0) ? 1: 0;
	send(connFd, &permit, 1, 0);
	if (!permit) return;

	string userHistoryPath = historyFolder + string(user);
	FILE *fp = fopen(userHistoryPath.c_str(), "r");

	char line[1024];
	int lineCount;
	if (fp == NULL) {
		if (errno != ENOENT) {
			perror(userHistoryPath.c_str());
			exit(-1);
		}

		lineCount = 0;
		send(connFd, &lineCount, sizeof(int), 0);
		return;
	}

	queue < string > history;
	while (fgets(line, 1024, fp) != NULL) {
		history.push(string(line));
		if (history.size() > bufSize) history.pop();
	}

	fclose(fp);

	lineCount = history.size();
	send(connFd, &lineCount, sizeof(int), 0);

	while (!history.empty()) {
		string front = history.front();
		history.pop();
		int L = front.length();
		send(connFd, &L, sizeof(int), 0);
		send(connFd, front.c_str(), L, 0);
	}
}

void Server::sendOfflineData(string username)
{
	string offlineContentPath = offlineFolder + username;
	FILE *fp = fopen(offlineContentPath.c_str(), "r");
	if (fp == NULL) {
		if (errno != ENOENT) {
			perror(offlineContentPath.c_str());
			exit(-1);
		}
		return;
	}

	int connFd = onlineUsers[username];
	char fromUserName[64];
	char content[256];
	char time_cstr[32];
	int type;

	queue < tuple < string, string, int, string > > offline;

	while (fscanf(fp, "%s%s%d%s", fromUserName, content, &type, time_cstr) != EOF) {
		offline.push
		(
			tuple < string, string, int, string >
			(
				string(fromUserName),
				string(content),
				type,
				string(time_cstr)
			)
		);
	}

	fclose(fp);

	int size = offline.size();
	send(connFd, &size, sizeof(int), 0);

	bool allReceived = true;
	while (!offline.empty()) {
		tuple < string, string, int, string > front = offline.front();
		string f1 = get<0> (front);
		string f2 = get<1> (front);
		int    f3 = get<2> (front);
		string f4 = get<3> (front);
		offline.pop();

		int f1Len = f1.length(); int f2Len = f2.length(); int f4Len = f4.length();
		send(connFd, &f1Len, sizeof(int), 0);
		send(connFd, f1.c_str(), f1Len, 0);

		send(connFd, &f2Len, sizeof(int), 0);
		send(connFd, f2.c_str(), f2Len, 0);

		send(connFd, &f3, sizeof(int), 0);

		send(connFd, &f4Len, sizeof(int), 0);
		send(connFd, f4.c_str(), f4Len, 0);

		char received = false;
		if (recv(connFd, &received, 1, 0) == 0) {
			closeConnection(connFd);
			return;
		}
		if (!received) allReceived = false;
	}

	if (allReceived) unlink(offlineContentPath.c_str());
}

void Server::CommandHandler::handleDownloadRequest(int connFd)
{
	map < string, int > &onlineUsers = server.onlineUsers;
	char username[64], filename[64];
	char time_cstr[32];
	int time_len;
	int usernameLen, filenameLen;

	if (recv(connFd, &usernameLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, username, usernameLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	username[usernameLen] = '\0';

	char a1 = (connFd == onlineUsers[string(username)]) ? 1: 0;
	send(connFd, &a1, 1, 0);
	if (!a1) return;

	if (recv(connFd, &filenameLen, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	if (recv(connFd, filename, filenameLen, 0) == 0) {
		server.closeConnection(connFd);
		return;
	}
	filename[filenameLen] = '\0';

	if (recv(connFd, &time_len, sizeof(int), 0) == 0) {
		server.closeConnection(connFd);
		return;
	}

	if (time_len > 0)
		if (recv(connFd, time_cstr, time_len, 0) == 0) {
			server.closeConnection(connFd);
			return;
		}

	time_cstr[time_len] = '\0';

	int permit = 0;
	string downloadFolder = fileUploadFolder + string(username) + "/";

	fprintf(stderr, "%s\n", downloadFolder.c_str());

	string timeStr;
	if (time_len > 0) {
		timeStr = string(TimeUtils::encode_time_str(time_cstr));
	} else {
		DIR *dir = opendir(downloadFolder.c_str());
		/* dir is NULL -> not exist */
		if (dir != NULL) {
			struct dirent *p;
			string latest;
			while ((p = readdir(dir)) != NULL) {
				if (strcmp(p->d_name, ".") == 0 || strcmp(p->d_name, "..") == 0) continue;
				char *prefix = strtok(p->d_name, del);
				char *postfix = strtok(NULL, del);
				if (prefix == NULL || postfix == NULL) {
					fprintf(stderr, "file format error: %s\n", p->d_name);
					exit(-1);
				}
				fprintf(stderr, "%s %s\n", prefix, postfix);
				if (strcmp(filename, postfix) == 0 && strcmp(latest.c_str(), prefix) < 0)
					latest = string(prefix);
			}
			closedir(dir);
			if (!latest.empty()) timeStr = latest;
		}

		if (timeStr.empty())
			fprintf(stderr, "no such file or directory %s/%s\n", username, filename);
	}

	string baseName = timeStr + del + string(filename);
	string path = downloadFolder + baseName;
	FILE *fp = fopen(path.c_str(), "rb");

	if (fp == NULL) {
		if (errno != ENOENT) {
			perror(path.c_str());
			exit(-1);
		}
		send(connFd, &permit, sizeof(int), 0);
		return;
	}

	permit = 1;
	send(connFd, &permit, sizeof(int), 0);

	fseek(fp, 0L, SEEK_END);
	int64_t sz = ftell(fp);
	fprintf(stderr, "file size: %" PRId64 "\n", sz);

	if (fseek(fp, 0L, SEEK_SET) < 0) {
		fprintf(stderr, "fseek error\n");
		exit(-1);
	}

	send(connFd, &sz, 8, 0);

	char buf[IOBufSize];
	while (sz > 0) {
		int32_t size = (sz < IOBufSize) ? sz: IOBufSize;
		fread(buf, 1, size, fp);
		send(connFd, buf, size, 0);
		sz -= size;
	}

	fclose(fp);
	fprintf(stderr, "transfer file %s to %s\n", filename, username);
}
