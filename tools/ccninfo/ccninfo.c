/*
 * Copyright (c) 2016-2019, National Institute of Information and Communications
 * Technology (NICT). All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the NICT nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NICT AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE NICT OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * ccninfo.c
 */

#define __CCNINFO_SOURECE__

/****************************************************************************************
 Include Files
 ****************************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>
#include <limits.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <sys/ioctl.h>
#include <net/if.h> 

#include <cefore/cef_define.h>
#include <cefore/cef_frame.h>
#include <cefore/cef_client.h>
#include <cefore/cef_ccninfo.h>
#include <cefore/cef_valid.h>
#include <cefore/cef_log.h>

/****************************************************************************************
 Macros
 ****************************************************************************************/

#define CefC_Ct_Str_Max 		256
#define CefC_Ct_Str_Buff 		257
#define CefC_Max_Buff 			64

/****************************************************************************************
 Structures Declaration
 ****************************************************************************************/

typedef struct {
	
	char 			prefix[CefC_Ct_Str_Buff];
	unsigned char 	name[CefC_Max_Length];
	int 			name_len;
	int 			hop_limit;
	int 			skip_hop;
	int				validf;
	char			valid_algo[256];
	int				chunkf;
	uint32_t		chunkno;
	uint8_t 		flag;
	
} CefT_Ct_Parms;

/****************************************************************************************
 State Variables
 ****************************************************************************************/

static char 	conf_path[PATH_MAX] = {0};
static int 		port_num = CefC_Unset_Port;

/****************************************************************************************
 Static Function Declaration
 ****************************************************************************************/

static CefT_Ct_Parms params;
static int ct_running_f;
static int ct_running_time = 7;
CefT_Client_Handle fhdl;

/*--------------------------------------------------------------------------------------
	Outputs usage
----------------------------------------------------------------------------------------*/
static void
ct_usage_output (
	const char* msg									/* Supplementary information 		*/
);
/*--------------------------------------------------------------------------------------
	Parses parameters
----------------------------------------------------------------------------------------*/
static int											/* error occurs, return negative	*/
ct_parse_parameters (
	int 	argc, 									/* same as main function 			*/
	char*	argv[]									/* same as main function 			*/
);
/*--------------------------------------------------------------------------------------
	Catch the signal
----------------------------------------------------------------------------------------*/
static void
cp_sigcatch (
	int sig
);
/*--------------------------------------------------------------------------------------
	Obtains my NodeID (IP Address)
----------------------------------------------------------------------------------------*/
static void
cp_node_id_get (
	uint16_t* node_id_len,
	unsigned char* node_identifer
);
/*--------------------------------------------------------------------------------------
	Outputs the results
----------------------------------------------------------------------------------------*/
static void
ct_results_output (
	unsigned char* msg,
	uint16_t packet_len, 
	uint16_t header_len, 
	uint64_t start_us, 
	uint64_t rtt_us,
	uint32_t start_ntp32b,
	CefT_Ccninfo_TLVs* tlvs
);
#if 0
/*--------------------------------------------------------------------------------------
	for debug
----------------------------------------------------------------------------------------*/
static void
cp_dbg_cpi_print (
	CefT_Parsed_Ccninfo* pci
);
#endif

/****************************************************************************************
 ****************************************************************************************/

