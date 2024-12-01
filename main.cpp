#include "server.h"
#include "logger.h"
#include <csignal>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <cstring>
#include <iostream>
#include <algorithm>

// Инициализация массива клиентов
void init_clients() {
    std::fill(std::begin(clients), std::end(clients), -1);
}

// Найти свободный слот для клиента
int find_free_slot() {
    for (int i = 0; i < MAX; ++i) {
        if (clients[i] == -1) {
            return i;
        }
    }
    return -1; // Нет свободных слотов
}

// Удалить клиента из массива
void remove_client(int client_fd) {
    for (int i = 0; i < MAX; ++i) {
        if (clients[i] == client_fd) {
            clients[i] = -1; // Освобождаем слот
            break;
        }
    }
}

// Обработка зомби-процессов
void reap_children(int sig) {
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main() {
    daemonize(); // Запускаем сервер как демон
    log_message(LOG_FILE, "Web-server started");

    startServer(PORT); // Запускаем сервер

    signal(SIGCHLD, reap_children); // Убираем зомби-процессы
    signal(SIGTERM, signal_handler); // Завершаем сервер при сигнале
    signal(SIGINT, signal_handler);  // Завершаем сервер при Ctrl+C

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    init_clients(); // Инициализируем массив клиентов

    // Основной цикл обработки подключений
    while (true) {
        int slot = find_free_slot();
        if (slot == -1) { // Нет свободных слотов
            log_message(LOG_FILE, "Too many connections. Connection refused.");
            sleep(1); // Ждем, пока освободится место
            continue;
        }

        int client_fd = accept(listenfd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            log_message(LOG_FILE, "Error accepting client connection.");
            perror("accept() error");
            continue;
        }

        clients[slot] = client_fd; // Добавляем клиента в массив

        // Создаем новый процесс для каждого клиента
        pid_t pid = fork();
        if (pid < 0) {
            log_message(LOG_FILE, "Fork error.");
            perror("fork() error");
            close(client_fd);
            clients[slot] = -1; // Освобождаем слот при ошибке
            continue;
        }

        if (pid == 0) { // Дочерний процесс
            close(listenfd); // Дочерний процесс не использует слушающий сокет
            log_message(LOG_FILE, "New client connected.");
            respond(client_fd); // Обрабатываем запрос клиента
            close(client_fd); // Закрываем клиентский сокет
            exit(0); // Завершаем дочерний процесс
        } else { // Родительский процесс
            close(client_fd); // Родительский процесс не использует клиентский сокет
            remove_client(client_fd); // Удаляем клиента из массива
        }
    }

    // Закрываем серверный сокет (в реальности сюда не дойдет из-за бесконечного цикла)
    close(listenfd);
    log_message(LOG_FILE, "Web-server shutting down.");
    return 0;
}
