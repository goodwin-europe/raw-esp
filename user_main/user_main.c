#include "user_interface.h"

#include "ets_sys.h"
#include "osapi.h"
#include "driver/uart.h"
#include "mem.h"
#include "missing_declarations.h"

#include "lwip/ip_addr.h"
#include "lwip/raw.h"
#include "lwip/udp.h"
#include "netif/wlan_lwip_if.h"

#include "comm.h"
#include "misc.h"

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define SIG_LUA 0
#define TASK_QUEUE_LEN 4
os_event_t *taskQueue;

#define UART0   0
#define UART1   1

static uint8_t forward_ip_broadcasts = 1;
static enum forwarding_mode global_forwarding_mode = FORWARDING_MODE_NONE;

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

static u8_t ICACHE_FLASH_ATTR
raw_receiver(void *arg, struct raw_pcb *pcb, struct pbuf *p0, ip_addr_t *addr)
{
	struct ip_hdr hdr;
	struct pbuf *p;

	COMM_DBG("WLan IP packet of size %d", p0->tot_len);
	if (global_forwarding_mode != FORWARDING_MODE_IP)
		return 0;

	if (pbuf_copy_partial(p0, &hdr, sizeof(hdr), 0) != sizeof(hdr)) {
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

static volatile netif_input_fn netif_input_orig = NULL;
static volatile netif_output_fn netif_output_orig = NULL;
static volatile netif_linkoutput_fn netif_linkoutput_orig = NULL;

static void ICACHE_FLASH_ATTR
init_wlan() {
	struct station_config config;
	struct ip_info ip;

	wifi_set_phy_mode(PHY_MODE_11N);
	wifi_set_sleep_type(LIGHT_SLEEP_T);
	/* wifi_set_opmode(STATION_MODE); */

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

	/* esp_now_unregister_recv_cb(); */
	/* esp_now_unregister_send_cb(); */

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


err_t netif_input_mitm(struct pbuf *p, struct netif *netif)
{
	COMM_DBG("mitm input, size=%d", (int)p->tot_len);
	/* COMM_DBG("**** mitm input, size=%d, %p", */
	/* 	 p->tot_len, netif_input_orig); */

	if (global_forwarding_mode != FORWARDING_MODE_ETHER) {
		if (netif_input_orig)
			return netif_input_orig(p, netif);

		COMM_WARN("mitm input zero pointer");
		return 0;
	}

	struct pbuf *tmp;
	comm_send_begin(MSG_ETHER_PACKET);
	for(tmp = p; tmp; tmp = tmp->next) {
		comm_send_data(tmp->payload, tmp->len);
	}
	comm_send_end();

	pbuf_free(p);
	return 0;
}

err_t netif_output_mitm(struct netif *netif, struct pbuf *p, ip_addr_t *ipaddr)
{
	COMM_DBG("mitm output, size=%d", (int)p->tot_len);

	if (global_forwarding_mode == FORWARDING_MODE_ETHER)
		return 0;

	if (netif_output_orig)
		return netif_output_orig(netif, p, ipaddr);

	COMM_WARN("mitm output zero pointer");
	return 0;
}

err_t netif_linkoutput_mitm(struct netif *netif, struct pbuf *p)
{
	COMM_DBG("mitm linkoutput, size=%d", (int)p->tot_len);

	if (global_forwarding_mode == FORWARDING_MODE_ETHER)
		return 0;

	if (netif_linkoutput_orig)
		return netif_linkoutput_orig(netif, p);

	COMM_WARN("mitm linkoutput zero pointer");
	return 0;
}

static void mitm_interface()
{
	struct netif *netif = eagle_lwip_getif(0);
	if (!netif) {
		COMM_DBG("********** netif not ready");
		return;
	}

	if (netif->input != netif_input_mitm) {
		COMM_INFO("************** mitm input");
		netif_input_orig = netif->input;
		netif->input = netif_input_mitm;
	}

	if (netif->output != netif_output_mitm) {
		COMM_INFO("************** mitm output");
		netif_output_orig = netif->output;
		netif->output = netif_output_mitm;
	}

	if (netif->linkoutput != netif_linkoutput_mitm) {
		COMM_INFO("************** mitm linkoutput");
		netif_linkoutput_orig = netif->linkoutput;
		netif->linkoutput = netif_linkoutput_mitm;
	}
}

static int ICACHE_FLASH_ATTR
inject_ip_packet(uint8_t *data, int n)
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

/* This funtion is called from UART interrupt, so I hope interface won't die
 *
 */
static int ICACHE_FLASH_ATTR
inject_ether_packet(uint8_t *data, int n)
{
	uint32_t irq_level = irq_save();

	if (!netif_linkoutput_orig) {
		COMM_WARN("netif_linkoutput_orig is zero");
		goto fail;
	}

	struct netif *netif = eagle_lwip_getif(0);
	if (!netif) {
		COMM_WARN("netif doesn't exist yet");
		goto fail;
	}

	struct pbuf *p = pbuf_alloc(PBUF_IP, n, PBUF_RAM);
	if (!p) {
		COMM_ERR("Failed to allocate packet of size %d", n);
		goto fail;
	}
	memcpy(p->payload, data, n);
	p->tot_len = n;
	p->len = n;

	netif_linkoutput_orig(netif, p);

	irq_restore(irq_level);

	COMM_INFO("***** sent %d bytes to linkoutput", n);
	return 0;
fail:
	irq_restore(irq_level);
	return -1;
}

static void ICACHE_FLASH_ATTR
scan_done(void *arg, STATUS status)
{
	struct bss_info *bss_link = (void *) arg;
	struct bss_info *bss_iter = bss_link;
	struct msg_wifi_scan_reply r;
	struct msg_wifi_scan_entry e;
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
	comm_send_begin(MSG_WIFI_SCAN_REPLY);
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

		comm_send_begin(MSG_WIFI_SCAN_ENTRY);
		comm_send_data((void *) &e, sizeof(e));
		comm_send_end();
	}
}

#define TRY(expr, ...)  do { \
	if (expr) { \
		FAIL(__VA_ARGS__); \
	} \
} while (0)

#define FAIL(...) do { \
	comm_send_status(255); \
	COMM_ERR(__VA_ARGS__); \
	return; \
} while(0)

