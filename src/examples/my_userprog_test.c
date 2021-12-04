#include <stdio.h>
#include <syscall.h>

int
main (int argc, char *argv[]) 
{
  int n = 3;
  //char* s;
  //de la ce ar trebui sa citeasca?
  char s[3] = {'c','e','0'};
  char readed[4] = "";
  if(create("ceva.txt",80)){
    int fd = open("ceva.txt");
    printf("%d \n", fd);

    printf("%d ", write(fd, s, n)); //cum trimit inapoi f->eax?

    seek(fd,1);

    printf("Inainte de citit:%s \n", readed);
    printf("%d ",read(fd, readed, n));
    printf("Am citit:%s \n", readed);

    seek(fd,0);

    printf("Inainte de citit:%s \n", readed);
    printf("%d ",read(fd, readed, n)); //cine ar trebui sa se asigure ca avem /0 la final?
    //ai grija, pare ca nu returnezi corect cate caractere ai citit!

    //Alex, tu cum zici sa ne testam testele?
    printf("Am citit:%s \n", readed);

  }
  
  //printf("%d ",create ((char *) 0x20101234, 0));

  return EXIT_SUCCESS;
}
