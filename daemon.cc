#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <condition_variable>
#include <iostream>
#include <cstdlib>
#include <csignal>
#include <climits>
#include <utility>
#include <string>
#include <cstdio>
#include <thread>
#include <vector>
#include <mutex>
#include <set>

std::vector<std::thread> threads;

template <typename T>
class Set {
 public:
     bool insert(const T& item) {
         std::unique_lock<std::mutex> lock(mutex_);
	 auto ret = set_.insert(item);
	 if (ret.second) {
	     std::cout << item << std::endl;
	     fflush(stdout);
	 }
	 lock.unlock();
	 cond_.notify_one();

	 return ret.second;
     }

 private:
     std::set<T> set_;
     std::mutex mutex_;
     std::condition_variable cond_;
};

const char *socket_path;

void sig_handler(int signo)
{
    if (signo == SIGINT) {
        unlink(socket_path);

        for (std::vector<std::thread>::iterator it = threads.begin(); it != threads.end(); ++it) {
            (*it).join();
        }

        exit(0);
    }
}

void usage(const char* prog)
{
    std::cout << prog << " <socket path>" << std::endl;
    exit(1);
}

void consume(Set<std::string> &s, int client) {
    int ret;
    char real_path[PATH_MAX];
    char buf[4096];
    char log_buf[1024];

    while (1) {
        bzero(buf, sizeof(buf));
        do {
            ret = recv(client, buf, sizeof(buf), 0);
        } while (ret < 0 && errno == EAGAIN);

        if (ret < 0)  {
            perror("recv()");
            break;
        }

        if (ret == 0) {
            break;
        }

	if (buf[ret-1] != '\n') {
	    std::cerr << "Fragmented packet received: " << buf << std::endl;
	}

        char *buf_end = buf + ret;
        char *iter = buf;
        char *delim;
        while ((delim = strchr(iter, '\n')) != NULL) {
            char *pos = strchr(iter, ';');
            if (!pos) {
                std::cerr << "Unable to find ';' in " << iter << std::endl;
                continue;
            }

            *pos = 0x0;
            bzero(real_path, sizeof(real_path));
            if (realpath(iter, &real_path[0]) == NULL) {
                std::cerr << "Unable to resolve path: " << iter << std::endl;
                continue;
            }

            *delim = 0x0;
            bzero(log_buf, sizeof(log_buf));
            snprintf(log_buf, sizeof(log_buf), "%s;%s", real_path, pos+1);

            s.insert(log_buf);

            // if our delimiter was found at the end, we are done
	    if (delim == (buf_end-1)) {
	        break;
	    }

	    iter = delim+1;
	}
    }

    close(client);
}

int main(int argc, char *argv[])
{
    int sock, client, rval;
    struct sockaddr_un addr;
    char buf[1024];
    char real_path[PATH_MAX];
    char log_buf[1024];

    if (argc < 2) {
        usage(argv[0]);
    }
    socket_path = argv[1];
    signal(SIGINT, sig_handler);

    sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket()");
        exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path)-1);

    if (bind(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_un))) {
        perror("bind()");
        exit(1);
    }

    Set<std::string> s;

    listen(sock, 5);
    while (1) {
        client = accept(sock, 0, 0);
        if (client == -1) {
            perror("accept()");
            close(client);
            continue;
        }

	threads.push_back(std::thread(std::bind(&consume, std::ref(s), client)));
    }
    close(sock);
    unlink(socket_path);

    for (std::vector<std::thread>::iterator it = threads.begin(); it != threads.end(); ++it) {
        (*it).join();
    }

    return 0;
}
