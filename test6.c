#include <stdio.h>


int main(int argc, char const *argv[])
{   
    // first assign  or first increase.
    int a = 0;
    int c = a++;
    printf("a++:　%d, %d\n", a, c);
    ++a;
    int d = ++a;
    printf("++a:　%d, %d\n", a, d);
    return 0;
}
