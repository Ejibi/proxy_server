#include <iostream>
#include <string>
#include <cstdlib>     
#include <cstring>     
#include <unistd.h>    
#include <arpa/inet.h>  
#include <sys/socket.h> 
#include <sys/types.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <cstdio>

struct child{
    int PID;
    int PPID;
    char User_ID[512];
    char info[512];
};

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

bool sendLine(int sockfd, const std::string &line) {
    std::string msg = line + "\n";
    ssize_t sent = send(sockfd, msg.c_str(), msg.size(), 0);
    return (sent == (ssize_t)msg.size());
}

int main(int argc, char* argv[] ){
    std::string eCmd;      
    std::string query;     
    std::string serverIP;  
    int port = 0;          

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "-e") == 0) && (i+1 < argc)) {
            eCmd = argv[++i];
        } else if ((strcmp(argv[i], "-q") == 0) && (i+1 < argc)) {
            query = argv[++i];
        } else if ((strcmp(argv[i], "-i") == 0) && (i+1 < argc)) {
            serverIP = argv[++i];
        } else if ((strcmp(argv[i], "-p") == 0) && (i+1 < argc)) {
            port = std::atoi(argv[++i]);
        }
    }

    int sock= socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket() failed");
        return 1;
    }


    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));

    struct hostent *server;
    if ((server = gethostbyname(serverIP.c_str())) == NULL) {
        close(sock);
        perror("Error, unknown host");
        return 1;
    }

    
    std::memcpy(&serv_addr.sin_addr.s_addr,server->h_addr, (size_t)server->h_length);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect() failed");
        close(sock);
        return 1;
    }
std::string leftover;
bool busy = false;
child* hello = nullptr; 
bool exitProgram = false;
while (!exitProgram) {
    char buf[512];
    ssize_t bytesRead = recv(sock, buf, sizeof(buf), 0);
    if (bytesRead <= 0) break; 
    leftover.append(buf, bytesRead);
    size_t pos = 0;
    while (true) {
        size_t newlinePos = leftover.find('\n', pos);
        if (newlinePos == std::string::npos) {
            leftover.erase(0, pos);  
            break; 
        }
        std::string oneLine = leftover.substr(pos, newlinePos - pos);
        while(!oneLine.empty() && 
              (oneLine.back() == '\r' || oneLine.back() == '\n')) {
            oneLine.pop_back();
        }
        oneLine = trim(oneLine);
        if (oneLine == "EXECUTE" && !busy) {
            sendLine(sock, "START");
            busy = true;
            FILE *in = popen(eCmd.c_str(),"r");
            if (!in) {
                perror("Failed to run command");
                sendLine(sock, "STOP");
                busy = false;
                continue;
            }
            
            char buffer[513];
            int count = 0;
            int init_size = 25; 
            hello = new child[init_size]; 
            bool exitNow = false;
            while (fgets(buffer,513,in) != NULL) {

                if (strstr(buffer, "EXITNOW") != NULL) {
                    exitNow = true;
                    break;
                }
                
                if(count >= init_size){
                    init_size = init_size*2;
                    child *newhello = new child[init_size];
                    for(int i = 0; i < init_size/2; i++){
                        newhello[i]= hello[i];
                    }
                    delete[] hello;
                    hello = newhello;
                }

                if(sscanf(buffer,"%d %d %511s %511s", &hello[count].PID, &hello[count].PPID, hello[count].User_ID, hello[count].info) == 4){;
                count++;
                }
            }
            std::string fin;
            char tmp[2048];
            for(int i = 0; i < count; i++){
                if  ((std::to_string(hello[i].PID)).find(query) != std::string::npos || (std::to_string(hello[i].PPID)).find(query) != std::string::npos ||
                    std::string(hello[i].User_ID).find(query) != std::string::npos|| std::string(hello[i].info).find(query) != std::string::npos){
                    for ( int k = 0; k < count; k++){
                        if(hello[i].PPID == hello[k].PID ){
                            snprintf(tmp,sizeof(tmp),"%s(%d) -- %s(%d)\n", hello[k].info, hello[k].PID , hello[i].info, hello[i].PID);
                            fin = fin + tmp;
                        } 
                    }
                }
            }
            busy = false;
            sendLine(sock, fin.c_str());
            sendLine(sock, "STOP");
            delete[] hello;
            pclose(in);
            leftover.erase(0, pos);
            break;
        } else if (oneLine == "QUIT") {
            sendLine(sock, "STOP");
            close(sock);
            leftover.erase(0, pos);
            delete[] hello;
            exitProgram = true;
            break;
        } else if (oneLine == "STOP") {
            break;
        } else {
            std::cerr << "Unknown command: " << oneLine << std::endl;
        }
    }
}
    close(sock);
    return 0;
}
