# Distributed Programming I

### Contents

This project is a simple cliente/server solution for downloading files.

- `tools/` contains software utilities that are used by the testing script
- `source/` the sources of your solutions
  - `client1` the client
  - `server1` the sequential server
  - `server2` the concurrent server
- `test.sh` the script that executes tests on Server1
- `test2.sh` the script that executes tests on Server2
- `README.md` this file

In order to launch the tests, use the following command:

```sh
./test.sh
```

The script compiles your solution (with the commands described later) and runs the tests.

### Requirements

#### Client1 and Server1

Develop a _TCP sequential server_ (listening to the port specified as the first parameter of the command line, as a decimal integer) that, after having established a TCP connection with a client, accepts file transfer requests from the client and sends the requested files back to the client, following the protocol specified below. The files available for being sent by the server are the ones accessible in the server file system from the working directory of the server.

Develop a _client_ that can connect to a TCP server (to the address and port number specified as first and second command-line parameters, respectively). After having established the connection, the client requests the transfer of the files whose names are specified on the command line as third and subsequent parameters, and stores them locally in its working directory. After having transferred and saved locally a file, the client must print a message to the standard output about the performed file transfer, including the file name, followed by the file size (in bytes, as a decimal number) and timestamp of last modification (as a decimal number).

The protocol for file transfer works as follows: to request a file the client sends to the server the three ASCII characters `GET` followed by the ASCII space character and the ASCII characters of the file name, terminated by the ASCII carriage return (`CR`) and line feed (`LF`):

```
    |G|E|T| \* filename *\ |CR|LF|
```

*Note*: _the command includes a total of 6 characters plus the characters of the file name._

The server replies by sending:

```
    |+|O|K|CR|LF|B1|B2|B3|B4|T1|T2|T3|T4| \* file content *\
```

*Note*: _this message is composed of 5 characters followed by the number of bytes of the requested file (a 32-bit unsigned integer in network byte order - bytes `B1 B2 B3 B4` in the figure), then by the timestamp of the last file modification (Unix time, i.e. number of seconds since the start of epoch, represented as a 32-bit unsigned integer in network byte order - bytes `T1 T2 T3 T4` in the figure) and then by the bytes of the requested file._

The client can request more files using the same TCP connection, by sending many `GET` commands, one after the other. When it intends to terminate the communication it sends:

``` 
    |Q|U|I|T|CR|LF|
```
(6 characters) and then it closes the communication channel.

In case of error (e.g. illegal command, non-existing file) the server always replies with:
```
    |-|E|R|R|CR|LF|
```
(6 characters) and then it closes the connection with the client.

---
*Note*: _The tools folder includes some testing tools, among which you can find the executable files of a reference client and server that behave according to the specified protocol and that you can use for interoperability tests.
Try to connect your client with the reference server, and the reference client with your server for testing interoperability (note that the executable files are provided for both 32bit and 64bit architectures. Files with the _32 suffix are compiled for and run on 32bit Linux systems. Files without that suffix are for 64bit systems.). If fixes are needed in your client or server, make sure that your client and server can communicate correctly with each other after the modifications. Finally you should have a client and a server that can communicate with each other and that can interoperate with the reference client and server._

#### Server2

Develop a concurrent TCP server (listening to the port specified as the first parameter of the command line, as a decimal integer) that, after having established a TCP connection with a client, accepts file transfer requests from the client and sends the requested files back to the client, following the same protocol used in _Server1_. The server must create processes on demand (a new process for each new TCP connection).

#### How to build

From the source folder run:

```sh
gcc -std=gnu99 -o server server1/*.c *.c -Iserver1 -lpthread -lm
gcc -std=gnu99 -o client client1/*.c *.c -Iclient1 -lpthread -lm
gcc -std=gnu99 -o server server2/*.c *.c -Iserver2 -lpthread -lm
```
