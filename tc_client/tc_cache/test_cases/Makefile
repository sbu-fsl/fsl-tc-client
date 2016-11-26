all: tc1 tc2 tc3 tc4 tc5 tc6
tc1: lru_eviction.cpp
	g++ -Wextra -Wreorder -g -O0 -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x lru_eviction.cpp -o lru_eviction

tc2: expire_eviction.cpp
	g++ -Wextra -Wreorder -g -O0 -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x expire_eviction.cpp -o expire_eviction

tc3: expire_lru_eviction.cpp
	g++ -Wextra -Wreorder -g -O0 -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x expire_lru_eviction.cpp -o expire_lru_eviction

tc4: thread_safe1.cpp
	g++ -Wextra -Wreorder -g -O0 -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x thread_safe1.cpp -o thread_safe1

tc5: thread_safe2.cpp
	g++ -Wextra -Wreorder -g -O0 -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x thread_safe2.cpp -o thread_safe2

tc6: shared_pointer.cpp
	g++ -Wextra -Wreorder -g -O0 -I /usr/local/include/Poco -lPocoUtil -lPocoNet -lPocoXML -lPocoFoundation -lPocoJSON -std=c++0x shared_pointer.cpp -o shared_pointer

clean:
	rm -rf lru_eviction
	rm -rf expire_eviction
	rm -rf expire_lru_eviction
	rm -rf thread_safe1
	rm -rf thread_safe2
	rm -rf shared_pointer
