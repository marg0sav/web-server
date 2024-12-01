#include "server.h"
#include "logger.h"
#include "utils.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> // Для struct addrinfo и getaddrinfo
#include <unistd.h>
#include <arpa/inet.h>
#include <cstdlib>
#include <cstring> // Для memset
#include <iostream>
#include <sstream> // Для std::istringstream, std::ostringstream
#include <fstream> 
#include <signal.h>   // Для SIGTERM, SIGINT, SIGCHLD
#include <sys/wait.h> // Для waitpid
#include <sys/stat.h> // Для umask
#include <vector>
#include <algorithm> // Для std::transform

// Определения глобальных переменных
int listenfd;
int clients[MAX];
std::map<std::string, std::string> headers;
size_t payload_size = 0;

// Реализация функции request_header
std::string request_header(const std::string& name) {
    auto it = headers.find(name);
    return it != headers.end() ? it->second : "";
}

// Запуск сервера на порту `port`
void startServer(const std::string& port) {
    struct addrinfo addrConfig, * addrResults, * currentAddr;

    // Настраиваем структуру addrConfig для конфигурации сокета
    memset(&addrConfig, 0, sizeof(addrConfig));
    addrConfig.ai_family = AF_INET;        // Используем IPv4
    addrConfig.ai_socktype = SOCK_STREAM; // Потоковый сокет (TCP)
    addrConfig.ai_flags = AI_PASSIVE;     // Подходит для прослушивающего сокета

    // Получаем список возможных адресов для указанного порта
    if (getaddrinfo(NULL, port.c_str(), &addrConfig, &addrResults) != 0) {
        perror("getaddrinfo() error");
        log_message(LOG_FILE, "getaddrinfo() error");
        exit(1);
    }

    // Цикл по списку адресов для создания и привязки сокета
    for (currentAddr = addrResults; currentAddr != NULL; currentAddr = currentAddr->ai_next) {
        int option = 1;

        // Создаем сокет
        listenfd = socket(currentAddr->ai_family, currentAddr->ai_socktype, 0);
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option)); // Разрешаем повторное использование адреса

        // Если сокет создан успешно, пытаемся привязать его к адресу
        if (listenfd == -1)
            continue;
        if (bind(listenfd, currentAddr->ai_addr, currentAddr->ai_addrlen) == 0){
    break; // Успешно привязали сокет
}
    }

    // Проверяем, удалось ли привязать сокет
    if (currentAddr == NULL) {
        perror("socket() or bind()");
        log_message(LOG_FILE, "error in socket() or bind()");
        exit(1);
    }

    // Освобождаем память, выделенную для addrResults
    freeaddrinfo(addrResults);

    // Переводим сокет в режим ожидания подключений
    if (listen(listenfd, 1000000) != 0) {
        perror("listen() error");
        log_message(LOG_FILE, "listen() error");
        exit(1);
    }
}


// Обработчик сигналов
void signal_handler(int sig) {
    switch (sig) {
    case SIGTERM:
    case SIGINT:
        log_message(LOG_FILE, "Web server is shutting down.");
        exit(0);
        break;
    }
}

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        log_message(LOG_FILE, "Error: Unable to fork process");
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
        log_message(LOG_FILE, "Error: Unable to create a new session");
        exit(EXIT_FAILURE);
    }
    signal(SIGCHLD, SIG_DFL);
    signal(SIGTERM, signal_handler);

    umask(0);
    chdir("/");

    for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
        close(fd);
    }

    // Заменяем openlog на логирование в файл
    log_message(LOG_FILE, "Daemon started");
}

void serveStaticFile(int client_fd, const std::string& path) {
    std::ifstream file(path, std::ios::binary);

    if (file) {
        // Чтение файла
        std::ostringstream content;
        content << file.rdbuf();
        file.close();

        // Определение MIME-типа
        std::string extension = getExtension(path);
        std::string content_type = "text/plain"; // по умолчанию

        if (extension == "gif")
            content_type = "image/gif";
        else if (extension == "html")
            content_type = "text/html";
        else if (extension == "css")
            content_type = "text/css";
        else if (extension == "jpg" || extension == "jpeg")
            content_type = "image/jpeg";

        // Формирование и отправка ответа
        okResponse(client_fd, content.str(), content_type);
    } else {
        // Обработка отсутствия файла
        notFound(client_fd, path);
    }
}