int main (
	int argc,
	char** argv
) {
	struct timeval tv;
	uint64_t end_us_t;
	uint64_t start_us_t;
	uint64_t now_us_t;
	CefT_Ccninfo_TLVs tlvs;
	uint16_t node_id_len;
	unsigned char node_identifer[CefC_Max_Node_Id];
	unsigned char buff[CefC_Max_Length];
	unsigned char msg[CefC_Max_Length];
	int res;
	uint16_t pkt_len;
	uint16_t index = 0;
	struct fixed_hdr* fixhdr;
	uint32_t start_ntp32b;

	/*----------------------------------------------------------------
		Init variables
	------------------------------------------------------------------*/
	/* Inits logging 		*/
	cef_log_init ("ccninfo", 1);
	
	cef_frame_init ();
	memset (&params, 0, sizeof (CefT_Ct_Parms));
	memset (&tlvs, 0, sizeof (CefT_Ccninfo_TLVs));
	ct_running_f = 0;
	
	/* Sets default values that are not zero to parameters		*/
	params.hop_limit = 32;
	
	/*----------------------------------------------------------------
		Parses parameters
	------------------------------------------------------------------*/
	if (ct_parse_parameters (argc, argv) < 0) {
		exit (0);
	}
	if (params.skip_hop >= params.hop_limit) {
		ct_usage_output ("error: [-s hop_count] is greater than or equal to [-r hop_count]");
		exit (0);
	}
	cef_log_init2 (conf_path, 1 /* for CEFNETD */);
#ifdef CefC_Debug
	cef_dbg_init ("ccninfo", conf_path, 1);
#endif // CefC_Debug
	
	/*----------------------------------------------------------------
		Connects to cefnetd
	------------------------------------------------------------------*/
	res = cef_client_init (port_num, conf_path);
	if (res < 0) {
		fprintf (stdout, "[ccninfo] ERROR: Failed to init the client package.\n");
		exit (0);
	}
	
	fhdl = cef_client_connect ();
	if (fhdl < 1) {
		fprintf (stderr, "[ccninfo] ERROR: fail to connect cefnetd\n");
		exit (0);
	}
	

	/*----------------------------------------------------------------
		Creates and puts a ccninfo request
	------------------------------------------------------------------*/
	tlvs.hoplimit = (uint8_t) params.hop_limit;
	tlvs.name_len = params.name_len;
	memcpy (tlvs.name, params.name, tlvs.name_len);
	tlvs.chunk_num = params.chunkno;
	tlvs.chunk_num_f = params.chunkf;

	tlvs.opt.ccninfo_flag = params.flag;
	if (params.skip_hop > 0) {
		tlvs.opt.skip_hop = (uint8_t) params.skip_hop;
	}
	
	srand ((unsigned) time (NULL));
	tlvs.opt.req_id = (uint16_t)(rand () % 65535);
	tlvs.opt.req_id |= 0x8080;
	/* get my node IP */
	cp_node_id_get (&node_id_len, node_identifer);
	tlvs.opt.node_id_len = node_id_len;
	memcpy(tlvs.opt.node_identifer, node_identifer, tlvs.opt.node_id_len);
	/* Set Validation Alglithm */
	if (params.validf == 1) {
		tlvs.alg.valid_type = (uint16_t) cef_valid_type_get (params.valid_algo);
		if (tlvs.alg.valid_type == CefC_T_ALG_INVALID) {
			fprintf (stdout, "ERROR: -v has the invalid parameter %s\n", params.valid_algo);
			exit (1);
		}
		res = cef_valid_init_ccninfoUSER (conf_path, tlvs.alg.valid_type);
		if (res < 0) {
			exit (1);
		}
	} else {
		cef_valid_init(conf_path);
	}
	
	fprintf (stderr, "ccninfo to %s with "	, params.prefix);
	fprintf (stderr, "HopLimit=%d, "		, params.hop_limit);
	fprintf (stderr, "SkipHopCount=%d, "	, params.skip_hop);
	fprintf (stderr, "Flag=0x%04X, "		, params.flag);
	fprintf (stderr, "Request ID=%u and "		, tlvs.opt.req_id);
	{
		char 		addrstr[256];
		struct hostent* host;
		if (node_id_len == 4) {
			host = gethostbyaddr ((const char*)node_identifer, 4, AF_INET);
			if (host != NULL) {
				fprintf (stderr, "node ID=%s\n", host->h_name);
			} else {
				inet_ntop (AF_INET, &node_identifer, addrstr, sizeof (addrstr));
				fprintf (stderr, "node ID=%s\n", addrstr);
			}
		} else if (node_id_len ==  16) {
			host = gethostbyaddr ((const char*)&node_identifer, 16, AF_INET6);
			if (host != NULL) {
				fprintf (stderr, "node ID=%s\n", host->h_name);
			} else {
				inet_ntop (AF_INET6, node_identifer, addrstr, sizeof (addrstr));
				fprintf (stderr, "node ID=%s\n", addrstr);
			}
		}
	}
	res = cef_client_ccninfo_input (fhdl, &tlvs);
	if (res < 0){
		exit (-1);
	}
	
	/*----------------------------------------------------------------
		Main loop
	------------------------------------------------------------------*/
	gettimeofday (&tv, NULL);
	start_us_t = cef_client_covert_timeval_to_us (tv);
	end_us_t = start_us_t + (uint64_t)(ct_running_time * 1000000);
	{	/* set 32bit-NTP time */
    	struct timespec tv;
		clock_gettime(CLOCK_REALTIME, &tv);
		start_ntp32b = ((tv.tv_sec + 32384) << 16) + ((tv.tv_nsec << 7) / 1953125);
	}
	
	ct_running_f = 1;
	
	while (ct_running_f) {
		if (SIG_ERR == signal (SIGINT, cp_sigcatch)) {
			break;
		}
		
		/* Obtains the current time in usec 		*/
		gettimeofday (&tv, NULL);
		now_us_t = cef_client_covert_timeval_to_us (tv);
		
		/* Obtains the replay from cefnetd 			*/
		res = cef_client_read (fhdl, &buff[index], CefC_Max_Length - index);
		
		if (res > 0) {
			res += index;
			
			gettimeofday (&tv, NULL);
			now_us_t = cef_client_covert_timeval_to_us (tv);
			
			do {
				fixhdr = (struct fixed_hdr*) buff;
				pkt_len = ntohs (fixhdr->pkt_len);
				
				if (res >= pkt_len) {
					ct_results_output (buff, pkt_len, 
							fixhdr->hdr_len, start_us_t, now_us_t - start_us_t, start_ntp32b,
							&tlvs);
					
					if (res - pkt_len > 0) {
						memcpy (msg, &buff[pkt_len], res - pkt_len);
						memcpy (buff, msg, res - pkt_len);
						index = res - pkt_len;
					} else {
						index = 0;
					}
					res -= pkt_len;
				} else {
					index = res;
					break;
				}
			} while (res > 0);
		}
		
		/* Checks the waiting time 		*/
		if (now_us_t > end_us_t) {
			break;
		}
	}
	cef_client_close (fhdl);
	exit (0);
}
/*--------------------------------------------------------------------------------------
	Outputs the results
----------------------------------------------------------------------------------------*/
static void
ct_results_output (
	unsigned char* msg, 
	uint16_t packet_len, 
	uint16_t header_len, 
	uint64_t start_us,
	uint64_t rtt_us,
	uint32_t start_ntp32b,
	CefT_Ccninfo_TLVs* tlvs
) {
	CefT_Parsed_Ccninfo	pci;
	int					res, i;
	CefT_Ccninfo_Rpt*	rpt_p;
	CefT_Ccninfo_Rep*	rep_p;
	uint32_t			prev_ar_time;
	unsigned			char uri_str[2048];
	char 				addrstr[256];
	double 				diff_us;
	struct hostent*		host;
	char				con_type;
	
	/* Checks the Validation 			*/
	res = cef_valid_msg_verify_forccninfo (msg, packet_len, NULL, NULL);
	if (res != 0) {
		return;
	}
	
	/* Parses the received Ccninfo Replay 	*/
	res = cef_frame_ccninfo_parse (msg, &pci);
	if (res < 0) {
		return;
	}
	
	/* Check Reply hoplimit and ID */
	if (pci.pkt_type != CefC_PT_REPLY ||
		tlvs->hoplimit < pci.rpt_blk_num ||
		tlvs->opt.req_id != pci.req_id ||
		tlvs->opt.node_id_len != pci.id_len ||
		memcmp (tlvs->opt.node_identifer, pci.node_id, pci.id_len) != 0) {
		return;
	}
	if (pci.rpt_blk_num == 0) {
		return;
	}
		
	
#ifdef DEB_CCNINFO
{
	int dbg_x;
	fprintf (stderr, "DEB_CCNINFO-ccninfo: Ccninfo Reply's Msg [ ");
	for (dbg_x = 0 ; dbg_x < packet_len ; dbg_x++) {
		fprintf (stderr, "%02x ", msg[dbg_x]);
	}
	fprintf (stderr, "](%d(h=%d, p=%d))\n", packet_len, header_len, packet_len-header_len);
}
#endif //DEB_CCNINFO
	
	/* Outputs header message 			*/
	fprintf (stderr, "\nresponse from ");
	if (pci.rpt_blk_tail != NULL) {
		if (pci.rpt_blk_tail->id_len == 4) {
			host = gethostbyaddr ((const char*)pci.rpt_blk_tail->node_id, 4, AF_INET);
			if (host != NULL) {
				fprintf (stderr, "%s: ", host->h_name);
			} else {
				inet_ntop (AF_INET, pci.rpt_blk_tail->node_id, addrstr, sizeof (addrstr));
				fprintf (stderr, "%s: ", addrstr);
			}
		} else if (pci.rpt_blk_tail->id_len == 16) {
			host = gethostbyaddr ((const char*)pci.rpt_blk_tail->node_id, 16, AF_INET6);
			if (host != NULL) {
				fprintf (stderr, "%s: ", host->h_name);
			} else {
				inet_ntop (AF_INET, pci.rpt_blk_tail->node_id, addrstr, sizeof (addrstr));
				fprintf (stderr, "%s: ", addrstr);
			}
		} else {
			for (res = 0 ; res < pci.rpt_blk_tail->id_len ; res++) {
				fprintf (stderr, "%02X", pci.rpt_blk_tail->node_id[res]);
			}
			fprintf (stderr, ": ");
		}
	} else {
		return;
	}
	
	/* Checks Return Code 			*/
	switch (pci.ret_code) {
		case CefC_CtRc_NO_ERROR: {
			fprintf (stderr, "NO_ERROR");
			break;
		}
		case CefC_CtRc_WRONG_IF: {
			fprintf (stderr, "WRONG_IF");
			break;
		}
		case CefC_CtRc_INVALID_REQUEST: {
			fprintf (stderr, "INVALID_REQUEST");
			break;
		}
		case CefC_CtRc_NO_ROUTE: {
			fprintf (stderr, "NO_ROUTE");
			break;
		}
		case CefC_CtRc_NO_INFO: {
			fprintf (stderr, "NO_INFO");
			break;
		}
		case CefC_CtRc_NO_SPACE: {
			fprintf (stderr, "NO_SPACE");
			break;
		}
		case CefC_CtRc_INFO_HIDDEN: {
			fprintf (stderr, "INFO_HIDDEN");
			break;
		}
		case CefC_CtRc_ADMIN_PROHIB: {
			fprintf (stderr, "ADMIN_PROHIB");
			break;
		}
		case CefC_CtRc_UNKNOWN_REQUEST: {
			fprintf (stderr, "UNKNOWN_REQUEST");
			break;
		}
		case CefC_CtRc_FATAL_ERROR: {
			fprintf (stderr, "FATAL_ERROR");
			break;
		}
		default: {
			fprintf (stderr, "unknown");
			break;
		}
	}
	
	/* Outputs RTT[ms]					*/
	fprintf (stderr, ", time=%f ms\n\n", (double)((double) rtt_us / 1000.0));
	
	/* Outputs Route 					*/
	fprintf (stderr, "route information:\n");
	rpt_p = pci.rpt_blk;
	for (i = 0 ; i < pci.rpt_blk_num ; i++) {
		fprintf (stderr, "%2d ", i + 1);
		
		if (rpt_p->id_len == 4) {
			host = gethostbyaddr ((const char*)rpt_p->node_id, 4, AF_INET);
			if (host != NULL) {
				fprintf (stderr, "%s\t\t\t", host->h_name);
			} else {
				inet_ntop (AF_INET, rpt_p->node_id, addrstr, sizeof (addrstr));
				fprintf (stderr, "%s\t\t\t", addrstr);
			}
		} else if (rpt_p->id_len == 16) {
			host = gethostbyaddr ((const char*)rpt_p->node_id, 16, AF_INET6);
			if (host != NULL) {
				fprintf (stderr, "%s\t\t\t", host->h_name);
			} else {
				inet_ntop (AF_INET6, rpt_p->node_id, addrstr, sizeof (addrstr));
				fprintf (stderr, "%s\t\t\t", addrstr);
			}
		} else {
			for (res = 0 ; res < rpt_p->id_len ; res++) {
				fprintf (stderr, "%02X", rpt_p->node_id[res]);
			}
			fprintf (stderr, "\t\t\t");
		}
		
		if (i) {
			diff_us  = (double)(((0xffff0000 & rpt_p->req_arrival_time) >>16)  + (0x0000ffff & rpt_p->req_arrival_time)/65536.0);
			diff_us -= (double)(((0xffff0000 & prev_ar_time) >>16)  + (0x0000ffff & prev_ar_time)/65536.0);
			diff_us *= 1000000.0;
		} else {
			diff_us  = (double)(((0xffff0000 & rpt_p->req_arrival_time) >>16)  + (0x0000ffff & rpt_p->req_arrival_time)/65536.0);
			diff_us -= (double)(((0xffff0000 & start_ntp32b) >>16)  + (0x0000ffff & start_ntp32b)/65536.0);
			diff_us *= 1000000.0;
		}
		prev_ar_time = rpt_p->req_arrival_time;
		
		fprintf (stderr, "%.3f ms\n", diff_us / 1000.0);
		
		rpt_p = rpt_p->next;
	}
	fprintf (stderr, "\n");
	
	/* Outputs Cache Status 				*/
	if (pci.rep_blk_num == 0) {
		return;
	}
	
	fprintf (stderr, "cache information:"
		"   prefix    size    cobs    interests   start-end    cachetime    lifetime\n");
	rep_p = pci.rep_blk;
	for (i = 0 ; i < pci.rep_blk_num ; i++) {
		if(!(rep_p->rep_type == CefC_T_DISC_CONTENT || 
			 rep_p->rep_type == CefC_T_DISC_CONTENT_OWNER)) {
			fprintf (stderr, "Invalid Reply Block!!!\n");
			break;
		}
		if (rep_p->rep_type == CefC_T_DISC_CONTENT){
			con_type = 'c';
		} else if (rep_p->rep_type == CefC_T_DISC_CONTENT_OWNER){
			con_type = 'p';
		} 
		
		fprintf (stderr, "%2d %c ", i+1, con_type);
		
		{
			uint16_t tmp_nmlen;
			uint32_t seqno;
			unsigned char seqno_str[16];
			tmp_nmlen = cef_frame_get_name_without_chunkno (
							rep_p->rep_name, rep_p->rep_name_len, &seqno);
			if (tmp_nmlen > 0) {
				memset(seqno_str, 0, sizeof(seqno_str));
				cef_frame_conversion_name_to_uri (
							rep_p->rep_name, tmp_nmlen, (char *)uri_str);
				sprintf((char *)seqno_str, "/Chunk=%d", seqno);
				fprintf (stderr, "%s%s", uri_str, seqno_str);
			} else {
				cef_frame_conversion_name_to_uri (
							rep_p->rep_name, rep_p->rep_name_len, (char *)uri_str);
				fprintf (stderr, "%s", uri_str);
			}
		}
		
		fprintf (stderr, "\t%7u B %7u %5u   "
			, rep_p->obj_size, rep_p->obj_cnt, rep_p->rcv_interest_cnt);
		
		fprintf (stderr, "%u-%u   ", rep_p->first_seq, rep_p->last_seq);
		
		fprintf (stderr, "%u secs   %u secs\n"
			, rep_p->cache_time, rep_p->lifetime);
		rep_p = rep_p->next;
	}
	
	fprintf (stderr, "\n");
	
	cef_frame_ccninfo_parsed_free (&pci);
	return;
}

