#include <stdio.h>
#include <stdlib.h>
/* You will to add includes here */
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <unistd.h>
#include <signal.h>
// Included to get the support library
#include <calcLib.h>

#include "protocol.h"

#define DEBUG
//#define DEBUG1
#define MaxBufferSize 1500
//1. 13.53.76.30:5000
//2. 发送和接收message
//   calcMessage which contacts the server(type:22, message:0, protocol:17, major_version:1, minor_version:0)
//   calcProtocol message(包含要计算的信息)
//   calcMessage which to terminate(type=2, message=2, major_version=1,minor_version=0)
//3. 处理message(计算值并发送结果给服务器,就像TCP里面一样)
//4. 重传(2s一次,最多重传两次)
int main(int argc, char *argv[])
{
  //setbuf(stdout,NULL);//取消打印阻塞？
  /* Do magic */
  int sockfd;
  //addrinfo结构体
  struct addrinfo hints, *servinfo, *p;
  int rv;
  int numbytes;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s hostname (%d)\n", argv[0], argc);
    exit(1);
  }

  char delim[] = ":";                      //分割符号:
  char *Desthost = strtok(argv[1], delim); //目的主机号
  char *Destport = strtok(NULL, delim);    //目的端口号
  if(Destport == NULL) return 0;
  // *Desthost now points to a sting holding whatever came before the delimiter, ':'.
  // *Dstport points to whatever string came after the delimiter.

  int port = atoi(Destport);
#ifdef DEBUG
  printf("Host %s, and port %d.\n", Desthost, port);
#endif

  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;    // AF_INET , AF_INET6,地址族
  hints.ai_socktype = SOCK_DGRAM; //报文类型

  /*
    int getaddrinfo( const char *hostname, const char *service, const struct addrinfo *hints, struct addrinfo **result );
	  hostname:一个主机名或者地址串(IPv4的点分十进制串或者IPv6的16进制串)
    service：服务名可以是十进制的端口号，也可以是已定义的服务名称，如ftp、http等
    hints：可以是一个空指针，也可以是一个指向某个 addrinfo结构体的指针，调用者在这个结构中填入关于期望返回的信息类型的暗示。举例来说：如果指定的服务既支持TCP也支持UDP，那么调用者可以把hints结构中的ai_socktype成员设置成SOCK_DGRAM使得返回的仅仅是适用于数据报套接口的信息。
    result：本函数通过result指针参数返回一个指向addrinfo结构体链表的指针。
    返回值：0——成功，非0——出错
  */
  if ((rv = getaddrinfo(Desthost, Destport, &hints, &servinfo)) != 0)
  { //getaddrinfo函数
    perror("getaddrinfo fail");
    exit(1);
  }

  // loop through all the results and make a socket
  for (p = servinfo; p != NULL; p = p->ai_next)
  {
    //socket函数，第一个参数是地址的格式，第二个参数是报文类型，第三个参数是协议类型
    //函数返回文件（套接字）描述符，返回-1表示出错
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
    {
      perror("create socket fail"); //输出这段字符，同时还会输出错误描述
      exit(1);
    }
    break;
  }

  if (p == NULL)
  {
    fprintf(stderr, "talker: failed to create socket\n");
    exit(1);
  }

  /*
    sendto函数返回发送的字节数，无连接的发送数据报文的方式
	  第一个参数是目标套接字，第二个参数是要发送的报文，第三个参数是报文长度
	  第四个参数是Flag，第五个参数是目标主机地址，第六个参数是目标主机地址长度
    numbytes = sendto(sockfd, argv[q], strlen(argv[q]), 0, p->ai_addr, p->ai_addrlen)
	*/
  //初始化calcMessage
  calcMessage initializeMessage;
  initializeMessage.type = 22;
  initializeMessage.message = 0;
  initializeMessage.protocol = 17;
  initializeMessage.major_version = 1;
  initializeMessage.minor_version = 0;

  //改变字节序
  initializeMessage.type = htons(initializeMessage.type);
  initializeMessage.message = htonl(initializeMessage.message);
  initializeMessage.protocol = htons(initializeMessage.protocol);
  initializeMessage.major_version = htons(initializeMessage.major_version);
  initializeMessage.minor_version = htons(initializeMessage.minor_version);

  char messageBuf[50]; //buffer
  //memcpy(&initializeMessage, (char*)messageBuf, sizeof(initializeMessage));
  if ((numbytes = sendto(sockfd, (char *)&initializeMessage, sizeof(initializeMessage), 0, p->ai_addr, p->ai_addrlen)) == -1)
  {
    perror("send fail");
    exit(1);
  }
