# C Chat

## Overview
C Chat is a TCP chat server written in C. It is being built step by step to practice Linux systems programming concepts such as sockets, ports, `select`, fixed-size buffers, and simple protocol design.

## Build
make

## Run
./bin/server 5555

## Quick Test
In another terminal:
nc 127.0.0.1 5555

To test multiple clients, open more than one terminal and connect with `nc` in each one.

## Current Features
- TCP server built with POSIX sockets
- Multi-client handling with `select`
- Broadcast messaging to all connected clients
- Per-client input buffering with newline-based message framing
- Username handshake on connect
- Join and leave announcements
- Slash commands for `/help`, `/users`, `/nick`, and `/quit`
- Fixed-size buffers with no dynamic allocation
- Protocol documentation and a small parser test

## Milestones
Day 1: Server binds, listens, accepts connections, and logs clients.

Day 2: Server keeps one client connected, reads messages with `recv`, and echoes them back.

Day 3: Server handles multiple clients with `select` and broadcasts messages to everyone connected.

Day 4: Server buffers input by newline, assigns usernames, and announces join and leave events.

Day 5: Server adds slash commands, protocol documentation, a parser test, and improved Makefile targets.

## Learning Goals
This project is focused on building confidence with:
- C programming fundamentals
- Linux networking APIs
- Buffer management
- Event-driven server design
- Incremental development with Git and GitHub

## Next Steps
- Add cleaner message parsing tests
- Improve disconnect and overflow edge-case handling
- Build a dedicated client program instead of relying on `nc`
