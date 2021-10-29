//SERVER USERNAME: water
//SERVER PASSWORD: soda

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iostream>
#include <dirent.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/wait.h>
#include <signal.h>
#include <vector>
#include <algorithm>
#include <iterator>
#include <limits.h>
#include <time.h>
#include <dlfcn.h>


void processRequest(int fd);
void processRequestThread(int fd);
void * poolSlave(void* socket);
void error401(int fd);
extern "C" void handle(int sig);
char * sendDirHtml(char path[], int fd, char sortMode[]);
void processCGIBin(char docPath[], char realPath[], int fd);

bool compareNA(std::string x, std::string y);
bool compareSA(std::string x, std::string y);
bool compareND(std::string x, std::string y);
bool compareSD(std::string x, std::string y);

double maxRequestTime;
double minRequestTime;
char prevSortOrder = 'A';
time_t startTime;
int QueueLength = 5;
int numRequests = 0;
sem_t sema;
char currentIP[50] = {0};


extern "C" void handle(int signal) {
  if (signal == SIGCHLD) {
    int pid;
    while((pid = waitpid(-1, NULL, WNOHANG)) > 0);
  }
}

bool compareNA(std::string x, std::string y) {
  char  x_name[500];
  char  y_name[500];
  strcpy(x_name, strstr(x.c_str(), "href=\"") + strlen("href=\""));
  for (int i = 0; i < strlen(x_name); i++) {
    if (x_name[i] == '"') {
      x_name[i] = 0;
      break;
    }
  }
  strcpy(y_name, strstr(y.c_str(), "href=\"") + strlen("href=\""));
  for (int i = 0; i < strlen(y_name); i++) {
    if (y_name[i] == '"') {
      y_name[i] = 0;
      break;
    }
  }
  return (strcmp(x_name, y_name) > 0);
}

bool compareND(std::string x, std::string y) {
  char  x_name[500];
  char  y_name[500];
  strcpy(x_name, strstr(x.c_str(), "href=\"") + strlen("href=\""));
  for (int i = 0; i < strlen(x_name); i++) {
    if (x_name[i] == '"') {
      x_name[i] = 0;
      break;
    }
  }
  strcpy(y_name, strstr(y.c_str(), "href=\"") + strlen("href=\""));
  for (int i = 0; i < strlen(y_name); i++) {
    if (y_name[i] == '"') {
      y_name[i] = 0;
      break;
    }
  }
  return (strcmp(y_name, x_name) > 0);
}

bool compareSA(std::string x, std::string y) {
  char  x_name[500];
  char  y_name[500];
  strcpy(x_name, strstr(x.c_str(), "KB") - 3);
  for (int i = 0; i < strlen(x_name); i++) {
    if (x_name[i] == '&') {
      x_name[i] = 0;
      break;
    }
  }
  strcpy(y_name, strstr(y.c_str(), "KB") - 3);
  for (int i = 0; i < strlen(y_name); i++) {
    if (y_name[i] == '&') {
      y_name[i] = 0;
      break;
    }
  }
  return (strcmp(y_name, x_name) > 0);
}


bool compareSD(std::string x, std::string y) {
  char  x_name[500];
  char  y_name[500];
  strcpy(x_name, strstr(x.c_str(), "2021") - 3);
  for (int i = 0; i < strlen(x_name); i++) {
    if (x_name[i] == '&') {
      x_name[i] = 0;
      break;
    }
  }
  strcpy(y_name, strstr(y.c_str(), "2021"));
  for (int i = 0; i < strlen(y_name); i++) {
    if (y_name[i] == '&') {
      y_name[i] = 0;
      break;
    }
  }
  printf("IN COMPARESD, x_name: %s, y_name: %s\n\n", x_name, y_name);
  return (strcmp(x_name, y_name) > 0);
}


