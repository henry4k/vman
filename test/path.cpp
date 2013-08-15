#include <string.h>
#include <assert.h>
#include <Util.h>

using namespace vman;

int main()
{
    if(GetFileType("Foo") != FILE_TYPE_INVALID)
        assert(!"'Foo' does already exist. :I Please remove it.");

    assert(MakeDirectory("Foo") == true);
    assert(GetFileType("Foo") == FILE_TYPE_DIRECTORY);

    assert(MakePath("Foo/Bar/Moo") == true);
    assert(GetFileType("Foo") == FILE_TYPE_DIRECTORY);
    assert(GetFileType("Foo/Bar") == FILE_TYPE_DIRECTORY);
    assert(GetFileType("Foo/Bar/Moo") == FILE_TYPE_INVALID);

    return 0;
}
