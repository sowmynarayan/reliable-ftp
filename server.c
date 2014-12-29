#include <stdio.h>
#include "unpifiplus.h"
#include "common.h"
#include "sendFile.h"

extern struct ifi_info *Get_ifi_info_plus(int family, int doaliases);
char allIPAddresses[16][16],allMasks[16][16];
int numIPs;

int sendflags;
int childCreatedSoFar = -1;
int backoffTimer_sec = 3;
int backoffTimer_usec = 0;

int handshakeChild(int *, int ,char *, struct sockaddr_in *);
int exists(struct sockaddr_in);
void sig_chld(int);

int main()
{
    int i, n;
    FILE *fp;
    pid_t pid;
    const int on = 1;
    
    head = NULL;
    
    signal(SIGCHLD,sig_chld);
    
    //Declrations for motherServer Sockets
    int sockfd[16];
    int serverPort;
    struct sockaddr_in servaddr[16], *p_cliaddr,temp;
    socklen_t clientSockLen = sizeof(struct sockaddr_in);
    char msg[MAXLINE + 1];
    int flags;
    int s = 0;
    
    //Declarations for select
	int maxfdp1 =-1;
	fd_set rset;
	FD_ZERO(&rset);

    if( (numIPs = getAllIPAddressAndMasks()) < 0)
    {
        printf("Error getting IPs!\n");
        return(-1);
    }

    printf("My IP addresses are - \n");
    printf("%3s | %16s\n", "n", "IP Address");
    printf("--- | ----------------\n");
    for(i=0;i<numIPs;i++)
        printf("%3d | %16s\n",i,allIPAddresses[i]);
    printf("\n");

            
    //Reading from server.in file
    if( (fp = fopen("server.in","r")) == NULL)
    {
        printf("Error reading from file");
        return(-1);
    }
    fscanf(fp,"%d", &serverPort);
    fscanf(fp,"%d", &sBufferSize);
    
    #ifdef DEBUG
    printf("Read from file : %d\n", serverPort);
    #endif
    
    for(i=0; i<numIPs; i++)
    {
        sockfd[i] = Socket(AF_INET, SOCK_DGRAM, 0);
        
        bzero(&servaddr[i], sizeof(servaddr[i]));
        servaddr[i].sin_family = AF_INET;
        Inet_pton(AF_INET, allIPAddresses[i], &servaddr[i].sin_addr);
        servaddr[i].sin_port = htons(serverPort);
        Setsockopt(sockfd[i], SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
        Bind(sockfd[i], (SA *)&servaddr[i], sizeof(servaddr[i]));
        
    }
    //Using select to determine which socket gets incoming data
    for(;;)
    {
    
        for(i=0; i < numIPs; i++)
        {
            FD_SET(sockfd[i], &rset);
            if( maxfdp1 <= sockfd[i])
                maxfdp1 = sockfd[i] + 1;
        }
        strcpy(msg, "");
        n = 0;
        if( (s = select(maxfdp1, &rset, NULL, NULL, NULL))  < 0 )
        {
            if(errno == EINTR)
                continue;
            else
            {
                printf("Select error : %s", strerror(errno));
                return -1;
            }
        }
        #ifdef DEBUG
        printf("Out of first select\n");
        #endif
        for(i=0; i<numIPs; i++)
        {
            if (FD_ISSET(sockfd[i], &rset)) 
            {
                n = recvfrom(sockfd[i], msg, MAXLINE, 0, (SA *)(p_cliaddr), &clientSockLen);
                msg[n] = 0; 
                #ifdef DEBUG
                printf("First message received:%s\n",msg);
                #endif
                
                //Check to see if the child is already created for client
                if( exists( *p_cliaddr) == EXISTS )
                {
                    printf("Duplicate request received from %s:%d\n", Sock_ntop_host((SA *)p_cliaddr, sizeof(*p_cliaddr)),p_cliaddr->sin_port);
                }
                else
                {
                    #ifdef DEBUG
                    printf("New request received\n");
                    #endif
                    if( (pid=fork())!=0 )
                    {
                        //Parent process
                        
                        //Adding new reocrd in childTable for the
                        addToTable(pid, *p_cliaddr);
                        #ifdef DEBUG
                            printf("Added to table\n");
                        #endif
                        break;
                    }   
                    else
                    {
                        #ifdef DEBUG
                            printf("Child process starts\n");
                        #endif
                        usleep(1000);
                        //Child Process
                        if(handshakeChild(sockfd, i, msg, p_cliaddr) == -1)
                            return -1;
                        #ifdef DEBUG
                            printf("Child process ends\n");
                        #endif
                        return 1; //Child finished its main function
                    }
                }
                
            }//End of if FD_ISSET
        }//End of for which checks all sockets
        
    }// The infinte loop
    return(1);
}// End of main


//Only child process should call this fucntion
int handshakeChild(int *socket, int listener, char * fileRequested, struct sockaddr_in * p_cliaddr)
{
    int i, n, local;
    const int on = 1;
    int shouldResend = 0;
    printf("Connection request received from :  \t");
    printf("%s:%d\n",Sock_ntop_host((SA *)p_cliaddr, sizeof(*p_cliaddr)), p_cliaddr->sin_port);
    
    local = isLocal(Sock_ntop_host((SA *)p_cliaddr, sizeof(*p_cliaddr)));
    if(local == NONLOCAL)
    {
        printf("Client is not local to server\n");
        sendflags = 0;
    }
    else
    {
        printf("Client is local to server \n");
        sendflags = MSG_DONTROUTE;
    }
    
    //Closing other sockets
    for(i=0; i<numIPs; i++)
    {
        if(i != listener)
            close(socket[i]);
    }        
    
    //Declaring sockets for childServer
    int childSocket;
    struct sockaddr_in childSockAddr, temp;
    socklen_t clientSockLen = sizeof(childSockAddr);
    char msg[MAXLINE + 1];
    int flags;
    
    //Declarations for select
	int maxfdp1 =-1;
	int timesRetried = 0;
	fd_set rset;
	FD_ZERO(&rset);
	time_t ticks;
	struct timeval timer;
	timer.tv_sec = backoffTimer_sec;
	timer.tv_usec = backoffTimer_usec;
    
    //Creating the childSocket
    childSocket = Socket(AF_INET, SOCK_DGRAM, 0);
    bzero(&childSockAddr, sizeof(childSockAddr));
    childSockAddr.sin_family = AF_INET;
    childSockAddr.sin_port = htons(0);
    Inet_pton(AF_INET, allIPAddresses[listener], &childSockAddr.sin_addr);
    Setsockopt(childSocket, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    Bind(childSocket, (SA *)&childSockAddr, sizeof(childSockAddr));

    if( (connect(childSocket, (SA*) p_cliaddr,sizeof(*p_cliaddr))) < 0)
    {
        printf("Connect failed, errno:%d\n!",strerror(errno));
        return(-1);
    }
    
    //Getting new port number of childSocket
    if( getsockname(childSocket, (SA *)(&temp), &clientSockLen) == 0)
    {
        printf("Details of connection socket created for above- \t%s:%d\n", Sock_ntop_host( (SA *)&temp, sizeof(temp)), (int) ntohs(temp.sin_port));
    }
    
    sprintf(msg, "%d", (int) htons(temp.sin_port));
    
    //Sending childSocket details to client via Mother
    Sendto(socket[listener], msg, strlen(msg), sendflags, (SA *)p_cliaddr, sizeof(* p_cliaddr));
    
    resendFromBothParentAndChild:
        //resend from both if resend == 1
        if(shouldResend == 1)
        {
            printf("Ack not received from client, resending new port details.\n");
            Sendto(socket[listener], msg, strlen(msg), sendflags, (SA *)p_cliaddr, sizeof(* p_cliaddr));
            shouldResend = 0;
        }
         
    for(;;)
    {
        FD_SET(childSocket, &rset);
        maxfdp1 = childSocket + 1;
        select(maxfdp1, &rset, NULL, NULL, &timer);
        
        //Either timeout or packet received
        if (FD_ISSET(childSocket, &rset))
        {
            //childServer details recevied
            n = recv(childSocket, msg, MAXLINE, 0);
            msg[n] = 0;
            #ifdef DEBUG
            printf("Final ack from client recevied\n");
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
                shouldResend = 1;
                #ifdef DEBUG
                printf("Final ack not received from client/Client unaware of child. Resending packet.\n");
                #endif
                goto resendFromBothParentAndChild;
            }
        }
    }//End of infinite loop

  
    //Receiving ack from client about childServer details
    //Recvfrom(childSocket, msg, MAXLINE, 0, NULL, NULL);
    printf("Handshake completed with client.\n");
    printf("Details Received :-\n");
    printf("Filename : %s, Receiver Window Size : %d", fileRequested, atoi(msg));
    printf("\n***************** ********* *******************\n");
    
    if( (send_file(fileRequested, sBufferSize, atoi(msg), childSocket, (SA *)p_cliaddr, sizeof(*p_cliaddr), p_cliaddr->sin_port, sendflags)) < 0)
    {
        close(childSocket);
        return -1;
    }
    close(childSocket);
    return 1;
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

void sig_chld(int signo) {
	pid_t pid;
	int stat;
	while ( (pid = waitpid(-1, &stat, WNOHANG)) > 0) {
		//childExists = 0;	
		printf("Client closed,deleting child pid : %d from table\n", pid);
		deleteFromTable(pid);
	}
}

int isLocal(char *sIP)
{
    int i,lastmatch,temp;
    struct in_addr serv_addr,my_addr,my_mask;

    lastmatch = NONLOCAL;
    if(inet_pton(AF_INET,sIP,&serv_addr) == -1)
    {
        printf("IP address invalid!\n");
        return(-1);
    }
    for(i=0;i<numIPs;i++)
    {
        if(inet_pton(AF_INET,allIPAddresses[i],&my_addr) == -1)
        {
            printf(" %dth address invalid!\n",i);
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
