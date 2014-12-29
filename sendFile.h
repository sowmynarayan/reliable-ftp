#include<string.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<unistd.h>
#include<time.h>
#include<signal.h>

int send_file(char*, int, int, int, const SA*, socklen_t, int, int);
void correctSBuffer(struct senderWindowElem * , int );
static void sig_alrm(int);
void zeroFlag(struct senderWindowElem * , int , int, int);

int pipefd[2],sendsize;
static struct rtt_info rttinfo;
static int rttinit = 0;

//Parameters related to Congestion Control

int ackReceivedPerPhase = 0;
int prevPhaseAcks = 0;
int sentPerPhase = 0;
int cwin = 1;
int ssthresh = 1;
//int ssGrowth = 1;

FILE *fp;
int noOfMsgsToSend, noOfMsgSent = 0, noOfMsgPutInBuffer = 0, filesize;

int send_file(char* filename, int sBufferSize, int rBufferSize, int sockfd, const SA *pcliaddr, socklen_t clilen, int cliport, int sendflags)
{
    int n, flags, numsecs, totalSegs;
    int i,ctr=1,diff, pick;
	char temp[MAXLINE];
    int currRecWindow = rBufferSize, actualSends = 0; //For flow control
    int whereToPut = 0;
    struct msghdr msgsend, msgrecv = {};
	struct iovec iovsend[2], iovrecv[1];
	struct hdr mes_header;
    struct stat buf;
    struct s_reply reply; 
    struct itimerval it_val;
    sigset_t sigset_alrm;
    int num_msgs,maxfdp;
	int deadLock = 0, pTimer = 3;
	struct timeval sysTime, tripTime, zeroTime;
	int timeForAck;
	struct sockaddr_in *p_dropSocket;
	socklen_t dropSockLen = sizeof(struct sockaddr_in);
    fd_set rset;
	
    if(rttinit == 0)
    {
        rtt_init(&rttinfo);
        rttinit = 1;
    }
    #ifdef DEBUG
        printf("Calling sendFile for file : %s, sBufferSize : %d\n", filename, sBufferSize);
    #endif

    //Creating sender window
    struct senderWindowElem sBuffer[sBufferSize];
	ssthresh = rBufferSize;
    bzero(&sBuffer, sBufferSize * sizeof(struct senderWindowElem));
	bzero(&msgrecv, sizeof(struct msghdr));
    lastSegSentInFlight = 0;
	
	msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 1;
    iovrecv[0].iov_base = (char *)& (reply);
    iovrecv[0].iov_len = SEGSIZE;

    if( (flags = fcntl (sockfd, F_GETFL, 0)) < 0)
        fprintf(stderr,"F_GETFL error for sockfd[i]");
    flags |= O_NONBLOCK;
    if( fcntl (sockfd, F_SETFL, flags) < 0)
        fprintf(stderr,"F_SETFL error for sockfd[i]");

	if( (fp=fopen(filename,"rb")) == NULL )
    {
        printf("Requested file does not exist! Quitting the program.\n");
        return;
    }
    fstat(fileno(fp),&buf);
    filesize = buf.st_size;

    /* Number of segments num_msgs is ceil(filesize/SEGSIZE)*/
    noOfMsgsToSend = (int) (filesize / SEGSIZE);
    if((filesize % SEGSIZE) != 0)
        noOfMsgsToSend++;
	num_msgs = noOfMsgsToSend;
	
	//Testing correct buffer
	correctSBuffer(sBuffer, sBufferSize);
	//End of test
	
    numsecs = 10 * rtt_start(&rttinfo);
    it_val.it_value.tv_sec =  (int) numsecs / 1000 ;
    it_val.it_value.tv_usec = (numsecs *1000) / 1000000;
    it_val.it_interval.tv_sec = 0;
    it_val.it_interval.tv_usec = 0;
	setitimer(ITIMER_REAL, &it_val, NULL);

    Pipe(pipefd);
    maxfdp = pipefd[0] + 1; 
    FD_ZERO(&rset);
    Signal(SIGALRM, sig_alrm);
    rtt_newpack(&rttinfo);
    
    totalSegs = num_msgs;
    #ifdef DEBUG
        printf("Before while, noOfMsgsToSend : %d, noOfMsgSent :%d, lPS : %d\n", noOfMsgsToSend, noOfMsgSent, lastProperSend);
		printf("CC Parameters : msgsSent : %d | acksRPP - %d | cwin - %d | ssthresh - %d\n", sentPerPhase, ackReceivedPerPhase, cwin, ssthresh);
		printf("Start- actualSends : %d, currRecWindow : %d,lPS : %d \n",actualSends, currRecWindow, lastProperSend);
    #endif
	
    while(lastProperSend != noOfMsgsToSend ) 
    {
        FD_SET(pipefd[0],&rset);
        zeroTime.tv_sec = 0;
        zeroTime.tv_usec = 0;
        if( (n = select(maxfdp, &rset, NULL, NULL, &zeroTime)) < 0)
        {
            if(errno == EINTR)
                continue;
            else
            {
                printf("Error in select, quitting!\n");
                return(-1);
            }
        }
        if(FD_ISSET(pipefd[0], &rset))
        {
            int n;
            Read(pipefd[0], &n, 1);
            if(rtt_timeout(&rttinfo) < 0)
            {
                printf("No response from client, giving up!\n");
                rttinit = 0;
                errno = ETIMEDOUT;
                return(-1);
            }

            numsecs = rttinfo.rtt_rto;
            printf("Message timed out! Resending data segment for %dth time to client %s:%d.\n",
                    rttinfo.rtt_nrexmt,Sock_ntop_host((SA *)pcliaddr, sizeof(*pcliaddr)),cliport);
            it_val.it_value.tv_sec =  (int) numsecs / 1000 ;
            it_val.it_value.tv_usec = (numsecs *1000) / 1000000;
            it_val.it_interval.tv_sec = 0;
            it_val.it_interval.tv_usec = 0;

            setitimer(ITIMER_REAL, &it_val, NULL);
            actualSends = lastProperSend;
            noOfMsgSent = lastProperSend;
			sentPerPhase = ackReceivedPerPhase;
			ssthresh = ssthresh / 2;
			if(ssthresh == 0)
				ssthresh = 1;
            zeroFlag(sBuffer, sBufferSize, 1,0);
            if(actualSends == 0)
                sBuffer[0].flagSent = 0;
            continue;
        }
        if(deadLock == 1)
        {
            bzero(&msgsend,sizeof(msgsend));
            msgsend.msg_name = NULL;
            msgsend.msg_namelen = 0;
            msgsend.msg_iov = iovsend;
            msgsend.msg_iovlen = 2;

            iovsend[0].iov_base = (char *)& (mes_header);
            iovsend[0].iov_len = sizeof(struct hdr);
            iovsend[1].iov_base = "";
            iovsend[1].iov_len = SEGSIZE;

            mes_header.seq = 0;
            mes_header.fin = 0;
            mes_header.probe = 1;
            printf("Sending probe message to client %s:%d\n",
                    Sock_ntop_host((SA *)pcliaddr, sizeof(*pcliaddr)),cliport);
            #ifdef DEBUG
            printf("Sending probe message,actualSends : %d, currRecWindow : %d \n",actualSends, currRecWindow);
            #endif
            if( (sendmsg(sockfd, &msgsend, sendflags)) <= 0 )
            {
                printf("Sendmsg error, exitting! Errno: %s\n", strerror(errno));
                return(-1);
            }
            sleep(pTimer);
            mes_header.probe = 1;
        }

        //Send when following conditions are met
        // There are messages left to send
        // There is space at receiverBuffer
        // Something to do with Congestion Control

        else if( actualSends < currRecWindow && sentPerPhase < cwin)
        {
            if( (pick = pickUp(sBuffer, sBufferSize)) < 0)
            {
                //All elements in sBuffer sent
                #ifdef DEBUG
                    printf("Sending buffer is full\n");
                #endif
            }
            else
            {

                printf("Sending Seq : %d to client %s:%d\n",
                        sBuffer[pick].seqNum,Sock_ntop_host((SA *)pcliaddr, sizeof(*pcliaddr)),cliport);

                //Construct message from sBuffer[pick] to send
                bzero(&msgsend,sizeof(msgsend));
                msgsend.msg_name = NULL;
                msgsend.msg_namelen = 0;
                msgsend.msg_iov = iovsend;
                msgsend.msg_iovlen = 2;

                mes_header.seq = sBuffer[pick].seqNum;
                mes_header.probe = 0;
                if(sBuffer[pick].seqNum == noOfMsgsToSend)
                    mes_header.fin = 1;
                else
                    mes_header.fin = 0;

                iovsend[0].iov_base = (char *)& (mes_header);
                iovsend[0].iov_len = sizeof(struct hdr);
                iovsend[1].iov_base = sBuffer[pick].data;
                iovsend[1].iov_len = SEGSIZE;

                //Sending to client
                if( (sendmsg(sockfd, &msgsend, sendflags)) <= 0 )
                {
                    printf("Error sending packet,quitting! Errno: %s\n", strerror(errno));
                    return(-1);
                }
                else
                {
					sentPerPhase++;
                    printf("cwin - %d | ssthresh - %d\n", cwin, ssthresh);
                    #ifdef DEBUG
					    printf("CC Parameters : msgsSent : %d | acksRPP - %d | cwin - %d | ssthresh - %d\n", 
                                sentPerPhase, ackReceivedPerPhase, cwin, ssthresh);
    					printf("Start- actualSends : %d, currRecWindow : %d,lPS : %d \n",actualSends, currRecWindow, lastProperSend);
					#endif

					sBuffer[pick].flagSent++;
                    gettimeofday(&(sBuffer[pick].timeSpent), NULL);
                    noOfMsgSent++;

                    //Flow Control variable increment
                    actualSends++;

					#ifdef DEBUG
                        printf("Sent :- Seq Num : %d, Time : %d\n", 
                          sBuffer[pick].seqNum, sBuffer[pick].timeSpent.tv_sec);
					    printf("CC Parameters : msgsSent : %d | acksRPP - %d | cwin - %d | ssthresh - %d\n", 
                          sentPerPhase, ackReceivedPerPhase, cwin, ssthresh);
    					printf("Start- actualSends : %d, currRecWindow : %d,lPS : %d \n",
                          actualSends, currRecWindow, lastProperSend);
					#endif
                }
            }//End of send to client else part

        }//End of if all messages sent
		
		msgrecv.msg_name = NULL;
		msgrecv.msg_namelen = 0;
		msgrecv.msg_iov = iovrecv;
		msgrecv.msg_iovlen = 1;
		iovrecv[0].iov_base = (char *)& (reply);
		iovrecv[0].iov_len = SEGSIZE;

        //Receive ack from client
        if( (n = recvmsg(sockfd, &msgrecv, 0)) > 0)
        {
            
			ackReceivedPerPhase = 0;
			ackReceivedPerPhase = ( reply.ack - 1) - prevPhaseAcks;
			
            lastProperSend = reply.ack - 1;
            if(actualSends < (reply.ack - 1))
                actualSends = reply.ack - 1;
            correctSBuffer(sBuffer, sBufferSize);

            //Last ack-ed element will be left shifted to 0th position by above function
            sBuffer[0].noOfAcksReceived++;
            sBuffer[0].flagSent = 1;

            //Setting flow control parameters
            currRecWindow = reply.ack + reply.windowSize - 1;
            printf("Received ack %d, %dth time from client %s:%d, Advertized window: %d \n", 
                reply.ack, sBuffer[0].noOfAcksReceived, Sock_ntop_host((SA *)pcliaddr, sizeof(*pcliaddr)),cliport,
                reply.windowSize);
				
			//printf("cwin - %d | ssthresh - %d\n",  cwin, ssthresh);
            #ifdef DEBUG
    			printf("Start- actualSends : %d, currRecWindow : %d,lPS : %d \n",actualSends, currRecWindow, lastProperSend);
            #endif

            if(reply.windowSize == 0)
            {
                printf("Receiver window locked!\n");
                deadLock = 1;
            }
            else
            {
                deadLock = 0;
            }
            if(sBuffer[0].noOfAcksReceived >= 3)
            {
                printf("Three duplicate acks have been received !\n");	
				sentPerPhase = ackReceivedPerPhase;
				ssthresh = ssthresh / 2;
				if(ssthresh == 0)
					ssthresh = 1;
                zeroFlag(sBuffer, sBufferSize, 0, 1);
                if(reply.ack == 1)
                    sBuffer[0].flagSent = 0;
                sBuffer[0].noOfAcksReceived == 0;
				printf("cwin - %d | ssthresh - %d\n", cwin, ssthresh);
                #ifdef DEBUG
    				printf("Start- actualSends : %d, currRecWindow : %d,lPS : %d \n",actualSends, currRecWindow, lastProperSend);
                #endif
                continue;
            }


            //Set alarm to zero, recalculate RTO and reset alarm
            if(sBuffer[0].noOfAcksReceived == 1)
            {
                it_val.it_value.tv_sec = 0;
                it_val.it_value.tv_usec = 0;
                setitimer(ITIMER_REAL, &it_val, NULL);

                gettimeofday(&sysTime, NULL);
                timeval_subtract(&tripTime, sysTime, sBuffer[0].timeSpent);
                rtt_stop(&rttinfo,(tripTime.tv_sec * 1000 + tripTime.tv_usec/1000));
            }
            if(deadLock == 0)
            {
                numsecs = rttinfo.rtt_rto;
                it_val.it_value.tv_sec =  (int) numsecs / 1000 ;
                it_val.it_value.tv_usec = (numsecs *1000) / 1000000;
                it_val.it_interval.tv_sec = 0;
                it_val.it_interval.tv_usec = 0;
                setitimer(ITIMER_REAL, &it_val, NULL);
                rtt_newpack(&rttinfo);
            }

        }
        else if(n < 0)
        {
            if(errno == 97)
			{
				//deadLock = 1;
				//printf("1 - Read %d bytes of reply, Error ? %d %s\n", n, errno, strerror(errno));
				//n = recv(sockfd, temp, MAXLINE, 0);//, (SA *)(p_dropSocket), &dropSockLen);
				//printf("2 - Read %d bytes of reply, Error ? %d %s\n", n, errno, strerror(errno));
			}
            //sleep(5);
            //return(-1);
        }
		if( ackReceivedPerPhase == cwin )
		{
            #ifdef DEBUG
    			printf("Congestion Control Next Phase !!!!!\n");
            #endif
			if( cwin >= ssthresh)
				cwin = cwin++;
			else
				cwin *= 2;
			sentPerPhase = 0;
			prevPhaseAcks = prevPhaseAcks + ackReceivedPerPhase;
			ackReceivedPerPhase = 0;
			printf("cwin - %d | ssthresh - %d\n", cwin, ssthresh);
			
		}
		
    }//End of while
	
    printf("Done sending file!\n");
    fclose(fp);
    return 0;
}


