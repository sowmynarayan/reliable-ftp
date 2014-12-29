#define NOTEXISTS -1
#define EXISTS 1
#define MAXRETRIES 10
#define SAMEHOST -1
#define NONLOCAL -2

#define SEGSIZE 500
#define RTT_RXTMIN      1000   /* min retransmit timeout value, ms */
#define RTT_RXTMAX      3000   /* max retransmit timeout value, ms */
#define RTT_MAXNREXMT   12   /* max #times to retransmit */
#define RTT_RTOCALC(ptr) (((ptr)->rtt_rttvar) + ((ptr)->rtt_srtt)>>3)

#define NODEBUG

int mean;

struct hdr
{
    int seq;
    int fin;
	int probe;
};

struct s_reply
{
    int ack;
    int windowSize;
};

//sendingBuffer Elements
struct senderWindowElem
{
    int seqNum;
    int noOfAcksReceived;
    int flagSent;
    char data[SEGSIZE];
    struct timeval timeSpent;
};
int sBufferSize = 0;
int lastSegSentInFlight = 0;
int lastProperSend = 0;

struct rtt_info {
    uint32_t     rtt_rtt;    /* most recent measured RTT, seconds */
    uint32_t     rtt_srtt;   /* smoothed RTT estimator, seconds */
    uint32_t     rtt_rttvar; /* smoothed mean deviation, seconds */
    uint32_t     rtt_rto;    /* current RTO to use, seconds */
    uint32_t     rtt_nrexmt; /* #times retransmitted: 0, 1, 2, ... */
    uint32_t     rtt_base;   /* #sec since 1/1/1970 at start */
};

//receivingBuffer Elements
struct receiverWindowElem
{
    int seqNum;
    char data[SEGSIZE];
    int flag; //0 - Not Received, 1 - Received, 2 - Received and is a fin
};
int receiverBufferSize = 0;
int lastProperRcv = 0;
int lastConsumed = 0;

/***Things pertaining to childTable used to keep track of the clients*****/
/***********************server is talking with****************************/ 

struct childTableStruct
{
    pid_t pid;
    struct sockaddr_in client;
    struct childTableStruct *next;
};

struct childTableStruct *head;

void addToTable(pid_t pid,struct sockaddr_in cliaddr)
{
    struct childTableStruct *temp;
    struct childTableStruct *newnode = 
        (struct childTableStruct*)malloc(sizeof(struct childTableStruct));

    newnode->pid = pid;
    newnode->client = cliaddr;
    if(head!=NULL)
    {
        temp=head;
        while(temp->next != NULL)
            temp=temp->next;
        temp->next=newnode;
    }
    else
    {
        head = newnode;
    }
    newnode->next=NULL;
}

void deleteFromTable(pid_t pid)
{
    struct childTableStruct *temp,*todelete;
    temp = head;
    if(pid == head->pid)
    {
        head=temp->next;
        todelete = temp;
    }
    else
    {
        while(temp->next !=NULL)
        {
            if(temp->next->pid == pid)
            {
                todelete = temp->next;
                temp->next = todelete->next;
                break;
            }
            temp = temp->next;
        }
    }
    todelete = NULL;
    free(todelete);
}

void printTable()
{
    struct childTableStruct *temp=head;
    int i=0;
    if(temp==NULL)
        return;
    while(temp != NULL)
    {
        printf("%d: pid:%d,addr:%s--->",i,temp->pid,Sock_ntop_host((SA*)&(temp->client),sizeof(struct sockaddr_in)) );
        temp=temp->next;
        i++;
    }
    printf("\n");
}

int exists(struct sockaddr_in cli)
{
    struct childTableStruct *temp=head;
    if(temp == NULL)
        return NOTEXISTS;
    while(temp != NULL)
    {
        if( ( strcmp( Sock_ntop_host((SA*)&(temp->client),sizeof(struct sockaddr_in)) , 
                        Sock_ntop_host((SA*)&(cli),sizeof(struct sockaddr_in)) ) == 0 )
             &&
            temp->client.sin_port == cli.sin_port
         )
            return EXISTS;
        temp = temp->next;
    }
    return NOTEXISTS;
}

/****End of things pertaining to childTable used to keep track of the*****/
/******************client server is talking with**************************/ 

/******** Functions for calculating RTO **********************************/

int rtt_minmax(int rto)
{
    if (rto < RTT_RXTMIN)
        rto = RTT_RXTMIN;
    else if (rto > RTT_RXTMAX)
        rto = RTT_RXTMAX;
    return(rto);
}

void rtt_init(struct rtt_info *ptr)
{
    struct timeval  tv;

    Gettimeofday(&tv, NULL);
    ptr->rtt_base = tv.tv_sec;      /* # sec since 1/1/1970 at start */

    ptr->rtt_rtt    = 0;
    ptr->rtt_srtt   = 0;
    ptr->rtt_rttvar = 3000;
    ptr->rtt_rto = rtt_minmax(RTT_RTOCALC(ptr));
    /* first RTO at (srtt + (4 * rttvar)) = 3 seconds */
}


uint32_t rtt_ts(struct rtt_info *ptr)
{
    uint32_t                ts;
    struct timeval  tv;

    if (gettimeofday(&tv, NULL) == -1) 
        printf("gettimeofday error: ");

    ts = (tv.tv_sec*1000) - ptr->rtt_base;
    return(ts);
}

void rtt_newpack(struct rtt_info *ptr)
{
    ptr->rtt_nrexmt = 0;
}

int rtt_start(struct rtt_info *ptr)
{
    //ptr->rtt_rto = ptr->rtt_rto;
    return((int) (ptr->rtt_rto));
}

int rtt_timeout(struct rtt_info *ptr)
{
    ptr->rtt_rto *= 2;              /* next RTO */
    ptr->rtt_rto = rtt_minmax(ptr->rtt_rto);

    if (++ptr->rtt_nrexmt > RTT_MAXNREXMT)
        return(-1);                     /* time to give up for this packet */
    return(0);
}

void rtt_stop(struct rtt_info *ptr, uint32_t measured)
{
    ptr->rtt_rtt = measured;
    ptr->rtt_rtt -= (ptr->rtt_srtt)>>3;
    ptr->rtt_srtt += ptr->rtt_rtt;

    if (ptr->rtt_rtt < 0) {
        ptr->rtt_rtt = -ptr->rtt_rtt;
    }

    ptr->rtt_rtt -= (ptr->rtt_rttvar)>>2;
    ptr->rtt_rttvar += ptr->rtt_rtt;
    ptr->rtt_rto = rtt_minmax(RTT_RTOCALC(ptr));
}

/*********** End of RTO calculation functions ***********************/
