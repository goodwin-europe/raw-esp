#pragma once
/*
TODO:
  - add serial numbers to detect lost packets,
  - add id field to request and reply, and mark reply with request's id.
    Currently if requests are sent asyncronously from different parts of
    host program, it can be difficult to tell what reply corresponds to
    what request.

Description of communication protocol
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
ESP8266 and host exchange messages via rs232. Default baud rate is 115200.
Flow control isn't used, but can be implemented with CTS/RTS if required.

Since rs232 has no inherent concept of framing, messages are packed
using COBS framing (the project used DLC in earlier releases). Zero
byte always means end of frame. All occurrences of a zero byte in payload
are replaced with distance to the next zero in payload or distance
to end of packet for the last zero. This scheme is slightly extended
to accomodate packets larger than 254 bytes, see cobs.h and cobs.c.
Here is a short example:
  payload:        13 08 00 F0 00 00 34 A4 11
  framed data: 03 13 08 02 F0 01 04 34 A4 11 00

Unpacked message body has following format:
| uint8_t type | uint8_t data[] | uint16_t crc |
Multibyte integers are encoded in LE order if not specified otherwise.
Total length must hot exceed MAX_MESSAGE_SIZE. Here is description of
fields:
    type -- type of message, see enum command_type,
    data -- data of variable length,
    crc  -- checksum, CRC16-CCITT ("XModem" type, initialization
            value -- 0). See crc.h and crc.c for specifics.

Messages of certain types are valid only when sent in a specific
direction, e. g. WiFi configuration requests make sense only when
sent from host to ESP8266. Some messages have no payload, some include
structs, some contain variable-sized data. All commands are fully desribed
below.

All integers are packed in native endianness mode, i.e. little-endian.

Baud rate is set to 115200 on boot. It can be reconfigured to something faster
at any time, but this setting is volatile and isn't saved between reboots.
Keep in mind that on ESP baud is derived from APB's 80MHz by an integer
divider. Looks like ESP8266 I/Os may be limited to 20MHz; I've managed to get
max 2MHz with FT232BM and 20cm cable. On iMX53 it worked fine at 4MHz; judging
 by waveforms it could be easily pushed up to 8MHz had Linux's userspace
 allowed it.
*/

/* Message size includes command_id byte, actual data and CRC, doesn't include
byte stuffing. */
#define MAX_MESSAGE_SIZE 2040


enum command_type {
	/* Packet & data related messages */
	MSG_IP_PACKET              = 0x00,
	MSG_ETHER_PACKET           = 0x01,
	MSG_FORWARD_IP_BROADCASTS  = 0x10,
	MSG_SET_FORWARDING_MODE    = 0x11,

	/* Basic WIFI messages */
	MSG_WIFI_MODE_SET         = 0x20,
	MSG_WIFI_SLEEP_MODE_SET   = 0x21,
	MSG_WIFI_SCAN_REQUEST     = 0x22,
	MSG_WIFI_SCAN_REPLY       = 0x23,
	MSG_WIFI_SCAN_ENTRY       = 0x24,
	MSG_WIFI_GET_MACADDR_REQUEST = 0x25,
	MSG_WIFI_GET_MACADDR_REPLY   = 0x26,

	/* STA related */
	MSG_STATION_CONF_SET             = 0x40,
	MSG_STATION_STATIC_IP_CONF_SET   = 0x41,
	MSG_STATION_DHCPC_STATE_SET      = 0x42,
	MSG_STATION_IP_CONF_REQUEST      = 0x43,
	MSG_STATION_IP_CONF_REPLY        = 0x44,
	MSG_STATION_CONN_STATUS_REQUEST  = 0x45,
	MSG_STATION_CONN_STATUS_REPLY    = 0x46,
	MSG_STATION_RSSI_REQUEST         = 0x47,
	MSG_STATION_RSSI_REPLY           = 0x48,

	/* SoftAP related */
	MSG_SOFTAP_CONF_SET           = 0x60,
	MSG_SOFTAP_NET_CONF_SET       = 0x61,

