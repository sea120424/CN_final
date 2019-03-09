#include "CommandHelper.hpp"
#include "color.hpp"
#include "Command.hpp"
#include "CreateAccountResult.hpp"
#include "LoginResult.hpp"
#include "SignUpHelper.hpp"
#include "DataType.hpp"
#include <assert.h>
#include <libgen.h>
#include <stdint.h>
#include <string.h>
#include "common.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <queue>

using namespace std;

const char *CommandHelper::arrow[2] = {"=>", "->"};
const char *del = "-----------------------------------------------";

const string CommandHelper::strEmpty = "[Empty]";
const string CommandHelper::strHidden = "[Hidden]";
const string CommandHelper::savedPasswordFolder = "../data/client/pass/";
const string CommandHelper::downloadFolder = "../data/client/download/";
const string CommandHelper::downloadListFolder = "../data/client/downloadList/";

const char *CommandHelper::version = "1.0";

CommandHelper::CommandHelper(int connFd, State state)
{
	this->connFd = connFd;
	setState(state);
	signUpHelper.setFd(connFd);
	memset(storedPassword, '\0', PasswordBuffer);
	refresh();
}

void CommandHelper::help()
{
	fprintf(stderr, "input " BYEL REFRESH RESET " to refresh your web page\n");
	showLocalCommands();
}

void CommandHelper::showLocalCommands()
{
	if (state == ::HOME) {
		fprintf(stderr, "\ninput " BYEL SIGN_UP RESET " to enter the signup page\n");

		fprintf(stderr, "\ninput " BYEL LOGIN   RESET " to log in\n");

		fprintf(stderr, "\ninput " BYEL QUIT    RESET " to leave the application\n");
		return;
	}

	if (state == ::REGISTER) {
		fprintf(stderr, "\ninput " BYEL USERNAME " [username]" RESET
			" to register account with [username]\n");

		fprintf(stderr, "\ninput " BYEL PASSWORD           RESET
			" to set the password; to execute this instruction, the username must be set.\n");

		fprintf(stderr, "\ninput " BYEL CONFIRM_PASSWORD    RESET
			" to confirm your password; Password should be set before.\n");

		fprintf(stderr, "\ninput " BYEL CANCEL              RESET
			" to cancel sign up and return to previous page\n");

		fprintf(stderr, "\ninput " BYEL CREATE_ACCOUNT     RESET
			": you can create the account successfully if username, password, and confirm-password have been correctly set\n");
		return;
	}

	if (state == ::ONLINE) {
		fprintf(stderr,"\ninput " BYEL SEND " [-m] [ID] \'message\'"   RESET
			" to send a message to a specific ID; message transmitting is the default action of " BYEL SEND RESET
			", so [-m] is optional.\n");

		#define COLOR "\033[36m\033[1m"
		fprintf(stderr,"\ninput " BYEL SEND " [-f] [ID] \'filenames\'"  RESET
			" to send files to a specific ID, where " BYEL "filenames" RESET
			" are separated by \'" COLOR "," RESET "\';"
			" make sure you have specify \'" COLOR "-f" RESET "\' for file sending.\n");
		#undef COLOR

		fprintf(stderr,"\ninput " BYEL LIST    RESET
			" to get a list of person who has signed up for Chatroom\n");

		fprintf(stderr,"\ninput " BYEL HISTORY RESET
			" to view previous message\n");

		fprintf(stderr,"\ninput " BYEL DOWNLOADLIST     RESET
			" to view the files available for downloading\n");

		fprintf(stderr,"\ninput " BYEL DOWNLOAD " [filename]" RESET
			" to download the corresponding file\n");

		fprintf(stderr,"\ninput " BYEL LOGOUT  RESET
			" to log out and go back to initial login menu\n");
	}
}

void CommandHelper::refresh()
{
	if (state == ::HOME) {
		showHomePage();
		return;
	}
	if (state == ::REGISTER) {
		signUpHelper.refresh();
		return;
	}
	if (state == ::ONLINE) {
		showOnlinePage();
		return;
	}
}

