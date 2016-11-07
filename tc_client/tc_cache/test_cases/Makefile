all: tc1 tc2 tc3 tc4
tc1: lru_eviction.cpp
	g++ -Wextra -Wreorder -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x lru_eviction.cpp -o lru_eviction

tc2: expire_eviction.cpp
	g++ -Wextra -Wreorder -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x expire_eviction.cpp -o expire_eviction

tc3: expire_lru_eviction.cpp
	g++ -Wextra -Wreorder -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x expire_lru_eviction.cpp -o expire_lru_eviction

tc4: thread_safe.cpp
	g++ -Wextra -Wreorder -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x thread_safe.cpp -o thread_safe

clean:
	rm -rf lru_eviction
	rm -rf expire_eviction
	rm -rf expire_lru_eviction
	rm -rf thread_safe
