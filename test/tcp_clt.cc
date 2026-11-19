#include "../source/Server.hpp"

int main()
{
    Socket cli_sock;
    cli_sock.CreateClient(8080, "127.0.0.1");
    std::string str = "hello lcx";

    cli_sock.Send(str.c_str(), str.size());

    char buffer[1024] = {0};
    cli_sock.Recv(buffer, 1024);

    DBG_LOG("%s", buffer);

    return 0;
}