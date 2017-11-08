/*FAST_gpu_thread.c
 *
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include "hashpipe.h"
#include "FAST_databuf.h"
#include "math.h"

static const char * status_key;

typedef struct {
    uint64_t    net_mcnt;
    bool        initialized;
    int         out_block_idx;
    int 	in_block_idx;
} cov_block_info_t;

static inline void initialize_block_info(cov_block_info_t * binfo)
{

    // If this block_info structure has already been initialized
    if(binfo->initialized) {
        return;
    }

    binfo->in_block_idx     = 0;
    binfo->out_block_idx    = 0;
    binfo->net_mcnt	    = 0;
    binfo->initialized	    = 1;
}

static cov_block_info_t binfo;


static polar_data_t  polarization_process(FAST_input_databuf_t *db_in)


{

/*abstract polarization (I,Q,U,V) form Original Data, according to data type information.

        I = X*X + Y*Y
        Q = X*X - Y*Y
        U = 2 * XY_real
        V = -2 * XY_img
*/
    
    int	 block_in  = binfo.in_block_idx;
    bool data_type;
    polar_data_t data;
//    printf("Data_type %d\n",db_in->block[block_in].header.data_type);
//    printf("sizeof char=%ld\n", sizeof(data) );

    data_type = db_in->block[block_in].header.data_type;
 //   printf("Data_type %d\n",data_type);
 //   fprintf(stderr,"sha?????\n");
    if (data_type == 0)
    {	
     //  fprintf(stderr,"Na ni!!!!!!!\n");
       for(int j=0;j<N_CHANS_BUFF;j++)
	  {
//	   fprintf(stderr,"loop j in I : %d\n",j);
	   data.Polar1[j]  = 
		        db_in->block[block_in].data[j*N_POLS_CHAN] 
		      + db_in->block[block_in].data[j*N_POLS_CHAN+1];
//			fprintf(stderr,"loop j in I : %d\n",j);

           data.Polar2[j]  = 
			db_in->block[block_in].data[j*N_POLS_CHAN]
		      - db_in->block[block_in].data[j*N_POLS_CHAN+1];
// 			printf("loop j in Q: %d \n",j);
          }

		
    }
    else if (data_type = 1)
    {
       for(int j=0;j<N_CHANS_BUFF;j++)
          {

           data.Polar1[j] = 
			db_in->block[block_in].data[j*N_POLS_CHAN]
		      + db_in->block[block_in].data[j*N_POLS_CHAN+1];

           data.Polar2[j] = 
			db_in->block[block_in].data[j*N_POLS_CHAN]
		      - db_in->block[block_in].data[j*N_POLS_CHAN+1];
	  }
    }
    return data;
}


static void *run(hashpipe_thread_args_t * args)
{
    // Local aliases to shorten access to args fields
    FAST_input_databuf_t *db_in = (FAST_input_databuf_t *)args->ibuf;
    FAST_output_databuf_t *db_out = (FAST_output_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
    status_key = args->thread_desc->skey;
    polar_data_t data;
//    printf("sizeof data=%ld\n", sizeof(data) );	
//    int binfo.in_block_idx	= binfo.in_block_idx;
//    int binfo.out_block_idx	= binfo.out_block_idx;
    uint64_t netmcnt    = db_in->block[binfo.in_block_idx].header.netmcnt;
    int rv;

    if(!binfo.initialized) {
        initialize_block_info(&binfo);
    }
    while (run_threads()) {

        hashpipe_status_lock_safe(&st);
        hputi4(st.buf, "COVT-IN", binfo.in_block_idx);
        hputs(st.buf, status_key, "waiting");
        hputi4(st.buf, "COVT-OUT", binfo.out_block_idx);
	hputi8(st.buf,"BUFMCNT",netmcnt);
        hashpipe_status_unlock_safe(&st);

        // Wait for new input block to be filled
        while ((rv=FAST_input_databuf_wait_filled(db_in, binfo.in_block_idx)) != HASHPIPE_OK) {
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
	

        // Note processing status
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "processing");
        hashpipe_status_unlock_safe(&st);
	
        data = polarization_process(db_in);
        // Mark input block as free and advance
        FAST_input_databuf_set_free(db_in, binfo.in_block_idx);
        binfo.in_block_idx = (binfo.in_block_idx + 1) % db_in->header.n_block;

	// Wait for new output block to be free
	 while ((rv=FAST_output_databuf_wait_free(db_out, binfo.out_block_idx)) != HASHPIPE_OK) {
            if (rv==HASHPIPE_TIMEOUT) {
                hashpipe_status_lock_safe(&st);
                hputs(st.buf, status_key, "block_out");
                hashpipe_status_unlock_safe(&st);
                continue;
            } else {
                hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                pthread_exit(NULL);
                break;
            }
        }
//	printf("wait for output writting");
//	exit(1);
	//db_out->block[binfo.out_block_idx].data ;
        memcpy(&db_out->block[binfo.out_block_idx].data,&data,N_POLS_CHAN*N_CHANS_BUFF*sizeof(char));
//	db_out->block[binfo.out_block_idx].data = data;
//	fprintf(stderr,"Data size: %lu \n",sizeof(data));
//	fprintf(stderr,"Block Data size: %lu \n",sizeof(db_out->block[binfo.out_block_idx].data));
	//exit(1);
        // Mark output block as full and advance
        FAST_output_databuf_set_filled(db_out,binfo.out_block_idx);
        binfo.out_block_idx = (binfo.out_block_idx + 1) % db_out->header.n_block;		
        /* Check for cancel */
        pthread_testcancel();
    }

    return THREAD_OK;
}

static hashpipe_thread_desc_t FAST_gpu_thread = {
    name: "FAST_gpu_thread",
    skey: "COV-STAT",
    init: NULL,
    run:  run,
    ibuf_desc: {FAST_input_databuf_create},
    obuf_desc: {FAST_output_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&FAST_gpu_thread);
}

