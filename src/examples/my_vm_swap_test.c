/*
 * my_userprog_test.c
 *
 *  Created on: Nov 11, 2012
 *      Author: acolesa
 */


#include <stdio.h>
#include <syscall.h>
#include <string.h>

int swaped_out;

int
main (int argc, char **argv)
{
	int size, read_cnt, write_cnt;
	char name[20];

	size = get_swap_size();
	printf("AICIIIIIIIII size = %d\n", size);

	get_swap_name(name);
	printf("name = \"%s\"\n", name);

	read_cnt = get_swap_read_cnt();
	printf("read_cnt = %d\n", read_cnt);

	write_cnt = get_swap_write_cnt();
	printf("write_cnt = %d\n", write_cnt);

	swaped_out = 10;

	swap_out(&swaped_out);

	printf("Swapped out variable is %d\n", swaped_out);

	return EXIT_SUCCESS;
}