void processCGIBin(char docPath[], char realPath[], int fd) {
  char sendBack[100] = "HTTP/1.1 200 Document follows\r\n"
                       "Server: CS 252 Lab 5\r\n";
  write(fd, sendBack, strlen(sendBack));
  char args[50] = {0};
  //obtain arguments
  if (strstr(docPath, "?") != NULL) {
    strcpy(args, strstr(docPath, "?") + 1);
  }

  printf("docPath in CGI function: %s, real: %s, arguments: %s\n", docPath, realPath, args);


  if (strstr(docPath, ".so") == NULL) { //run cgi-bin
    pid_t ret = fork();
    if (ret == 0) {
      if (args[0] != 0) {
        setenv("REQUEST_METHOD", "GET", 1);
        setenv("QUERY_STRING", args, 1);
      }
      dup2(fd, 1);
      close(fd);
      if (args[0] != 0) {
        execl(realPath, realPath, args, (char*) NULL);
      } else {
        execl(realPath, realPath, (char*) NULL);
      }
      exit(0);
    }
    waitpid(fd, NULL, 0);
    close(fd);
  }
}

void error401(int fd) {
  char sendBack[1000] =
  "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"myhttpd-cs252\"";
  write(fd, sendBack, strlen(sendBack));
}

void processRequestThread(int fd) {
  processRequest(fd);
  close(fd);
}

void * poolSlave(void* socket) {
  int masterSocket = *(int *)socket;

  while(1) {
    struct sockaddr_in clientIPAddress;
    int alen = 0;
    int clientSocket = accept(masterSocket,
                             (struct sockaddr *)&clientIPAddress,
                             (socklen_t *)&alen);
    if (clientSocket < 0) {
      perror("accept");
      exit(-1);
    }
    processRequest(clientSocket);
    close(clientSocket);
  }
}

char * sendDirHtml(char path[], int fd, char sortMode[]) {
  char expandedPath[256] = {0};
  //obtain directory path
  getcwd(expandedPath, 256);
  if (strstr(expandedPath, "http-root-dir") == NULL) {
    strcat(expandedPath, "/http-root-dir");
  }
  if (strstr(expandedPath, "htdocs") == NULL) {
    strcat(expandedPath, "/htdocs");
  }
  strcat(expandedPath, path);
  expandedPath[strlen(expandedPath) - 1] = 0;
  std::vector<std::string> entries;
  int i = 0;
  DIR *dp;
  struct dirent *ent;
  dp = opendir(expandedPath);
  if (dp != NULL) {
    //chdir("http-root-dir/htdocs/dir1");
    while(ent = readdir(dp)) {
      if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, "..")) {
        continue;
      }
      char name[50] = {0};
      strcpy(name, ent->d_name);
      char realPath[100] = {0};
      strcpy(realPath, expandedPath);
      strcat(realPath, "/");
      strcat(realPath, name);
      struct stat filestat;
      if (stat(realPath, &filestat)) {
        perror(realPath);
      }
      char line[300] = {0};
      strcat(line, "<tr><td valign=\"top\">");
      if (ent->d_name[strlen(ent->d_name) - 1] == '1') {
        strcat(line, "<img src=\"/icons/folder.gif\" alt=\"[DIR]\">");
        strcat(name, "/");
      } else {
        strcat(line, "<img src=\"/icons/unknown.gif\" alt=\"[   ]\">");
      }
      strcat(line, "</td><td><a href=\"");
      strcat(line, name);
      strcat(line, "\">");
      strcat(line, name);
      strcat(line, "</a></td><td align=\"right\">");
      strncat(line, ctime(&filestat.st_mtime), strlen(ctime(&filestat.st_mtime)) - 1);
      strcat(line, "  </td><td align=\"right\">  ");

      char size[100] = {0};
      FILE * for_size = fopen(realPath, "rb");
      fseek(for_size, 0, SEEK_END);
      int numBytes = ftell(for_size);
      fclose(for_size);
      sprintf(size, "%dKB", numBytes/1000);
      strcat(line, size);

      strcat(line, " </td><td>&nbsp;</td></tr>");
      strcat(line, "\n");
      entries.push_back(std::string(line));
      i++;
    }
    closedir(dp);
  } else { //directory failed to open
    fprintf(stderr, "error: directory %s does not exist\n", expandedPath);
  }

  //sort entries
  if (sortMode[0] == 'N') {
    if (sortMode[1] == 'A') {
      std::sort(entries.begin(), entries.end(), compareNA);
    } else if (sortMode[1] == 'D') {
      std::sort(entries.begin(), entries.end(), compareND);
    }
  } else if (sortMode[0] == 'S') {
    if (sortMode[1] == 'A') {
      std::sort(entries.begin(), entries.end(), compareSA);
    } else if (sortMode[1] == 'D') {
      std::sort(entries.begin(), entries.end(), compareSD);
    }
 }


  //begin writing html doc string
  char htmlDoc[5000] = {0};
  strcat(htmlDoc, "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\">\n");
  strcat(htmlDoc, "<html>\n");
  strcat(htmlDoc, " <head>\n");
  strcat(htmlDoc, "  <title>Index of "
        "/homes/cs252/lab5-http-server/lab5-src/http-root-dir/htdocs/dir1</title>\n");
  strcat(htmlDoc, " </head>\n");
  strcat(htmlDoc, " <body>\n");
  strcat(htmlDoc, "<h1>Index of "
        "/homes/cs252/lab5-http-server/lab5-src/http-root-dir/htdocs/dir1</h1>\n");
  strcat(htmlDoc, "  <table>\n");
  char sortLine[500] = {0};
  sprintf(sortLine, "   <tr><th valign=\"top\"><img src=\"/icons/blank.gif\" "
        "alt=\"[ICO]\"></th><th><a href=\"?C=N;O=%c\">Name</a></th><th><a href=\"?C=M;O=D\">"
        "Last modified</a></th><th><a href=\"?C=S;O=D\">Size</a></th><th>"
        "<a href=\"?C=D;O=A\">Description</a></th></tr>\n", prevSortOrder);
  strcat(htmlDoc, sortLine);
  strcat(htmlDoc, "   <tr><th colspan=\"5\"><hr></th></tr>\n");
  strcat(htmlDoc, "<tr><td valign=\"top\"><img src=\"/icons/back.gif\" "
        "alt=\"[PARENTDIR]\"></td><td><a href=\"");

  //set hyper link to parent directory
  char parent[50] = {0};
  strcpy(parent, expandedPath);
  int k;
  for (k = strlen(parent); k > 0; k--) {
    if (parent[k] == '/') {
      parent[k] = 0;
      break;
    }
  }
  if (strstr(parent, "dir1") != NULL) {
    strcpy(parent, "/dir1/");
  } else {
    strcpy(parent, "/");
  }
  strcat(htmlDoc, parent);
  strcat(htmlDoc, "\">Parent Directory</a></td><td>&nbsp;</td><td align=\"right\">  - "
        "</td><td>&nbsp;</td></tr>\n");

  //add directory entries to html doc
  for(int j = 0; j < entries.size(); j++) {
    strcat(htmlDoc, entries[j].c_str());
  }

  strcat(htmlDoc, "   <tr><th colspan=\"5\"><hr></th></tr>\n");
  strcat(htmlDoc, "</table>\n");
  strcat(htmlDoc, "</body></html>\n");

  //write html doc to client
  write(fd, htmlDoc, strlen(htmlDoc));
}


