# Purpose

This is a C++ program to tunnel UDP packets over a TCP connection, preserving message boundaries and source-identity distinction.
Note that it doesn't preserve source addresses.
At the ingress end, it can bind to multiple UDP addresses, receive packets, and transmit them over several TCP tunnels, each tagged with a label to select the ultimate destinations, and with a client id to preserve distinction between source addresses.
On egress, it can listen on multiple TCP addresses, receive the annotated packets, and deliver to multiple UDP addresses.

# Installation

## Runtime dependencies

```
sudo apt-get install zlib1g libyaml-cpp0.8
```

```
sudo dnf install zlib yaml-cpp
```

## Build dependencies

Install dependencies using one of the following sets of commands:

```
sudo apt-get install build-essential findutils diffutils yaml-cpp-dev zlib1g-dev
```

```
sudo dnf install epel-release git make gcc-c++ findutils diffutils zlib-devel
sudo dnf install yaml-cpp-devel
```

C++17 is required.

You need Binodeps to build using the supplied `Makefile`:
```
cd /tmp
git clone https://github.com/simpsonst/binodeps.git
cd binodeps
make && sudo make install
```

## Local build parameters

Create a file `config.mk` adjacent to `Makefile` to customize.
For example:

```
CPPFLAGS += -pedantic -Wall -W -Wno-unused-parameter
CPPFLAGS += -g

CFLAGS += -O3
CFLAGS += -std=gnu11

CXXFLAGS += -O2
CXXFLAGS += -std=gnu++17

CPPFLAGS += -D_XOPEN_SOURCE=600
CPPFLAGS += -D_GNU_SOURCE=1
CPPFLAGS += -Wno-missing-field-initializers
```

You should include the following, if they work:

```
CPPFLAGS += -DWITH_STRERROR_NP
CPPFLAGS += -DWITH_SIGNAMES
```

Disable `WITH_STRERROR_NP` if you have trouble compiling calls to `strerrorname_np`.
Disable `WITH_SIGNAMES` for problems with `strsignal` or `sigabbrev_np`.

On some systems, you might need to explicitly link some libraries:

```
udptun_lib += -lstdc++fs -lanl
```

# Configuration

A configuration file is a YAML document with at least one of the following keys: `ingress` (for injecting UDP packets into a TCP tunnel) and `egress` (for extracting them from a tunnel, and redelivering as UDP).

Suppose that, on the egress host `monitor.example.com`, you had five UDP sockets expecting datagrams.
Summary traffic should go to one of them for local monitoring.
Detailed traffic should go to another for local monitoring, and to one of the three others for external monitoring.
The egress-side configuration could be:

```
egress:
  destinations:
    internal-summary:
      host: localhost
      port: 10000
      ipv6: false
    internal-detailed:
      host: localhost
      port: 10001
      ipv6: false
    shoveler-1:
      host: localhost
      port: 10002
      ipv6: false
    shoveler-2:
      host: localhost
      port: 10003
      ipv6: false
    shoveler-3:
      host: localhost
      port: 10004
      ipv6: false
  groups:
    shoveler:
      - shoveler-1
      - shoveler-2
      - shoveler-3
  peers:
    gw00:
      hosts:
        - 10.20.30.1
    gw01:
      hosts:
        - 10.20.30.2
    gw02:
      hash: 4
      hosts:
        - 10.20.30.3
  tunnels:
    main:
      tcp:
	    host: monitor.example.com
        port: 9992
      channels:
        0: [ internal-summary ]
        1: [ internal-detailed, shoveler ]
```

(The strings `gw00`, `gw01`, `gw02`, `internal-summary`, `internal-detailed`, `shoveler-1`, `shoveler-2`, `shoveler-3` and `shoveler` are user-defined.)

