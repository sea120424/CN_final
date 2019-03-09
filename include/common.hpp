#ifndef _COMMON_HPP_
#define _COMMON_HPP_

#define IOBufSize 1024
#define MB 1048576
#define Limit 20
#define MaxFile Limit * MB

#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <bsd/md5.h>
#include <string>
using namespace std;

int mkdirIfNotExist(const char *dirname)
{
	if (dirname == NULL) {
		fprintf(stderr, "can\'t create directory with null argument.\n");
		return -2;
	}

	DIR *dir = opendir(dirname);
	if (dir != NULL) {
		closedir(dir);
		return 0;
	}

	if (mkdir(dirname, 0777) < 0) {
		perror(dirname);
		return -1;
	}

	return 0;
}

string md5Hash(string input)
{
	uint8_t digest[16];
	char encoded[33];
	MD5_CTX ctx;
  	MD5Init(&ctx);
  	MD5Update(&ctx, (const uint8_t*)input.c_str(), input.length());
  	MD5Final(digest, &ctx);
  	for (int i = 0; i < 16; i++) {
  		int a = digest[i] / 16;
  		int b = digest[i] % 16;
  		encoded[2 * i] = (a < 10) ? a + '0': 'a' + (a - 10);
  		encoded[2 * i + 1] = (b < 10) ? b + '0': 'a' + (b - 10);
  	}
  	encoded[32] = '\0';
  	return (string(encoded));
}

#endif