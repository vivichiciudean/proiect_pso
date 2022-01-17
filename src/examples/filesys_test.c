#include <stdio.h>
#include <syscall.h>

int
main (int argc, char *argv[]) 
{
    printf("START\n");
    int a = mkdir("a");
    printf("\nMkdir: %d \n", a);
    printf("\nCreate dir: %d \n", create("a/b", 512));
    printf("\nChdir: %d \n", chdir ("a"));
    printf("\nOpen: %d \n",open ("b"));
    printf("\nOpen: %d \n",open ("c"));
    printf("STOP\n");
    return EXIT_SUCCESS;
}