void route(int client_fd, const std::string& method, const std::string& uri, const std::map<std::string, std::string>& headers, const std::string& body) {
    // Обработка только методов GET
    if (method == "GET") {
        // Формируем путь для файла
        std::string path = (uri == "/") ? std::string(ROOT) + FIRST_PAGE : std::string(ROOT) + uri;

        serveStaticFile(client_fd, path);
        return;
    }

    // Обработка методов POST
    if (method == "POST") {
        log_message(LOG_FILE, "Handling POST request.");
        log_message(LOG_FILE, "URI: " + uri);
        log_message(LOG_FILE, "Body: " + body);

        if (uri == "/uploads") {
            handlePostRequest(uri, body, headers, client_fd);
        } else {
            notFound(client_fd, uri);
        }
        return;
    }

    methodNotAllowed(client_fd);
}


// Функция для обработки клиентского запроса
void respond(int client_fd) {
	// Установим тайм-аут для операций чтения и записи
   	 struct timeval timeout;
    	timeout.tv_sec = 10;  // Тайм-аут в секундах
    	timeout.tv_usec = 0;

	    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    	setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        std::vector<char> buffer(65535); // Буфер для получения запроса
        int received = recv(client_fd, buffer.data(), buffer.size() - 1, 0); // Получение данных от клиента
        log_message(LOG_FILE, "Raw buffer: " + std::string(buffer.data(), received));

        if (received < 0) {
            log_message(LOG_FILE, "recv() error: Unable to read data.");
            perror("recv() error");
            internalServerError(client_fd);
            return;
        } else if (received == 0) {
            log_message(LOG_FILE, "Client disconnected unexpectedly.");
        }

        log_message(LOG_FILE, "Request received from client.");
        buffer[received] = '\0'; // Завершаем строку

        // Парсинг запроса
        std::istringstream request_stream(buffer.data());
        std::string line, method, uri, prot;
        std::map<std::string, std::string> headers;
        std::string body;

        // Читаем первую строку (метод, URI, протокол)
        if (std::getline(request_stream, line)) {
            //log_message(LOG_FILE, "Parsing request line...");
            std::istringstream line_stream(line);
            line_stream >> method >> uri >> prot;

            if (method.empty() || uri.empty() || prot.empty()) {
                log_message(LOG_FILE, "Invalid request line.");
                badRequest(client_fd);
                return;
            }
            //log_message(LOG_FILE, "Request line parsed: " + method + " " + uri + " " + prot);
        } else {
            log_message(LOG_FILE, "Failed to parse request line.");
            internalServerError(client_fd);
            return;
        }

        // Разбор заголовков
        while (std::getline(request_stream, line) && line != "\r") {
            auto colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string name = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);
                value.erase(0, value.find_first_not_of(" \t")); // Убираем пробелы перед значением
                headers[name] = value;

                //log_message(LOG_FILE, "Header parsed: " + name + ": " + value);
            } else {
                log_message(LOG_FILE, "Invalid header format: " + line);
                badRequest(client_fd);
                return;
            }
        }
    // Если метод POST, считываем тело запроса
    if (method == "POST") {
        auto contentLengthIt = headers.find("Content-Length");
        if (contentLengthIt == headers.end()) {
            log_message(LOG_FILE, "Missing Content-Length header in POST request.");
            badRequest(client_fd);
            return;
        }

        size_t contentLength = std::stoul(contentLengthIt->second);
        std::streampos body_start_pos = request_stream.tellg(); // Позиция начала тела в исходном buffer

        // Убедимся, что тело запроса присутствует в buffer
        if (body_start_pos < received) {
            size_t bodySizeInBuffer = received - body_start_pos;
            body = std::string(buffer.data() + body_start_pos, bodySizeInBuffer);

            // Если тело запроса в buffer меньше, чем Content-Length
            if (bodySizeInBuffer < contentLength) {
                log_message(LOG_FILE, "Incomplete body in initial buffer. Reading remaining body...");
                std::vector<char> remainingBuffer(contentLength - bodySizeInBuffer);
                int bytesRead = recv(client_fd, remainingBuffer.data(), remainingBuffer.size(), 0);

                if (bytesRead < 0) {
                    log_message(LOG_FILE, "Error receiving remaining POST body: " + std::string(strerror(errno)));
                    internalServerError(client_fd);
                    return;
                } else if (bytesRead == 0) {
                    log_message(LOG_FILE, "Client closed connection prematurely while receiving POST body.");
                    badRequest(client_fd);
                    return;
                }

                body.append(remainingBuffer.begin(), remainingBuffer.begin() + bytesRead);
            }
        } else {
            log_message(LOG_FILE, "Body not found in initial buffer.");
            badRequest(client_fd);
            return;
        }

        log_message(LOG_FILE, "POST Request Body: " + body);
    }
        
    route(client_fd, method, uri, headers, body);

    // Закрытие соединения
    log_message(LOG_FILE, "Closing client connection.");
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
}


