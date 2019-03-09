#ifndef _CREATEACCOUNTRESULT_HPP_
#define _CREATEACCOUNTRESULT_HPP_

enum CreateAccountResult
{
	OK = 0x00,
	UsernameInvalid = 0x01,
	PasswordInvalid = 0x02,
	UsernameTaken = 0x03,
	FullAccount = 0x04,
	Incomplete = 0x05,
	Undefined = 0x06
};

#endif