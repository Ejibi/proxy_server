#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <cstdio>

#define MAXHOSTNAME 256

static std::string trim(const std::string &s) {
    if (s.empty()) return s;
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char)s[start])) {
        start++;
    }
    size_t end = s.size() - 1;
    while (end > start && isspace((unsigned char)s[end])) {
        end--;
    }
    return s.substr(start, end - start + 1);
}


char* getIPAddress()
{
    char myname[MAXHOSTNAME + 1];
    static char IPinASCII[MAXHOSTNAME];
    memset(myname, 0, sizeof(myname));
    memset(IPinASCII, 0, sizeof(IPinASCII));

    if (gethostname(myname, MAXHOSTNAME) != 0) {
        perror("gethostname() failed");
        return const_cast<char*>("IP not found");
    }

    struct hostent *hp = gethostbyname(myname);
    if (!hp) {
        perror("gethostbyname() failed");
        return const_cast<char*>("IP not found");
    }
    if (!inet_ntop(hp->h_addrtype, hp->h_addr_list[0],
                   IPinASCII, MAXHOSTNAME)) {
        perror("inet_ntop() failed");
        return const_cast<char*>("IP not found");
    }
    return IPinASCII;
}

int getPortNumber(int socketNum)
{
    struct sockaddr_in addr;
    socklen_t addrLen = sizeof(addr);
    if (getsockname(socketNum, (struct sockaddr*)&addr, &addrLen) != 0) {
        perror("getsockname() failed in getPortNumber()");
        return -1;
    }
    return (int)ntohs(addr.sin_port);
}

int main(int argc, char* argv[])
{
    std::string cmd;       // -e <command>
    std::string agent;     // -a <agent_path>
    std::string shell;     // -s <shell_path>
    std::vector<std::string> rmtsys; // -r <remote_system> (can appear multiple times)
    std::string query;     // -q <query>
    int iter_num = 0;      // -n <iteration_count>
    int port = 0;          // -p <port_number>
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-e") == 0 && (i+1 < argc)) {
            cmd = argv[++i];
        } else if (strcmp(argv[i], "-a") == 0 && (i+1 < argc)) {
            agent = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && (i+1 < argc)) {
            shell = argv[++i];
        } else if (strcmp(argv[i], "-r") == 0 && (i+1 < argc)) {
            rmtsys.push_back(argv[++i]);
        } else if (strcmp(argv[i], "-q") == 0 && (i+1 < argc)) {
            query = argv[++i];
        } else if (strcmp(argv[i], "-n") == 0 && (i+1 < argc)) {
            iter_num = std::atoi(argv[++i]);
        } else if (strcmp(argv[i], "-p") == 0 && (i+1 < argc)) {
            port = std::atoi(argv[++i]);
        }
    }
    if (cmd.empty() || agent.empty() || shell.empty() || rmtsys.empty() ||
        query.empty() || iter_num <= 0 || port <= 0)
    {
        std::cerr << "Usage: " << argv[0]
                  << " -e <command> -a <agent> -s <shell>"
                  << " -r <remote_system> ... -q <query> -n <iter_num> -p <port>\n";
        return 1;
    }

    int listenSock = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSock < 0) {
        perror("socket() failed");
        return 1;
    }
    int optval = 1;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family      = AF_INET;
    serv_addr.sin_port        = htons(port);
    serv_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listenSock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Error binding socket to this server");
        close(listenSock);
        return 1;
    }

    if (listen(listenSock, 5) < 0) {
        perror("Error listening on socket");
        close(listenSock);
        return 1;
    }

    std::string localIP   = getIPAddress();
    int actualPort        = getPortNumber(listenSock);
    if (actualPort <= 0) {
        std::cerr << "Failed to get valid port number.\n";
        close(listenSock);
        return 1;
    }
    std::string portStr = std::to_string(actualPort);


    for (auto &ip : rmtsys) {
        std::string sshCommand = shell + " " + ip + " '" + agent + " -e \"" + cmd + "\" -q \"" + query + "\" -i " + localIP + " -p " + portStr + " </dev/null >/dev/null 2>&1'";

        pid_t pid;
        pid = fork();
        if ( pid < 0) {
            perror("fork() failed");
            close(listenSock);
            return 1;
        } else if (pid == 0) {
            std::system(sshCommand.c_str());
            exit(0);
        }
    }

    std::vector<int> agents;
    agents.reserve(rmtsys.size());
    for (size_t i = 0; i < rmtsys.size(); i++) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int connected_socket_fd = accept(listenSock,(struct sockaddr*)&client_addr,&client_len);
        if (connected_socket_fd < 0) {
            perror("Error accepting connection");
            continue;
        }
        agents.push_back(connected_socket_fd);
    }
    if (agents.empty()) {
        std::cerr << "No agents connected. Exiting.\n";
        close(listenSock);
        return 1;
    }
    for (int iteration = 1; iteration <= iter_num; iteration++) {
        std::cout << "N=" << iteration << std::endl;
        for (int sock : agents) {
            std::string execMsg = "EXECUTE\n";
            ssize_t bytesSent = send(sock, execMsg.c_str(), execMsg.size(), 0);
        }
        for (size_t i = 0; i < agents.size(); i++) {
            int sock = agents[i];
            std::string ip = rmtsys[i];
            bool inBlock = false;
            std::string buffer;
        
            while (true) {
                char recvBuf[2048];
                ssize_t bytesRecv = recv(sock, recvBuf, sizeof(recvBuf) - 1, 0);
                if (bytesRecv <= 0) {
                    break;
                }
                recvBuf[bytesRecv] = '\0'; 
                buffer += recvBuf; 
        
               
                size_t newlinePos;
                while ((newlinePos = buffer.find('\n')) != std::string::npos) {

                    std::string line = buffer.substr(0, newlinePos);
                    buffer.erase(0, newlinePos + 1); 
        
                    
                    line = trim(line);
        
                    if (line == "START") {
                        inBlock = true;
                    } else if (line == "STOP") {
                        inBlock = false;
                        break;
                    } else {
                        if (inBlock && !line.empty()) { 
                            std::cout << ip << "::" << line << std::endl;
                        }
                    }
                }
                if (!inBlock) {
                    break;
                }
            }
        }
    }
    for (int sock : agents) {
        std::string quitMsg = "QUIT\n";
        send(sock, quitMsg.c_str(), quitMsg.size(), 0);
        close(sock);
    }

    close(listenSock);
    return 0;
}
