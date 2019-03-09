#ifndef _LOGINRESULT_HPP_
#define _LOGINRESULT_HPP_

enum LoginResult
{
	Login = 0x00,
	UsernameDoesNotExist = 0x01,
	PasswordIncorrect = 0x02,
	AlreadyOnline = 0x03,
	LoginByAnotherProcess = 0x04,
	ChatroomFull = 0x05,
	Uninitialized = 0x06
};

#endif