void processRequest(int fd) {
  numRequests++;

  //start request timer
  clock_t clockStart;
  clock_t clockEnd;
  double requestTime;
  clockStart = clock();

  //read and parse request
  const int maxLength = 2048;
  char request[maxLength + 1] = {0};
  char docPath[maxLength + 1] = {0};
  char fullString[maxLength + 1] = {0};
  int requestLength = 0;
  int n;

  unsigned char newChar = 0;
  unsigned char lastChar = 0;
  unsigned char secondToLastChar = 0;
  unsigned char thirdToLastChar = 0;

  bool gotGet = false;
  bool gotDocPath = false;
  while (requestLength < maxLength &&
        (n = read(fd, &newChar, sizeof(newChar)) > 0)) {

    fullString[requestLength] = newChar;
    requestLength++;
    if (newChar == ' ') {
      if (!gotGet) {
        gotGet = true;
      } else if (!gotDocPath) {
        request[requestLength - 1] = 0;
        strcpy(docPath, request + strlen("Got "));
        gotDocPath = true;
      }
    } else if (newChar == '\n' && lastChar == '\r' &&
               secondToLastChar == '\n' && thirdToLastChar == '\r') {
      break;
    } else {
      //printf("%c", newChar);
      thirdToLastChar = secondToLastChar;
      secondToLastChar = lastChar;
      lastChar = newChar;
      request[requestLength - 1] = newChar;
    }
  }

  //check for authorization/verify credentials
  char * auth;
  if ((auth = strstr(fullString, "Authorization: Basic ")) != NULL) {
    if (strncmp(auth, "Authorization: Basic d2F0ZXI6c29kYQ==",
        strlen("Authroization: Basic d2F0ZXI6c29kyQ==")) != 0) {
      printf("Incorrect username or password\n");
      error401(fd);
      return;
    }
  } else {
    error401(fd);
    return;
  }

  //obtain requested file path
  char filePath[maxLength + 1] = {0};
  char cwd[256] = {0};
  getcwd(cwd, 256);
  strcpy(filePath, cwd);
  bool sendDir = false;
  char sortMode[2] = {0};
  if (!strcmp(docPath, "/")) {
    strcpy(docPath, "/http-root-dir/htdocs/index.html");
  } else if (strstr(docPath, "dir") != NULL) {
    //browsing subdirectories
    if ((docPath[strlen(docPath) - 1] == '/')) {
      sendDir = true;
    } else {
      strcat(filePath, "/http-root-dir/htdocs");
    }
    //determine sorting mode and fix docPath
    if (strstr(docPath, "?C=") != NULL) {
      sortMode[0] = docPath[strlen(docPath) - 5];
      sortMode[1] = docPath[strlen(docPath) - 1];
      if (sortMode[1] == 'A') {
        prevSortOrder = 'D';
      } else {
        prevSortOrder = 'A';
      }
      docPath[strlen(docPath) - 8] = 0;
      sendDir = true;
    }
  } else if (!sendDir) {
    if ((strstr(docPath, "/icons")) != NULL ||
        strstr(docPath, "/htdocs") != NULL ||
        strstr(docPath, "cgi-bin") != NULL) {
      if (strstr(filePath, "http-root-dir") == NULL) {
        strcat(filePath, "/http-root-dir");
      }
    } else {
      strcat(filePath, "/http-root-dir/htdocs");
    }
  }
  strcat(filePath, docPath);

  printf("docPath: %s,  file path generated: %s\n", docPath, filePath);

  //run CGI bin and loadables
  if (strstr(docPath, "cgi-bin") != NULL) {
    printf("RUNNING CGI BIN...\n");
    processCGIBin(docPath, filePath, fd);
    return;
  }

  //obtain content type
  char contentType[20] = {0};
  if ((strstr(docPath, ".html") != NULL) || (docPath[strlen(docPath) - 1] == '/')) {
    strncpy(contentType, "text/html", 9);
  } else if ((strstr(docPath, ".gif") != NULL) || (strstr(docPath, ".png") != NULL) ||
             (strstr(docPath, ".svg") != NULL)) {
    strncpy(contentType, "image/gif", 9);
  } else {
    strncpy(contentType, "text/plain", 10);
  }

  //open file

  FILE *fp = fopen(filePath, "rb");
  if (!sendDir) {
    if (fp == NULL) {
      //TODO send error back to client
      char sendBack[maxLength + 1] = "HTTP/1.0 404 File Not Found\r\nServer: CS 252 Lab5\r\nContent-type: ";
      strcat(sendBack, contentType);
      strcat(sendBack, "\r\n");
      write(fd, sendBack, strlen(sendBack));
      return;
      printf("File failed to open\n");
  }
  }

  //respond to client
  char sendBack[maxLength + 1] = "HTTP/1.0 200 Document follows\r\nServer: CS 252 Lab5\r\nContent-type: ";
  strcat(sendBack, contentType);
  strcat(sendBack, "\r\n\r\n");
  write(fd, sendBack, strlen(sendBack));
  if (sendDir) {
    sendDirHtml(docPath, fd, sortMode);
    return;
  }
  int buffSize = 1024;
  char dataBuff[buffSize] = {0};
  int numRead = 0;
  while ((numRead = fread(dataBuff, sizeof(char), buffSize, fp)) > 0) {
    write(fd, dataBuff, numRead);
  }
  write(fd, "\n", 1);
  fclose(fp);

  //update stats file
  FILE * stat_fp = fopen("http-root-dir/htdocs/stats", "w");
  if (stat_fp == NULL) {
    perror("fopen for stat");
  }
  fwrite("Time that server has been running: ", 35, 1, stat_fp);
  time_t endTime;
  time(&endTime);
  char buf[250];
  sprintf(buf, "%lf seconds\n", difftime(endTime, startTime));
  fwrite(buf, strlen(buf), 1, stat_fp);
  sprintf(buf, "Number of requests: %d\n", numRequests);
  fwrite(buf, strlen(buf), 1, stat_fp);
  clockEnd = clock();
  requestTime = difftime(clockEnd, clockStart);
  if (requestTime > maxRequestTime) {
    maxRequestTime = requestTime;
  }
  if ((requestTime < minRequestTime) || (minRequestTime == 0)) {
    minRequestTime = requestTime;
  }
  sprintf(buf, "Maximum request time: 0.%lf", maxRequestTime);
  fwrite(buf, strlen(buf) - 7, 1, stat_fp);
  fwrite(" seconds\n", 9, 1, stat_fp);
  sprintf(buf, "Minimum request time: 0.%lf", minRequestTime);
  fwrite(buf, strlen(buf) - 7, 1, stat_fp);
  fwrite(" seconds\n", 9, 1, stat_fp);
  fclose(stat_fp);

  //update log file
  FILE * log_fp = fopen("http-root-dir/htdocs/logs", "a");
  sprintf(buf, "Client IP: %s, Requested Directory: %s\n", currentIP, filePath);
  fwrite(buf, strlen(buf), 1, log_fp);
  fclose(log_fp);
}

