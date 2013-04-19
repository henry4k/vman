#include "Util.h"


namespace vman
{


bool IsLittleEndian_()
{
	const int i = 1;
	return ( (*(const char*)&i) != 0 );
}
bool IsLittleEndian = IsLittleEndian_();



#if defined(_WIN32) || defined(WIN32)
char DirSep = '\\';
#else
char DirSep = '/';
#endif


}
