/* Description of communication protocol

ESP8266 and host exchange messages via rs232. Baud rate is 921600. Flow
control isn't used, but can be implemented with CTS/RTS if required.

Since rs232 has no inherent concept of framing, messages are packed
using HDLC-like byte-stuffing. End of message is marked with END byte.
If a control byte (END or ESC) is encountered in message body, it's
replaced with two bytes: (ESC) and (cntrl ^ XOR). Values for ESC, END,
XOR are defined in comm.h.

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
sent to ESP8266. Some messages have no payload, some include
structs, some contain variable-sized data. Commands are fully desribed
below.
*/

#ifndef MESSAGE_H
#define MESSAGE_H

/* Includes command_id byte, actual data and CRC */
// #define MAX_MESSAGE_SIZE 1604
#define MAX_MESSAGE_SIZE 2040


enum command_type {
	MSG_IP_PACKET            = 0x00,
	MSG_SET_STATIC_IP_CONF   = 0x10,
	MSG_DHCPC                = 0x11,
	MSG_GET_IP_CONF_REQUEST  = 0x12,
	MSG_GET_IP_CONF_REPLY    = 0x13,
	MSG_SET_STATION_CONF     = 0x14,
	MSG_SET_SLEEP_MODE       = 0x15,
	MSG_SCAN_REQUEST         = 0x16,
	MSG_SCAN_REPLY           = 0x17,
	MSG_SCAN_ENTRY           = 0x18,
	MSG_CONN_STATUS_REQUEST  = 0x19,
	MSG_CONN_STATUS_REPLY    = 0x20,
	MSG_FORWARD_IP_BROADCASTS= 0x21,
	MSG_SET_LOG_LEVEL        = 0x22,
	MSG_STATUS               = 0x7F,
	MSG_LOG                  = 0x80,
	MSG_ECHO_REQUEST         = 0x81,
	MSG_ECHO_REPLY           = 0x82,
	MSG_PRINT_STATS          = 0x83,
	MSG_BOOT                 = 0x84,
};
/*
IP_PACKET
  dir: to/from host
  data: packet with IP header
  reply: none
  Transmits packet to host or from host to network. Currently only
  UDP/TCP are supported.

SET_STATIC_IPCONF
  dir: from host
  data: struct ipconf
  reply: STATUS
  Use static network settings. If device's DHCP client was on it's
  switched off. Use ip, netmask and gateway provided in the payload.

DHCPC
  dir: from host
  data: uint8_t
  reply: STATUS
  Disable DHCP if the byte in payload is 0. Enable it otherwise.

GET_IPCONF_REQUEST
  dir: from host
  data: none
  reply: GET_IPCONF_REPLY or STATUS with error
  Request to get wireless IP configuration (set by DHCPC or STATIC_IPCONF)

GET_IPCONF_REPLY
  dir: to host
  data: struct msg_ip_conf
  reply: none
  IP, netmask, gateway of wireless IP configuration (set by DHCPC or
  STATIC_IPCONF)

SET_STATION_CONF
  dir: from host
  data: struct wifi_sta_conf
  reply: STATUS
  Set SSID, password, etc.

SET_SLEEP
  dir: from host
  data: uint8_t sleep_type
  reply: STATUS
  Set sleep level.
  0 -- None
  1 -- Modem sleep (CPU operational, radio shut down)
  2 -- Light sleep (CPU in sleep, radio shut down)

SCAN_REQUEST
  dir: from host
  data: struct scan_request
  reply: STATUS immidiately and SCAN_REPLY with SCAN_ENTRYs later
  Request network scan. It's possible to specify channel, SSID and BSS
  (in any combinations). When scan is done, several SCAN_REPLY messages
  (hopefully) will be sent back, one per AP.

SCAN_REPLY
  dir: to host
  data: scan_reply
  reply: none
  If reply.status != 0, scan has failed for some reason.
  If reply.status == 0, then reply.entries_n contains number of SCAN_ENTRY
  messages that will follow.

SCAN_ENTRY
  dir: to host
  data: struct scan_entry
  reply: none
  Results of SCAN_REQUEST. Contain SSID, BSSID, channel, signal strength,
  index of current message and total number of messages.

CONN_STATUS_REQUEST
  dir: from host
  data: none
  reply: CONN_STATUS_REPLY
  Request status of WiFi connection

CONN_STATUS_REPLY
  dir: to host
  data: uint8_t status
  reply: none
  status values:
    0 -- idle,
    1 -- connecting,
    2 -- wrong password,
    3 -- no AP found,
    4 -- connect failed,
    5 -- got IP.

FORWARD_IP_BROADCASTS
  dir: from host
  data: uint8_t forward
  reply: STATUS
  If forward == 0, all broadcast packets will be passed to internal IP stack.
  If forward != 0, broadcasts will be forwarded to host.

SET_LOG_LEVEL
  dir: from host
  data: uint8_t loglevel
  reply: STATUS
  Set log level to a given value. Messages with log level below loglevel will
  be suppressed. Also see LOG command.

STATUS
  dir: to/from host (currently only to host)
  data: uint8_t res
  reply: none
  This is reply to a command that must return status.
  0 means success, any other value is error code.

LOG
  dir: to host
  data: uint8_t level, uint8_t message[]
  reply: none
  Log message in text format, level values:
    10 -- DEBUG
    20 -- INFO
    30 -- WARNING
    40 -- ERROR
    50 -- CRITICAL

ECHO_REQUEST
  dir: from/to host
  data: arbitrary
  reply: ECHO_REPLY
  Request echo from the other side.

ECHO_REPLY
  dir: from/to host
  data: copied from ECHO_REQUEST
  reply: none
  Reply to previously requested echo

PRINT_STATS
  dir: from host
  data: none
  reply: LOG with level INFO
  Get some statistics in human-readable form. Currently it's only heap usage.

BOOT
  dir: to host
  data: none
  reply: none
  This message is sent on module boot.

*/

#define PACKED __attribute__((packed))

struct msg_ip_conf {
	/* everything is in network order, i. e. BE */
	uint32_t address;
	uint32_t netmask;
	uint32_t gateway;
} PACKED;

struct msg_station_conf {
	uint8_t ssid_len;
	uint8_t ssid[32];
	uint8_t password_len;
	uint8_t password[64];
} PACKED;

struct msg_scan_request {
	uint8_t ssid_len; /* length of SSID. If 0, don't filter by SSID */
	uint8_t ssid[32];
	uint8_t use_bssid; /* if 0, don't filter by bssid */
	uint8_t bssid[6];
	uint8_t channel; /* if 0, don't filter by channel */
	uint8_t show_hidden; /* show hidden AP's */
} PACKED;

struct msg_scan_reply {
	uint8_t status;
	uint16_t entries_n;
} PACKED;

struct msg_scan_entry {
	uint16_t index; /* index of current message, starts at 0 */
	uint8_t ssid_len; /* ssid len */
	uint8_t ssid[32];
	uint8_t bssid[6];
	uint8_t channel;
	uint8_t auth_mode; /* see below */
	int16_t rssi;
	uint8_t is_hidden;
} PACKED;
/* auth_mode:
     0 -- open,
     1 -- WEP,
     2 -- WPA_PSK,
     3 -- WPA2_PSK
     4 -- WPA_WPA2_PSK.
*/

#endif