The example creates a TCP server socket on `monitor.example.com:9992`, and accepts connections on it, recognizing clients on hosts `10.20.30.1`, `10.20.30.2` and `10.20.30.3` under the user-defined names `gw00`, `gw01` and `gw02`, respectively.
(Several addresses may be listed per name.)
Encapsulated datagrams are received on these connections, and decapsulated.
Each datagram is tagged with a 16-bit label and a 16-bit client id.
Any datagram labelled with `0` is sent to `localhost:10000`.
Any datagram labelled with `1` is sent to `localhost:10001` and one of the `shoveler` destinations, chosen by hashing on the peer name (`gw00`, etc), or taken from the `hash` field (as is the case for `gw02`).

Datagrams are sent from dynamically created local sockets.
Datagrams from different peers or different client ids are sent from different sockets, so they will appear to have distinct identities when received by the various destinations.

On each of `10.20.30.{1,2,3}`, a corresponding ingress-side configuration could be:

```
ingress:
  tunnels:
    monitor:
      tcp:
        host: monitor.example.com
        port: 9992
  channels:
    summary:
      tunnel: monitor
      label: 0
    detailed:
      tunnel: monitor
      label: 1
  sockets:
    detailed:
      udp:
        ipv6: false
        host: localhost
        port: 9400
      channels: [ detailed ]
    summary:
      udp:
        ipv6: false
        host: localhost
        port: 9401
      channels: [ summary ]
```

(`main`, `detailed`, `summary` and `monitor` are used-defined.)

This example creates UDP sockets on `localhost:9400` and `localhost:9401`, and opens a TCP connection to `monitor.example.com:9992`.
Every datagram received on `9400` is passed through a queue `ingress/detailed`, and on `9401` through `ingress/summary`.
The peer address of each datagram is mapped to a 16-bit client id, stored with the datagram.
Each datagram from a queue is sent over the TCP socket, encapsulated with its client id and the label associated with the queue.
As described for the egress side, the label determines which destinations a datagram is ultimately delivered to.
The client id, meanwhile, ensures that distinct datagram senders in the ingress side are represented by distinct senders on the egress side.

## Addresses

Usually, `host` can be specified wherever `port` can be, and defaults to `localhost`.
Use an empty string `""` for binding passive sockets (tunnel egresses and all UDP sockets) to `INADDR_ANY`.

Booleans `ipv4` and `ipv6` can also be specified, and default to `true`.
These four parameters are resolved with `getaddrinfo` into internal socket parameters.

(An egress configuration can also configure `clid_timeout` as a duration, e.g., `10s`, `20m`, `1h`.
Records for client ids not seen from a given peer are discarded after this time.
The default is `1h`.)

## Testing

Mostly for testing purposes, you can combine `ingress` and `egress` in one configuration, and then maybe use `netcat` to test datagrams over it.

## Persistence

Queues are stored in `/var/spool/udptun/` by default, overridden with the likes of:

```
queues:
  path: ~/.local/var/spool/udptun
```

You can also set a quota, so that older messages are discarded:

```
queues:
  quota: 100k
```

## Logging

The top-level field `logging` configures logging.
For example:

```
logging:
  file: /var/log/udptun.log
  level: info
  children:
    udptun:
      children:
        egress:
          children:
            tunnel:
              level: trace
```

`file` specifies an output file, and defaults to `stderr`.
Sending `SIGHUP` to the process causes the process to close and re-open this file, to support log rotation.
(This will likely be changed to a different signal, e.g. `SIGUSR1`.)

`level` indicates one of several levels of detail:

- `silent`
- `critical`
- `error`
- `warn`
- `info`
- `debug`
- `trace`
- `detail`
- `all`

`children` lists program components for overriding the detail level.
Each component can also have `children` and `level` fields.
The example specifies that the component `udptun:egress:tunnel` and subcomponents are logged using the `trace` level of detail.

## SystemD service

With the binary in `/usr/local/bin/udptun`, and configuration in `/etc/udptun.yaml`, you could define a SystemD unit in `/etc/systemd/system/udptun.service` such as this:

```
[Unit]
Description=UDP Tunnelling over TCP
After=network-online.target

[Service]
Restart=on-failure
ExecStart=/usr/local/bin/udptun /etc/udptun.yaml
ExecReload=/bin/kill -HUP $MAINPID

[Install]
WantedBy=multi-user.target
```