static void sig_alrm(int signo)
{
    Write(pipefd[1], "", 1);
    return;
}

void correctSBuffer(struct senderWindowElem * sBuffer, int sBufferSize)
{
    int i=0, j=0, k=0;
	char data[SEGSIZE];
	while ( (lastProperSend > 0) && (sBuffer[i].seqNum != lastProperSend) )
    { i++; }

    for(j=0; j<sBufferSize; j++)
    {
		if( ((i+j) < sBufferSize) && sBuffer[j+i].seqNum!= 0 )
        {
			#ifdef DEBUG
				//printf("Moving %dth element to %d : %d Seq\n", j+i, j, sBuffer[j+i].seqNum);
			#endif
            sBuffer[j].seqNum = sBuffer[j+i].seqNum;
            sBuffer[j].noOfAcksReceived = sBuffer[j+i].noOfAcksReceived;
			sBuffer[j].flagSent = sBuffer[j+i].flagSent;
			//strcpy(sBuffer[j].data, "");
            strcpy(sBuffer[j].data,sBuffer[j+i].data);
            sBuffer[j].timeSpent = sBuffer[j+i].timeSpent;
        }
        else if (noOfMsgPutInBuffer == noOfMsgsToSend)
		{
			bzero(&sBuffer[j], sizeof(struct senderWindowElem));
		}
		else
        {
			//Read from file and fill into msgsend
			sBuffer[j].seqNum = ++noOfMsgPutInBuffer;
			sBuffer[j].noOfAcksReceived = 0;
			sBuffer[j].flagSent = 0;
			strcpy(sBuffer[j].data, "");
			if(noOfMsgPutInBuffer == noOfMsgsToSend)
			{
				sendsize = filesize % SEGSIZE;
				fread(data,sendsize,1,fp);
                data[sendsize] = 0;
			}
			else
			{
				sendsize = SEGSIZE;
				fread(data,1,sendsize,fp);
			}
			//Put msgsend into sBuffer
			strcpy(sBuffer[j].data, data);
			
		}
		#ifdef DEBUG
		//printf("Filled value for %d\n",j);
		//printf("%d | %s\n", sBuffer[j].seqNum, data);
		#endif
    }//End of for
	
	#ifdef DEBUG
		/*
		printf("End of correctSBuffer\n");
		for(j=0; j<sBufferSize; j++)
			printf("%d | %d | %d\n", sBuffer[j].seqNum, sBuffer[j].noOfAcksReceived, sBuffer[j].flagSent);
		*/
	#endif
	
}