void CommandHelper::signUp()
{
	Command command = ::signUp;
	char permit = false;
	send(connFd, &command, sizeof(int), 0);
	recv(connFd, &permit, 1, 0);
	if (permit) {
		state = ::REGISTER;
		fprintf(stderr, "entering sign-up page ...\n%s\n", del);
		signUpHelper.refresh();
	} else {
		fprintf(stderr, "rejected (reason: account full)\n");
	}
}

void CommandHelper::login()
{
	fprintf(stderr,"Please input your username or input " BYEL RETURN RESET " to exit\n");
	fprintf(stderr,"Input your username: ");
	char line[64];
	char username[32];
	fgets(line, 64, stdin);
	sscanf(line, "%s", username);
	if(!strcmp(username, RETURN)){
		refresh();
		return;
	}

	char* password;

	string savedPasswordPath = savedPasswordFolder + string(username);
	FILE *fp = fopen(savedPasswordPath.c_str(), "r");
	bool passwordFileAccessible = (fp != NULL);
	if (passwordFileAccessible) {
		#define COLOR "\033[33m\033[5m"
		fprintf(stderr, COLOR "fast login" RESET "\n");
		#undef COLOR
		if (fscanf(fp, "%s", storedPassword) == EOF) storedPassword[0] = '\0';
		password = storedPassword;
		fclose(fp);
	} else {
		if (errno != ENOENT) {
			perror(savedPasswordPath.c_str());
			exit(-1);
		}
		fprintf(stderr,"Please key in your password or input " BYEL RETURN RESET " to exit\n");
		password = getpass("Input your password: ");
		if (!strcmp(password, RETURN)) {
			refresh();
			return;
		}
	}

	Command command = ::login;

	send(connFd, &command, sizeof(int), 0);
	int userLen = strlen(username);
	int passwordLen = strlen(password) ;
	send(connFd, &userLen, sizeof(int), 0);
	send(connFd, username, userLen, 0);
	send(connFd, &passwordLen, sizeof(int), 0);
	send(connFd, password, passwordLen, 0);
	LoginResult result = Uninitialized;
	recv(connFd, &result, sizeof(int), 0);

	if (result == Login) {
		fprintf(stderr, GRN "=> login successful" RESET "\n");
		setUsername(string(username));
		setState(::ONLINE);
		if (!passwordFileAccessible)
			savePassword(savedPasswordPath.c_str(), password);
		refresh();
		return;
	}

	if (result == UsernameDoesNotExist) {
		fprintf(stderr, YEL "=> username does not exist" RESET "\n");
		if (passwordFileAccessible)
			unlink(savedPasswordPath.c_str());
		refresh();
		return;
	}

	if (result == PasswordIncorrect) {
		fprintf(stderr, RED "=> password incorrect" RESET "\n");
		if (passwordFileAccessible)
			unlink(savedPasswordPath.c_str());
		refresh();
		return;
	}

	if (result == AlreadyOnline) {
		fprintf(stderr, CYN "=> rejected (already online)" RESET "\n");
		return;
	}

	if (result == LoginByAnotherProcess) {
		fprintf(stderr, CYN "=> rejected (login by another process)" RESET "\n");
		return;
	}
	if (result == ChatroomFull) {
		fprintf(stderr, BLU "=> rejected (chatroom full)" RESET "\n");
		return;
	}

	fprintf(stderr, "server disconnected\n");
}

void CommandHelper::savePassword(string path, const char *password)
{
	FILE *fp = fopen(path.c_str(), "w");
	if (fp == NULL) {
		perror(path.c_str());
		exit(-1);
	}

	#define COLOR "\033[31m\033[1m"
	fprintf(stderr, COLOR "Do you want to save your password? (Y/N) : " RESET);
	#undef COLOR
	char line[64];
	char feedback[64];
	fgets(line, 64, stdin);
	sscanf(line, "%s", feedback);
	int L = strlen(feedback);
	for (int i = 0; i < L; i++)
		feedback[i] = tolower(feedback[i]);

	if (strcmp(feedback, "y") == 0 || strcmp(feedback, "yes") == 0) {
		fprintf(fp, "%s\n", password);
		fclose(fp);
	} else {
		fclose(fp);
		unlink(path.c_str());
	}
}

