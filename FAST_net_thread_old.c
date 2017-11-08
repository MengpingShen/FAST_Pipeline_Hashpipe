/*
 * FAST_net_thread.c
 * Add misspkt correct mechanism.
 *  
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>
#include "hashpipe.h"
#include "FAST_databuf.h"
//#include "FAST_net_thread.h"
//defining a struct of type hashpipe_udp_params as defined in hashpipe_udp.h
//unsigned long long miss_pkt = 0;
//static long miss_gap = 0;
static int total_packets_counted = 0;
static hashpipe_status_t *st_p;
static  const char * status_key;
static int init(hashpipe_thread_args_t * args)
{
        hashpipe_status_t st = args->st;
        hashpipe_status_lock_safe(&st);
	hputi8(st.buf,"NETMCNT",0);
        hputi8(st.buf, "NPACKETS", 0);
        hputi8(st.buf, "RCVMB",0);
	hputi8(st.buf,"MiSSPKT",0);
        hashpipe_status_unlock_safe(&st);
        return 0;

}

typedef struct {
    uint64_t    mcnt;            // counter for packet
    bool        source_from;    // 0 - power spectra, 1 - pure ADC sample
    bool        beam_type;      // 0 - single beam, 1 - multi-beam
    int         beam_ID;        // beam ID
    bool	data_type;	// spectra: 0 - power term, 1 - cross term
} packet_header_t;


typedef struct {
    uint64_t 	cur_mcnt;
    uint64_t	net_mcnt;
    long 	miss_pkt;
    long   	offset;
    int         initialized;
    int		block_idx;
    bool	start_flag;
} block_info_t;

static block_info_t binfo;
// This function must be called once and only once per block_info structure!
// Subsequent calls are no-ops.
static inline void initialize_block_info(block_info_t * binfo)
{

    // If this block_info structure has already been initialized
    if(binfo->initialized) {
        return;
    }

    binfo->cur_mcnt	= 0;
    binfo->block_idx	= 0;
    binfo->start_flag	= 0;
    binfo->offset	= 0;
    binfo->miss_pkt	= 0;
    binfo->initialized	= 1;
}


static inline void get_header( packet_header_t * pkt_header, char *data0)
{
    uint64_t raw_header;
//    raw_header = le64toh(*(unsigned long long *)p->data);
    memcpy(&raw_header,data0,N_BYTES_HEADER*sizeof(char));
    printf("Header raw: %lu \n",raw_header);
    pkt_header->mcnt        =  raw_header	& 0x00ffffffffffffff;
    pkt_header->source_from = (raw_header >> 7) & 0x0000000000000001;
    pkt_header->beam_type   = (raw_header >> 6) & 0x0000000000000001;
    pkt_header->beam_ID     = (raw_header >> 1) & 0x000000000000003f;
    pkt_header->data_type   = (raw_header >> 0) & 0x0000000000000001;
    printf("Mcnt of header is :%lu \n ",pkt_header->mcnt);
}

static inline void miss_pkt_process( uint64_t pkt_mcnt, block_info_t *binfo,FAST_input_databuf_t *db) 
{
    binfo->miss_pkt	+= (pkt_mcnt - binfo->cur_mcnt);
    long  miss_pkt       =  pkt_mcnt - binfo->cur_mcnt;
    uint64_t miss_size   =  miss_pkt * DATA_SIZE_PACK;
    int rv;

    if (((binfo->offset + miss_size ) >= BUFF_SIZE) && (miss_size < BUFF_SIZE)){

        while (( rv = FAST_input_databuf_wait_free(db, binfo->block_idx))!= HASHPIPE_OK) {
              if (rv==HASHPIPE_TIMEOUT) {
                  hashpipe_status_lock_safe(st_p);
                  hputs(st_p->buf, status_key, "blocked");
                  hashpipe_status_unlock_safe(st_p);
                  continue;
               } else {
                   hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                   pthread_exit(NULL);
                   break;
              }
        }

        memset(db->block[binfo->block_idx].data+binfo->offset,0,(BUFF_SIZE - binfo->offset)*sizeof(char));
        binfo->offset  = binfo->offset + miss_size - BUFF_SIZE;//Give new offset after 1 buffer zero.

        // Mark block as full
        if(FAST_input_databuf_set_filled(db, binfo->block_idx) != HASHPIPE_OK) {
            hashpipe_error(__FUNCTION__, "error waiting for databuf filled call");
            pthread_exit(NULL);}

        binfo->block_idx = (binfo->block_idx + 1) % db->header.n_block;

        while ((rv = FAST_input_databuf_wait_free(db, binfo->block_idx))!= HASHPIPE_OK) {
              if (rv==HASHPIPE_TIMEOUT) {
                  hashpipe_status_lock_safe(st_p);
                  hputs(st_p->buf, status_key, "blocked");
                  hashpipe_status_unlock_safe(st_p);
                  continue;
               } else {
                   hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                   pthread_exit(NULL);
                   break;
              }
          }
        memset(db->block[binfo->block_idx].data,0,(binfo->offset)*sizeof(char));
     }
    
    else if(miss_size > BUFF_SIZE){
		 printf("SYSTEM mcnt:%lu \n", binfo->cur_mcnt);
                 printf("Packet mcnt:%lu \n", pkt_mcnt);
		 fprintf(stderr,"Missing Pkt much more than one Buffer...");
                 pthread_exit(NULL);}
    else{
           while (( rv = FAST_input_databuf_wait_free(db, binfo->block_idx))!= HASHPIPE_OK) {
                 if (rv==HASHPIPE_TIMEOUT) {
                     hashpipe_status_lock_safe(st_p);
                     hputs(st_p->buf, status_key, "blocked");
                     hashpipe_status_unlock_safe(st_p);
                     continue;
                  } else {
                      hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                      pthread_exit(NULL);
                      break;
                 }
             }
           memset(db->block[binfo->block_idx].data,0,(miss_size)*sizeof(char));

	}
 
}


static inline uint64_t process_packet(FAST_input_databuf_t *db,char *data0)
{
    packet_header_t pkt_header;
    const uint64_t *payload_p;
    uint64_t pkt_mcnt	= 0;
    int	pkt_source	= 0;
    int seq             = 0;
    int pkt_bmtype	= 0;
    int pkt_beamID	= 0;
    int pkt_dtype	= 0;
    int rv		= 0;
//    uint64_t dest_p	= 0;

    // Parse packet header
    get_header(&pkt_header,data0);
    pkt_mcnt	= pkt_header.mcnt;
    pkt_source	= pkt_header.source_from;
    pkt_bmtype	= pkt_header.beam_type;
    pkt_beamID	= pkt_header.beam_ID;
    pkt_dtype	= pkt_header.data_type;// Data type for power term or cross term
    
    seq =  pkt_mcnt % N_PACKETS_PER_SPEC;
    printf("seq :%d \n ",seq);
    printf("start flag :%d \n ",binfo.start_flag);
    if (seq == 0 || binfo.start_flag == 1){
	printf("\n ********start !!!******\n\n");
        if (total_packets_counted == 0 ){ 
		binfo.cur_mcnt = pkt_mcnt;
	}

        total_packets_counted++;


        if (binfo.cur_mcnt < pkt_mcnt){
	    miss_pkt_process(pkt_mcnt, &binfo , db);
            binfo.cur_mcnt   = pkt_mcnt+1;
	    binfo.start_flag = 0;
          }
        else if(binfo.cur_mcnt == pkt_mcnt){
	    while (( rv = FAST_input_databuf_wait_free(db, binfo.block_idx))!= HASHPIPE_OK) {
                   if (rv==HASHPIPE_TIMEOUT) {
                       hashpipe_status_lock_safe(st_p);
                       hputs(st_p->buf, status_key, "blocked");
                       hashpipe_status_unlock_safe(st_p);
                       continue;
                    } else {
                        hashpipe_error(__FUNCTION__, "error waiting for free databuf");
                        pthread_exit(NULL);
                        break;
                       }
             }


            // Copy data into buffer
//            payload_p = (uint64_t *)(p->data+8);
            memcpy((db->block[binfo.block_idx].data)+binfo.offset, data0+8, DATA_SIZE_PACK*sizeof(char));
	    // Show Status of buffer
            hashpipe_status_lock_safe(st_p);
            hputi8(st_p->buf,"NETMCNT",binfo.net_mcnt);
            hputi8(st_p->buf,"PKTseq",seq);
            hputi8(st_p->buf,"MiSSPKT",binfo.miss_pkt);
            hashpipe_status_unlock_safe(st_p);

            binfo.offset     += DATA_SIZE_PACK;
            binfo.start_flag  = 1;
            binfo.cur_mcnt   += 1;

            if (binfo.offset == BUFF_SIZE){
	         // Mark block as full
		db->block[binfo.block_idx].header.netmcnt +=1;
            	if(FAST_input_databuf_set_filled(db, binfo.block_idx) != HASHPIPE_OK) {
	                  hashpipe_error(__FUNCTION__, "error waiting for databuf filled call");
            	      pthread_exit(NULL);
                                                                           }
	            binfo.block_idx = (binfo.block_idx + 1) % db->header.n_block;
	            binfo.offset = 0;
	            binfo.start_flag = 0;
                            }
            }
	}
	return db->block[binfo.block_idx].header.netmcnt;
}




static void *run(hashpipe_thread_args_t * args)
{
    if(!binfo.initialized) {
        initialize_block_info(&binfo);
    }

    FAST_input_databuf_t *db  = (FAST_input_databuf_t *)args->obuf;
    hashpipe_status_t st = args->st;
//    const char * status_key = args->thread_desc->skey;
    status_key = args->thread_desc->skey;
    st_p = &st; // allow global (this source file) access to the status buffer

    sleep(5);
    
    /*Start to receive data*/
    struct hashpipe_udp_params up;
    strcpy(up.bindhost,"10.10.10.2");
    up.bindport = 12345;