/*--------------------------------------------------------------------------------------
	Outputs usage
----------------------------------------------------------------------------------------*/
static void
ct_usage_output (
	const char* msg									/* Supplementary information 		*/
) {
	fprintf (stderr, 	"Usage:ccninfo name_prefix [-f] [-n] [-o] [-r hop_count]"
		                " [-s hop_count] [-v valid_algo] name_prefix "
		                " [-d config_file_dir] [-p port_num]\n");
	
	if (msg) {
		fprintf (stderr, "%s\n", msg);
	}
	
	return;
}

/*--------------------------------------------------------------------------------------
	Parses parameters
----------------------------------------------------------------------------------------*/
static int											/* error occurs, return negative	*/
ct_parse_parameters (
	int 	argc, 									/* same as main function 			*/
	char*	argv[]									/* same as main function 			*/
) {
	int 	i, n;
	char*	work_arg;
	int 	res;
	
	/* counters of each parameter 	*/
	int 	num_opt_f 		= 0;
	int 	num_opt_n 		= 0;
	int 	num_opt_o 		= 0;
	int 	num_opt_r 		= 0;
	int 	num_opt_s 		= 0;
	int 	num_opt_v 		= 0;
	int 	dir_path_f 		= 0;
	int 	port_num_f 		= 0;
	int		num_opt_prefix	= 0;
	
	params.flag = CefC_CtOp_Cache;
	
	/* Parses parameters 		*/
	for (i = 1 ; i < argc ; i++) {
		
		work_arg = argv[i];
		if (work_arg == NULL || work_arg[0] == 0) {
			break;
		}

		/*-----------------------------------------------------------------------*/
		/****** -f (requires the full discovery request) 					******/
		/*-----------------------------------------------------------------------*/
		if (strcmp (work_arg, "-f") == 0) {
			/* Checks whether [-f] is not specified more than twice. 	*/
			if (num_opt_f) {
				ct_usage_output ("error: [-f] is duplicated.");
				return (-1);
			}
			params.flag |= CefC_CtOp_FullDisCover;
			num_opt_f++;
		}
		/*-----------------------------------------------------------------------*/
		/****** -n (requires only the routing path) 						******/
		/*-----------------------------------------------------------------------*/
		else if (strcmp (work_arg, "-n") == 0) {
			/* Checks whether [-n] is not specified more than twice. 	*/
			if (num_opt_n) {
				ct_usage_output ("error: [-n] is duplicated.");
				return (-1);
			}
			
			params.flag &= ~CefC_CtOp_Cache;
			num_opt_n++;
		}
		/*-----------------------------------------------------------------------*/
		/****** -o (requires the path to the content publisher) 			******/
		/*-----------------------------------------------------------------------*/
		else if (strcmp (work_arg, "-o") == 0) {
			/* Checks whether [-o] is not specified more than twice. 	*/
			if (num_opt_o) {
				ct_usage_output ("error: [-o] is duplicated.");
				return (-1);
			}
			params.flag |= CefC_CtOp_Publisher;
			num_opt_o++;
		}
		/*-----------------------------------------------------------------------*/
		/****** -r <Number of traced routers>								******/
		/*-----------------------------------------------------------------------*/
		else if (strcmp (work_arg, "-r") == 0) {
			/* Checks whether [-r] is not specified more than twice. 	*/
			if (num_opt_r) {
				ct_usage_output ("error: [-r hop_count] is duplicated.");
				return (-1);
			}
			if (i + 1 == argc) {
				ct_usage_output ("error: [-r hop_count] is invalid.");
				return (-1);
			}
			
			/* Checks whethre the specified value is valid or not.	*/
			work_arg = argv[i + 1];
			for (n = 0 ; work_arg[n] ; n++) {
				if (isdigit (work_arg[n]) == 0) {
					ct_usage_output ("error: [-r hop_count] is invalid.");
					return (-1);
				}
			}
			params.hop_limit = atoi (work_arg);
			
			if (params.hop_limit < 1) {
				ct_usage_output ("error: [-r hop_count] is smaller than 1.");
				return (-1);
			}
			if (params.hop_limit > 255) {
				ct_usage_output ("error: [-r hop_count] is greater than 255.");
				return (-1);
			}
			num_opt_r++;
			i++;
		}
		/*-----------------------------------------------------------------------*/
		/****** -s <Number of skipped routers>								******/
		/*-----------------------------------------------------------------------*/
		else if (strcmp (work_arg, "-s") == 0) {
			/* Checks whether [-s] is not specified more than twice. 	*/
			if (num_opt_s) {
				ct_usage_output ("error: [-s hop_count] is duplicated.");
				return (-1);
			}
			if (i + 1 == argc) {
				ct_usage_output ("error: [-s hop_count] is invalid.");
				return (-1);
			}
			
			/* Checks whethre the specified value is valid or not.	*/
			work_arg = argv[i + 1];
			for (n = 0 ; work_arg[n] ; n++) {
				if (isdigit (work_arg[n]) == 0) {
					ct_usage_output ("error: [-s hop_count] is invalid.");
					return (-1);
				}
			}
			params.skip_hop = atoi (work_arg);
			
			if (params.skip_hop < 1) {
				ct_usage_output ("error: [-s hop_count] is smaller than 1.");
				return (-1);
			}
			if (params.skip_hop > 255) {
				ct_usage_output ("error: [-s hop_count] is greater than 254.");
			}
			num_opt_s++;
			i++;
		}
		/*-----------------------------------------------------------------------*/
		/****** -v <Validation algorithm>									******/
		/*-----------------------------------------------------------------------*/
		else if (strcmp (work_arg, "-v") == 0) {
			if (num_opt_v) {
				fprintf (stderr, "ERROR: [-v] is duplicated.\n");
				return (-1);
			}
			if (i + 1 == argc) {
				fprintf (stderr, "ERROR: [-v] has no parameter.\n");
				return (-1);
			}
			work_arg = argv[i + 1];
			if (!(strcmp(work_arg, "crc32")==0 || strcmp(work_arg, "sha256")==0)) {
				fprintf (stderr, "ERROR: [-v] has invalid algorithm(%s)\n", work_arg);
				return (-1);
			}
			
			strcpy (params.valid_algo, work_arg);
			params.validf = 1;
			num_opt_v++;
			i++;
		}
		/*-----------------------------------------------------------------------*/
		/****** -d 															******/
		/*-----------------------------------------------------------------------*/
		else if (strcmp (work_arg, "-d") == 0) {
			if (dir_path_f) {
				ct_usage_output ("error: [-d] is duplicated.\n");
				return (-1);
			}
			if (i + 1 == argc) {
				ct_usage_output ("error: [-d] has no parameter.\n");
				return (-1);
			}
			work_arg = argv[i + 1];
			strcpy (conf_path, work_arg);
			dir_path_f++;
			i++;
		}
		/*-----------------------------------------------------------------------*/
		/****** -p 															******/
		/*-----------------------------------------------------------------------*/
		else if (strcmp (work_arg, "-p") == 0) {
			if (port_num_f) {
				ct_usage_output ("error: [-p] is duplicated.\n");
				return (-1);
			}
			if (i + 1 == argc) {
				ct_usage_output ("error: [-p] has no parameter.\n");
				return (-1);
			}
			work_arg = argv[i + 1];
			port_num = atoi (work_arg);
			port_num_f++;
			i++;
		}
		/*-----------------------------------------------------------------------*/
		/****** name_prefix 												******/
		/*-----------------------------------------------------------------------*/
		else {
			/* Error to the invalid option. 		*/
			work_arg = argv[i];
			if (work_arg[0] == '-') {
				ct_usage_output ("error: unknown option is specified.");
				return (-1);
			}
			
			/* Checks whether name_prefix is not specified more than twice. 	*/
			if (num_opt_prefix) {
				ct_usage_output ("error: name_prefix is duplicated.");
				return (-1);
			}
			res = strlen (work_arg);
			
			if (res >= CefC_Ct_Str_Max) {
				ct_usage_output ("error: name_prefix is too long.");
				return (-1);
			}
			
			/* Split name_prefix and chunk# */
			{
				char* chunkp;
				char* chunknop;
				char* workp;
				chunkp = strstr(work_arg, "/Chunk=");
				if (chunkp != NULL) {
					chunknop = chunkp+sizeof("/Chunk=")-1;
					workp = chunknop;
					while (*workp != 0) {
						if (!isdigit((unsigned char)*workp)) {
							ct_usage_output ("error: Chunk# is invalid.");
							return(-1);
						}
						workp++;
					}
					if (workp == chunknop) {
						ct_usage_output ("error: Chunk# is invalid.");
						return(-1);
					}
					params.chunkno = (uint32_t) atoi(chunknop);
					params.chunkf = 1;
					strcpy (params.prefix, work_arg);
					*chunkp = '\0';
				}
			}
			/* Converts the URI to Name. 			*/
			res = cef_frame_conversion_uri_to_name (work_arg, params.name);
			
			if (res < 0) {
				ct_usage_output ("error: prefix is invalid.");
				return (-1);
			}
			if (res < 5) {
				ct_usage_output ("error: prefix MUST NOT be ccn:/.");
				return (-1);
			}
			params.name_len = res;
			if (params.chunkf == 0) {
				strcpy (params.prefix, work_arg);
			}
			
			num_opt_prefix++;
		}
	}
	if (num_opt_prefix == 0) {
		ct_usage_output ("error: prefix is not specified.");
		return (-1);
	}
	return (1);
}
/*--------------------------------------------------------------------------------------
	Catch the signal
----------------------------------------------------------------------------------------*/
static void
cp_sigcatch (
	int sig
) {
	if (sig == SIGINT) {
		ct_running_f = 0;
	}
}
/*--------------------------------------------------------------------------------------
	Obtains my NodeID (IP Address)
----------------------------------------------------------------------------------------*/
static void
cp_node_id_get (
	uint16_t* node_id_len,
	unsigned char* node_identifer
) {
#ifndef CefC_Android
	struct ifaddrs *ifa_list;
	struct ifaddrs *ifa;
	int n;
	int nodeid4_num = 0;
	int nodeid16_num = 0;
    unsigned char** nodeid4;
    unsigned char** nodeid16;

	n = getifaddrs (&ifa_list);
	if (n != 0) {
		return;
	}

	for (ifa = ifa_list ; ifa != NULL ; ifa=ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) {
			continue;
		}
		if (ifa->ifa_addr->sa_family == AF_INET) {
			if (ifa->ifa_flags & IFF_LOOPBACK) {
				continue;
			}
			nodeid4 = (unsigned char**) calloc (1, sizeof (unsigned char*));
			nodeid4[0] = (unsigned char*) calloc (4, 1);
			memcpy (nodeid4[0], &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, 4);
			nodeid4_num = 1;
			break;
		} else {
			/* NOP */;
		}
	}
	if (nodeid4_num == 0) {
		for (ifa = ifa_list ; ifa != NULL ; ifa=ifa->ifa_next) {
			if (ifa->ifa_addr == NULL) {
				continue;
			}
			if (ifa->ifa_addr->sa_family == AF_INET6) {
				if (ifa->ifa_flags & IFF_LOOPBACK) {
					continue;
				}
				nodeid16 = (unsigned char**) calloc (1, sizeof (unsigned char*));
				nodeid16[0] = (unsigned char*) calloc (16, 1);
				memcpy (nodeid16[0], &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr, 16);
				nodeid16_num = 1;
				break;
			} else {
				/* NOP */;
			}
		}
	}
	
	freeifaddrs (ifa_list);

