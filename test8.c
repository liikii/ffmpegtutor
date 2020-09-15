#include <stdio.h>


/*
gcc -o test8 test8.c 
*/
void show_bin(int n){
    if (n < 0)
    {
        /* code */
        return;
    }
    while (n) {
        if (n & 1)
            printf("1");
        else
            printf("0");

        n >>= 1;
    }
    printf("\n");
}


int main(int argc, char const *argv[])
{   
    show_bin(-3);
    /* code */
    return 0;
}