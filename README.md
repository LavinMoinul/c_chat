# c chat

## build
make

## run
./bin/server 5555

## quick test
in another terminal:
nc 127.0.0.1 5555

## milestones
day 1: server binds, listens, accepts connections, and logs clients

day 2: server keeps one client connected, reads messages with recv, and echoes them back

day 3: server handles multiple clients with select and broadcasts messages to everyone connected

day 4: server buffers input by newline, assigns usernames, and announces join and leave events
