#include <pthread.h>
#include <math.h>

void recv_file(int,SA*,socklen_t,float,int);
void *consumer(void *);
pthread_mutex_t counter_mutex = PTHREAD_MUTEX_INITIALIZER;

//If no items have been consumed 12 times, quit the client
volatile int noneConsumed = 0;

void recv_file(int sockfd, SA *pservaddr, socklen_t serverlen, float probablity, int seed)
{
    int n = 0, toDrop, lastSeq = -1, whereToPut=0, maxfd;
    socklen_t   len;
    char mesg[MAXLINE],data[SEGSIZE],ack[SEGSIZE];
    pthread_t tid; 
    struct msghdr msgrecv = {}, msgsend = {};
    struct s_reply reply;
    struct iovec iovrecv[2], iovsend[1];
    struct receiverWindowElem rBuffer[receiverBufferSize];
    struct sockaddr_in *p_dropSocket;
    struct timeval rem_time;
    socklen_t dropSockLen = sizeof(struct sockaddr_in);
    fd_set rset;
    FD_ZERO(&rset);
    rem_time.tv_sec = 3;
    rem_time.tv_usec = 0;

    struct hdr mes_header;
    bzero(&msgrecv,sizeof(msgrecv));
    bzero(&rBuffer,receiverBufferSize * sizeof(struct receiverWindowElem));
    //Initialize the randomizer
    srand48(seed);
    
    pthread_create(&tid, NULL, &consumer, rBuffer);
    
    len = serverlen;
    msgrecv.msg_name = NULL;
    msgrecv.msg_namelen = 0;
    msgrecv.msg_iov = iovrecv;
    msgrecv.msg_iovlen = 2;
    
    msgsend.msg_name = NULL;
    msgsend.msg_namelen = 0;
    msgsend.msg_iov = iovsend;
    msgsend.msg_iovlen = 1;
    
    //Receiving data from Server
    do
    {
		iovrecv[0].iov_base = (char *)&mes_header;
		iovrecv[0].iov_len = sizeof(struct hdr);
		//strcpy(data, "");
		iovrecv[1].iov_base = data;
		iovrecv[1].iov_len = SEGSIZE;
		
        FD_SET(sockfd,&rset);
        maxfd = sockfd + 1;
        if( (select(maxfd,&rset,NULL,NULL,&rem_time)) < 0)
        {
            if(errno == EINTR)
                continue;
            else
            {
                printf("Select error, exitting program! Errno: %s", strerror(errno));
                exit(1);
            }
        }

        if(FD_ISSET(sockfd,&rset))
        {
            (drand48() < probablity) ? (toDrop = 1) : (toDrop = 0) ;
            if(toDrop == 1)
            {
                // Receive message on a dummy socket to simulate drop
                n = recvfrom(sockfd, mesg, MAXLINE, 0, (SA *)(p_dropSocket), &dropSockLen);
                printf("Data Segment dropped!\n");
                strcpy(mesg,"");
                continue;
            }

            if( (n = recvmsg(sockfd, &msgrecv, 0)) < 0 )
            {
                printf("Recvmsg error, quitting the program! Errno: %s\n", strerror(errno));
                exit(1);
            }
        }
        else
        {
            if(noneConsumed == 12)
            {
                printf("No data receieved from server for some time, quitting program!\n");
                break;
            }
            else
                continue;
        }
		if(mes_header.probe == 1)
		{
			//Send previous ack
			reply.windowSize = receiverBufferSize - lastProperRcv + lastConsumed;
            #ifdef DEBUG
    			printf("Replying to probe message, %d | %d | %d\n", receiverBufferSize, lastProperRcv, lastConsumed);
            #endif
			printf("Replying to probe message\tAdvertized Window: %d\n", reply.windowSize);
			if( ( n = sendmsg(sockfd, &msgsend, 0)) < 0)
			{
				printf("Sendmsg error,exitting the program! Ernno: %s", strerror(errno));
				return;
			}
			
		}
		else
		{
			printf("Received Message: Seq:%d | Fin:%d\n",mes_header.seq, mes_header.fin);
			
			whereToPut = mes_header.seq - lastConsumed -1;

			//Store received message in buffer
			pthread_mutex_lock(&counter_mutex);
			strcpy(rBuffer[whereToPut].data, data);
			rBuffer[whereToPut].seqNum = mes_header.seq;
			rBuffer[whereToPut].flag = 1;
			lastProperRcv = lPRCalculate(rBuffer);
			
			//Sending ack
			reply.ack = lastProperRcv + 1;
			reply.windowSize = receiverBufferSize - lastProperRcv + lastConsumed;
			
			iovsend[0].iov_base = (char *)& (reply);
			iovsend[0].iov_len = SEGSIZE;
			
			if(mes_header.fin == 1) 
			{
                #ifdef DEBUG
				    printf("Found fin, setting flag to 2 for Seq : %d\n", mes_header.seq);
                #endif
				rBuffer[whereToPut].flag = 2;
				lastSeq = mes_header.seq;
			}
			
			//Actual sending of ack
			(drand48() < probablity) ? (toDrop = 1) : (toDrop = 0) ;
			if(toDrop == 1 && mes_header.fin != 1)
			{
				printf("Dropping Ack %d\n",reply.ack);
			} 
			else
            {
                if( ( n = sendmsg(sockfd, &msgsend, 0)) < 0)
                {
                    printf("Sendmsg error,quitting the program! Errno: %s", strerror(errno));
                    return;
                }
                else
                {
                    printf("Sending acknowledgement: Ack :%d | Advertized window:%d\n", reply.ack, reply.windowSize);
                    if ( lastSeq == reply.ack -1 )
                    {
                        printf("Sending ack for last segment !!\n");
                        pthread_mutex_unlock(&counter_mutex);
                        break;
                    }
                }
            }
			
		}
        pthread_mutex_unlock(&counter_mutex);
		if( noneConsumed == 12)
			break;

    } while(n > 0);

	pthread_join(tid, NULL);
    if(noneConsumed < 12)
        printf("\n*********** FILE TRANSFER COMPLETE!! DOWNLOADED AT ./file_dl.txt **********\n");
    else
        printf("\n No data received from server, giving up after 12 retries!!Partially downloaded file at file_dl.txt\n");
}

