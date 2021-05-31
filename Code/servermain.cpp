#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/time.h>

/* You will to add includes here */
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <mutex>
#include <map>
#include <math.h>
// Included to get the support library
#include <calcLib.h>

#include "protocol.h"

#define MaxBufferSize 1500
#define DEBUG

using namespace std;
/* Needs to be global, to be rechable by callback and main */
//int loopCount = 0;
//int terminate = 0;
int id = 0;
map<int, int> clientTimeRec; //record id and time
//mutex mutexLock;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
  if (sa->sa_family == AF_INET)
  {
    return &(((struct sockaddr_in *)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

/* Call back function, will be called when the SIGALRM is raised when the timer expires. */
void checkJobbList(int signum)
{
  //modify and check the clientTimeVec's id and time
  //mutexLock.lock();
  for (auto it = clientTimeRec.begin(); it != clientTimeRec.end();)
  {
    it->second++;
    if (it->second >= 10)
      it = clientTimeRec.erase(it);
    else
      it++;
  }
  return;
  //mutexLock.unlock();
}

//1. The servers should accept IP:port as the first command-line argument.
//2. The server must handle multiple clients, near-simultaneous.
//3. The server only supports protocol 1.0, i.e. calcMessage.{type=22, message=0, protocol=17, major_version=1, minor_version=0}.
//4. If a client has not replied to an assignment after 10s (or more), that job should be removed, and the client should be considered lost.
//5. If a lost client tries to provide a result, it should be rejected with an error.
//6. If a client tries to report a result but provides an incorrect ID it should be rejected. Hence, the server needs to track client IP, port, ID, and assignment.

//使用一个哈希表存储id和时间,使用信号量隔段时间中断来观察哈希表中的时间
int main(int argc, char *argv[])
{
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s hostname (%d)\n", argv[0], argc);
    exit(1);
  }

  char delim[] = ":";                      //分割符号:
  char *Desthost = strtok(argv[1], delim); //目的主机号
  char *Destport = strtok(NULL, delim);    //目的端口号
  if(Destport == NULL) return 0;

  int port = atoi(Destport);
#ifdef DEBUG
  printf("Host %s, and port %d.\n", Desthost, port);
#endif

  /* Do more magic */
  int sockfd;
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;
  //struct sockaddr_in *the_addr;
  char buf[MaxBufferSize];
  calcMessage message1;
  calcProtocol message2;
  //初始化部分message2
  message2.type = htons(1);
  message2.major_version = htons(1);
  message2.minor_version = htons(0);

  //初始化正确消息(OK)并改变字节序
  calcMessage okMessage;
  okMessage.type = htons(2);
  okMessage.message = htonl(1);
  okMessage.protocol = htons(17);
  okMessage.major_version = htons(1);
  okMessage.minor_version = htons(0);

  //初始化错误消息(NOT OK)并改变字节序
  calcMessage failMessage;
  failMessage.type = htons(2);
  failMessage.message = htonl(2);
  failMessage.protocol = htons(17);
  failMessage.major_version = htons(1);
  failMessage.minor_version = htons(0);

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; // set to AF_INET to force IPv4
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_flags = AI_PASSIVE; // use my IP

  if ((rv = getaddrinfo(Desthost, Destport, &hints, &servinfo)) != 0)
  {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
    return 1;
  }

  //socket() and bind()

  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
    {
      perror("udp server: socket");
      exit(1);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1)
    {
      close(sockfd);
      perror("udp server: bind");
      exit(1);
    }

    break;
  }

  if (p == NULL)
  {
    fprintf(stderr, "udp server: failed to bind socket\n");
    return 2;
  }

  //计时器，信号量相关
  /* 
     Prepare to setup a reoccurring event every 10s. If it_interval, or it_value is omitted, it will be a single alarm 10s after it has been set. 
  */
  struct itimerval alarmTime;

  /* Regiter a callback function, associated with the SIGALRM signal, which will be raised when the alarm goes of */
  signal(SIGALRM, checkJobbList);

  while (1)
  {
    alarmTime.it_interval.tv_sec = 1;
    alarmTime.it_interval.tv_usec = 0;
    alarmTime.it_value.tv_sec = 1;
    alarmTime.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &alarmTime, NULL); // Start/register the alarm.

    //获取客户端的地址
    struct sockaddr_storage their_addr;
    struct sockaddr_in *client_addr;
    socklen_t addr_len;

    //send() and receive()
    if ((numbytes = recvfrom(sockfd, buf, MaxBufferSize, 0, (struct sockaddr *)&their_addr, &addr_len)) == -1)
    {
      perror("receive error");
      exit(1);
    }
#ifdef DEBUG
    client_addr = (struct sockaddr_in *)&their_addr;
    char show_addr[INET6_ADDRSTRLEN];
    printf("-------------------------------------\n");
    printf("udp server reveives: from %s:%d ", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), show_addr, sizeof show_addr), ntohs(client_addr->sin_port));
