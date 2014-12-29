Authors
=======

Sowmiya Narayan Srinath<br>
Sai Santhosh Vaidyam Anandan

Description
===========

A UDP based reliable file transfer application that focusses on two broad aspects:
<br>
Setting up the exchange between the client and server in a secure way despite the lack of a formal connection (as in TCP) between the two, so that ‘outsider’ UDP datagrams (broadcast, multicast, unicast - fortuitously or maliciously) cannot intrude on the communication.
<br>
Introducing application-layer protocol data-transmission reliability, flow control and congestion control in the client and server using TCP-like ARQ sliding window mechanisms.

Functionality
=============

1. The getifiinfo() function returns the set of all IP addresses of the host among other details. We
    ensure that only unicast addresses are bound to the socket by ignoring all ifi_brdaddr members of the
    returned structure. Based on whether the client is local or not, the MSG_DONTROUTE flag is set. 
<br>
2. For faster calculation, the RTT calculation has been converted to integer arithmetic. All the rtt_info
    structure members were made integers and represented in milliseconds. rtt_rttvar is scaled at 4 times
    its actual value and rtt_srtt at 8 times. The multiplication operations to calculate new srtt and RTO
    are converted to corresponding left shift operations. These changes are made in rtt_stop() and the
    RTT_RTOCALC macro.
<br>
3. Flow control and congestion control mechanisms are implemented on top of the unreliable UDP infrastructure.
    The reciever maintains a sliding window whose remaining size is advertized with every acknowlegement. The
    sender sends an appropriate amount of data segments. When the reciever window becomes 0 i.e 'locks' the 
    server sends a periodic probe message and if an ack with a window size greater than 0 is recieved, the afore
    mentioned process continues.
<br>
4. Congestion control is implemented by means of a congestion window. Initially the cwin doubles till it reaches
    the slow start threshold (initialized to receiving window size) , after which cwin increases linearly. This
    allows the sender to utilize as much bandwidth as possible. When a drop occurs, the cwin and ssthresh are
    decremented appropriately.
<br>
5. Reliablity of data transmission is ensured by retransmission of data segments in case of a drop. Drops are 
    detected if there is a timeout (calculated per point 2) which is exponentially backed off. Fast retransmit
    is also supported, in which a case a segment is retransmitted immediately if there are 3 duplicate ACKs
    irrespective of the timeout.
<br>
6. A fin flag in the header of the data segment enables us to detect the last segment. In this case, the receiver
    sends a FIN-ACK and enters a TIME_WAIT state. If the FIN_ACK was lost, the sender would have retransmitted
    the last segment, so we resend the FIN_ACK and close the connection.
<br>
7. If the client quits unexpectedly in the middle of the program, the server will detect this after 12 consecutive
    timeouts and quit gracefully. Similarly if the server closes unexpectedly, the client consumer thread, seeing 
    that no data has been consumed 12 times will indicate the client to quit gracefully.
<br>