void CommandHelper::quit()
{
	Command command = ::quit;
	char ack = false;
	send(connFd, &command, sizeof(int), 0);
	recv(connFd, &ack, 1, 0);
	if (ack) exit(0);
}

void CommandHelper::inputUsername(string arg)
{
	signUpHelper.handleInputUsername(arg);
	signUpHelper.refresh();
}

void CommandHelper::inputPassword()
{
	if (!signUpHelper.usernameValid()) {
		fprintf(stderr, "Please complete username first\n");
		return;
	}

	char *password;
	password = getpass("Please key in your password: ");
	signUpHelper.handleInputPassword(string(password));
	signUpHelper.refresh();
}

void CommandHelper::confirmPassword()
{
	if (!signUpHelper.usernameValid()) {
		fprintf(stderr, "Please complete username first\n");
		return;
	}

	if (!signUpHelper.passwordValid()) {
		fprintf(stderr, "Please complete password first\n");
		return;
	}

	char *password;
	password = getpass("Please re-input your password: ");
	signUpHelper.handleConfirmPassword(string(password));
	signUpHelper.refresh();
}

void CommandHelper::cancelSignUp()
{
	setState(::HOME);
	signUpHelper.cancel();
	promptReturningToHomePage();
	showHomePage();
}

void CommandHelper::createAccount()
{
	CreateAccountResult result = signUpHelper.createAccount();

	if (result == UsernameInvalid) {
		fprintf(stderr, "Username Invalid\n%s\n", del);
		return;
	}

	if (result == PasswordInvalid) {
		fprintf(stderr, "Password Invalid\n%s\n", del);
		return;
	}

	if (result == UsernameTaken) {
		fprintf(stderr, "sorry, this account was taken by another client process\n%s\n", del);
		signUpHelper.refresh();
		return;
	}

	if (result == FullAccount) {
		fprintf(stderr, "rejected (full account)\n");
		promptReturningToHomePage();
		setState(::HOME);
		signUpHelper.reset();
		showHomePage();
		return;
	}

	if (result == Incomplete) {
		fprintf(stderr, "please complete the form first\n");
		return;
	}

	if (result == OK) {
		fprintf(stderr, "create account success\n");
		promptReturningToHomePage();
		setState(::HOME);
		signUpHelper.reset();
		showHomePage();
		return;
	}

	fprintf(stderr, "server seems to disconnect\n");
}

void CommandHelper::sendData(string option, string targetUserName, string content)
{
	if (option == "-m") {
		sendMessage(targetUserName, content);
	} else {
		if (option == "-f") {
			sendFile(targetUserName, content);
		} else {
			fprintf(stderr, "Unknown option %s\n", option.c_str());
		}
	}
}

void CommandHelper::sendMessage(string target, string message)
{
	Command command = ::sendMessage;
	send(connFd, &command, sizeof(int), 0);

	int usernameLen = username.length();
	int targetLen = target.length();
	int messageLen = message.length();

	send(connFd, &usernameLen, sizeof(int), 0);
	send(connFd, username.c_str(), usernameLen, 0);

	char a1 = false;
	recv(connFd, &a1, 1, 0);

	if (!a1) {
		fprintf(stderr, "src username incorrect\n");
		return;
	}

	send(connFd, &targetLen, sizeof(int), 0);
	send(connFd, target.c_str(), targetLen, 0);

	char a2 = false;
	recv(connFd, &a2, 1, 0);

	if (!a2) {
		fprintf(stderr, "dst username does not exist\n");
		return;
	}

	send(connFd, &messageLen, sizeof(int), 0);
	send(connFd, message.c_str(), messageLen, 0);
}

