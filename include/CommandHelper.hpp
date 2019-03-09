#ifndef _COMMANDHELPER_HPP_
#define _COMMANDHELPER_HPP_

#include <string>
#include "State.hpp"
#include "SignUpHelper.hpp"
using namespace std;

#define HELP	"\\help"
#define REFRESH	"\\refresh"
#define SIGN_UP	"\\sign-up"
#define LOGIN	"\\login"
#define QUIT	"\\quit"
#define	USERNAME	"\\username"
#define PASSWORD	"\\password"
#define CONFIRM_PASSWORD	"\\confirm-password"
#define CANCEL	"\\cancel"
#define CREATE_ACCOUNT	"\\create-account"
#define LIST	"\\list"
#define SEND	"\\send"
#define LOGOUT	"\\logout"
#define HISTORY	"\\history"
#define DOWNLOAD	"\\download"
#define DOWNLOADLIST	"\\download-list"
#define RETURN	"\\return"


class CommandHelper {
	public:
		static const char *version;
		static const char *arrow[2];
		static const string strEmpty;	/* [Empty] */
		static const string strHidden;  /* [Hidden] */
		static const string savedPasswordFolder; /* ../data/client/pass/ */
		static const string downloadFolder;
		static const string downloadListFolder;

		CommandHelper(int connFd, State state);
		void help();
		void refresh();
		void signUp();
		void login();
		void quit();
		void inputUsername(string arg);
		void inputPassword();
		void confirmPassword();
		void cancelSignUp();
		void createAccount();
		void sendData(string option, string targetUserName, string content);
		void list();
		void history(const char *arg);
		void showDownloadList(const char *arg);
		void downloadRequest(string filename, string timeStr);
		void logout();
		void setState(State state);
		State getState();
		void setUsername(string username);
		string getUsername();
		void promptStateIncorrect();

	private:
		static const int PasswordBuffer = 1024;
		char storedPassword[PasswordBuffer];
		int connFd;
		State state;
		string username;
		SignUpHelper signUpHelper;

		void promptReturningToHomePage();
		void showHomePage();
		void showOnlinePage();

		void showLocalCommands();
		void savePassword(string path, const char *password);
		void sendMessage(string target, string message);
		void sendFile(string target, string arg);
};
#endif