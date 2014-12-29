#include <stdio.h>
#include "unpifiplus.h"
#include <math.h>
#include "common.h"
#include "recvFile.h"

extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
char allIPAddresses[16][16] ,allMasks[16][16];
int numIPs,sendflags;

int backoffTimer_sec = 3;
int backoffTimer_usec = 0;

int talkWithMotherServer(char *, char *, int, char *,float, int, int);

int main()
{
    int i,local,seed;
    float probablity;
    FILE *fp;
    char serverIP[16], clientIP[16];
    int serverPort;
    char fileToRead[50];
    int childServerSocketfd;
    
    //Getting and printing all IP addresses of the client	
    if( (numIPs = getAllIPAddressAndMasks()) < 0)
    {
        printf("Error getting IPs!\n");
        return(-1);
    }
    printf("My IP addresses are - \n");
    printf("%3s | %16s | %16s\n", "n", "IP Address", "Subnet Mask");
    printf("--- | ---------------- | ----------------\n");
    for(i=0;i<numIPs;i++)
        printf("%3d | %16s | %16s\n",i,allIPAddresses[i],allMasks[i]);
    printf("\n");


    //Reading from client.in file
    if( (fp = fopen("client.in","r")) == NULL)
    {
        printf("Error reading from file");
        return(-1);
    }
    fscanf(fp,"%s %d %s %d %d %f %d",serverIP, &serverPort, fileToRead, &receiverBufferSize, &seed, &probablity, &mean);
    #ifdef DEBUG
    printf("Read from file : %s, %d, %s, %d, %d, %f, %d\n", serverIP, serverPort, fileToRead, receiverBufferSize, seed, probablity, mean);
    #endif
    local = isLocal(serverIP);

    //Checking if server IP is local to client
    if(local == SAMEHOST)
    {
        strcpy(clientIP,"127.0.0.1");
        strcpy(serverIP,"127.0.0.1");
        sendflags = MSG_DONTROUTE;
        printf("Server is localhost\n");
    }
    else if(local == NONLOCAL)
    {
        strcpy(clientIP,allIPAddresses[1]); 
        sendflags = 0;
        printf("Server is not local!\n");
    }
    else
    {
        strcpy(clientIP,allIPAddresses[local]);
        sendflags = MSG_DONTROUTE;
        printf("Server is local\n");
    }    
    
    printf("IPClient: %s and IPServer: %s\n\n",clientIP,serverIP);

    childServerSocketfd = talkWithMotherServer(clientIP, serverIP, serverPort, fileToRead, probablity, seed, local);
    
    fclose(fp);
    return(1);
}

