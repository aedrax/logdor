// Links only Logdor::Core. Instantiates every public core class so the linker
// must resolve their symbols without any GUI library on the link line.
// check_no_gui.cmake then runs ldd on this binary to reject transitive GUI deps.

#include <logdor/Version.h>

#include <cstdio>

int main()
{
    std::printf("logdor-core %s\n", qPrintable(logdor::coreVersion()));
    return 0;
}