	/* Logging, misc */
	MSG_STATUS                 = 0x80,
	MSG_BOOT                   = 0x81, /* TODO: send boot reason ? */
	MSG_LOG_LEVEL_SET          = 0x82,
	MSG_LOG                    = 0x83,
	MSG_ECHO_REQUEST           = 0x84,
	MSG_ECHO_REPLY             = 0x85,
	MSG_SET_BAUD               = 0x86,
	MSG_PRINT_STATS            = 0x90,
};

/* On boot/reset module is configured with whatever settings were present
in NVRAM and bootup code. It would be flaky to account for this stored state
in host's state machine, so module should be cleanly reinitialized. Here is
typical communication sequence.

Typical configuration / use after boot:
0) Set desired loglevel, packet forwarding options, sleep mode with
   MSG_LOG_LEVEL_SET, MSG_SET_FORWARDING_MODE, MSG_FORWARD_IP_BROADCASTS
   (if forwarding_mode is IP) and MSG_WIFI_SLEEP_MODE_SET. Each of those
   commands returns MSG_STATUS as documented below. Set baud with SET_BAUD.
   It's actually a good idea to syncronize with MSG_ECHO before configuring
   module, and once more after new baud is set to ensure that serial link
   is operational.

1) Send MSG_WIFI_MODE_SET with desired operation mode.

2) If STA mode is used, available APs may be listed with
   MSG_WIFI_SCAN_REQUEST. To select an AP, send MSG_STATION_CONF_SET. After
   this message the module will start connecting to the given AP. Reconnects
   are automatic and doesn't require further actions from host.
   If static IP is desired, set it with MSG_STATION_STATIC_IP_CONF_SET.
   If ip should be acquired by DHCP, enable DHCP client with
     MSG_STATION_DHCPC_STATE_SET message.

3) If SoftAP mode is enabled, send MSG_SOFTAP_CONF_SET,
   MSG_SOFTAP_STATIC_IP_CONF_SET, MSG_SOFTAP_DHCPD_STATE_SET
   and MSG_SOFTAP_DHCPD_CONF_SET (if dhcpd is enabled).

4) Enjoy MSG_IP_PACKET / MSG_ETHERNET_PACKET frames.

5) Resilency could be optionally enchanced by checking if module is still
   alive using periodic polling with MSG_ECHO_REQUEST / MSG_ECHO_REPLY.

Be ready for unexpected MSG_BOOT, firmware is definitely not bug free,
and tends to reset on OOM. When MSG_BOOT is received, full reconfiguration
is mandatory. If non-default baud rate is used, on spurious reboot module will
most likely go silent though.


MSG_IP_PACKET
  dir: to/from host
  data: packet with IP header
  reply: none
  Transmits IP packet to host or from host to network. Currently only
  UDP/TCP are supported. This packet is valid only in IP-forwarding mode.

MSG_ETHER_PACKET
  dir: to/from host
  data: packet with Ethernet header
  reply: none
  Transmits ethernet-level packets to host or from host to network. This
  type of packet is valid only in Ethernet-forwarding mode.

MSG_FORWARD_IP_BROADCASTS
  dir: from host
  data: uint8_t forward
  reply: STATUS
  If forward == 0, all broadcast packets will be passed to internal IP stack.
  If forward != 0, broadcasts will be forwarded to host. This setting makes
  sense only in IP-forwarding mode. In Ethernet mode broadcasts are not
  filtered.

MSG_SET_FORWARDING_MODE
  dir: from host
  data: uint8_t mode
  reply: STATUS
  Argument is `enum forwarding_mode` packed as `uint8_t`. This message
  selects packet capture and injection methods depending on `mode`.

MSG_WIFI_MODE_SET
  dir: from host
  data: uint8_t
  reply: none
  Sets noop, STA, SoftAP or STA+SoftAP mode of operation. Use MODE_STA and
  MODE_SOFTAP bitflags.

MSG_WIFI_SLEEP_MODE_SET
  dir: from host
  data: uint8_t sleep_type (enum wifi_sleep_mode)
  reply: STATUS
  Set sleep level.
  0 -- None
  1 -- Modem sleep (CPU operational, radio shut down)
  2 -- Light sleep (CPU in sleep, radio shut down)

MSG_WIFI_SCAN_REQUEST
  dir: from host
  data: struct scan_request
  reply: STATUS immidiately and SCAN_REPLY with SCAN_ENTRYs later
  Request network scan. It's possible to specify channel, SSID and BSS
  (in any combinations). When scan is done, several SCAN_REPLY messages
  (hopefully) will be sent back, one per AP.

MSG_WIFI_SCAN_REPLY
  dir: to host
  data: scan_reply
  reply: none
  If reply.status != 0, scan has failed for some reason.
  If reply.status == 0, then reply.entries_n contains number of SCAN_ENTRY
  messages that will follow.

MSG_WIFI_SCAN_ENTRY
  dir: to host
  data: struct scan_entry
  reply: none
  Results of SCAN_REQUEST. Contain SSID, BSSID, channel, signal strength,
  index of current message and total number of messages.

MSG_WIFI_GET_MACADDR_REQUEST
  dir: from host
  data: none
  reply: WIFI_GET_MACADDR_REPLY or STATUS with error
  Request module's MAC address.

MSG_WIFI_GET_MACADDR_REPLY
  dir: to host
  data: uint8_t mac[6]
  reply: none
  Reply for MAC address request


MSG_STATION_CONF_SET
  dir: from host
  data: struct msg_station_conf
  reply: STATUS
  Set SSID, password, etc.

MSG_STATION_STATIC_IP_CONF_SET
  dir: from host
  data: struct msg_ip_conf
  reply: STATUS
  Switch to static network settings. If device's DHCP client was on it's
  switched off. Sets ip, netmask and gateway from the payload.

MSG_STATION_DHCPC_STATE_SET
  dir: from host
  data: uint8_t
  reply: STATUS
  Disable DHCP if the byte in payload is 0. Enable it otherwise.

MSG_STATION_IP_CONF_REQUEST
  dir: from host
  data: none
  reply: STATION_IP_CONF_REPLY or STATUS with error
  Request wireless IP configuration (set by DHCPC or STATIC_IP_CONF)

MSG_STATION_IP_CONF_REPLY
  dir: to host
  data: struct msg_ip_conf
  reply: none
  IP, netmask, gateway of wireless IP configuration (set by DHCPC or
  STATIC_IP_CONF)

MSG_STATION_CONN_STATUS_REQUEST
  dir: from host
  data: none
  reply: MSG_STATION_CONN_STATUS_REPLY
  Request status of WiFi connection

MSG_STATION_CONN_STATUS_REPLY
  dir: to host
  data: uint8_t status
  reply: none
  status values:
    0 -- idle,
    1 -- connecting,
    2 -- wrong password,
    3 -- no AP found,
    4 -- connect failed,
    5 -- got IP (or connected, if static setting or Ethernet-forwarding moe
         is used)

MSG_STATION_RSSI_REQUEST
  dir: from host
  data: none
  reply: MSG_STATION_RSSI_REPLY or STATUS with error
  Request RSSI level of AP that station is connected to.

MSG_STATION_RSSI_REPLY
  dir: to host
  data: int8_t status
  reply: none
  Returned RSSI level should be negative, otherwise docs are incorrect and
  value is incorrect too.

MSG_SOFTAP_CONF_SET
  dir: from host
  data: struct msg_softap_conf
  reply: MSG_STATUS
  Sets SSID, password, channel, authentification mode and beacon interval.

MSG_SOFTAP_NET_CONF_SET
  dir: from host
  data: struct msg_softap_net_conf
  reply: MSG_STATUS
  Sets IP config and DHCPD options.


MSG_STATUS
  dir: to/from host (currently only to host)
  data: uint8_t res
  reply: none
  This is reply to a command that must return status.
  0 means success, any other value is error code.

MSG_BOOT
  dir: to host
  data: none
  reply: none
  This message is sent on module boot. It can be used to catch unexpected
  reboots if default baud rate isn't changed.

MSG_LOG_LEVEL_SET
  dir: from host
  data: uint8_t loglevel
  reply: STATUS
  Set log level to a given value. Messages with log level below loglevel will
  be suppressed. Also see LOG command.

MSG_LOG
  dir: to host
  data: uint8_t level, uint8_t message[]
  reply: none
  Log message in text format, level values:
    10 -- DEBUG
    20 -- INFO
    30 -- WARNING
    40 -- ERROR
    50 -- CRITICAL

MSG_ECHO_REQUEST
  dir: from/to host
  data: arbitrary
  reply: ECHO_REPLY
  Request echo from the other side.

MSG_ECHO_REPLY
  dir: from/to host
  data: copied from ECHO_REQUEST
  reply: none
  Reply to previously requested echo.

MSG_SET_BAUD
  dir: from host
  data: uint32_t
  reply: none
  Sets baud rate to the specified value immidiately and doesn't reply.
  APB base frequency is 80.0 MHz and it's divided by integer to derive UART
  frequency.

MSG_PRINT_STATS
  dir: from host
  data: none
  reply: LOG with level INFO
  Get some statistics in human-readable form. Currently it's only heap usage.

*/