int talkWithMotherServer(char * clientIP, char * serverIP, int serverPort, char * fileToRead, float probablity, int seed, int local)
{
    int sockfd;
    int n;
    const int on = 1;
    struct sockaddr_in servaddr,cliaddr, *p_dropSocket;
    socklen_t servaddrLen = sizeof(servaddr);
    socklen_t dropSockLen = sizeof(struct sockaddr_in);
    char sendline[MAXLINE], recline[MAXLINE + 1];
    int toDrop;

    struct sockaddr_in  temp;
    socklen_t clientSockLen = sizeof(temp);

    //Initialize the randomizer
    srand48(seed);
    //Setting up client details
    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sin_family = AF_INET;
    cliaddr.sin_port = htons(0);
    Inet_pton(AF_INET, clientIP, &cliaddr.sin_addr);
    
    //Setting up server details
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(serverPort);
    Inet_pton(AF_INET, serverIP, &servaddr.sin_addr);
    
    //Declarations for select
	int maxfdp1 =-1;
	int timesRetried = 0;
	fd_set rset;
	FD_ZERO(&rset);
	time_t ticks;
	struct timeval timer;
	timer.tv_sec = backoffTimer_sec;
	timer.tv_usec = backoffTimer_usec;
    
    
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    Setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR , &on, sizeof(on));
    Bind(sockfd,(SA*) &cliaddr,sizeof(cliaddr));
    


    if( (connect(sockfd, (SA*) &servaddr,sizeof(servaddr))) < 0)
    {
        printf("Connect error with motherServer, errno:%s!\n",strerror(errno));
        return(-1);
    }
    
    printf("After connect with server :-\n");
    if( getsockname(sockfd, (SA *)(&temp), &clientSockLen) == 0)
        printf("IPClient- \t%s:%d\n", clientIP, (int) ntohs(temp.sin_port));
    if( getpeername(sockfd, (SA *)(&temp), &clientSockLen) == 0)
        printf("IPServer-\t%s:%d\n\n", serverIP, (int) ntohs(temp.sin_port));

    //Sending first message to the server (Mother)
    sendFirstMessage :
    (drand48() < probablity) ? (toDrop = 1) : (toDrop = 0) ;
    if(toDrop != 1)
    {
        if(send(sockfd, fileToRead, strlen(fileToRead),sendflags) <0)
        {
            printf("Send failed,errno:%s",strerror(errno));
            return(-1);
        }
    }
    else
        printf("Dropping connection request message!\n");
    
    
    //Receiving from server (Mother) new details of the childServer
    for(;;)
    {
        FD_SET(sockfd, &rset);
        maxfdp1 = sockfd + 1;
        select(maxfdp1, &rset, NULL, NULL, &timer);
        //Either timeout or packet received
        if (FD_ISSET(sockfd, &rset))
        {
            //childServer details recevied
            if( errno == ECONNREFUSED)
            {
                printf("Server refused the connection, exiting the program.\n");
                return -1;
            }
            //Test to drop receive from server about childServer details
            (drand48() < probablity) ? (toDrop = 1) : (toDrop = 0) ;
            if(toDrop == 1)
            {
                // Receive message on a dummy socket to simulate drop
                n = recvfrom(sockfd, recline, MAXLINE, 0, (SA *)(p_dropSocket), &dropSockLen);
                #ifdef DEBUG
                    printf("Message: %s dropped here.\n", recline);
                #endif
                strcpy(recline,"");
                continue;
            }
            
            n = recv(sockfd, recline, MAXLINE, 0);
            recline[n] = 0;
            #ifdef DEBUG
                printf("Message: %s received.\n", recline);
            #endif
            break;
        }
        else
        {
            //Timeout
            if(++timesRetried > MAXRETRIES)
            {
                printf("Connection with server terminated. Network/server is down.\n");
                return(-1);
            }
            else
            {
                printf("Did not receive new port details from server child, resending connection request\n");
                goto sendFirstMessage;
            }
        }
    }

    //Details of childServer obtained. Reconfiguring socket.
    serverPort = atoi(recline);
    //Setting up server details
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_UNSPEC;  //To disconnect
    servaddr.sin_port = htons(serverPort);
    Inet_pton(AF_INET, serverIP, &servaddr.sin_addr);

    if( (connect(sockfd, (SA*) &servaddr,sizeof(servaddr))) < 0 )
    {
        printf("Error disconnecting socket, errno:%s",strerror(errno));
        return(-1);
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(serverPort);
    Inet_pton(AF_INET, serverIP, &servaddr.sin_addr);
    
    if( (connect(sockfd, (SA*) &servaddr,sizeof(servaddr))) < 0)
    {
        printf("Connect error with Server,quitting the program! Errno:%s!\n",strerror(errno));
        return(-1);
    }

    printf("After second handshake with server :-\n");
    if( getsockname(sockfd, (SA *)(&temp), &clientSockLen) == 0)
        printf("IPclient- \t%s:%d\n", clientIP, (int) ntohs(temp.sin_port));
    if( getpeername(sockfd, (SA *)(&temp), &clientSockLen) == 0)
        printf("IPserver-\t%s:%d\n\n", serverIP, (int) ntohs(temp.sin_port));


    //Talk to childServer
    sprintf(sendline, "%d", receiverBufferSize);
    if(send(sockfd, sendline, strlen(sendline),sendflags) <0)
    {
        printf("Send failed, errno:%s\n",strerror(errno));
        return(-1);
    }
    printf("Handshake completed with server\n");
    printf("\n ****************** *************** ******************* \n");
    
    recv_file(sockfd, (SA *)&servaddr, sizeof(servaddr),probablity,seed);
    close(sockfd);
    return(1);
}

int getAllIPAddressAndMasks()
{
    struct ifi_info *p_ifi, *p_ifihead;
    struct sockaddr *sa;
    int i;

    for((p_ifihead = p_ifi = Get_ifi_info_plus(AF_INET,1)),i=0;
            p_ifi!=NULL;
            p_ifi = p_ifi->ifi_next,i++)
    {
        if( (sa = p_ifi->ifi_addr) !=NULL )
            strcpy(allIPAddresses[i],Sock_ntop_host(sa, sizeof(*sa)));
        else
            return(-1);

        if ( (sa = p_ifi->ifi_ntmaddr) != NULL)
            strcpy(allMasks[i],Sock_ntop_host(sa, sizeof(*sa)));
        else
            return(-1);
    }
    return(i);
}

/*
 * This function determines if given IP ( in dotted decimal ) is in the 
 * same network of any of the interface IPs of the client. 
 * If it is one of the interface IPs, the given IP is in localhost and 
 * return SAMEHOST.
 * Else perform a longest prefix match and return the position of the 
 * IP in the global array to which sIP is local.
 * If prefix match fails ,return NONLOCAL
 */

int isLocal(char *sIP)
{
    int i,lastmatch,temp;
    struct in_addr serv_addr,my_addr,my_mask;

    lastmatch = NONLOCAL;
    if(inet_pton(AF_INET,sIP,&serv_addr) == -1)
    {
        printf("Server IP address invalid!\n");
        return(-1);
    }
    for(i=0;i<numIPs;i++)
    {
        if(inet_pton(AF_INET,allIPAddresses[i],&my_addr) == -1) 
        {
            printf("Client's %dth address invalid!\n",i);
            return(-2);
        }

        if(inet_pton(AF_INET,allMasks[i],&my_mask) == -1) 
        {
            printf("Invalid IP address!\n");
            return(-2);
        }

        if(serv_addr.s_addr == my_addr.s_addr )  //Exact match i.e server is localhost
            return SAMEHOST;
        
        if( (serv_addr.s_addr & my_mask.s_addr) == (my_addr.s_addr & my_mask.s_addr) )
        {
            if((lastmatch == NONLOCAL) ||
                    (lastmatch < (my_mask.s_addr)) )      //Current prefix is longer, so update the lastmatch
            {
                lastmatch = i;
            }
            else
                continue;
        }
    }
    return(lastmatch);
}