#ifdef DEBUG
    //获得本地地址信息
    char myAddress[20];

    struct sockaddr_in local_sin;
	  socklen_t local_sinlen = sizeof(local_sin);
	  getsockname(sockfd,(struct sockaddr*)&local_sin, &local_sinlen);

	  inet_ntop(local_sin.sin_family,&local_sin.sin_addr,myAddress,sizeof(myAddress));
    printf("local address %s:%d\n",myAddress,ntohs(local_sin.sin_port));
#endif
#ifdef DEBUG
  printf("send calcMessage\n");
#endif
  // #ifdef DEBUG
  //   //send colmessage's numbytes is: 12 (发送的calcMessage的长度)
  //   printf("send calcMessage's numbytes is: %d\n", numbytes);
  // #endif

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

  memset(&messageBuf, 0, sizeof(messageBuf));

  calcProtocol recvMessage1;
  calcMessage recvMessage2;

  timeval timeout; //定时变量
  fd_set rfd;      //读描述符
  int nRet;        //select返回值
  int maxfdp;
  //设置超时
  // timeout.tv_sec = 2;
  // timeout.tv_usec = 0;
  int retranTimes = 0; //重传次数
  //重传有两种情况
  int retranState = 0; //0表示重传初始化报文，1表示重传答案的报文

  while (true)
  {
    //设置超时时间
    memset(&messageBuf, 0, sizeof(messageBuf));
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    FD_ZERO(&rfd);        // 在使用之前总是要清空
    FD_SET(sockfd, &rfd); // 把sockfd放入要测试的描述符中
    maxfdp = sockfd + 1;
    nRet = select(maxfdp, &rfd, NULL, NULL, &timeout); //检测是否有套接口是否可读
    if (nRet == -1)
    { //错误
      printf("error\n");
    }
    else if (nRet == 0) //超时,重传
    {
      printf("timeout\n");
      if (retranTimes >= 2)
      { //重传两次,打印通知并退出
        printf("the server have no response\n");
        break;
      }
      if (retranState == 0) //重传的第一个状态为重传初始化报文
      {
        if ((numbytes = sendto(sockfd, (char *)&initializeMessage, sizeof(initializeMessage), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("send fail");
          exit(1);
        }
#ifdef DEBUG
        //send colmessage's numbytes is: 12 (发送的calcMessage的长度)
        printf("send calcMessage\n");
#endif
        retranTimes++;
        continue;
      }
      else if (retranState == 1) //重传的第二个状态为重传calcProtocol
      {
        if ((numbytes = sendto(sockfd, (char *)&recvMessage1, sizeof(recvMessage1), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("send fail");
          exit(1);
        }
#ifdef DEBUG
        //send colmessage's numbytes is: 12 (发送的calcMessage的长度)
        printf("send calcProtocol's numbytes is: %d\n", numbytes);
#endif
        retranTimes++;
        continue;
      }
      else
      {
        perror("retranState has no match value\n");
        exit(1);
      }
    }
    else if(FD_ISSET(sockfd,&rfd))//检测到有套接口可读
    {
      //接收消息,处理消息
      if ((numbytes = recvfrom(sockfd, messageBuf, sizeof(messageBuf), 0, p->ai_addr, &(p->ai_addrlen))) == -1)
      {
        perror("receive fail");
        exit(1);
      }
      // #ifdef DEBUG
      //       printf("receive number of bytes %d\n", numbytes);
      // #endif
      if (numbytes == 12) //差错报文
      {
        memcpy(&recvMessage2, messageBuf, numbytes);
        if (recvMessage2.type == failMessage.type && recvMessage2.protocol == failMessage.protocol && recvMessage2.message == failMessage.message && recvMessage2.major_version == failMessage.major_version && recvMessage2.minor_version == failMessage.minor_version)
        {
          printf("server sends Not OK, the result is wrong or maybe timeout happens\n");
        }
        else if (recvMessage2.type == okMessage.type && recvMessage2.protocol == okMessage.protocol && recvMessage2.message == okMessage.message && recvMessage2.major_version == okMessage.major_version && recvMessage2.minor_version == okMessage.minor_version)
        {
          printf("server sends OK, the result is right\n");
        }
        else
        {
          printf("type: %u\n", ntohs(recvMessage2.type));
          printf("message: %u\n", ntohl(recvMessage2.message));
          printf("protocol: %u\n", ntohs(recvMessage2.protocol));
          printf("major_version: %u\n", ntohs(recvMessage2.major_version));
          printf("minor_version: %u\n", ntohs(recvMessage2.minor_version));
          printf("don't receive OK or not OK\n");
        }
        break;
      }
      else if (numbytes == 50) //要求计算的报文
      {
        sleep(11);
#ifdef DEBUG
        printf("receive protocol message.\n");
#endif
        memcpy(&recvMessage1, messageBuf, numbytes);
        retranState = 1;
        //读取操作符号
        uint32_t arith = ntohl(recvMessage1.arith);
#ifdef DEBUG
        printf("airth is %u\n", arith);
#endif
        //计算操作
        if (arith >= 1 && arith <= 4) //整数
        {
          uint32_t inValue1 = ntohl(recvMessage1.inValue1);
          uint32_t inValue2 = ntohl(recvMessage1.inValue2);
          uint32_t result;
#ifdef DEBUG
          printf("inValue1 is: %d, inValue2 is: %d\n", inValue1, inValue2);
#endif
          if (arith == 1)
          {
            result = inValue1 + inValue2;
          }
          else if (arith == 2)
          {
            result = inValue1 - inValue2;
          }
          else if (arith == 3)
          {
            result = inValue1 * inValue2;
          }
          else if (arith == 4)
          {
            result = inValue1 / inValue2;
          }
          else
          {
            perror("arith value wrong.\n");
            exit(1);
          }
#ifdef DEBUG
          printf("the calculated intResult is: %d\n", result);
#endif
          recvMessage1.inResult = htonl(result);
        }
        else if (arith >= 5 && arith <= 8) //浮点数
        {
          //double和float类型不需要考虑字节序
          double flValue1 = recvMessage1.flValue1;
          double flValue2 = recvMessage1.flValue2;

#ifdef DEBUG
          printf("flValue1 is: %f, flValue2 is: %f\n", flValue1, flValue2);
#endif

          double fResult;
          if (arith == 5)
          {
            fResult = flValue1 + flValue2;
          }
          else if (arith == 6)
          {
            fResult = flValue1 - flValue2;
          }
          else if (arith == 7)
          {
            fResult = flValue1 * flValue2;
          }
          else if (arith == 8)
          {
            fResult = flValue1 / flValue2;
          }
          else
          {
            perror("arith value wrong.\n");
            exit(1);
          }
#ifdef DEBUG
          printf("the calculated flResult is: %f\n", fResult);
#endif
          recvMessage1.flResult = fResult;
        }
        else
        {
          perror("arith value wrong.\n");
          exit(1);
        }
        //发送,但是在发送前要改变type的值
        recvMessage1.type = 2;
        recvMessage1.type = htons(recvMessage1.type);

        // printf("sleep now\n");

        if ((numbytes = sendto(sockfd, (char *)&recvMessage1, sizeof(recvMessage1), 0, p->ai_addr, p->ai_addrlen)) == -1)
        {
          perror("send fail");
          exit(1);
        }
        continue;
      }
    }
    else{
      printf("get wrong socket info\n");
    }
  }

  freeaddrinfo(servinfo);
  close(sockfd); //关闭套接字

  return 0;
}
