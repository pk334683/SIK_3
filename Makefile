CC= g++ -std=c++11
CFLAGS= -Wall -c
LFLAGS= -Wall
RFLAGS= -lboost_system -lpthread  -lboost_regex -lboost_program_options -lboost_thread

ALL= serwer klient

objects: $(ALL)

serwer: server.o client_session.o mixer.o constans.h
	$(CC) $(LFLAGS) server.o client_session.o mixer.o -o $@ $(RFLAGS)

klient: client.cpp constans.h
	$(CC) $(LFLAGS) client.cpp -o $@ $(RFLAGS)

test: test.cpp mixer.o
	$(CC) $(LFLAGS) mixer.o test.cpp -o $@ $(RFLAGS)

server.o: server.cpp client_session.h
	$(CC) $(CFLAGS) server.cpp -o $@

client_session.o: client_session.cpp client_session.h
	$(CC) $(CFLAGS) client_session.cpp -o $@

mixer.o: mixer.cpp mixer.h
	$(CC) $(CFLAGS) mixer.cpp -o $@

clean:
	rm -rf *.o $(ALL) *~

#-lboost_system -lpthread -lboost_thread-mt
#-L/usr/local/lib -libboost_thread.a