#else // CefC_Android
	int res;
	res = cef_android_node_id_get (&nodeid4, &nodeid4_num, &nodeid16, &nodeid16_num);
	if (res < 0) {
		LOGE ("ERROR : cef_android_node_id_get()\n");
		nodeid4_num = 0;
		nodeid16_num = 0;
	}
	LOGD ("DEBUG : cef_android_node_id_get() success\n");
	LOGD ("DEBUG : ipv4 num = %d, ipv6 num = %d\n", nodeid4_num, nodeid16_num);
#endif // CefC_Android

	if (nodeid4_num > 0) {
		memcpy (node_identifer, nodeid4[0], 4);
		*node_id_len = 4;
	} else if (nodeid16_num > 0) {
		memcpy (node_identifer, nodeid16[0], 16);
		*node_id_len = 16;
	} else {
		node_identifer[0] = 0x7F;
		node_identifer[1] = 0x00;
		node_identifer[2] = 0x00;
		node_identifer[3] = 0x01;
		*node_id_len = 4;
	}
	
	/* free allocated memory */
	for (n = 0; n < nodeid4_num; n++) {
		free (nodeid4[n]);
	}
	if (nodeid4_num != 0) {
		free (nodeid4);
	}
	for (n = 0; n < nodeid16_num; n++) {
		free (nodeid16[n]);
	}
	if (nodeid16_num != 0) {
		free (nodeid16);
	}

	return;
}
#if 0
/*--------------------------------------------------------------------------------------
	for debug
----------------------------------------------------------------------------------------*/
static void
cp_dbg_cpi_print (
	CefT_Parsed_Ccninfo* pci
) {
	int aaa, bbb;
	CefT_Ccninfo_Rpt* rpt_p;
	CefT_Ccninfo_Rep* rep_p;
	
	fprintf(stderr, "----- cef_frame_ccninfo_parse -----\n");
	fprintf(stderr, "PacketType                : 0x%02x\n", pci->pkt_type);
	fprintf(stderr, "ReturnCode                : 0x%02x\n", pci->ret_code);
	fprintf(stderr, "  --- Request Block ---\n");
	fprintf(stderr, "  Request ID              : %u\n", pci->req_id);
	fprintf(stderr, "  SkipHopCount            : %u\n", pci->skip_hop);
	fprintf(stderr, "  Flags                   : 0x%02x F(%c), O(%c), C(%c)\n", pci->ccninfo_flag,
						(pci->ccninfo_flag & CefC_CtOp_FullDisCover) ? 'o': 'x',
						(pci->ccninfo_flag & CefC_CtOp_Publisher)    ? 'o': 'x',
						(pci->ccninfo_flag & CefC_CtOp_Cache)        ? 'o': 'x');
	fprintf(stderr, "  Request Arrival Time    : %u\n", pci->req_arrival_time);
	fprintf(stderr, "  Node Identifier         : ");
	for (aaa=0; aaa<pci->id_len; aaa++)
		fprintf(stderr, "%02x ", pci->node_id[aaa]);
	fprintf(stderr, "\n");
	fprintf(stderr, "  --- Report Block ---(%d)\n", pci->rpt_blk_num);
	rpt_p = pci->rpt_blk;
	for (bbb=0; bbb<pci->rpt_blk_num; bbb++) {
		fprintf(stderr, "  [%d]\n", bbb);
		fprintf(stderr, "    Request Arrival Time  : %u\n", rpt_p->req_arrival_time);
		fprintf(stderr, "    Node Identifier       : ");
		for (aaa=0; aaa<rpt_p->id_len; aaa++)
			fprintf(stderr, "%02x ", rpt_p->node_id[aaa]);
		fprintf(stderr, "\n");
		rpt_p = rpt_p->next;
	}
	fprintf(stderr, "  --- Discovery ---\n");
	fprintf(stderr, "  Name                    : ");
	for (aaa=0; aaa<pci->disc_name_len; aaa++)
		fprintf(stderr, "%02x ", pci->disc_name[aaa]);
	fprintf(stderr, "(%d)\n", pci->disc_name_len);
	fprintf(stderr, "  --- Reply Block ---(%d)\n", pci->rep_blk_num);
	rep_p = pci->rep_blk;
	for (bbb=0; bbb<pci->rep_blk_num; bbb++) {
		fprintf(stderr, "  [%d]\n", bbb);
		fprintf(stderr, "    Content Type          : %s\n", 
			(rep_p->rep_type == CefC_T_DISC_CONTENT) ? "T_DISC_CONTENT" : "T_DISC_CONTENT_OWNER");
		fprintf(stderr, "    Object Size           : %u\n", rep_p->obj_size);
		fprintf(stderr, "    Object Count          : %u\n", rep_p->obj_cnt);
		fprintf(stderr, "    # Received Interest   : %u\n", rep_p->rcv_interest_cnt);
		fprintf(stderr, "    First Seqnum          : %u\n", rep_p->first_seq);
		fprintf(stderr, "    Last Seqnum           : %u\n", rep_p->last_seq);
		fprintf(stderr, "    Elapsed Cache Time    : %u\n", rep_p->cache_time);
		fprintf(stderr, "    Remain Cache Lifetime : %u\n", rep_p->lifetime);
		fprintf(stderr, "    Name                  : ");
		for (aaa=0; aaa<rep_p->rep_name_len; aaa++)
			fprintf(stderr, "%02x ", rep_p->rep_name[aaa]);
		fprintf(stderr, "(%d)\n", rep_p->rep_name_len);
		rep_p = rep_p->next;
	}
}
#endif
