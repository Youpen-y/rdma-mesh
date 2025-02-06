RDMA-MESH
=============================================================================
**rdma-mesh** is a simple rdma communication test program for [jiajia](https://github.com/Youpen-y/jiajia)(SDSM).

It uses multi-threads to establish _RDMA RC_ full connection ( _any two hosts in a cluster have a connection_ ), and a simple communication example between two hosts.

Dependency
-----------------------------------------------------------------------------
* librdmacm
* libibverbs

Structure
-----------------------------------------------------------------------------
![rdma-mesh-structure](./rdma-mesh.png)

- client thread: `run_client()` in rdma-mesh/rdma_mesh.c, used to prepare the connection resources and listen the connect request.
- server thread: `run_server()` in rdma-mesh/rdma_mesh.c, used to prepare the connection resources and initiate the connect request.

After the connection established, `cm_id_array[i]` (struct rdma_cm_id) in every host(!i) was used to communicate with host i.

Beside the threads that used to establish the connection, there are other threads used to construct a simple communication model.

- `rdma_client_thread`: post send wr continuously.

- `rdma_listen_thread`: post recv wr continuously.

- `rdma_server_thread`: wait new msg and serve.

Usage
-----------------------------------------------------------------------------
As an example program, rdma-mesh only support two hosts (master-slave). (_Complete version (i.e. more hosts version) can be found in [jiajia](https://github.com/Youpen-y/jiajia)._)

`ip_array` in main.c is the ip array of hosts in a cluster, which need to reconfigure according to the cluster's setting.

1. clone: clone the program to two hosts
```bash
git clone git@github.com:Youpen-y/rdma-mesh.git
```
2. modify: modify the Makefile `CFLAGS`, one with `-DMASTER` and another without.
```Makefile
# master
CFLAGS = -Wno-unused-variable -Wall -g -DMASTER

# slave
# CFLAGS = -Wno-unused-variable -Wall -g
```
3. build
```bash
make
```
4. execute
```bash
./mesh <host_id> <total_hosts>

# master: ./mesh 0 2
# slave:  ./mesh 1 2
```
**Example:**
slave
```bash
./mesh 1 2
Host 1: Server listening on port 40001
Received Connect Request from host 0
Host 1: Accepted connection
Host 1: Connection established
generate string: 2SNzE5ghy
Received data at in_mr[0] address 0x55ff17c4a000: MsBFoLqk9
Received data at in_mr[1] address 0x55ff17c55000: K7d0JteM8
generate string: EK1CuxCgp
Received data at in_mr[2] address 0x55ff17c60000: dwTo6lCGv
generate string: bo1Nm6TNc
Received data at in_mr[3] address 0x55ff17c6b000: FZ3JDHuuX
generate string: dX1qAZsVY
```
master
```bash
After retried 0 connect, Host 0: Connected to host 1
Connection setup with host 1
generate string: MsBFoLqk9
generate string: K7d0JteM8
Received data at in_mr[0] address 0x562a316ad000: 2SNzE5ghy
Received data at in_mr[1] address 0x562a316b8000: EK1CuxCgp
generate string: dwTo6lCGv
Received data at in_mr[2] address 0x562a316c3000: bo1Nm6TNc
generate string: FZ3JDHuuX
Received data at in_mr[3] address 0x562a316ce000: dX1qAZsVY
```

5. clean
```bash
make clean
```

Getting Help
-----------------------------------------------------------------------------
The best way to ask questions is to submit an issue on GitHub.

Contributing
-----------------------------------------------------------------------------
Welcome any bug fix and feature extension. Have other thing to discuss, welcome to contact! ([yongy2022@outlook.com]())