void CommandHelper::sendFile(string target, string arg)
{
	static char buf[IOBufSize];
	int argLen = arg.length();
	char *filenames = (char *)malloc(argLen + 1);
	strcpy(filenames, arg.c_str());
	if (filenames == NULL) {
		fprintf(stderr, "malloc returns NULL\n");
		exit(-1);
	}

	vector < string > fileNames;
	char *path = strtok(filenames, ",");
	while (path != NULL) {
		fileNames.push_back(string(path));
		path = strtok(NULL, ",");
	}

	free(filenames);

	Command command = ::sendFile;
	send(connFd, &command, sizeof(int), 0);

	int usernameLen = username.length();
	int targetLen = target.length();

	send(connFd, &usernameLen, sizeof(int), 0);
	send(connFd, username.c_str(), usernameLen, 0);

	char a1 = false;
	recv(connFd, &a1, 1, 0);
	if (!a1) {
		fprintf(stderr, "username incorrect\n");
		return;
	}

	send(connFd, &targetLen, sizeof(int), 0);
	send(connFd, target.c_str(), targetLen, 0);

	char a2 = false;
	recv(connFd, &a2, 1, 0);
	if (!a2) {
		fprintf(stderr, "dst username does not exist\n");
		return;
	}

	for (int i = 0; i < fileNames.size(); i++) {
		struct stat buffer;
		if (lstat(fileNames[i].c_str(), &buffer) < 0) {
			char err_msg[100];
			sprintf(err_msg, "lstat: %s", fileNames[i].c_str());
			perror(err_msg);
			if (errno == ENOENT) continue;

			int64_t end = 0;
			send(connFd, &end, 8, 0);
			exit(-1);
		}

		if (!S_ISREG(buffer.st_mode)) {
			string t_str;
					if (S_ISDIR(buffer.st_mode))	t_str = "directory";
			else	if (S_ISCHR(buffer.st_mode))	t_str = "character special";
			else	if (S_ISBLK(buffer.st_mode))	t_str = "block special";
			else	if (S_ISFIFO(buffer.st_mode))	t_str = "fifo";
			else	if (S_ISLNK(buffer.st_mode))	t_str = "symbolic link";
			else									t_str = "unknown mode!";
			fprintf(stderr, "%s: file type is " RED "%s" RESET ", this is not allowed to upload\n",
				fileNames[i].c_str(),
				t_str.c_str()
				);
			continue;
		}

		int64_t sz = buffer.st_size;
		if (sz > MaxFile) {
			fprintf(stderr, "%s: file size too large ... no more than %d MB.\n",
				fileNames[i].c_str(),
				Limit
				);
			continue;
		}

		FILE *fp = fopen(fileNames[i].c_str(), "rb");
		if (fp == NULL) {
			perror(fileNames[i].c_str());
			if (errno == ENOENT) continue;

			int64_t end = 0;
			send(connFd, &end, 8, 0);
			exit(-1);
		}

		send(connFd, &sz, 8, 0);
		char permit = false;
		recv(connFd, &permit, 1, 0);

		if (!permit) {
			fprintf(stderr, "server says file size too large!\n");
			fclose(fp);
			continue;
		}

		char *path = (char *)malloc(fileNames[i].length() + 1);
		if (path == NULL) {
			fprintf(stderr, "malloc returns NULL\n");
			exit(-1);
		}

		strcpy(path, fileNames[i].c_str());

		char *ts = strdup(path);
		if (ts == NULL) {
			fprintf(stderr, "strdup error\n");
			exit(-1);
		}

		char *filename = basename(ts);
		int filenameLen = strlen(filename);

		send(connFd, &filenameLen, sizeof(int), 0);
		send(connFd, filename, filenameLen, 0);

		while (sz > 0) {
			int32_t size = (sz < IOBufSize) ? sz: IOBufSize;
			fread(buf, 1, size, fp);
			send(connFd, buf, size, 0);
			sz -= size;
		}

		fprintf(stderr, "%s uploaded\n", filename);
		fclose(fp); free(path); free(ts);
	}

	int64_t end = 0;
	send(connFd, &end, 8, 0);
	return;
}

