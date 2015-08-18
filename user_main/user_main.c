/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_main.c
 *
 * Description: entry file of user application
 *
 * Modification history:
 *     2014/1/1, v1.0 create this file.
*******************************************************************************/
#include "user_interface.h"

#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"
#include "mem.h"
#include "missing_declarations.h"

#include "lwip/ip_addr.h"
#include "lwip/raw.h"
#include "lwip/udp.h"

#include "comm.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define SIG_LUA 0
#define TASK_QUEUE_LEN 4
os_event_t *taskQueue;

#define UART0   0
#define UART1   1

static uint8_t forward_ip_broadcasts = 1;

void user_rf_pre_init() {
}

/* void ICACHE_FLASH_ATTR dhcps_start(struct ip_info *info) { */
/* } */
/* void ICACHE_FLASH_ATTR dhcps_stop(void) { */
/* } */
/* void ICACHE_FLASH_ATTR dhcps_coarse_tmr(void) { */
/* } */
/* void ICACHE_FLASH_ATTR espconn_init(void) { */
/* } */

/* void task_lua(os_event_t *e){ */
/* 	COMM_DBG("task: Heap size::%d.\n", system_get_free_heap_size()); */
/* 	asm("WAITI 0"); // power consumption doesn't change */
/* 	/\* os_delay_us(1*1000);   // delay 50ms before init uart *\/ */
/* 	system_os_post(USER_TASK_PRIO_0, SIG_LUA, 's'); */
/* } */

/* void task_init(void){ */
/* 	taskQueue = (os_event_t *)os_malloc(sizeof(os_event_t) * TASK_QUEUE_LEN); */
/* 	system_os_task(task_lua, USER_TASK_PRIO_0, taskQueue, TASK_QUEUE_LEN); */
/* 	system_os_post(USER_TASK_PRIO_0, SIG_LUA, 's'); */
/* } */

