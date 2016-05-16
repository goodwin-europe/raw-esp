Raw IP / Ethernet packet firmware for ESP8266 WiFi module
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

It's sometimes desirable to bypass ESP's TCP/IP stack and use network stack
of a host microcontroller instead.

This firmware presents ESP8266 to the host as IP- or Ethernet-layer link
with additional functions to configure WiFi, logging, etc. All communication
with ESP8266 is done via binary message-based protocol over RS232.

In IP-forwarding mode ESP8266 firmware internally runs ARP and, optionally,
DHCP. Non-DHCP UDP packets and TCP packets are forwarded to the host. Packets
received from host are injected into ESP's network stack. ICMP packets aren't
currently forwarded (because I haven't bothered, that's easy to fix).
Both AP/STA modes are supported. Network may be configured with DHCP or
statically. This mode works with ESP SDK <1.1.1. On version >=1.1.1 it tends
to hang after the first injected packed, though I haven't tested with
versions >=1.4.

In Ethernet-forwarding mode ESP's LwIP stack is completely detached. Its timers
are operational and it attemps to send something from time to time, but those
packets are dropped. All packets coming from WiFi interface are redirected to
the host, and packets coming from host are passed directly to ESP's WiFi
interface.

Compilation
~~~~~~~~~~~
Install cross-compilers as usual, change `SDK_BASE` in the `Makefile` to the
root of non-OS ESP8266 SDK. After that run `make`. Firmware binaries will be
built in `firmware` dir.

Prebuilt firmware is also provided in `bin` dir.

Host interface
~~~~~~~~~~~~~~
Host interface is documented in `user_main/message.h`.