void internalServerError(int client_fd) {
    std::string body = "500 Internal Server Error\nAn unexpected error occurred on the server.\n";
    std::string response = "HTTP/1.1 500 Internal Server Error\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n" +
                           body;
    send(client_fd, response.c_str(), response.size(), 0);
   log_message(LOG_FILE, "Internal Server Error: Sent 500 response.");
}

void badRequest(int client_fd) {
    std::string body = "400 Bad Request\nThe server could not understand the request.\n";
    std::string response = "HTTP/1.1 400 Bad Request\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n"
                           "Connection: close\r\n\r\n" +
                           body;
    send(client_fd, response.c_str(), response.size(), 0);
    log_message(LOG_FILE, "Bad Request: Sent 400 response.");
}

void methodNotAllowed(int client_fd) {
    std::string body = "405 Method Not Allowed\nThe requested HTTP method is not supported.\n";
    std::string response = "HTTP/1.1 405 Method Not Allowed\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n"
                           "Connection: close\r\n\r\n" +
                           body;
    send(client_fd, response.c_str(), response.size(), 0);
    log_message(LOG_FILE, "Method Not Allowed: Sent 405 response.");
}

void notFound(int client_fd, const std::string& uri) {
    std::string body = "404 Not Found\nThe requested resource " + uri + " was not found on this server.\n";
    std::string response = "HTTP/1.1 404 Not Found\r\n"
                           "Content-Type: text/plain\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n"
                           "Connection: close\r\n\r\n" +
                           body;
    send(client_fd, response.c_str(), response.size(), 0);
    log_message(LOG_FILE, "Not Found: Sent 404 response for URI: " + uri);
}

void okResponse(int client_fd, const std::string& content, const std::string& content_type) {
    std::string body = content;
    std::string response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: " + content_type + "\r\n"
                           "Content-Length: " + std::to_string(body.size()) + "\r\n"
                           "Connection: close\r\n\r\n" +
                           body;
    send(client_fd, response.c_str(), response.size(), 0);
    log_message(LOG_FILE, "OK Response: Sent 200 OK with Content-Type: " + content_type);
}