#define PACKED __attribute__((packed))

#define MODE_STA 1
#define MODE_SOFTAP 2

enum forwarding_mode {
	FORWARDING_MODE_NONE = 0,
	FORWARDING_MODE_IP,
	FORWARDING_MODE_ETHER,
} PACKED;

enum wifi_auth_mode {
	WIFI_AUTH_OPEN = 0,
	WIFI_AUTH_WEP = 1,
	WIFI_AUTH_WPA_PSK = 2,
	WIFI_AUTH_WPA2_PSK = 3,
	WIFI_AUTH_WPA_WPA2_PSK = 4,
} PACKED;

enum wifi_sleep_mode {
	WIFI_SLEEP_NONE = 0,
	WIFI_SLEEP_MODEM,
	WIFI_SLEEP_LIGHT
} PACKED;

struct msg_station_conf {
	uint8_t ssid_len;
	uint8_t ssid[32];
	uint8_t password_len;
	uint8_t password[64];
} PACKED;

struct msg_ip_conf {
	/* everything is in network order, i. e. BE */
	uint32_t address;
	uint32_t netmask;
	uint32_t gateway;
	uint32_t dns[3];
} PACKED;

struct msg_wifi_scan_request {
	uint8_t ssid_len; /* length of SSID. If 0, don't filter by SSID */
	uint8_t ssid[32];
	uint8_t use_bssid; /* if 0, don't filter by bssid */
	uint8_t bssid[6];
	uint8_t channel; /* if 0, don't filter by channel */
	uint8_t show_hidden; /* show hidden AP's */
} PACKED;

