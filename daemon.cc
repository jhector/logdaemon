#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <iostream>
#include <cstdlib>
#include <csignal>
#include <climits>
#include <utility>
#include <string>
#include <cstdio>
#include <set>

std::set<std::string> issues;
const char *socket_path;

void sig_handler(int signo)
{
    if (signo == SIGINT) {
        unlink(socket_path);
        exit(0);
    }
}

void usage(const char* prog)
{
    std::cout << prog << " <socket path>" << std::endl;
    exit(1);
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

    listen(sock, 5);
    while (1) {
        client = accept(sock, 0, 0);
        if (client == -1) {
            perror("accept()");
            close(client);
            continue;
        }

        do {
            bzero(buf, sizeof(buf));
            if ((rval = read(client, buf, sizeof(buf))) < 0) {
                perror("read()");
            } else if (rval == 0) {
                continue;
            } else {
                char *pos = strchr(buf, ';');
                if (!pos) {
                    std::cerr << "Unable to find ';'" << std::endl;
                    continue;
                }

                *pos = 0x0;
                bzero(real_path, sizeof(real_path));
                realpath(buf, &real_path[0]);

                bzero(log_buf, sizeof(log_buf));
                snprintf(log_buf, sizeof(log_buf), "%s;%s", real_path, pos+1);

                auto ret = issues.insert(log_buf);
                if (ret.second) {
                    std::cout << log_buf << std::endl;
                    fflush(stdout);
                }
            }
        } while (rval > 0);
        close(client);
    }
    close(sock);
    unlink(socket_path);

    return 0;
}
