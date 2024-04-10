server: main.cpp ./threadpool/threadpool.h ./http_parser/http_parser.cpp ./http_parser/http_connection.h ./lockWrapper/lock.h  ./log/log.cpp ./log/log.h ./log/block_queue.h
	g++ -o server main.cpp ./threadpool/threadpool.h ./http_parser/http_parser.cpp ./http_parser/http_connection.h ./lockWrapper/lock.h  ./log/log.cpp ./log/log.h  -lpthread -g


clean:
	rm  -r server