#endif
    if (numbytes == 12)
    { //收到的是calcMessage
#ifdef DEBUG
      printf("receive a calcMessage\n");
#endif
      memcpy(&message1, buf, numbytes);
      //正确的初始化报文, type = 22, message = 0, protocol = 17, major_version = 1, minor_version = 0
      if (ntohs(message1.type) == 22 && ntohl(message1.message) == 0 && ntohs(message1.protocol) == 17 && ntohs(message1.major_version) == 1 && ntohs(message1.minor_version) == 0)
      {
        //如果是正确的报文就制造calcProtocol并发送，将id号存入clientTimeRec中（需要判断id是不是已经在clientTimeRec中，存在则设为0），id++
        char *operateType = randomType();
        if (strcmp(operateType, "add") == 0)
          message2.arith = 1;
        else if (strcmp(operateType, "sub") == 0)
          message2.arith = 2;
        else if (strcmp(operateType, "mul") == 0)
          message2.arith = 3;
        else if (strcmp(operateType, "div") == 0)
          message2.arith = 4;
        else if (strcmp(operateType, "fadd") == 0)
          message2.arith = 5;
        else if (strcmp(operateType, "fsub") == 0)
          message2.arith = 6;
        else if (strcmp(operateType, "fmul") == 0)
          message2.arith = 7;
        else if (strcmp(operateType, "fdiv") == 0)
          message2.arith = 8;
        if (message2.arith <= 4 && message2.arith >= 1)
        { //说明是浮点数
          message2.inValue1 = htonl(randomInt());
          message2.inValue2 = htonl(randomInt());
        }
        else
        {
          message2.flValue1 = randomFloat();
          message2.flValue2 = randomFloat();
        }
        message2.arith = htonl(message2.arith);
        message2.id = htonl(id);
        clientTimeRec.insert(make_pair(id, 0));
        id++;
        if ((numbytes = sendto(sockfd, (char *)&message2, sizeof(message2), 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
        {
          perror("send calcProtocol fail");
          exit(1);
        }
#ifdef DEBUG
        printf("send a calcProtocol\n");
        printf("the airth is:%d\n", ntohl(message2.arith));
        if (ntohl(message2.arith) >= 1 && ntohl(message2.arith) <= 4)
        {
          printf("the protocol's inValue1 is %d\n", ntohl(message2.inValue1));
          printf("the protocol's inValue2 is %d\n", ntohl(message2.inValue2));
        }
        else
        {
          printf("the protocol's fValue2 is %f\n", message2.flValue1);
          printf("the protocol's fValue2 is %f\n", message2.flValue2);
        }
#endif
      }
      else
      {
        //错误的报文或者其它
        if ((numbytes = sendto(sockfd, (char *)&failMessage, sizeof(failMessage), 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
        {
          perror("send Not Ok message fail");
          exit(1);
        }
      }
    }
    else if (numbytes == 50)
    { //收到的是calcProtocol
#ifdef DEBUG
      printf("reveive a calcProtocol\n");
#endif
      memcpy(&message2, buf, numbytes);
      if (!clientTimeRec.count(ntohl(message2.id)))
      { //判断id是否在record中，如果不在，发送相应消息（这个消息发什么？），进入下一个循环
        //发送error报文（Reject）
#ifdef DEBUG
        printf("The calcProtocol message is from a lost client or invalid client\n");
#endif
        if ((numbytes = sendto(sockfd, (char *)&failMessage, sizeof(failMessage), 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
        {
          perror("send Not Ok message fail");
          exit(1);
        }
        continue;
      }
      else
      { //如果在
        //判断计算结果正不正确
        //发送消息后清空在clientTimeRec中的相应id条目
        bool isEqual;
        switch (ntohl(message2.arith))
        {
        case 1:
          isEqual = (abs((int)ntohl(message2.inValue1) + (int)ntohl(message2.inValue2) - (int)ntohl(message2.inResult)) < 0.00001);
          break;
        case 2:
          isEqual = (abs((int)ntohl(message2.inValue1) - (int)ntohl(message2.inValue2) - (int)ntohl(message2.inResult)) < 0.00001);
          break;
        case 3:
          isEqual = (abs((int)ntohl(message2.inValue1) * (int)ntohl(message2.inValue2) - (int)ntohl(message2.inResult)) < 0.00001);
          break;
        case 4:
          isEqual = (abs((int)ntohl(message2.inValue1) / (int)ntohl(message2.inValue2) - (int)ntohl(message2.inResult)) < 0.00001);
          break;
        case 5:
          isEqual = (fabs(message2.flValue1 + message2.flValue2 - message2.flResult) < 0.0001);
          break;
        case 6:
          isEqual = (fabs(message2.flValue1 - message2.flValue2 - message2.flResult) < 0.0001);
          break;
        case 7:
          isEqual = (fabs(message2.flValue1 * message2.flValue2 - message2.flResult) < 0.0001);
          break;
        case 8:
          isEqual = (fabs(message2.flValue1 / message2.flValue2 - message2.flResult) < 0.0001);
          break;
        default:
          //差错处理
          break;
        }
        if (isEqual)
        {
#ifdef DEBUG
          printf("the result is right\n");
#endif
          //发送ok报文
          if ((numbytes = sendto(sockfd, (char *)&okMessage, sizeof(okMessage), 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
          {
            perror("send Ok message fail");
            exit(1);
          }
        }
        else
        {
#ifdef DEBUG
          printf("the result is wrong\n");
#endif
          //发送error报文（Reject）
          if ((numbytes = sendto(sockfd, (char *)&failMessage, sizeof(failMessage), 0, (struct sockaddr *)&their_addr, addr_len)) == -1)
          {
            perror("send Not Ok message fail");
            exit(1);
          }
        }
        //清空id
        auto it = clientTimeRec.find(ntohl(message2.id));
        //mutexLock.lock();
        clientTimeRec.erase(it);
        //mutexLock.unlock();
        continue;
      }
    }
    else
    {
      printf("unknown numbytes\n");
    }
  }

  //close()
  close(sockfd);
  printf("done.\n");
  return 0;
}