void CommandHelper::list()
{
	Command command = ::listUsers;
	send(connFd, &command, sizeof(int), 0);

	char permit = false;
	recv(connFd, &permit, 1, 0);
	if (!permit) {
		fprintf(stderr, "list request rejected (state is not ONLINE)\n");
		return;
	}

	int size;
	recv(connFd, &size, sizeof(int), 0);
	#define COLOR "\033[32m\033[1m\033[45m"
	fprintf(stderr, COLOR "enrolled users:" RESET "\n\n");
	#undef COLOR
	while (size--) {
		char isOnline = false;
		int usernameLen;
		char username[32];
		recv(connFd, &isOnline, 1, 0);
		recv(connFd, &usernameLen, sizeof(int), 0);
		recv(connFd, username, usernameLen, 0);
		username[usernameLen] = '\0';

		#define COLOR "\033[36m\033[1m"
		if (isOnline)
			fprintf(stderr, COLOR);
		fprintf(stderr,"* %s\n", username);
		if (isOnline)
			fprintf(stderr, RESET);
	}
}

void CommandHelper::history(const char *arg)
{
	#define DEFAULT 20
	int bufSize = DEFAULT;
	if (arg != NULL) {
		bufSize = atoi(arg);
		if (bufSize <= 0) {
			fprintf(stderr, "argument should be positive\n");
			return;
		}
	}

	#undef DEFAULT

	Command command = ::history;
	send(connFd, &command, sizeof(int), 0);
	int usernameLen = username.length();
	send(connFd, &usernameLen, sizeof(int), 0);
	send(connFd, username.c_str(), usernameLen, 0);

	char a1 = false;
	recv(connFd, &a1, 1, 0);
	if (!a1) {
		fprintf(stderr, "username incorrect\n");
		return;
	}

	char permit = false;
	send(connFd, &bufSize, sizeof(int), 0);
	recv(connFd, &permit, 1, 0);
	if (!permit) {
		fprintf(stderr, "server: argument should be positive\n");
		return;
	}

	int lineCount = -1;
	char line[1000];
	char srcUser[64];
	char dstUser[64];
	char content[256];
	char time_cstr[32];
	int type;

	recv(connFd, &lineCount, sizeof(int), 0);
	#define COLOR "\033[36m\033[1m"

	while (lineCount--) {
		int L = -1;
		recv(connFd, &L, sizeof(int), 0);
		recv(connFd, line, L, 0);

		line[L] = '\0';

		type = Zero;

		sscanf(line, "%s%s%s%d%s", srcUser, dstUser, content, &type, time_cstr);
		if (type != Message && type != File) {
			fprintf(stderr, "Unknown type %d\n", type);
			continue;
		}

		int isFile = (type == File) ? 1: 0;

		fprintf(stderr, COLOR "%s" RESET " %s " COLOR "%s" RESET " %s\t%s\n",
			srcUser,
			arrow[isFile],
			dstUser,
			content,
			time_cstr
			);
	}

	#undef COLOR
}

void CommandHelper::showDownloadList(const char *arg)
{
	string downloadListPath = downloadListFolder + getUsername();
	FILE *fp = fopen(downloadListPath.c_str(), "r");
	if (fp == NULL) {
		if (errno != ENOENT) {
			perror(downloadListPath.c_str());
			exit(-1);
		}
		fprintf(stderr,"No available file for downloading.\n");
		return;
	}

	fprintf(stderr,"\ninput " BYEL DOWNLOAD " [%%s]" RESET " to require downloading a file.\n");

	char name[64];
	char time_cstr[32];
	queue < pair < string, string > > q;
	#define DEFAULT 10
	int bufSize = DEFAULT;
	#undef DEFAULT
	if (arg != NULL) {
		bufSize = atoi(arg);
		if (bufSize <= 0) {
			fprintf(stderr, "argument should be positive\n");
			return;
		}
	}
	while (fscanf(fp, "%s%s", name, time_cstr) != EOF) {
		q.push( pair < string, string > (string(name), string(time_cstr)) );
		if (q.size() > bufSize) q.pop();
	}

	while (!q.empty()) {
		pair < string, string > p = q.front();
		q.pop();
		fprintf(stderr, BYEL "%s\t" RESET "%s\n",
			p.first.c_str(),
			p.second.c_str()
			);
	}

	fclose(fp);
}

