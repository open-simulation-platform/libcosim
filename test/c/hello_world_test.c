#include <cse.h>
#include <string.h>

#define FAILURE 1;


int main()
{
    char buf1[20];
    char buf2[10];
    int ans1 = 0;
    int ans2 = 0;

    ans1 = cse_hello_world(buf1, 20);
    if (strcmp(buf1, "Hello World!")) return FAILURE;
    if (ans1 != 42) return FAILURE;

    ans2 = cse_hello_world(buf2, 10);
    if (strcmp(buf2, "Hello Wor")) return FAILURE;
    if (ans2 != 42) return FAILURE;

    return 0;
}