//    up.packet_size = PKTSIZE;
    
//       fprintf(stderr,"This???????");
//    exit(1);


    hashpipe_status_lock_safe(&st);
    // Get info from status buffer if present (no change if not present)
    hgets(st.buf, "BINDHOST", 80, up.bindhost);
    hgeti4(st.buf, "BINDPORT", &up.bindport);
    // Store bind host/port info etc in status buffer
    hputs(st.buf, "BINDHOST", up.bindhost);
    hputi4(st.buf, "BINDPORT", up.bindport);
    hputs(st.buf, status_key, "running");
    hashpipe_status_unlock_safe(&st);

    struct hashpipe_udp_packet p;

    /* Give all the threads a chance to start before opening network socket */
    sleep(5);



    /* Set up UDP socket */
    int rv = hashpipe_udp_init(&up);
    if (rv!=HASHPIPE_OK) {
        hashpipe_error("FAST_net_thread",
                "Error opening UDP socket.");
        pthread_exit(NULL);
    }


    /* Main loop */

    while (run_threads()) {
	int n;
	//rcvmb  = nbytes/1024/1024;
        hashpipe_status_lock_safe(&st);
        hputs(st.buf, status_key, "waiting");
        hputi4(st.buf, "NETBKOUT", binfo.block_idx);
//	hputi8(st.buf,"NETMCNT",mcnt);
        hputi8(st.buf, "NPACKETS", total_packets_counted);
//        hputi8(st.buf, "RCVMB", binfo.rcvmb);
        hashpipe_status_unlock_safe(&st);

//	p.packet_size = recv(up.sock,p.data,HASHPIPE_MAX_PACKET_SIZE,0);
 //       p.packet_size = recv(up.sock,p.data,PKTSIZE,0);
	char *data0;
	data0 = (char *)malloc(PKTSIZE*sizeof(char));
	n=recvfrom(up.sock,data0,PKTSIZE*sizeof(char),0,NULL,NULL);
	if(!run_threads()) {break;}

//	if (up.packet_size == PKTSIZE){
	if (n == PKTSIZE){
		const uint64_t netmcnt = process_packet((FAST_input_databuf_t *)db, data0);
	}
        pthread_testcancel();
     }// Main loop
     return THREAD_OK;
}

static hashpipe_thread_desc_t FAST_net_thread = {
    name: "FAST_net_thread",
    skey: "NETSTAT",
    init: init,
    run:  run,
    ibuf_desc: {NULL},
    obuf_desc: {FAST_input_databuf_create}
};

static __attribute__((constructor)) void ctor()
{
  register_hashpipe_thread(&FAST_net_thread);
}
