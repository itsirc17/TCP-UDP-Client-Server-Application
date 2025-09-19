#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <math.h>

#include <netinet/tcp.h>

#include <unordered_map>
#include <iostream>

using namespace std;

#define MAX_CONNECTIONS 32

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
  char *buff = (char* )buffer;

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

struct __attribute__((packed)) Message{
  char topic[50];
  uint8_t type;
  char payload[1500];
};

struct Connected{
  bool isConnected;
  int socket;
};

void run_connection(int socketTCP, int socketUDP) {
  struct pollfd poll_fds[MAX_CONNECTIONS];

  poll_fds[0].fd = STDIN_FILENO;
  poll_fds[0].events = POLLIN;

  int num_sockets = 3;
  int rc;

  // Setam socket-ul socketTCP pentru ascultare
  rc = listen(socketTCP, MAX_CONNECTIONS);
  DIE(rc < 0, "listen socketTCP");

  // Adaugam noul file descriptor (socketul pe care se asculta conexiuni) in
  // multimea poll_fds
  poll_fds[1].fd = socketTCP;
  poll_fds[1].events = POLLIN;

  poll_fds[2].fd = socketUDP;
  poll_fds[2].events = POLLIN;

  // Hashmap uri
  unordered_map <int, string> socketToIdMap;
  unordered_map <string, Connected> connected;
  unordered_multimap <string, string> topicToIdMap;

  while (1) {
    // Asteptam sa primim ceva pe unul dintre cei num_sockets socketi
    rc = poll(poll_fds, num_sockets, -1);
    DIE(rc < 0, "poll");

    char buffer[MSG_MAXSIZE + 1];
    memset(buffer, 0, MSG_MAXSIZE + 1);

    for (int i = 0; i < num_sockets; i++) {
      if (poll_fds[i].revents & POLLIN) {

        if(poll_fds[i].fd == STDIN_FILENO){
          scanf("%s", buffer);
          if (strcmp(buffer, "exit") == 0) {
            for(int j = 2; j < num_sockets; j++){
              close(poll_fds[i].fd);
            }
            return;
          } else {
            cout << "Comanda gresita." << endl;
          }
          continue;
        }

        if (poll_fds[i].fd == socketTCP) {
          // Am primit o cerere de conexiune pe socketul TCP, pe care
          // o acceptam
          struct sockaddr_in cli_addr;
          socklen_t cli_len = sizeof(cli_addr);
          const int newSocketTCP =
              accept(socketTCP, (struct sockaddr *)&cli_addr, &cli_len);
          int flag = 1;
          setsockopt(newSocketTCP, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int)); // Dezactivam Nagle
          DIE(newSocketTCP < 0, "accept");

          // Adaugam noul socket intors de accept() la multimea descriptorilor
          // de citire
          poll_fds[num_sockets].fd = newSocketTCP;
          poll_fds[num_sockets].events = POLLIN;
          num_sockets++;

          memset(buffer, 0, MSG_MAXSIZE + 1);
          recv(newSocketTCP, buffer, sizeof(buffer), 0); // buffer = id

          if(connected.find(buffer) != connected.end() && connected[buffer].isConnected == true){
            cout << "Client " << buffer << " already connected." << endl;

            uint32_t len = htonl(strlen("exit"));
            send_all(newSocketTCP, &len, sizeof(len));
            send_all(newSocketTCP, (void*)"exit", strlen("exit"));

            num_sockets--;
            close(newSocketTCP);
          }else{
            cout << "New client " << buffer << " connected from "
                << inet_ntoa(cli_addr.sin_addr) << ":"
                << htons(cli_addr.sin_port) << endl;

            socketToIdMap.insert({newSocketTCP, buffer});

            if(connected.find(buffer) != connected.end()){
              connected[buffer].isConnected = true;
              connected[buffer].socket = newSocketTCP;
            }else{
              connected.insert({buffer, {true, newSocketTCP}});
            }
          }
        } else if (poll_fds[i].fd == socketUDP){
          // Am primit date pe socektul UDP. Interpretam si trimitem la subscriberi.       
          char bufferUDP[sizeof(Message)];
          memset(bufferUDP, 0, sizeof(Message) - 1);
          struct sockaddr_in addrUDP;
          socklen_t addrLen = sizeof(addrUDP);

          rc = recvfrom(socketUDP, bufferUDP, sizeof(Message), 0,
                              (struct sockaddr*)&addrUDP, &addrLen);
          DIE(rc < 0, "recvfrom");

          Message *message = (Message*)bufferUDP;

          char topicWithNull[51];
          memcpy(topicWithNull, message->topic, 50);
          topicWithNull[50] = '\0';

          string topicStr(topicWithNull);
        
          string type;
          string payload;
          switch (message->type){
            case 0:{
                type = "INT";
                int value;
                uint8_t sign = (int)message->payload[0];
                memcpy(&value, message->payload + 1, sizeof(uint32_t));
                if(sign == 1) {
                  value = (-1) * ntohl(value);
                  payload = to_string(value);
                } else {
                  value = ntohl(value);
                  payload = to_string(value);
                }
              }
              break;
            case 1:{
                type = "SHORT_REAL";
                int value;
                memcpy(&value, message->payload, sizeof(uint16_t));
                float floatValue = htons(value) / 100.00;
                if(floatValue == (int)floatValue) {
                  char bufferTmp[20];
                  sprintf(bufferTmp, "%d", (int)floatValue);
                  payload = string(bufferTmp);
                } else {
                  char bufferTmp[20];
                  sprintf(bufferTmp, "%.2f", floatValue);
                  payload = string(bufferTmp);
                }
              }
              break;
            case 2: {
                type = "FLOAT";
                uint8_t sign = message->payload[0];
                uint32_t value;
                memcpy(&value, message->payload + 1, sizeof(uint32_t));
                value = ntohl(value);

                uint8_t power = message->payload[5];

                double floatValue = value / pow(10.0, power);
                if (sign == 1) {
                  floatValue *= -1;
                }

                if (floatValue == (int)floatValue) {
                  char bufferTmp[32];
                  sprintf(bufferTmp, "%d", (int)floatValue);
                  payload = string(bufferTmp);
                } else {
                  char bufferTmp[32];
                  sprintf(bufferTmp, "%f", floatValue);
                  payload = string(bufferTmp);
                }
              }
              break;
            case 3:{
                type = "STRING";
                char payloadWithNull[1501];
                memcpy(payloadWithNull, message->payload, 1500);
                payloadWithNull[1500] = '\0';

                payload = string(payloadWithNull);
              }
              break;
            default:
              break;
          }

          string ip = inet_ntoa(addrUDP.sin_addr);
          int port = ntohs(addrUDP.sin_port);
          string toSend = ip + ":" + to_string(port) + " - " + topicStr + " - " + type + " - " + payload;

          auto range = topicToIdMap.equal_range(topicStr);

          for (auto it = range.first; it != range.second; ++it) {
            if(connected[it -> second].isConnected){
              int currentSocket = connected[it -> second].socket;

              uint32_t msg_len = htonl(toSend.length());
              rc = send_all(currentSocket, &msg_len, sizeof(msg_len));
              DIE(rc <= 0, "send_all");

              rc = send_all(currentSocket, (void*)toSend.c_str(), toSend.length());
              DIE(rc <= 0, "send_all");
            }
          }
          

        } else {
          // Am primit date pe unul din socketii de client, asa ca le receptionam
          int rc = recv(poll_fds[i].fd, &buffer, sizeof(buffer), 0);
          DIE(rc < 0, "recv");

          // Inchidere fortata
          if (rc == 0) {
            cout << "Client " << socketToIdMap.at(poll_fds[i].fd)
            << " disconnected." << endl;

            connected[socketToIdMap.at(poll_fds[i].fd)].isConnected = false;
            socketToIdMap.erase(poll_fds[i].fd);
            close(poll_fds[i].fd);

            // Scoatem din multimea de citire socketul inchis
            for (int j = i; j < num_sockets - 1; j++) {
              poll_fds[j] = poll_fds[j + 1];
            }
            num_sockets--;

          } else {
            // Inchidem conexiunea
            if(strncmp(buffer, "exit", 4) == 0){
              cout << "Client " << socketToIdMap.at(poll_fds[i].fd)
              << " disconnected." << endl;

              connected[socketToIdMap.at(poll_fds[i].fd)].isConnected = false;
              socketToIdMap.erase(poll_fds[i].fd);
              close(poll_fds[i].fd);

              // Scoatem din multimea de citire socketul inchis
              for (int j = i; j < num_sockets - 1; j++) {
                poll_fds[j] = poll_fds[j + 1];
              }
              num_sockets--;
            }

            // Comanda Subscribe
            if(strncmp(buffer, "subscribe", 9) == 0){
              buffer[strlen(buffer) - 1] = '\0'; // Pentru ca mai are un '\n' la final
              string topic = string(buffer + 10);

              // Aici este foarte important sa folosim topic.c_str() deoarece daca
              // nu l-am folosi ar aparea un \0 in plus in reprezentarea string
              topicToIdMap.insert({topic.c_str(), socketToIdMap.at(poll_fds[i].fd)});
            }

            // Comanda Unsubscribe
            if(strncmp(buffer, "unsubscribe", 11) == 0){
              buffer[strlen(buffer) - 1] = '\0'; // Pentru ca mai are un '\n' la final
              string topic = string(buffer + 12);
              for (auto it = topicToIdMap.equal_range(topic.c_str()).first; 
                      it != topicToIdMap.equal_range(topic.c_str()).second; ++it) {
                  if (it->second == socketToIdMap.at(poll_fds[i].fd)) {
                      topicToIdMap.erase(it);
                      break;
                  }
              }
            }
          }
        }
         // Odata ce am primit un mesaj de la un socket, vrem sa asteptam iar
         // de la poll, deci fortam for ul sa se termine.
        i = num_sockets;
      }
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fprintf(stderr, "\n Usage: %s <port>\n", argv[0]);
    return 1;
  }

  setvbuf(stdout, NULL, _IONBF, BUFSIZ);

  // Parsam port-ul ca un numar
  uint16_t port;
  int rc = sscanf(argv[1], "%hu", &port);
  DIE(rc != 1, "Given port is invalid");

  // Obtinem un socket TCP si UDP pentru receptionarea conexiunilor
  const int socketTCP = socket(AF_INET, SOCK_STREAM, 0);
  DIE(socketTCP < 0, "socketTCP");

  const int socketUDP = socket(AF_INET, SOCK_DGRAM, 0);
  DIE(socketUDP < 0, "socketUDP");

  // Completăm in serverAddress adresa serverului, familia de adrese si portul
  // pentru conectare
  struct sockaddr_in serverAddress;

  memset(&serverAddress, 0, sizeof(serverAddress));
  serverAddress.sin_family = AF_INET;
  serverAddress.sin_port = htons(port);
  
  // Accept conexiuni de pe orice interfata de retea
  serverAddress.sin_addr.s_addr = INADDR_ANY;

  // Asociem adresa serverului cu socketurile create folosind bind
  rc = bind(socketTCP, (const struct sockaddr *)&serverAddress, sizeof(serverAddress));
  DIE(rc < 0, "bind socketTCP");
  rc = bind(socketUDP, (const struct sockaddr *)&serverAddress, sizeof(serverAddress));
  DIE(rc < 0, "bind socketUDP");

  run_connection(socketTCP, socketUDP);

  // Inchidem socketTCP
  close(socketTCP);

  return 0;
}