void *consumer(void *arg)
{
    #ifdef DEBUG
        printf("Consumer thread started\n");
    #endif

    int i, j, sleep_value;
    int finReceived = 0;
	struct receiverWindowElem * rBuffer = ( struct receiverWindowElem *) arg;
    //pthread_detach(pthread_self());	
    FILE *fp = fopen("file_dl.txt", "wb");
	int prevLastComsumed = 0;
	while (finReceived == 0) 
    {
        sleep_value = (-1)*mean*log(drand48())  * 1000;
        usleep(sleep_value);
		pthread_mutex_lock(&counter_mutex);
        #ifdef DEBUG
			
            printf("Consumer slept for %d, awakens now and lock obtained\n", sleep_value);
			
			for(i = 0; i < receiverBufferSize; i++)
			{
				printf("%d | %d; ", rBuffer[i].seqNum, rBuffer[i].flag);
			}
			printf("\n");
			
        #endif
		
		//Writing to file
        for(i = 0; i < receiverBufferSize; i++)
        {
            if(rBuffer[i].flag == 0)
                break;
            fprintf(fp,"%s", rBuffer[i].data);
            if(rBuffer[i].flag == 2)
            {
                finReceived = 1;
                #ifdef DEBUG
                    printf("Fin received, should close thread\n");
                #endif
                break;
            }
        }

        lastConsumed += i;
        if(finReceived != 1)
        {
            for(j=0; j<receiverBufferSize; j++)
            {
                if( (j+i) >= receiverBufferSize)
                {
                    bzero(&rBuffer[j], sizeof(struct receiverWindowElem));    
                }
                else
                {
                    rBuffer[j].seqNum = rBuffer[j+i].seqNum;
                    rBuffer[j+i].seqNum = 0;
                    strcpy(rBuffer[j].data, rBuffer[j+i].data);
                    strcpy(rBuffer[j+i].data, "");
                    rBuffer[j].flag = rBuffer[j+i].flag;
                    rBuffer[j+i].flag = 0;
                }
            }
        }
		if(prevLastComsumed == lastConsumed)
		{
			printf("Consumer woke, no new data consumed\n");
			noneConsumed ++;
		}
		else
		{
			printf("Consumer woke, consumed %d segments\n", (lastConsumed - prevLastComsumed) );
			noneConsumed = 0;
		}
		prevLastComsumed = lastConsumed;
		if(noneConsumed == 120)
			break;
        #ifdef DEBUG
            printf("Consumer sleeps, lock released\n");
			
			for(i = 0; i < receiverBufferSize; i++)
			{
				printf("%d | %d; ", rBuffer[i].seqNum, rBuffer[i].flag);
			}
			printf("\n");
			
        #endif
        pthread_mutex_unlock(&counter_mutex);
            
    }//End of while

    fclose(fp);
    #ifdef DEBUG
        printf("Consumer ends\n");
    #endif
}

int lPRCalculate(struct receiverWindowElem * rBuffer)
{
    int i, result = 0;
	for(i = 0; i < receiverBufferSize; i++)
    {
		if(rBuffer[i].flag == 0)
            break;
        result++;
    }
	return lastConsumed + result;
}
