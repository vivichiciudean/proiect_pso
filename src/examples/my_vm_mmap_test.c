/*
 * my_userprog_test.c
 *
 *  Created on: Nov 11, 2012
 *      Author: acolesa
 */


#include <stdio.h>
#include <syscall.h>
#include <string.h>


char *file_name = "test.txt";

// This is PHYS_BASE - 100*4096
#define MAPPING_ADDR 3220815872

int
main (int argc, char **argv)
{
	int fd, no, size, i;
	char buf;
	char *addr = MAPPING_ADDR;
	mapid_t mid;

	fd = open(file_name);
	size = 0;

	// Display the file's contents using read system call
	printf("\n\n[USER PROGRAM] The contents of file \"%s\" before mapping it on memory is:\n", file_name);
	while ((no=read(fd, &buf, 1)) == 1) {
		write(1, &buf, no);
		size++;
	}

	// Map the file on process memory
	mid = mmap(fd, addr);

	// Display the file's contents using memory accesses
	printf("\n\n[USER PROGRAM] The contents of file \"%s\" during mapping it on memory (initial contents) is: ... TO DO ...\n", file_name);
	//for (i=0; i<size; i++)
	//	printf("%c", addr[i]);

	// Change the file contents using memory accesses; shift-rotate its contents to left with one position
	//for (i=0; i<size; i++)
	//	addr[i] = addr[(i+1)%size];

	// Display the file's contents using memory accesses
	printf("\n\n[USER PROGRAM] The contents of file \"%s\" during mapping it on memory (changed contents) is ... TO DO ...:\n", file_name);
	//for (i=0; i<size; i++)
	//	printf("%c", addr[i]);

	munmap(mid);

	close(fd);

	fd = open(file_name);
	size = 0;

	// Display the file's contents using read system call
	printf("\n\n[USER PROGRAM] The contents of file \"%s\" after un-mapping it from memory is:\n", file_name);
	while ((no=read(fd, &buf, 1)) == 1) {
		write(1, &buf, no);
	}



	return EXIT_SUCCESS;
}


