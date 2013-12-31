sonos-forwarder
===============

Forwards the sonos SSDP packets across a nat device

This tools is useful in case you have a nat device able to run this program (such as a linux box), with a Sonos device behind it.
Problem is that in this case normally Sonos clients on the outside of the nat cannot find the Sonos box.

Practical problem
-----------------

In my case I had a Sonos device, however I am only able to connect wifi devices to the network I am on.
An obvious solution would be to connect a wifi bridge (one that doesn't do NAT), however the access point was protected, it wouldn't allow wifi bridges, it wouldn't connect to anything that had 4addr set to on.
In addition, I can imagine the network operators frowning on the idea of me connecting a Sonos to their network.

To solve this problem I set up a Raspberry Pi as a NAT device, NATting from wlan0 to eth0 (I used the instructions from [here](http://hackhappy.org/uncategorized/how-to-use-a-raspberry-pi-to-create-a-wireless-to-wired-network-bridge/) to configure the Raspberry Pi. Please note that there is a typo in the page, the last line of /etc/network/interfaces should read "up iptables-restore < /etc/iptables.ipv4.nat", with a < in stead on a >).
Problem is that all my controllers are on the other side of the NAT, so now the Sonos clients (on my mac, iPad, etc), can't connect to the Sonos system any more.


Terminology
-----------

Just to make the terms clear, my network consists of the following:

 * Sonos box (box): the device that connects Sonos to the internet. In my case this is a Play:1, but this can be Sonos:bridge or any Sonos component. It's the thing you bought from Sonos, in which you plug your ethernet cable. In my examples this is 192.168.2.101.
 * NAT router: In my case a Raspberry Pi. It has a wlan0 interface, connecting to the wider network, and an eth0 interface connected to the Sonos. In my case this is a direct connection from the Pi to the Sonos, but putting a switch in between here, or daisy chaining other network devices behind your Sonos, are all not a problem. The network on the Sonos side is called the network inside the NAT, the network on the other side connecting to the Internet is called outside the NAT. In my examples this device has the IP addresses 192.168.1.82 on the outside and 192.168.2.1 on the inside.
 * Sonos controller/Sonos client (client): These are the devices you use to control your Sonos, in my case an iPad, iPhone, Mac. These are in 192.168.1.0/24 network.

Sonos Discovery protocol
------------------------

To understand why this is, we have to look at the protocol used to discover a Sonos device.
This leans on [SSDP](http://en.wikipedia.org/wiki/Simple_Service_Discovery_Protocol).
The Sonos box regularly sends SSDP packets to announce its presence, and when a client wants to connect to a box it sends out SSDP messages as well.
An SSDP message is a UDP packet to broadcast address 239.255.255.250 on port 1900.
A client trying to connect expects a message back from the Sonos box, a unicast UDP message on the UDP port it used to send the broadcast, in which the Sonos box makes itself known, and gives the client a HTTP address where to connect to it.
This HTTP address is on port 1400.

In addition there is a problem that the sonos box will send NOTIFY http calls to the client on port 340x (possibly even 34xx, actually I only ever saw it use 3400 for desktop clients and 3401 for mobile clients. For safety the instructions below reserver the whole 3400-3500 range).
This NOTIFY call contains the ip address of the sonos box, which, of course, is not reachable from the network of the client.

What we need to do to route this protocol is 4 steps (after we have the NAT set up).
 * Do NAT port forwarding on port 1400 to the Sonos device.
 * Redirect all traffic from the sonos box to the network outside of the NAT router (but not the internet) for the ports 3400-3500 to the router itself on port 3400.
 * Listen on the NAT device on the external interface for SSDP packets. As soon as we find one, send this through to the internal network.
 * As soon as we retransmit an SSDP packet, listen on the transmitting port for a reply. If we receive one, retransmit it on the other side, to the correct host. These reply packets should have their content rewritten, so that the ip address of the sonos box gets replaced by the ip address of the 
 * Any message going from the inside of the NAT to the outside, should rewrite the IP addresses in the packet. For instance, if the box is on 192.168.2.100, and the client on 192.168.1.20, and the NAT router's outside IP address is 192.168.1.30, then a message from the box saying "Hey connect to me on 192.168.2.100" means nothing to the client because it doesn't know how to route packets to 192.168.2.x. So the router must rewrite the message to "Hey connect to me on 192.168.1.30". The NAT port forwarding will then take care of the rest.

Solution
--------
 * Make sure NATting works, including the port forwarding. Consult the Internet to do this. You can check that it's set up right by connecting to http://(router external ip):1400/xml/devicedescription.xml. This should show you a bunch of information in XML about your Sonos system.
 * Redirect all traffic from the sonos box to the network outside of the NAT router (but not the internet) for the ports 3400-3500 to the router itself on port 3400:

    -A PREROUTING -i eth0 -p tcp -s 192.168.2.101 -d 192.168.1.0/24 -m multiport --dports 3400:3499 -j DNAT --to-destination 192.168.2.1:3400

 * compile this program

    make all

 * run in 2 windows the 2 executables sonos-forwarder and notify-forwarder. Note: you only need to run the sonos-init-forwarder if you want to pair a new device. Obviously you can run the programs headless with nohup, just watch out for your disk filling up.
 * enjoy the music

Caveat
------
For now (testing time about 25 minutes :) ) everything seems to be working fine. 
