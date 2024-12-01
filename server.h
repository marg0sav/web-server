#ifndef SERVER_H
#define SERVER_H

#include <string>
#include <map>

#define PORT "8080"

#define LOG_FILE "/home/margo/lab4/weblog"
#define ROOT "/home/margo/lab4/web-server"
#define FIRST_PAGE "/start.html"
#define MAX 1000

extern int listenfd, clients[MAX];

extern size_t payload_size;

std::string request_header(const std::string& name);
void startServer(const std::string& port);
void respond(int client_fd);
void daemonize();
void route(int client_fd, const std::string& method, const std::string& uri, const std::map<std::string, std::string>& headers, const std::string& body = "");
void serveStaticFile(int client_fd, const std::string& path, const std::string& mime_type);
void methodNotAllowed(int client_fd);
void badRequest(int client_fd);
void internalServerError(int client_fd);
void notFound(int client_fd, const std::string& uri);
void okResponse(int client_fd, const std::string& content, const std::string& content_type);
void handlePostRequest(const std::string& uri, const std::string& body, const std::map<std::string, std::string>& headers, int client_fd);
void signal_handler(int sig);

#endif