void CommandHelper::downloadRequest(string filename, string timeStr)
{
	fprintf(stderr, "file requesting\n");

	Command command = ::download;
	send(connFd, &command, sizeof(int), 0);
	int usernameLen = username.length(), filenameLen = filename.length();
	int timeStrLen = timeStr.length();
	send(connFd, &usernameLen, sizeof(int), 0);
	send(connFd, username.c_str(), usernameLen, 0);

	char a1 = false;
	recv(connFd, &a1, 1, 0);
	if (!a1) {
		fprintf(stderr, "server says username incorrect\n");
		return;
	}

	send(connFd, &filenameLen, sizeof(int), 0);
	send(connFd, filename.c_str(), filenameLen, 0);

	send(connFd, &timeStrLen, sizeof(int), 0);
	if (timeStrLen > 0)
		send(connFd, timeStr.c_str(), timeStrLen, 0);


	int permit;
	recv(connFd, &permit, sizeof(int), 0);

	if (!permit) {
		fprintf(stderr, "File not exist.\n");
		return;
	}

	string selfDownloadFolder = downloadFolder + username + "/";

	mkdirIfNotExist(selfDownloadFolder.c_str());

	string downloadFilePath = selfDownloadFolder + filename;
	FILE *fp = fopen(downloadFilePath.c_str(), "wb");
	if (fp == NULL) {
		perror(downloadFilePath.c_str());
		exit(-1);
	}

	int64_t sz = 0;
	recv(connFd, &sz, 8, 0);
	assert(sz <= MaxFile);

	char buf[IOBufSize];
	int ret;
	while (sz > 0) {
		int32_t size = (sz < IOBufSize) ? sz: IOBufSize;
		int ret = recv(connFd, buf, size, 0);
		if (ret == 0) {
			fprintf(stderr, "error\n");
			exit(-1);
		}
		fwrite(buf, 1, ret, fp);
		sz -= ret;
	}
	fclose(fp);
	fprintf(stderr, "%s downloaded\n", filename.c_str());
}

void CommandHelper::logout()
{
	Command command = ::logout;
	int usernameLen = username.length();
	send(connFd, &command, sizeof(int), 0);
	send(connFd, &usernameLen, sizeof(int), 0);
	if (usernameLen > 0)
		send(connFd, username.c_str(), usernameLen, 0);
	char ack = true;
	recv(connFd, &ack, 1, 0);

	if (ack) {
		setState(::HOME);
		setUsername("");
		refresh();
	} else {
		fprintf(stderr, "username incorrect\n");
	}
}

void CommandHelper::setState(State state)
{
	this->state = state;
}

State CommandHelper::getState()
{
	return state;
}

void CommandHelper::setUsername(string username)
{
	this->username = username;
}

string CommandHelper::getUsername()
{
	return username;
}

void CommandHelper::promptStateIncorrect()
{
	fprintf(stderr, "state incorrect\n");
}

void CommandHelper::promptReturningToHomePage()
{
	fprintf(stderr, "returning to home page ...\n%s\n", del);
}

void CommandHelper::showHomePage()
{
	#define SPACE "    "
	fprintf(stderr,"\t"      "*===========================*\n");
	fprintf(stderr,"\t"      "*=                         =*\n");
	fprintf(stderr,"\t"      "*= \033[5mWelcome to Chatroom %s \033[0m=*\n", version);
	fprintf(stderr,"\t"      "*=                         =*\n");
	fprintf(stderr,"\t"      "*===========================*\n\n");
	fprintf(stderr,"\t"      "Start with an instruction\n");
	fprintf(stderr,"\t" BYEL SIGN_UP SPACE LOGIN SPACE QUIT SPACE HELP RESET "\n");
	#undef SPACE
}

void CommandHelper::showOnlinePage()
{
	#define COLOR "\033[32m\033[1m\033[5m"
	fprintf(stderr, "\n" "\t"      "Hello " COLOR "%s" RESET " >< How are you ?\n",
		username.empty() ? "[anonymous]": username.c_str()
		);
	#undef COLOR
	fprintf(stderr,      "\t" BYEL SEND "       " LIST "       " HISTORY " [%%d]\t" DOWNLOADLIST " [%%d]" RESET "\n");
	fprintf(stderr,      "\t" BYEL DOWNLOAD " [file] [time] " LOGOUT "         " HELP RESET "\n");
}