u8_t raw_receiver(void *arg, struct raw_pcb *pcb, struct pbuf *p0, ip_addr_t *addr)
{
	struct ip_hdr hdr;
	struct pbuf *p;

	/* uint32_t ps; */
	/* asm("RSR %0, PS" : "=r"(ps)); */
	/* COMM_INFO("IRQ level (raw_receiver): %d", (int)ps); */

	COMM_DBG("WLan packet of size %d", p0->tot_len);

	if (pbuf_copy_partial(p0, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
//	if (pbuf_take(p0, &hdr, sizeof(hdr)) != ERR_OK) {
		COMM_WARN("WLan packet of size %d has incomplete header",
			  p0->tot_len);
		return 0;
	}

	if (!forward_ip_broadcasts && (hdr.dest.addr == 0xffffffff)) {
		COMM_DBG("Passing broadcast to internal stack");
		return 0;
	}

	if ((hdr._proto == IP_PROTO_UDP)) {
		int offset = 4 * IPH_HL(&hdr);
		struct udp_hdr udp_h;
		if (!pbuf_copy_partial(p0, &udp_h, sizeof(udp_h), offset)) {
			COMM_WARN("Can't copy UDP header from WLan packet");
			return 0;
		}
		uint16_t src = ntohs(udp_h.src);
		uint16_t dst = ntohs(udp_h.dest);
		if ((src == 67) || (dst == 67) || (src == 68) || (dst == 68)) {
			COMM_DBG("Got DHCP packet, passing it to internal stack");
			return 0;
		}
	}

	comm_send_begin(MSG_IP_PACKET);
	for(p = p0; p; p = p->next) {
		comm_send_data(p->payload, p->len);
	}
	comm_send_end();

	pbuf_free(p0);
	return 1;
	/* return 0; // not processed */
}

static struct raw_pcb *raw_pcb_tcp = NULL;
static struct raw_pcb *raw_pcb_udp = NULL;

void init_wlan() {
	struct station_config config;
	struct ip_info ip;

	wifi_set_phy_mode(PHY_MODE_11N);
	wifi_set_sleep_type(LIGHT_SLEEP_T);
	wifi_set_opmode(STATION_MODE);

	/* os_sprintf((char *)&config.ssid, "g2test"); */
	/* os_sprintf((char *)&config.password, "H8SBsSO2h37"); */
	/* wifi_station_set_config(&config); */

	/* IP4_ADDR(&ip.ip, 10, 66, 0, 10); */
	/* IP4_ADDR(&ip.gw, 10, 66, 0, 1); */
	/* IP4_ADDR(&ip.netmask, 255, 255, 255, 0); */
	/* wifi_set_ip_info(STATION_IF, &ip); */

	/* wifi_station_dhcpc_stop(); */
	/* wifi_station_connect(); */
	/* wifi_station_dhcpc_stop(); */

	raw_pcb_tcp = raw_new(6);
	raw_pcb_udp = raw_new(17);
	if (!raw_pcb_tcp || !raw_pcb_udp) {
		COMM_DBG("Failed to init raw sockets");
	} else {
		// todo: check errors
		raw_bind(raw_pcb_tcp, IP_ADDR_ANY);
		raw_bind(raw_pcb_udp, IP_ADDR_ANY);
		raw_recv(raw_pcb_tcp, raw_receiver, NULL);
		raw_recv(raw_pcb_udp, raw_receiver, NULL);
	}
}


static int inject_packet(uint8_t *data, int n)
{
	struct ip_hdr hdr;
	ip_addr_t dest;
	uint8_t *payload;
	int hl;
	struct pbuf *p;
        struct raw_pcb *pcb;
	int status;

	/* uint32_t ps; */
	/* asm("RSR %0, PS" : "=r"(ps)); */
	/* COMM_INFO("IRQ level (inject_packet): %d", (int)ps); */

	if (n < sizeof(hdr)) {
		COMM_ERR("Packet of size %d is too short", n);
		return -1;
	}
	memcpy(&hdr, data, sizeof(hdr));

	if ((hdr._proto != IP_PROTO_TCP) && (hdr._proto != IP_PROTO_UDP)) {
		COMM_ERR("Proto %d is not supported", hdr._proto);
		return -1;
	}

	hl = 4 * IPH_HL(&hdr);
	if (hl > n) {
		COMM_ERR("Header is larger than data: hl=%d, dl=%d", hl, n);
		return -1;
	}
	payload = data + hl;
	n -= hl;

	p = pbuf_alloc(PBUF_IP, n, PBUF_RAM);
	if (!p) {
		COMM_ERR("Failed to allocate packet of size %d", n);
		return -1;
	}
	memcpy(p->payload, payload, n);
	p->tot_len = n;
	p->len = n;

	pcb = hdr._proto == 6 ? raw_pcb_tcp : raw_pcb_udp;
	dest.addr = hdr.dest.addr;
	status = raw_sendto(pcb, p, &dest);
	pbuf_free(p);
	return status;
}

static void ICACHE_FLASH_ATTR
scan_done(void *arg, STATUS status)
{
	struct bss_info *bss_link = (void *) arg;
	struct bss_info *bss_iter = bss_link;
	struct msg_scan_reply r;
	struct msg_scan_entry e;
	int bss_len;
	int i;

	if (status != OK) {
		r.status = 255;
		r.entries_n = 0;
		return;
	} else {
		r.status = 0;
		r.entries_n = 0;
		bss_iter = bss_link->next.stqe_next; //ignore first
		while (bss_iter) {
			r.entries_n++;
			bss_iter = bss_iter->next.stqe_next;
		}
	}
	comm_send_begin(MSG_SCAN_REPLY);
	comm_send_data((void *) &r, sizeof(r));
	comm_send_end();
	if (r.status) {
		COMM_ERR("Scan failed");
		return;
	}

	i = 0;
	bss_iter = bss_link->next.stqe_next; //ignore first
	while (bss_iter != NULL) {
		e.index = i++;
		bss_len = strlen(bss_iter->ssid);
		e.ssid_len = MIN(bss_len, sizeof(e.ssid));
		memcpy(e.ssid, bss_iter->ssid, e.ssid_len);
		memcpy(e.bssid, bss_iter->bssid, sizeof(e.bssid));
		e.channel = bss_iter->channel;
		e.auth_mode = bss_iter->authmode;
		e.rssi = bss_iter->rssi;
		e.is_hidden = bss_iter->is_hidden;
		bss_iter = bss_iter->next.stqe_next;

		comm_send_begin(MSG_SCAN_ENTRY);
		comm_send_data((void *) &e, sizeof(e));
		comm_send_end();
	}
}

static void packet_from_host(uint8_t type, uint8_t *data, uint32_t n)
{
	/* COMM_DBG("Got packet from host: type=%d, payload_len=%d", */
		  /* (int)type, n); */
	/* uint32_t ps; */
	/* asm("RSR %0, PS" : "=r"(ps)); */
	/* COMM_INFO("packet from host: %d", (int)ps); */

	switch(type) {
	case MSG_IP_PACKET:
		COMM_DBG("Packet from host, %d bytes", n);
		inject_packet(data, n);
		break;
	case MSG_SET_STATIC_IP_CONF: {
		struct msg_ip_conf *conf = (void *) data;
		struct ip_info info;
		if (n != sizeof(*conf)) {
			comm_send_status(255);
			COMM_ERR("Wrong size of STATIC_IP_CONF payload: %d", n);
			return;
		}

		if(wifi_station_dhcpc_status() == DHCP_STARTED) {
			if (!wifi_station_dhcpc_stop()) {
				comm_send_status(255);
				COMM_ERR("Unable to stop DHCPC");
				return;
			}
		}

		// TODO: check endianess
		info.ip.addr = conf->address;
		info.netmask.addr = conf->netmask;
		info.gw.addr = conf->gateway;
		if (!wifi_set_ip_info(STATION_IF, &info)) {
			comm_send_status(255);
			COMM_ERR("Unable to set IP info");
			return;
		}
		comm_send_status(0);
		break;
	}
	case MSG_DHCPC: {
		int ret;
		if (n != 1) {
			comm_send_status(255);
			COMM_ERR("Wrong size of DHCPC payload: %d", n);
			return;
		}
		if (data[0])
			ret = wifi_station_dhcpc_start();
		else
			ret = wifi_station_dhcpc_stop();
		if (!ret) {
			comm_send_status(255);
			COMM_ERR("Cannot change DHCPC state");
			return;
		}
		comm_send_status(0);
		break;
	}
	case MSG_GET_IP_CONF_REQUEST: {
		struct msg_ip_conf conf;
		struct ip_info info;
		if (!wifi_get_ip_info(STATION_IF, &info)) {
			comm_send_status(255);
			COMM_ERR("Wrong size of DHCPC payload: %d", n);
			return;
		}
		conf.address = info.ip.addr;
		conf.netmask = info.netmask.addr;
		conf.gateway = info.gw.addr;
		comm_send_begin(MSG_GET_IP_CONF_REPLY);
		comm_send_data((void *)&conf, sizeof(conf));
		comm_send_end();
		break;
	}
	case MSG_SET_STATION_CONF: {
		struct msg_station_conf *in_conf = (void *)data;
		struct station_config conf;
		if (n != sizeof(*in_conf)) {
			comm_send_status(255);
			COMM_ERR("Wrong size of STATION_CONF payload: %d", n);
			return;
		}
		// It seems Espressif are using zero-terminated fields
		// What happens if ssid/pw contain \0 or are at max allowed
		// length? Let's validate at least overflow for now.
		if ((in_conf->ssid_len + 1 > sizeof(conf.ssid)) ||
		    (in_conf->password_len + 1 > sizeof(conf.password))) {
			comm_send_status(255);
			COMM_ERR("SSID or password are too long, should be max"
				 " %d and %d", sizeof(conf.ssid) - 1,
				 sizeof(conf.password) - 1);
			return;
		}

		conf.bssid_set = 0;
		memset(conf.ssid, 0, sizeof(conf.ssid));
		memset(conf.bssid, 0, sizeof(conf.bssid));
		memset(conf.password, 0, sizeof(conf.password));

		memcpy(conf.ssid, in_conf->ssid, in_conf->ssid_len);
		memcpy(conf.password, in_conf->password, in_conf->password_len);

		if (!wifi_station_set_config(&conf)) {
			comm_send_status(255);
			COMM_ERR("Call to set WIFI ssid/pass failed");
			return;
		}
		wifi_station_disconnect(); // FIXME
		wifi_station_connect();
		comm_send_status(0);
		break;
	}
	case MSG_SET_SLEEP_MODE: {
		int mode;
		if (n != 1) {
			comm_send_status(255);
			COMM_ERR("Wrong size of Sleep Mode payload: %d", n);
			return;
		}
		switch (data[0]) {
		case 0: mode = NONE_SLEEP_T; break;
		case 1: mode = MODEM_SLEEP_T; break;
		case 2: mode = LIGHT_SLEEP_T; break;
		default:
			comm_send_status(255);
			COMM_ERR("Undefined sleep mode %d", (int) data[0]);
			return;
		}
		if (!wifi_set_sleep_type(mode)) {
			comm_send_status(255);
			COMM_ERR("Failed to set sleep mode");
			return;
		}
		comm_send_status(0);
		break;
	}
	case MSG_SCAN_REQUEST: {
		struct msg_scan_request *r = (void *) data;
		struct scan_config config;
		uint8_t ssid_buf[33];
		if (n != sizeof(*r)) {
			comm_send_status(255);
			COMM_ERR("Wrong size of Scan Request payload: %d", n);
			return;
		}
		/* looks like ESP accepts \0-terminated strings, so
		   no \0 in data */

		memset(&config, 0, sizeof(config));
		if (r->ssid_len) {
			memcpy(ssid_buf, r->ssid, sizeof(r->ssid));
			ssid_buf[sizeof(ssid_buf) - 1] = '\0';
			config.ssid = ssid_buf;
		}
		if (r->use_bssid) {
			config.bssid = r->bssid;
		}
		config.channel = r->channel;
		config.show_hidden = r->show_hidden;
		if (!wifi_station_scan(&config, scan_done)) {
			comm_send_status(255);
			COMM_ERR("Scan request failed");
			return;
		}
		comm_send_status(0);
		break;
	}
	case MSG_CONN_STATUS_REQUEST: {
		uint8_t out;
		switch (wifi_station_get_connect_status()) {
		case STATION_IDLE:
			out = 0; break;
		case STATION_CONNECTING:
			out = 1; break;
		case STATION_WRONG_PASSWORD:
			out = 2; break;
		case STATION_NO_AP_FOUND:
			out = 3; break;
		case STATION_CONNECT_FAIL:
			out = 4; break;
		case STATION_GOT_IP:
			out = 5; break;
		default:
			out = 0xff; break;
		}
		comm_send_begin(MSG_CONN_STATUS_REPLY);
		comm_send_u8(out);
		comm_send_end();
		break;
	}
	case MSG_FORWARD_IP_BROADCASTS: {
		if (n != 1) {
			comm_send_status(255);
			COMM_ERR("Wrong size of Forward Ip Broadcasts"
				 " payload: %d", n);
			return;
		}
		forward_ip_broadcasts = data[0];
		comm_send_status(0);
		break;
	}
	case MSG_SET_LOG_LEVEL: {
		if (n != 1) {
			comm_send_status(255);
			COMM_ERR("Wrong size of Set Loglevel payload: %d", n);
			return;
		}
		comm_set_loglevel(data[0]);
		comm_send_status(0);
	}
	case MSG_PRINT_STATS: {
		uint32_t rx_errors, crc_errors;
		comm_get_stats(&rx_errors, &crc_errors);
		COMM_INFO("HEAP free: %d, rx_err: %d, crc_err: %d",
			  system_get_free_heap_size(), rx_errors, crc_errors);
		break;
	}
	case MSG_ECHO_REQUEST:
		comm_send_begin(MSG_ECHO_REPLY);
		comm_send_data(data, n);
		comm_send_end();
		break;
	default:;
	}
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
	uint32_t ps=999;
	os_delay_us(50*1000);   // delay 50ms before init uart

	uart_init(BIT_RATE_921600, BIT_RATE_921600);
	comm_init(packet_from_host);

	comm_send_begin(MSG_BOOT);
	comm_send_end();

	/* lwip_init(); */

	COMM_INFO("Heap size: %d", system_get_free_heap_size());
	COMM_INFO("Alignment: %d", __BIGGEST_ALIGNMENT__);

	asm("RSR %0, PS" : "=r"(ps));
	COMM_INFO("IRQ level: %d", (int)ps);

	/* os_intr_lock(); */
	asm("RSR %0, PS" : "=r"(ps));
	COMM_INFO("IRQ level (lock): %d", (int)ps);

	/* os_intr_unlock(); */
	asm("RSR %0, PS" : "=r"(ps));
	COMM_INFO("IRQ level (unlock): %d", (int)ps);

	/* task_init(); */
	init_wlan();
}
