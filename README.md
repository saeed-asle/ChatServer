# Simple Chat Server
Authored by Saeed Asle
# Description
This project is a simple chat server implemented in C.
It uses sockets for communication and supports multiple client connections.
The server allows clients to send messages to each other through the server.
# Features
* Accepts incoming connections and adds them to the connection pool.
* Handles incoming messages from clients and broadcasts them to all other connected clients.
* Removes disconnected clients from the connection pool.
# How to Use
* Compile the code using a C compiler. For example, you can use GCC:
  
      gcc -o chatServer chatServer.c
  
* Run the server, specifying the port number to listen on:

      ./chatServer <port>

* Connect clients to the server using a TCP/IP client, such as telnet or netcat:

      telnet <server_ip> <port>

Start chatting! Messages sent by one client will be broadcasted to all other connected clients.
# Output
The server outputs information about incoming connections, received messages, and disconnections.
It provides basic logging to track the flow of communication.