int pickUp(struct senderWindowElem * sBuffer, int sBufferSize)
{
	int i=0;
	int j=0;
	
    /*
	printf("pickUp\n");
	for(j=0; j<sBufferSize; j++)
		printf("%d | %d | %d\n", sBuffer[j].seqNum, sBuffer[j].noOfAcksReceived, sBuffer[j].flagSent);
	printf("\n");
    */
	for(i=0; i<sBufferSize; i++)
	{
		if(sBuffer[i].flagSent == 0)
		{
			return i;
		}
	}
	return -1;
}

void zeroFlag(struct senderWindowElem * sBuffer, int sBufferSize, int sigTimeOut, int segDupAcks)
{
	int i;
	int j;
	
	#ifdef DEBUG
	for(j=0; j<sBufferSize; j++)
		printf("%d | %d | %d\n", sBuffer[j].seqNum, sBuffer[j].noOfAcksReceived, sBuffer[j].flagSent);
	printf("\n");
	#endif
	
	if(sigTimeOut == 1 || segDupAcks == 1)
		i = 1;
	for(; i < sBufferSize; i++)
	{
		sBuffer[i].flagSent = 0;
	}
	return;
}
 
int timeval_subtract (struct timeval *result, struct timeval *x,struct timeval  *y)  
{  
    /* Perform the carry for the later subtraction by updating y. */  
    if (x->tv_usec < y->tv_usec) {  
        int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;  
        y->tv_usec -= 1000000 * nsec;  
        y->tv_sec += nsec;  
    }  
    if (x->tv_usec - y->tv_usec > 1000000) {  
        int nsec = (y->tv_usec - x->tv_usec) / 1000000;  
        y->tv_usec += 1000000 * nsec;  
        y->tv_sec -= nsec;  
    }  

    /* Compute the time remaining to wait.
       tv_usec is certainly positive. */  
    result->tv_sec = x->tv_sec - y->tv_sec;  
    result->tv_usec = x->tv_usec - y->tv_usec;  

    /* Return 1 if result is negative. */  
    return x->tv_sec < y->tv_sec;  
}
