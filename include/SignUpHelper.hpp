#ifndef _SIGNUPHELPER_HPP_
#define _SIGNUPHELPER_HPP_

#include "SignUpUtils.hpp"
#include "CreateAccountResult.hpp"
class SignUpHelper: public SignUpUtils {
	private:
		int connFd;
		bool usernameTaken = false;
		string confirmPassword;
		bool passwordMatched();
		bool formComplete();

	public:
		SignUpHelper();
		void setFd(int connFd);
		void setConfirmPassword(string confirmPassword);
		bool usernameValid();
		void reset();
		void handleInputUsername(string username);
		void handleInputPassword(string password);
		void handleConfirmPassword(string confirmPassword);
		void cancel();
		CreateAccountResult createAccount();
		void refresh();
};
#endif