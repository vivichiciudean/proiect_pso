#include <stdio.h>
#include <syscall.h>

int
main (int argc, char *argv[]) 
{
    printf("START\n");
    int a = mkdir("a");
    printf("\nMkdir: %d \n", a);
    printf("\nCreate file: %d \n", create("a/b", 512));
    int c = mkdir("c");
    printf("\nMkdir: %d \n", c);
    printf("\nCreate file: %d \n", create("c/test1", 512));
    printf("\nCreate file: %d \n", create("c/test1", 512));

    int fd = open ("a");
    printf("\nOpen: %d \n", fd);
    char s[500] = { 0 };
    printf("readdir success? %d\n", readdir(fd, s));
    printf("first entry:%s\n", s);
    printf("readdir success? %d\n", readdir(fd, s));
    printf("second entry:%s\n", s);

    printf("\nChdir: %d \n", chdir ("a"));
    printf("\nOpen: %d \n",open ("b"));
    printf("\nOpen to fail: %d \n",open ("c"));

    printf("Inumber: %d \n", inumber(fd));

    printf("\nChdir: %d \n", chdir ("/"));

    fd = open ("c");
    printf("\nOpen: %d \n",fd);
    printf("Inumber: %d \n", inumber(fd));

    printf("readdir success? %d\n", readdir(fd, s));
    printf("first entry:%s\n", s);
    printf("readdir success? %d\n", readdir(fd, s));
    printf("second entry:%s\n", s);

    printf("\nChdir: %d \n", chdir ("c"));
    printf("\nChdir: %d \n", chdir (".."));
    fd = open ("c");
    printf("\nOpen: %d \n",fd);
    printf("Inumber: %d \n", inumber(fd));

    printf("readdir success? %d\n", readdir(fd, s));
    printf("first entry:%s\n", s);
    printf("readdir success? %d\n", readdir(fd, s));
    printf("second entry:%s\n", s);

    fd = open ("c");
    printf("Is dir? %d\n", isdir(fd));
    fd = open ("c/test");
    printf("Is dir? %d\n", isdir(fd));

    printf("STOP\n");
    return EXIT_SUCCESS;
}
