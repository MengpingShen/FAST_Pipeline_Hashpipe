/*
 * FAST_output_thread.c
 * 
 */

#include <stdio.h>
#include <time.h>
#include <math.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include "hashpipe.h"
#include "FAST_databuf.h"
#include "filterbank.h"
#include <sys/time.h>
//#include "FAST_net_thread.h"
//#include "FAST_net_thread.c"
static int block_idx=0;
extern bool store_flag;
static void *run(hashpipe_thread_args_t * args)
{
	printf("\n%ld Mbytes for each Filterbank file.\n ",N_MBYTES_PER_FILE);
	printf("\n%d Channels per Buff.\n ",N_CHANS_BUFF);
	// Local aliases to shorten access to args fields
	// Our input buffer happens to be a FAST_ouput_databuf
	FAST_output_databuf_t *db = (FAST_output_databuf_t *)args->ibuf;
	hashpipe_status_t st = args->st;
	const char * status_key = args->thread_desc->skey;
	int rv;
	uint64_t N_Mbytes_save = 0;
	uint64_t N_Mbytes_file = N_MBYTES_PER_FILE;
	int filb_flag = 1;
	FILE * FAST_file;

	/* Main loop */
	while (run_threads()) {
		hashpipe_status_lock_safe(&st);
		hputi4(st.buf, "OUTBLKIN", block_idx);
		hputi8(st.buf, "DATSAVMB",N_Mbytes_save/1024);
		hputs(st.buf, status_key, "waiting");
		hashpipe_status_unlock_safe(&st);

		// get new data
		while ((rv=FAST_output_databuf_wait_filled(db, block_idx))
		!= HASHPIPE_OK) {
		if (rv==HASHPIPE_TIMEOUT) {
			hashpipe_status_lock_safe(&st);
			hputs(st.buf, status_key, "blocked");
			hashpipe_status_unlock_safe(&st);
			continue;
			} else {
				hashpipe_error(__FUNCTION__, "error waiting for filled databuf");
				pthread_exit(NULL);
				break;
			}
		}

		hashpipe_status_lock_safe(&st);
		hputs(st.buf, status_key, "processing");
		hashpipe_status_unlock_safe(&st);

		if (filb_flag ==1){
		        char    f_fil[256];
		        struct tm  *now;
			time_t rawtime;
	                printf("START to writting!!!!!!");
		        printf("\n\nopen new filterbank file...\n\n");
        		time(&rawtime);
		        now = localtime(&rawtime);
	        	strftime(f_fil,sizeof(f_fil), "2017_Nov_03/Beam_%Y-%m-%d_%H-%M-%S.fil",now);
//        		strftime(f_fil,sizeof(f_fil), "/tmp/ramdisk/data_%Y-%m-%d_%H-%M-%S.fil",now);
			WriteHeader(f_fil);
		        printf("write header done!\n");
		        FAST_file=fopen(f_fil,"a+");
	        	printf("starting write data to %s...\n",f_fil);
					}

		fwrite(db->block[block_idx].data.Polar1,sizeof(char),N_CHANS_BUFF,FAST_file);
//                fwrite(db->block[block_idx].data.Q,sizeof(int),N_CHANS_BUFF,FAST_file);
//		fprintf(stderr, "Buffsize: %lu",BUFF_SIZE);
		N_Mbytes_save += BUFF_SIZE/N_POLS_CHAN/1024;		
//		printf("\nData save:%lu\n",N_Mbytes_save);
//		printf("\nTotal file size:%lu\n",N_Mbytes_file);
//		printf("\nDevide?:%lu\n",N_Mbytes_save%N_Mbytes_file);
		if (N_Mbytes_save % N_Mbytes_file ==0){
				filb_flag = 1;
				}
		else{
				filb_flag = 0;
				}		
	

		FAST_output_databuf_set_free(db,block_idx);
		block_idx = (block_idx + 1) % db->header.n_block;
		

		//Will exit if thread has been cancelled
		pthread_testcancel();

	}
	fclose(FAST_file);
	return THREAD_OK;
}

static hashpipe_thread_desc_t FAST_output_thread = {
	name: "FAST_output_thread",
	skey: "OUTSTAT",
	init: NULL, 
	run:  run,
	ibuf_desc: {FAST_output_databuf_create},
	obuf_desc: {NULL}
};

static __attribute__((constructor)) void ctor()
{
	register_hashpipe_thread(&FAST_output_thread);
}

