all:
	gcc sonos-forwarder.c str_replace.c -o sonos-forwarder
	gcc sonos-init-forwarder.c -o sonos-init-forwarder
	gcc notify-forwarder.c str_replace.c -o notify-forwarder
