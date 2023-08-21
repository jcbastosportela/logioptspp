// test_detour.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <windows.h>

static void do_shit(size_t t)
{
    while (1) {
        Sleep(t);
        WCHAR name[1000];
        DWORD size = 1000;
        auto res = QueryFullProcessImageNameW(0, 0, name, &size);
        std::wcout << name;
    }
}

int main()
{
    WCHAR name[1000];
    DWORD size = 1000;
    auto res = QueryFullProcessImageNameW(0,0,name,&size);

    if (res)
    {
        std::cout << "Hello World!\n";
        std::wcout << name;
        do_shit(500);
    }
    else {
        std::cout << "Failed!\n";
        std::wcout << name;
        do_shit(5000);
    }

}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
