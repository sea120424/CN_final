#ifndef _SOCKETADDR_HPP
#define _SOCKETADDR_HPP

#include <string>
using namespace std;
static char *host = (char *)"localhost";
static unsigned port = 3000;
class SocketAddr: pair < string, unsigned > {
	public:
		/*
		 *	first: string host (ip or hostname both ok)
		 *	second: unsigned port
		 */
		SocketAddr(): pair < string, unsigned > (::host, ::port)
		{

		}

		SocketAddr(string host, unsigned port): pair < string, unsigned > (host, port)
		{

		}

		inline string host() const
		{
			return first;
		}

		inline unsigned port() const
		{
			return second;
		}

		bool operator == (const SocketAddr & right) const
        {
            return (first == right.first && second == right.second);
        }

        bool operator < (const SocketAddr & right) const
        {
        	return (pair<string, unsigned>) *this < (pair<string, unsigned>) right;
        }
};

#endif