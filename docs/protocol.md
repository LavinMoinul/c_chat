# C Chat Protocol

## Overview
C Chat is a line-based TCP chat server. Each client connects over TCP, sends a username first, and then sends chat messages or slash commands as full newline-terminated lines.

## Connection Flow
1. Client connects to the server.
2. Server prompts for a username.
3. Client sends one full line ending in newline.
4. Server validates the username and announces the join.
5. Client can then send chat messages or slash commands.

## Username Rules
- Maximum length: 31 characters
- Allowed characters: letters, numbers, `_`, `-`
- Username must be unique among connected users

## Message Framing
The server treats each newline-terminated line as one complete message.

Examples:
- `lavin\n`
- `hello everyone\n`
- `/users\n`

## Server Messages
Join announcement:
- `*** lavin joined ***`

Leave announcement:
- `*** lavin left ***`

Rename announcement:
- `*** lavin is now known as lmoinul ***`

Chat message:
- `lavin: hello`

## Commands
`/help`
Shows the list of commands.

`/users`
Shows connected users.

`/nick NEW_NAME`
Changes the current username.

`/quit`
Disconnects from the server.

## Limits
- Maximum clients: 64
- Per-client input buffer: 1024 bytes
- Fixed-size buffers only
- No dynamic allocation
- Single-threaded server using `select`
