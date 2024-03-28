server: main.cpp ./threadpool/threadpool.h ./http_parser/http_parser.cpp ./http_parser/http_connection.h ./lockWrapper/lock.h 
	g++ -o server main.cpp ./threadpool/threadpool.h ./http_parser/http_parser.cpp ./http_parser/http_connection.h ./lockWrapper/lock.h  -lpthread 


clean:
	rm  -r server