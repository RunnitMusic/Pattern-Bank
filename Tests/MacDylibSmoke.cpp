#include <dlfcn.h>

#include <iostream>

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: StepShaperMacDylibSmoke <plugin.dylib>\n";
        return 2;
    }

    void* library = dlopen (argv[1], RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr)
    {
        std::cerr << "dlopen failed: " << dlerror() << '\n';
        return 1;
    }

    dlerror();
    void* entryPoint = dlsym (library, "CreatePlugInstance");
    if (const char* error = dlerror())
    {
        std::cerr << "CreatePlugInstance lookup failed: " << error << '\n';
        dlclose (library);
        return 1;
    }

    if (entryPoint == nullptr)
    {
        std::cerr << "CreatePlugInstance was null\n";
        dlclose (library);
        return 1;
    }

    if (dlclose (library) != 0)
    {
        std::cerr << "dlclose failed: " << dlerror() << '\n';
        return 1;
    }

    std::cout << "Loaded plug-in and resolved CreatePlugInstance\n";
    return 0;
}