int main(int argc, char ** argv)
{
  
  //zombie clean up
  struct sigaction sa;
  sa.sa_handler = handle;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, &sa, NULL)) {
    perror("sigaction");
    exit(2);
  }

  //get port number and flag
  int port = 0;
  char flag = 0;
  if (argc == 3) {
    flag = argv[1][1];
    port = atoi(argv[2]);
  } else if (argc == 2) {
    port = atoi(argv[1]);
  } else if (argc == 1) {
    port = 2068;
  } else {
    fprintf(stderr, "incorrect number of arguments");
  }

  //set up IP address and port for this server
  struct sockaddr_in serverIPAddress;
  memset(&serverIPAddress, 0, sizeof(serverIPAddress));
  serverIPAddress.sin_family = AF_INET;
  serverIPAddress.sin_addr.s_addr = INADDR_ANY;
  serverIPAddress.sin_port = htons((u_short) port);

  //allocate a socket
  int masterSocket = socket(PF_INET, SOCK_STREAM, 0);
  if (socket < 0) {
    perror("socket");
    exit(-1);
  }

  int optval = 1;
  int error = setsockopt(masterSocket, SOL_SOCKET, SO_REUSEADDR,
            (char *) &optval, sizeof(int));

  error = bind(masterSocket, (struct sockaddr *)&serverIPAddress, sizeof(serverIPAddress));
  if (error) {
    perror("bind");
    exit(-1);
  }
  error = listen(masterSocket, QueueLength);
  if (error) {
    perror("listen");
    exit(-1);
  }

  //start server timer
  time(&startTime);

  //handle pool of threads
  if (flag == 'p') {
    pthread_t tid[5];
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
    int * master_ptr = (int *)malloc(1);
    *master_ptr = masterSocket;
    for (int i = 0; i < 5; i++) {
      pthread_create(&tid[i], &attr, (void *(*)(void *))poolSlave, (void *)master_ptr);
    }
    for (int i = 0; i < 5; i++) {
      pthread_join(tid[i], NULL);
    }
    return 1;
  }

  //accept connection and handle response in all other modes
  int clientSocket;
  struct sockaddr_in clientIPAddress;
  int alen = sizeof(clientIPAddress);
  while ( (clientSocket = accept(masterSocket,
          (struct sockaddr *) &clientIPAddress,
          (socklen_t*) &alen)) ) {

    strcpy(currentIP, inet_ntoa(clientIPAddress.sin_addr));
    switch (flag) {
      case 0: {
        //iterative mode
       processRequest(clientSocket);
       close(clientSocket);
       break;
     }
     case 'f': {
       //create a new process for each request
       if (fork() == 0) { //child
         processRequest(clientSocket);
         close(clientSocket);
         exit(0);
       } else {} //ignore parent
       close(clientSocket);
       break;
     }
     case 't': {
       //create a new thread for each request
       pthread_t tid;
       pthread_attr_t attr;
       pthread_attr_init(&attr);
       pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
       pthread_create(&tid, &attr, (void * (*)(void *))processRequestThread,
                     (void *)clientSocket);
       break;
     }
     default:
       fprintf(stderr, "invalid flag\n");
     }

   }

}


