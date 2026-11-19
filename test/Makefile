all: client server
client:tcp_clt.cc
	g++ -std=c++11 $^ -o $@
server:tcp_scv.cc	
	g++ -std=c++11 $^ -o $@
.PHONY:clean
clean:
	rm -f client server