#ifndef __PPPOE_H
#define __PPPOE_H

#include <pthread.h>

#include <linux/if.h>
#include <linux/if_pppox.h>

#include "crypto.h"

/* PPPoE codes */
#define CODE_PADI           0x09
#define CODE_PADO           0x07
#define CODE_PADR           0x19
#define CODE_PADS           0x65
#define CODE_PADT           0xA7
#define CODE_SESS           0x00

/* PPPoE Tags */
#define TAG_END_OF_LIST        0x0000
#define TAG_SERVICE_NAME       0x0101
#define TAG_AC_NAME            0x0102
#define TAG_HOST_UNIQ          0x0103
#define TAG_AC_COOKIE          0x0104
#define TAG_VENDOR_SPECIFIC    0x0105
#define TAG_RELAY_SESSION_ID   0x0110
#define TAG_SERVICE_NAME_ERROR 0x0201
#define TAG_AC_SYSTEM_ERROR    0x0202
#define TAG_GENERIC_ERROR      0x0203

/* Discovery phase states */
#define STATE_SENT_PADI     0
#define STATE_RECEIVED_PADO 1
#define STATE_SENT_PADR     2
#define STATE_SESSION       3
#define STATE_TERMINATED    4

/* Header size of a PPPoE packet */
#define PPPOE_OVERHEAD 6  /* type, code, session, length */
#define HDR_SIZE (sizeof(struct ethhdr) + PPPOE_OVERHEAD)
#define MAX_PPPOE_PAYLOAD (ETH_DATA_LEN - PPPOE_OVERHEAD)
#define MAX_PPPOE_MTU (MAX_PPPOE_PAYLOAD - 2)

#define VENDOR_ADSL_FORUM 0xde9

#define MAX_SID 65534
#define SECRET_LENGTH 16
#define COOKIE_LENGTH 24
#define MAX_SERVICE_NAMES 8

struct pppoe_tag_t
{
	struct list_head entry;
	int type;
	int len;
};

struct pppoe_packet_t
{
	uint8_t src[ETH_ALEN];
	uint8_t dst[ETH_ALEN];
	int code;
	uint16_t sid;
	struct list_head tags;
};

struct pppoe_serv_t
{
	struct list_head entry;
	struct triton_context_t ctx;
	struct triton_md_handler_t hnd;
	uint8_t hwaddr[ETH_ALEN];
	char *ifname;
	int require_service_name:1;
	char *service_names[MAX_SERVICE_NAMES];

	uint8_t secret[SECRET_LENGTH];
	DES_key_schedule des_ks;

	pthread_mutex_t lock;
	struct pppoe_conn_t *conn[MAX_SID];
	uint16_t sid;
	int stopping:1;

	unsigned int conn_cnt;
	struct list_head conn_list;

	struct list_head pado_list;

	struct list_head padi_list;
	int padi_cnt;
	int padi_limit;
	time_t last_padi_limit_warn;
};

extern int conf_verbose;
extern char *conf_service_names[MAX_SERVICE_NAMES];
extern char *conf_ac_name;
extern char *conf_pado_delay;
extern int conf_reply_exact_service;

extern unsigned int stat_active;
extern unsigned int stat_delayed_pado;
extern unsigned long stat_PADI_recv;
extern unsigned long stat_PADO_sent;
extern unsigned long stat_PADR_recv;
extern unsigned long stat_PADR_dup_recv;
extern unsigned long stat_PADS_sent;
extern unsigned long stat_PADI_drop;

extern pthread_rwlock_t serv_lock;
extern struct list_head serv_list;

int mac_filter_check(const uint8_t *addr);
void pppoe_server_start(const char *intf, void *client);
void pppoe_server_stop(const char *intf);

int pppoe_add_service_name(char **list, const char *item);
int pppoe_del_service_name(char **list, const char *item);

extern int pado_delay;
void dpado_check_next(int conn_cnt);
void dpado_check_prev(int conn_cnt);
int dpado_parse(const char *str);

struct rad_packet_t;
int tr101_send_access_request(struct pppoe_tag *tr101, struct rad_packet_t *pack);
int tr101_send_accounting_request(struct pppoe_tag *tr101, struct rad_packet_t *pack);

#endif

