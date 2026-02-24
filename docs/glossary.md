# glossary

## addresses and endpoints
ip address: address of a computer on a network  
127.0.0.1 (localhost): special ip meaning this same computer  
port: numbered door on a computer that a program can claim  
host: the machine you connect to

## roles
server: program that waits for connections on a port  
client: program that connects to the server  
netcat (nc): simple cli tool used as a quick client for testing

## connection basics
tcp: reliable connection where data arrives in order  
connection: live link between a client and server  
disconnect: when one side ends the connection

## socket terms
socket: network communication object, like a network phone  
file descriptor (fd): os given number representing an open thing like a socket  
listen socket: the server’s front door socket waiting for connections  
client socket: the private connection socket for one client

## functions
socket(): create a socket  
bind(): claim a port  
listen(): start waiting for connections  
backlog: how many connections can wait in line  
accept(): accept a connection and get a new client fd  
close(): close an fd

## data handling
buffer: memory region used to store bytes  
bytes: raw data sent over the network  
recv(): read bytes into a buffer  
send(): write bytes from a buffer
