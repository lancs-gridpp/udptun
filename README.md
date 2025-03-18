# Purpose

This is a C++ program to tunnel UDP packets, preserving message boundaries, but not preserving source addresses.
At the ingress end, it can bind to multiple UDP addresses, receive packets, and transmit them over several TCP tunnels annotated with bitsets.
On egress, it can listen on multiple TCP addresses, received the annotated packets, and deliver to multiple UDP addresses.

# Installation

Install dependencies using one of the following commands:

```
sudo apt-get install yaml-cpp-dev
```

```
sudo dnf install yaml-cpp-devel
```

# Configuration

A configuration file is a YAML document with at least one of the following keys: `ingress` (for injecting UDP packets into a TCP tunnel) and `egress` (for extracting them from a tunnel, and redelivering as UDP).
A minimal egress-side configuration could be:

```
egress:
  destinations:
    detailed-dest:
      port: 6789
  sockets:
    - udp:
        port: 8000
      queues:
        detailed: detailed-dest
  tunnels:
    main:
      tcp:
        port: 9992
      channels:
        0: [ detailed ]
```

(The strings `detailed` and `detailed-dest` are user-defined.
Tunnel names are used only for logging.)

The example creates a TCP server socket on `localhost:9992`, and accepts connections on it.
Encapsulated datagrams are received on these connections, and decapsulated.
Any labelled with `0` are then passed through a queue called `egress/detailed`.
A UDP socket is also created on `localhost:8000`, and datagrams extracted from the queue are sent through it to `localhost:6789`.

Multiple destinations may be specified, along with multiple sockets with multiple queues to reference the destinations, and multiple tunnels to reference the queues.

A minimal ingress-side configuration could be:

```
ingress:
  tunnels:
    monitor:
      tcp:
        host: monitor.example.com
        port: 9992
  channels:
    detailed:
      tunnel: monitor
      labels: [ 0 ]
  sockets:
    main:
      udp:
        port: 9500
      channels: [ detailed ]
```

(`main`, `detailed` and `monitor` are used-defined.
Socket names are used only for logging.)

This example creates a UDP socket on `localhost:9500`, and opens a TCP connection to `monitor.example.com:9992`.
Everything datagram received on the UDP socket is queued on `ingress/detailed`, and then sent over the TCP socket encapsulated with a set of one label 0.

Multiple tunnels may be specified, along with multiple channels, each one referencing a tunnel.
Multiple sockets may be specified, each copied its received datagrams to multiple channels.

## Addresses

Usually, `host` can be specified wherever `port` can be, and defaults to `localhost`.
Booleans `ipv4` and `ipv6` can also be specified, and default to `true`.
These four parameters are resolved with `getaddrinfo` into internal socket parameters.

## Testing

Mostly for testing purposes, you can combine `ingress` and `egress` in one configuration, and then maybe use `netcat` to test datagrams over it.

## Persistence

Queues are stored in `/var/spool/udptun/` by default, overridden with the likes of:

```
state:
  queues: ~/.local/var/spool/udptun
```