void handlePostRequest(const std::string& uri, const std::string& body, const std::map<std::string, std::string>& headers, int client_fd) {
    // Нормализация URI
    std::string normalizedUri = uri;
    normalizedUri.erase(0, normalizedUri.find_first_not_of(" \t"));
    normalizedUri.erase(normalizedUri.find_last_not_of(" \t") + 1);
    std::transform(normalizedUri.begin(), normalizedUri.end(), normalizedUri.begin(), ::tolower);

    // Проверка заголовка Content-Type
    auto contentTypeIt = headers.find("Content-Type");
    std::string contentType = contentTypeIt != headers.end() ? contentTypeIt->second : "";
    contentType.erase(0, contentType.find_first_not_of(" \t"));
    contentType.erase(contentType.find_last_not_of(" \t") + 1);
    std::transform(contentType.begin(), contentType.end(), contentType.begin(), ::tolower);

    // Добавление проверки директории uploads
    std::string dirPath = std::string(ROOT) + "/uploads";
    struct stat info;

    if (stat(dirPath.c_str(), &info) != 0) {
        log_message(LOG_FILE, "Directory does not exist: " + dirPath);
        internalServerError(client_fd);
        return;
    } else if (!(info.st_mode & S_IFDIR)) {
        log_message(LOG_FILE, "Path exists but is not a directory: " + dirPath);
        internalServerError(client_fd);
        return;
    } else {
        log_message(LOG_FILE, "Directory exists and is accessible: " + dirPath);
    }

    // Проверка URI
    if (normalizedUri == "/uploads") {
        if (contentType.find("application/x-www-form-urlencoded") != std::string::npos) {
            // Сохранение текстовых данных
            std::string filePath = dirPath + "/data.txt";
            log_message(LOG_FILE, "Attempting to open file for writing: " + filePath);

            std::ofstream file(filePath, std::ios::app);
            if (!file) {
                log_message(LOG_FILE, "Failed to open file: " + filePath);
                internalServerError(client_fd);
                return;
            }
            file << body << "\n";
            file.close();

            log_message(LOG_FILE, "Form data saved to: " + filePath);
            okResponse(client_fd, "Data successfully uploaded and saved.\n", "text/plain");
        } else if (contentType.find("application/json") != std::string::npos) {
    log_message(LOG_FILE, "Processing JSON data");

    // Проверяем, является ли тело запроса корректным JSON
    if (body.empty()) {
        log_message(LOG_FILE, "Empty JSON body received.");
        badRequest(client_fd);
        return;
    }

    // Сохраняем JSON данные в файл
    std::string filePath = std::string(ROOT) + "/uploads/data.json";
    log_message(LOG_FILE, "Attempting to open file for writing JSON: " + filePath);

    std::ofstream file(filePath, std::ios::app); // Используем append для добавления данных
    if (!file) {
        log_message(LOG_FILE, "Failed to open file: " + filePath);
        internalServerError(client_fd);
        return;
    }

    file << body << "\n"; // Записываем JSON-объект в файл
    file.close();

    log_message(LOG_FILE, "JSON data saved to: " + filePath);
    okResponse(client_fd, "JSON data successfully uploaded and saved.", "application/json");
    return;
}else if (contentType.find("multipart/form-data") != std::string::npos) {
    log_message(LOG_FILE, "Processing multipart/form-data");

    // Парсинг boundary из заголовка
    auto boundaryPos = contentType.find("boundary=");
    if (boundaryPos == std::string::npos) {
        log_message(LOG_FILE, "Boundary not found in Content-Type header.");
        badRequest(client_fd);
        return;
    }

    std::string boundary = "--" + contentType.substr(boundaryPos + 9); // Извлекаем boundary
    std::string endBoundary = boundary + "--";

    // Открытие файла для записи
    std::string filename = dirPath + "/uploaded_file";
    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile) {
        log_message(LOG_FILE, "Failed to open file: " + filename);
        internalServerError(client_fd);
        return;
    }

    // Чтение данных из сокета
    std::vector<char> buffer(65536); // Буфер размером 64 KB
    size_t totalBytesRead = 0;
    bool isBodyStarted = false;

    while (true) {
        ssize_t bytesRead = recv(client_fd, buffer.data(), buffer.size(), 0);
        if (bytesRead < 0) {
            log_message(LOG_FILE, "Error reading from socket.");
            internalServerError(client_fd);
            return;
        } else if (bytesRead == 0) {
            break; // Конец данных
        }

        totalBytesRead += bytesRead;
        std::string data(buffer.data(), bytesRead);

        // Если тело ещё не началось, ищем начало контента после первого \r\n\r\n
        if (!isBodyStarted) {
            size_t bodyStart = data.find("\r\n\r\n");
            if (bodyStart != std::string::npos) {
                isBodyStarted = true;
                data = data.substr(bodyStart + 4); // Убираем заголовки
            } else {
                continue; // Заголовки ещё не закончились
            }
        }

        // Проверяем конец данных (boundary)
        size_t endBoundaryPos = data.find(endBoundary);
        if (endBoundaryPos != std::string::npos) {
            data = data.substr(0, endBoundaryPos); // Оставляем данные до endBoundary
            outFile.write(data.data(), data.size());
            break; // Достигли конца
        }

        // Записываем данные в файл
        outFile.write(data.data(), data.size());
    }

    outFile.close();
    log_message(LOG_FILE, "File saved to: " + filename);
    okResponse(client_fd, "File successfully uploaded and saved.", "text/plain");
} else {
            log_message(LOG_FILE, "Unsupported Content-Type: " + contentType);
            badRequest(client_fd);
        }
        return;
    }

    // Если URI не поддерживается
    notFound(client_fd, normalizedUri);
}