struct msg_wifi_scan_reply {
	uint8_t status;
	uint16_t entries_n;
} PACKED;

struct msg_wifi_scan_entry {
	uint16_t index; /* index of current message, starts at 0 */
	uint8_t ssid_len; /* ssid len */
	uint8_t ssid[32];
	uint8_t bssid[6];
	uint8_t channel;
	uint8_t auth_mode; /* enum wifi_auth_mode encoded as uint8_t */
	int16_t rssi;
	uint8_t is_hidden;
} PACKED;

struct msg_softap_conf {
	uint8_t ssid_len;
	uint8_t ssid[32];
	uint8_t password_len;
	uint8_t password[64];
	uint8_t channel;
	uint8_t auth_mode; /* enum wifi_auth_mode, everything except WEP */
	uint16_t beacon_interval; /* in (1/1024)s, valid: 100..60000ms */
} PACKED;

struct msg_softap_net_conf {
	/* everything is in network order, i. e. BE */
	uint32_t address;
	uint32_t netmask;
	uint32_t gateway;
	uint8_t  enable_dhcpd;
	/* Include gateway in DHCP offer.
           Gateway address is taken from field `gateway` of this struct */
	uint8_t  dhcpd_offer_gateway;
	uint32_t dhcpd_first_ip; /* if dhcpd is enabled */
	uint32_t dhcpd_last_ip; /* if dhcpd is enabled */
} PACKED;