static void ICACHE_FLASH_ATTR
packet_from_host(uint8_t type, uint8_t *data, uint32_t n)
{
	/* It's not clear how to intercept the exact moment when interface is
	   set up (it's not done on bootup). So we'll try to do it on every
	   message from host.
	   TODO: maybe use a timer instead? */
	mitm_interface();

	switch(type) {
	case MSG_IP_PACKET:
		COMM_DBG("Packet from host, %d bytes", n);
		if (global_forwarding_mode == FORWARDING_MODE_IP)
			inject_ip_packet(data, n);
		else
			COMM_ERR("Cannot forward IP packet in mode %d",
				 (int) global_forwarding_mode);
		break;
	case MSG_ETHER_PACKET:
		if (global_forwarding_mode == FORWARDING_MODE_ETHER)
			inject_ether_packet(data, n);
		else
			COMM_ERR("Cannot forward Ether packet in mode %d",
				 (int) global_forwarding_mode);
		break;
	case MSG_WIFI_MODE_SET: {
		int mode = 0;
		TRY(n != 1, "Wrong size of WIFI_MODE_SET payload: %d", n);
		switch (data[0]) {
		case 0:
			mode = NULL_MODE; break;
		case MODE_STA:
			mode = STATION_MODE; break;
		case MODE_SOFTAP:
			mode = SOFTAP_MODE; break;
		case (MODE_STA | MODE_SOFTAP):
			mode = STATIONAP_MODE; break;
		default:
			FAIL("Cannot decode bitmask %d", (int)data[0]);
		}

		TRY(!wifi_set_opmode(mode), "wifi_set_opmode() failed");
		comm_send_status(0);
		break;
	}
	case MSG_STATION_STATIC_IP_CONF_SET: {
		struct msg_ip_conf *conf = (void *) data;
		struct ip_info info;

		TRY(!(wifi_get_opmode() & STATION_MODE),
		    "Cannot set STA IP while STA is inactive");

		TRY(n != sizeof(*conf),
		    "Wrong size of STATIC_IP_CONF payload: %d", n);

		if (wifi_station_dhcpc_status() == DHCP_STARTED)
			TRY(!wifi_station_dhcpc_stop(), "Unable to stop DHCPC");

		info.ip.addr = conf->address;
		info.netmask.addr = conf->netmask;
		info.gw.addr = conf->gateway;
		TRY(!wifi_set_ip_info(STATION_IF, &info),
		    "wifi_set_ip_info() failed");

		comm_send_status(0);
		break;
	}
	case MSG_STATION_DHCPC_STATE_SET: {
		int ret;
		TRY(n != 1, "Wrong size of DHCPC payload: %d", n);

		if (data[0] && (wifi_station_dhcpc_status() == DHCP_STOPPED))
			TRY(wifi_station_dhcpc_start(),
			    "wifi_station_dhcpc_start() failed");

		if ((!data[0]) && (wifi_station_dhcpc_status() == DHCP_STARTED))
			TRY(wifi_station_dhcpc_stop(),
			    "wifi_station_dhcpc_stop() failed");

		comm_send_status(0);
		break;
	}
	case MSG_STATION_IP_CONF_REQUEST: {
		struct msg_ip_conf conf;
		struct ip_info info;
		TRY(!wifi_get_ip_info(STATION_IF, &info),
		    "wifi_get_ip_info() failed");
		conf.address = info.ip.addr;
		conf.netmask = info.netmask.addr;
		conf.gateway = info.gw.addr;

		size_t i;
		for (i = 0; i < ARRAY_SIZE(conf.dns); i++) {
			conf.dns[i] = dns_getserver(i);
		}
		comm_send_begin(MSG_STATION_IP_CONF_REPLY);
		comm_send_data((void *)&conf, sizeof(conf));
		comm_send_end();
		break;
	}
	case MSG_STATION_CONF_SET: {
		struct msg_station_conf *in_conf = (void *)data;
		struct station_config conf;
		TRY(n != sizeof(*in_conf),
		    "Wrong size of STATION_CONF payload: %d", n);

		// It seems Espressif are using zero-terminated fields
		// What happens if ssid/pw contain \0 or are at max allowed
		// length? Let's validate at least overflow for now.
		if (in_conf->ssid_len + 1 > sizeof(conf.ssid))
			FAIL("SSID is too long, should be max %d",
			     sizeof(conf.ssid) - 1);
		if (in_conf->password_len + 1 > sizeof(conf.password))
			FAIL("Password is too long, should be max %d",
			     sizeof(conf.password) - 1);

		conf.bssid_set = 0;
		memset(conf.ssid, 0, sizeof(conf.ssid));
		memset(conf.bssid, 0, sizeof(conf.bssid));
		memset(conf.password, 0, sizeof(conf.password));

		memcpy(conf.ssid, in_conf->ssid, in_conf->ssid_len);
		memcpy(conf.password, in_conf->password, in_conf->password_len);

		TRY(!(wifi_get_opmode() & STATION_MODE), // hmm, FIXME?
		    "Cannot set STA conf when not in STA mode");

		TRY(!wifi_station_set_config_current(&conf),
		    "Call to set WIFI ssid/pass failed");

		wifi_station_disconnect(); // FIXME?
		wifi_station_connect();
		comm_send_status(0);
		break;
	}
	case MSG_WIFI_SLEEP_MODE_SET: {
		int mode;
		TRY(n != 1, "Wrong size of Sleep Mode payload: %d", n);

		switch (data[0]) {
		case 0: mode = NONE_SLEEP_T; break;
		case 1: mode = MODEM_SLEEP_T; break;
		case 2: mode = LIGHT_SLEEP_T; break;
		default: FAIL("Undefined sleep mode %d", (int) data[0]);
		}

		TRY(!wifi_set_sleep_type(mode), "Failed to set sleep mode");

		comm_send_status(0);
		break;
	}
	case MSG_WIFI_SCAN_REQUEST: {
		struct msg_wifi_scan_request *r = (void *) data;
		struct scan_config config;
		uint8_t ssid_buf[33];
		TRY(n != sizeof(*r),
		    "Wrong size of Scan Request payload: %d", n);

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

		TRY(!wifi_station_scan(&config, scan_done), "Scan request failed");

		comm_send_status(0);
		break;
	}
	case MSG_STATION_CONN_STATUS_REQUEST: {
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
		comm_send_begin(MSG_STATION_CONN_STATUS_REPLY);
		comm_send_u8(out);
		comm_send_end();
		break;
	}
	case MSG_STATION_RSSI_REQUEST: {
		int8_t rssi = wifi_station_get_rssi();
		if (rssi == 31)
			FAIL("wifi_station_get_rssi() returned '31'");
		comm_send_begin(MSG_STATION_RSSI_REPLY);
		comm_send_u8((uint8_t)rssi);
		comm_send_end();
		break;
	}
	case MSG_FORWARD_IP_BROADCASTS: {
		TRY(n != 1, "Wrong size of Forward Ip Broadcasts payload: %d", n);
		forward_ip_broadcasts = data[0];
		comm_send_status(0);
		break;
	}
	case MSG_SET_FORWARDING_MODE: {
		TRY(n != 1, "Wrong size of Set Forwarding Mode payload: %d", n);
		global_forwarding_mode = data[0];
		comm_send_status(0);
		break;
	}
	case MSG_SOFTAP_CONF_SET: {
		struct msg_softap_conf *in_conf = (void *)data;
		struct softap_config conf;
		TRY(n != sizeof(*in_conf),
		    "Wrong size of SOFTAP_CONF payload: %d", n);

		// It seems Espressif are using zero-terminated fields
		// What happens if ssid/pw contain \0 or are at max allowed
		// length? Let's validate at least overflow for now.
		if (in_conf->ssid_len + 1 > sizeof(conf.ssid))
			FAIL("SSID is too long, should be max %d",
			     sizeof(conf.ssid) - 1);
		if (in_conf->password_len + 1 > sizeof(conf.password))
			FAIL("Password is too long, should be max %d",
			     sizeof(conf.password) - 1);

		memset(conf.ssid, 0, sizeof(conf.ssid));
		memcpy(conf.ssid, in_conf->ssid, in_conf->ssid_len);
		memset(conf.password, 0, sizeof(conf.password));
		memcpy(conf.password, in_conf->password, in_conf->password_len);
		conf.ssid_len = in_conf->ssid_len;
		conf.channel = in_conf->channel;
		conf.authmode = in_conf->auth_mode; // check
		conf.ssid_hidden = 0;
		conf.max_connection = 4; // is this current maximum?
		conf.beacon_interval = in_conf->beacon_interval;

		/* COMM_INFO("Conf: ssid_len=%d, ssid=%s pass=%s chan=%d " */
		/*           "auth=%d int=%d", */
		/* 	  conf.ssid_len, conf.ssid, conf.password, conf.channel, */
		/* 	  conf.authmode, conf.beacon_interval */
		/* ); */

		TRY(!(wifi_get_opmode() & SOFTAP_MODE), // FIXME?
		    "Cannot switch to SoftAP mode");
		TRY(!wifi_softap_set_config(&conf),
		     "Call to set WIFI ssid/pass failed");

		comm_send_status(0);
		break;
	}
	case MSG_SOFTAP_NET_CONF_SET: {
		struct msg_softap_net_conf *conf = (void *) data;
		struct ip_info info;
		struct dhcps_lease leases;
		/* int dhcp_status; */
		TRY(n != sizeof(*conf),
		    "Wrong size of STATION_STATIC_IP_CONF payload: %d", n);

		info.ip.addr = conf->address;
		info.netmask.addr = conf->netmask;
		info.gw.addr = conf->gateway;

		if (wifi_softap_dhcps_status() == DHCP_STARTED)
		    TRY(!wifi_softap_dhcps_stop(), "wifi_softap_dhcps_stop() failed");

		TRY(!wifi_set_ip_info(SOFTAP_IF, &info), "wifi_set_ip_info() failed");

		if (conf->enable_dhcpd) {
			leases.start_ip.addr = conf->dhcpd_first_ip;
			leases.end_ip.addr = conf->dhcpd_last_ip;
			TRY(!wifi_softap_set_dhcps_lease(&leases),
			    "wifi_softap_set_dhcps_lease() failed");

			/* not sure about types, so specify them explicitly */
			uint8_t omode = conf->dhcpd_offer_gateway;
			if (conf->dhcpd_offer_gateway) {
				TRY(!wifi_softap_set_dhcps_offer_option(
					    OFFER_ROUTER, &omode),
				"wifi_softap_set_dhcps_offer_option() failed");
			}

			TRY(!wifi_softap_dhcps_start(),
			    "wifi_softap_dhcps_start() failed");
		}
		comm_send_status(0);
		break;
	}
	case MSG_LOG_LEVEL_SET: {
		TRY(n != 1, "Wrong size of Set Loglevel payload: %d", n);
		comm_set_loglevel(data[0]);
		comm_send_status(0);
		break;
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
	case MSG_SET_BAUD: {
		uint32_t *baud = (void *) data;
		uart_div_modify(0, UART_CLK_FREQ / (*baud));
		break;
	}
	default:;
	}
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR
user_init(void)
{
	uint32_t ps=999;
	os_delay_us(50*1000);   // delay 50ms before init uart

	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	comm_init(packet_from_host);

	comm_send_begin(MSG_BOOT);
	comm_send_end();

	/* lwip_init(); */

	COMM_INFO("Heap size: %d", system_get_free_heap_size());
	COMM_INFO("Alignment: %d", __BIGGEST_ALIGNMENT__);

	/* asm("RSR %0, PS" : "=r"(ps)); */
	/* COMM_INFO("IRQ level: %d", (int)ps); */

	/* os_intr_lock(); */
	/* asm("RSR %0, PS" : "=r"(ps)); */
	/* COMM_INFO("IRQ level (lock): %d", (int)ps); */

	/* os_intr_unlock(); */
	/* asm("RSR %0, PS" : "=r"(ps)); */
	/* COMM_INFO("IRQ level (unlock): %d", (int)ps); */

	/* task_init(); */
	init_wlan();
}
