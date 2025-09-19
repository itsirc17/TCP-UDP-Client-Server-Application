#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <netinet/tcp.h>

#include <iostream>

using namespace std;

#define DIE(assertion, call_description)                                       \
  do {                                                                         \
    if (assertion) {                                                           \
      fprintf(stderr, "(%s, %d): ", __FILE__, __LINE__);                       \
      perror(call_description);                                                \
      exit(EXIT_FAILURE);                                                      \
    }                                                                          \
  } while (0)

#define MSG_MAXSIZE 2048

int recv_all(int sockfd, void *buffer, size_t len) {
  size_t bytes_received = 0;
  size_t bytes_remaining = len;
  char *buff = (char*)buffer;

  while(bytes_remaining) {
    int bytes_received_now = recv(sockfd, buff + bytes_received, bytes_remaining, 0);

    DIE(bytes_received_now == -1, "recv - recv_all");
    if(bytes_received_now == 0){
      return 0;
    }
    
    bytes_received += bytes_received_now;
    bytes_remaining -= bytes_received_now;
  }

  return bytes_received;
}

int send_all(int sockfd, void *buffer, size_t len) {
  size_t bytes_sent = 0;
  size_t bytes_remaining = len;
  char *buff = (char*)buffer;

  while(bytes_remaining) {
    int bytes_sent_now = send(sockfd, buff + bytes_sent, bytes_remaining, 0);

    DIE(bytes_sent_now == -1, "send - send_all");
    if(bytes_sent_now == 0){
      return 0;
    }
    
    bytes_sent += bytes_sent_now;
    bytes_remaining -= bytes_sent_now;
  }

  return bytes_sent;
}

void run_client(int sockfd) {
    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO;
    pfds[0].events = POLLIN;

    pfds[1].fd = sockfd;
    pfds[1].events = POLLIN;

    while (1) {
        int rc = poll(pfds, 2, -1);
        if (rc < 0) {
            perror("poll");
            break;
        }

        char buffer[MSG_MAXSIZE + 1];
        memset(buffer, 0, MSG_MAXSIZE + 1);
        // Verificăm dacă există input de la tastatură
        if (pfds[0].revents & POLLIN) {
            fgets(buffer, sizeof(buffer), stdin);
            
            if(strncmp(buffer, "exit", 4) == 0){
              rc = send(sockfd, buffer, sizeof(buffer), 0);
              DIE(rc <= 0, "exit");
              return;
            }

            if(strncmp(buffer, "subscribe", 9) == 0){
              rc = send(sockfd, buffer, sizeof(buffer), 0);
              DIE(rc <= 0, "subscribe");
              cout << "Subscribed to topic " << buffer + 10;
            }

            if(strncmp(buffer, "unsubscribe", 11) == 0){
              rc = send(sockfd, buffer, sizeof(buffer), 0);
              DIE(rc <= 0, "unsubscribe");
              cout << "Unsubscribed from topic " << buffer + 12;
            }
        }

        // Verificăm dacă am primit un mesaj de la server
        if (pfds[1].revents & POLLIN) {
            // Intai citim lungimea mesajului
            uint32_t msg_len;
            int n = recv_all(sockfd, &msg_len, sizeof(msg_len));
            if (n <= 0) { // Serverul a inchis conexiunea
              return;
            }
            msg_len = ntohl(msg_len);

            // Acum ca avem lungimea, putem citii mesajul
            char buffer[MSG_MAXSIZE + 1];
            memset(buffer, 0, MSG_MAXSIZE + 1);
            n = recv_all(sockfd, buffer, msg_len);
            if (n <= 0) {
              return;
            }

            buffer[msg_len] = '\0';
            cout << buffer << endl;
        }
    }
}

int main(int argc, char *argv[]) {
  if (argc != 4) {
    fprintf(stderr, "\n Usage: %s <id> <ip> <port>\n", argv[0]);
    return 1;
  }

  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  // Parsam port-ul ca un numar
  uint16_t port;
  int rc = sscanf(argv[3], "%hu", &port);
  DIE(rc != 1, "Given port is invalid");

  // Obtinem un socket TCP pentru conectarea la server
  const int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  DIE(sockfd < 0, "socket");

  // Completăm in serv_addr adresa serverului, familia de adrese si portul
  // pentru conectare
  struct sockaddr_in serv_addr;

  memset(&serv_addr, 0, sizeof(struct sockaddr_in));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);
  rc = inet_pton(AF_INET, argv[2], &serv_addr.sin_addr.s_addr);
  DIE(rc <= 0, "inet_pton");

  // Ne conectăm la server
  rc = connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  int flag = 1;
  setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)); // Dezactivam Nagle
  DIE(rc < 0, "connect");

  // Trimitem ID ul
  send_all(sockfd, argv[1], sizeof(argv[1]));

  run_client(sockfd);

  // Inchidem conexiunea si socketul creat
  close(sockfd);

  return 0;
}
