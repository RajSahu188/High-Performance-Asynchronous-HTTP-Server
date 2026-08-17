#include <iostream>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/event.h>   // kqueue / kevent (macOS/BSD equivalent of sys/epoll.h)
#include <sys/time.h>
#include <netinet/in.h>
#include <fcntl.h>

const int MAX_EVENTS = 1024;
const int BUFFER_SIZE = 2048;

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<int> taskQueue;
    std::mutex queueMutex;
    std::condition_variable cv;
    bool stop = false;

public:
    explicit ThreadPool(size_t threads) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    int clientSocket;
                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->cv.wait(lock, [this]() {
                            return this->stop || !this->taskQueue.empty();
                        });

                        if (this->stop && this->taskQueue.empty()) return;

                        clientSocket = this->taskQueue.front();
                        this->taskQueue.pop();
                    }
                    this->handleClientRequest(clientSocket);
                }
            });
        }
    }

    void enqueueTask(int clientSocket) {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            taskQueue.push(clientSocket);
        }
        cv.notify_one();
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            stop = true;
        }
        cv.notify_all();
        for (std::thread &worker : workers) {
            if (worker.joinable()) worker.join();
        }
    }

private:
    ssize_t sendAll(int sockfd, const char* data, size_t length) {
        size_t totalSent = 0;
        while (totalSent < length) {
            ssize_t sent = send(sockfd, data + totalSent, length - totalSent, 0);
            if (sent == -1) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    std::this_thread::yield();
                    continue;
                }
                return -1;
            }
            totalSent += sent;
        }
        return totalSent;
    }

    void handleClientRequest(int clientSocket) {
        std::string rawRequest;
        char buffer[BUFFER_SIZE];

        while (true) {
            ssize_t bytesRead = read(clientSocket, buffer, BUFFER_SIZE);
            if (bytesRead > 0) {
                rawRequest.append(buffer, bytesRead);
                if (rawRequest.find("\r\n\r\n") != std::string::npos) {
                    break;
                }
            } else if (bytesRead == 0) {
                break;
            } else {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break;
            }
        }

        if (!rawRequest.empty()) {
            if (rawRequest.rfind("GET /", 0) == 0) {
                std::string body = "Hello World! High performance C++ Kqueue Server is live!";
                std::string httpResponse =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain\r\n"
                    "Content-Length: " + std::to_string(body.length()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;

                sendAll(clientSocket, httpResponse.c_str(), httpResponse.length());
            } else {
                std::string badRequest = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
                sendAll(clientSocket, badRequest.c_str(), badRequest.length());
            }
        }

        close(clientSocket);
    }
};

void setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int port = 8080;
    if (const char* env_p = std::getenv("PORT")) {
        port = std::atoi(env_p);
    }

    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) return 1;

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(serverFd, (struct sockaddr*)&address, sizeof(address)) < 0) return 1;
    if (listen(serverFd, 128) < 0) return 1;

    setNonBlocking(serverFd);
    std::cout << "Server running on port " << port << "...\n";

    int kq = kqueue();
    if (kq == -1) return 1;

    // Register the listening socket for read events, edge-triggered (EV_CLEAR
    // mirrors epoll's EPOLLET behavior).
    struct kevent changeEvent;
    EV_SET(&changeEvent, serverFd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, nullptr);
    if (kevent(kq, &changeEvent, 1, nullptr, 0, nullptr) == -1) return 1;

    ThreadPool pool(std::thread::hardware_concurrency());

    struct kevent events[MAX_EVENTS];

    while (true) {
        int numEvents = kevent(kq, nullptr, 0, events, MAX_EVENTS, nullptr);
        if (numEvents == -1) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < numEvents; ++i) {
            int fd = static_cast<int>(events[i].ident);

            if (fd == serverFd) {
                while (true) {
                    sockaddr_in clientAddr;
                    socklen_t clientLen = sizeof(clientAddr);
                    int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &clientLen);

                    if (clientFd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        break;
                    }

                    setNonBlocking(clientFd);

                    // EV_ONESHOT is kqueue's equivalent of EPOLLONESHOT: the
                    // event fires once, then is automatically removed.
                    struct kevent clientEvent;
                    EV_SET(&clientEvent, clientFd, EVFILT_READ,
                           EV_ADD | EV_CLEAR | EV_ONESHOT, 0, 0, nullptr);
                    if (kevent(kq, &clientEvent, 1, nullptr, 0, nullptr) == -1) {
                        close(clientFd);
                    }
                }
            } else {
                int clientSocket = fd;
                // EV_ONESHOT already removed this fd's registration after
                // firing, so no explicit delete call is needed here (unlike
                // the epoll_ctl(EPOLL_CTL_DEL, ...) step on Linux).
                pool.enqueueTask(clientSocket);
            }
        }
    }

    close(kq);
    close(serverFd);
    return 0;
}