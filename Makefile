build: client/client.cpp 
	@g++ client/client.cpp -o client/client -pthread  -w  
	@g++ server/server.cpp -o server/server -pthread  -w  
  
clean:  
	-rm client/client  
	-rm server/server  