#ifndef _SIGNUPUTILS_HPP_
#define _SIGNUPUTILS_HPP_

#include <string>
#include <ctype.h>
using namespace std;
class SignUpUtils {
	private:
		string username;
		string password;

	public:

		static const int passwordMinLength = 5;
		static const int usernameMaxLength = 10;

		void setUsername(string username)
		{
			this->username = username;
		}

		void setPassword(string password)
		{
			this->password = password;
		}

		string getUsername()
		{
			return username;
		}

		string getPassword()
		{
			return password;
		}

		bool usernameTooLong() {
			return username.length() > usernameMaxLength;
		}

		bool usernameAlphaNumeric()
		{
			for (int i = 0; i < username.length(); i++) {
				if (!isalnum(username[i]))	return false;
			}
			return true;
		}

		bool usernameValid()
		{
			return !username.empty() && !usernameTooLong() && usernameAlphaNumeric();
		}

		bool passwordTooShort() {
			return password.length() < passwordMinLength;
		}

		bool passwordAlphaNumeric()
		{
			for (int i = 0; i < password.length(); i++) {
				if (!isalnum(password[i]))	return false;
			}
			return true;
		}

		bool passwordValid()
		{
			return !passwordTooShort() && passwordAlphaNumeric();
		}
};

#endif