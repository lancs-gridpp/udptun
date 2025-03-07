# Purpose

This is a C++ program to tunnel UDP packets, preserving message boundaries, but not preserving source addresses.
At the ingress end, it can bind to multiple UDP addresses, receive packets, and transmit them over several TCP tunnels annotated with bitsets.
On egress, it can listen on multiple TCP addresses, received the annotated packets, and deliver to multiple UDP addresses.

## Installation

Install dependencies:

```
sudo apt-get install yaml-cpp-dev
sudo dnf install yaml-cpp-devel
```
