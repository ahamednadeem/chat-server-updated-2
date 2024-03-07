// A basic chat server in cpp, the clients can send message to all and also send private messages to each other
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <time.h>
#include <bits/stdc++.h>
#include <cstdlib>
#include <errno.h>
#include <signal.h>
#include <thread>
#include <mutex>


using namespace std;

#define BACKLOG 20 
#define MAX_CLIENTS 20  // maximum number of clients in the queue
#define PORT "8060"  // port number of the server
#define GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" //  magic key which is to be concatenated to the key
//pthread_t thread_id;  // a variable thread_id is declared of type pthread_t 

typedef struct client_details  // each client has their own socket id and a name, the name must be unique
{
	int connfd;
	char name[35];
} client_t;

client_t *clients[MAX_CLIENTS];  // an array of structures to store the client information
mutex clients_mutex;

int server_socket; // server socket
class TCPServer 
{
	int create_server()  // created socket and listens for incoming connections
	{
		
	    	struct addrinfo hints, *servinfo, *p;
		memset(&hints, 0, sizeof (hints));  
		
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		
		if ((getaddrinfo (NULL, PORT, &hints, &servinfo)) != 0) // sets the address of the server with the port info. servinfo points to the first element of the linked list.
	    	{
			cout<<"Getaddrinfo error\n";
			exit(1);
		}
		
		int flag = 0;
		for (p = servinfo; p != NULL; p = p -> ai_next) // loop through all the results and bind to the socket in the first we can
	    	{
			server_socket = socket(p -> ai_family, p -> ai_socktype, p -> ai_protocol);  // a server socket is created
			if (server_socket == -1)
			{ 
				continue; 
			} 
			flag = 1;
			break;
		}
		if(!flag)
		{
			cout<<"Socket creation failed\n";
			exit(1);
		}
		
		int yes = 1;
		if (setsockopt (server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) // for reuse of port
		{
			cout<<"setsockopt error";
			close(server_socket);
			exit(1);	
		}
		
		if (bind(server_socket, p -> ai_addr, p -> ai_addrlen) == -1)  // bind the socket to the port and address
		{
			cout<<"Bind Error\n";
			close(server_socket);
			exit(1);
		}
		
		if (listen(server_socket, BACKLOG) == -1)  // server starts to listen 
	    	{ 
			cout<<"Failed to listen\n";
			close(server_socket);
			exit (1); 
		} 
		
	    	cout<<"server listening for connections on port "<<PORT<<"...\n";
		return server_socket;
	    	
 	}
 		
	public:
	int getServerSocket ()
	{
		return create_server();
	}
	
	int connection_accepting() 
	{
		int connfd;
		struct sockaddr_storage their_addr;
		socklen_t sin_size;
		
		sin_size = sizeof(their_addr); 
		connfd = accept(server_socket, (struct sockaddr*)&their_addr, &sin_size);  // accept connection from clients
		if (connfd == -1)
		{
		    cout<<"Error in accepting\n";
		    return -1;
		} 

		//printing the client name
		cout<<"New connection has arrived...\n";
		return connfd;
	}
	
	int getResponse (int client_socket, char *buff,int len)
	{
		return recv (client_socket, buff, len, 0);
	}
	
	
};
			

class WebSocketServer 
{
	TCPServer tcp;
	public:
	
	
	void send_frame(const uint8_t *frame, size_t length, int connfd)   // to send the pong frame to the client
	{
	    ssize_t bytes_sent = send(connfd, frame, length, 0); // we send the pong frame to the client
	    if (bytes_sent == -1)
		cout<<"Failed to send Pong frame\n";
	    else
		cout<<"Pong Frame sent to client\n";
	}

	void send_pong(const char *payload, size_t payload_length, int connfd) 
	{
	    uint8_t pong_frame[128]; 
	    pong_frame[0] = 0xA;  // 0xA is the opcode of pong
	    pong_frame[1] = (uint8_t) payload_length; // we store the payload_length in the 1st index
	    memcpy(pong_frame + 2, payload, payload_length); // now we store the payload data from the 2nd index
	    send_frame(pong_frame, payload_length + 2, connfd);  // we send the updated pong_frame
	}

	void ping(const uint8_t *data, size_t length, int connfd) // handle the ping from the client
	{
	    char ping_payload [126];  // stores the payload 
	    memcpy (ping_payload, data + 2, length - 2); // data + 2 is the payload , length is the total length of the data, length of payload is length - 2
	    ping_payload[length - 2] = '\0';
	    send_pong(ping_payload, length - 2, connfd); // we send the payload data as the pong
	}
	
	//Add newly joined client to list
	void queue_add(client_t *client)
	{
		clients_mutex.lock();  // to lock the resource, this prevents data corruption or race conditions
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if (!clients[i])  // to make sure the same client doesn't gets added in the queue
			{
				clients[i] = client;  // we add the client structure in the array
				break;
			}
		}
		
		clients_mutex.unlock();  // we unlock the resource, hence other threads can access it 
	}
	
	void queue_remove(int connfd)
	{
		clients_mutex.lock();  // to lock the resource, this prevents data corruption or race conditions
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if (clients[i] && clients[i] -> connfd == connfd)  // remove the diconnected client with connfd 
			{
				free (clients[i]);
		    		clients[i] = NULL;
				break;
			}
		}

		clients_mutex.unlock();  // we unlock the resource, hence other threads can access it 


	}

	
	int decode_websocket_frame_header(uint8_t *frame_buffer, uint8_t *fin, uint8_t *opcode, uint8_t *mask, uint64_t *payload_length)  // calculates header size
	{
		 // Extract header bytes and mask
	    	*fin = (frame_buffer[0] >> 7) & 1;  // fin is set during the connection termination phase 
	    	*opcode = frame_buffer[0] & 0x0F; // we perfrom frame_buffer[0] & 0F to get the opcode
	    	*mask = (frame_buffer[1] >> 7) & 1;   // we find if the masking bit is set or not
	    
	    	// Calculate payload length based on header type
	    	*payload_length = frame_buffer[1] & 0x7F;  //0x7F(0111 1111), in MSB Mask Bit is there
	    	if(*payload_length == 126) 
	    	{
			*payload_length = *(frame_buffer + 2);
			*payload_length <<= 8;
			*payload_length |= *(frame_buffer + 3);
			return 4; // we are returning the header size
	    	} 
	    	else if(*payload_length == 127)
	    	{
			*payload_length = 0;
			for (int i = 2; i < 10; ++i)
		    	*payload_length = (*payload_length << 8) | *(frame_buffer + i);  //calculating the payload length
		    	return 10;
	    	}

		
	    	return 2; // if payload length <= 125
	}
	
	int process_websocket_frame(uint8_t *data, size_t length, char **decoded_data, int connfd)   
	{
		uint8_t fin, opcode, mask;
	    	uint64_t payload_length;  // length of the payload(the data/message received from the client)
	    	uint8_t* masking_key;

	    	int header_size = decode_websocket_frame_header(data, &fin, &opcode, &mask, &payload_length);
	    	if (header_size == -1) 
	    	{
			cout<<"Error decoding WebSocket frame header\n";
			return -1;
	    	}
	    
	    	if (mask) // the mask bit will be set
	    	{
		    	masking_key = header_size + data;  // masking bits start after the payload_length
		    	header_size += 4;  // masking requires 4 bytes 
		}
		   
		size_t payload_offset = header_size; // payload_offset says the offset value by which the payload data starts
		if (opcode == 0x9) // if opcode is 9 (ping-pong takes place)
		{
			ping(data, length, connfd);  // send ping data 
			*decoded_data = NULL;
			return 0;
		} 
	    	else if (opcode == 0x8)  // if opcode is 8, then the connection is closed, hence return -1
			return -1; // by returning -1 the current user exits the chat

	    	*decoded_data = (char *)malloc(payload_length + 1);
	    	// we are decoding the data sent by the client
	    	if (mask)
	    		for (size_t i = 0; i < payload_length; ++i)
		     	(*decoded_data)[i] = data[payload_offset + i] ^ masking_key[i % 4];   // we are unmasking the payload data using masking key provided by the client

	    	(*decoded_data)[payload_length] = '\0';
	    	return 0;
	}
	
	int encode_websocket_frame(uint8_t fin, uint8_t opcode, uint8_t mask, uint64_t payload_length, uint8_t *payload, uint8_t *frame_buffer) 
	{
		    // Calculate header size based on payload length
		int header_size = 2;
		if (payload_length <= 125) 
		{
			header_size += 0; // no need to add any additional size to the header
		} 
		else if (payload_length <= 65535) 
		{
			// 2 additional bytes is added to the header size
			header_size += 2;
		} 
		else 
		{
			// 8 additional bytes is added to the header size
			header_size += 8;
		}

		// Encode header bytes
		frame_buffer[0] = (fin << 7) | (opcode & 0x0F);  
		frame_buffer[1] = mask << 7; 
		if (payload_length <= 125) 
			frame_buffer[1] |= payload_length;
		else if (payload_length <= 65535) 
		{
			frame_buffer [1] |= 126;
			frame_buffer [2] = (payload_length >> 8) & 0xFF;
			frame_buffer [3] = payload_length & 0xFF;
		} 
		else 
		{
			frame_buffer [1] |= 127;
			uint64_t n = payload_length;
			for (int i = 8; i >= 1; --i) 
			{
			    frame_buffer [i + 1] = n & 0xFF;
			    n >>= 8;
			}
		}

		    
	 	// Copy payload after header
		memcpy (frame_buffer + header_size, payload, payload_length); // copy payload length to the frame buffer starting from the offset of header_size
		return header_size + payload_length; // Total frame size
	}
	
	void hashing_and_encoding(char *client_key, char *accept_key)  // performs hashing and encoding to the key
	{
	    	char combined_key[2048];
	    	strcpy(combined_key, client_key);   // copy the key to the combined_key array
	    	strcat(combined_key, GUID);         // concatenate the GUID/MAGIC KEY to the combined_key

		// we are performing hashing
	    	unsigned char sha1[SHA_DIGEST_LENGTH]; 
	    	SHA1((unsigned char *)combined_key, strlen(combined_key), sha1); // SHA1 is an algorithm for hashing the key
		//cout<<sha1<<endl;
	    	// we perform base64encoding to the hashed key
	    	BIO *b64 = BIO_new (BIO_f_base64 ());
	    	BIO_set_flags (b64, BIO_FLAGS_BASE64_NO_NL);

	    	BIO *bio = BIO_new (BIO_s_mem ());
	    	BIO_push (b64, bio);

	    	BIO_write (b64, sha1, SHA_DIGEST_LENGTH);
	    	BIO_flush (b64);

	    	BUF_MEM *bptr;
	    	BIO_get_mem_ptr (b64, &bptr);

	    	strcpy (accept_key, bptr -> data);  // we copy the encoded data into the accept_key

	    	size_t len = strlen(accept_key);
	    	if (len > 0 && accept_key[len - 1] == '\n')
			accept_key [len - 1] = '\0'; // we are removing the trailing newline character
		accept_key [28] = '\0'; // we are removing the trailing newline character
	    	BIO_free_all (b64);
	}
	
	void handle_websocket_upgrade (int client_socket, char *request) 
	{
		if (strstr(request, "Upgrade: websocket") == NULL)   // checking whether it is an upgrade request
	    	{
			cout<<"Not an upgrade request\n";
			return;
	    	}
	    
	    	//extracting the key from the request
	    	char *key_start = strstr(request, "Sec-WebSocket-Key:"); 
	    	key_start += 19;
	    	char *key_end = strstr(key_start, "\n");
	    	key_end -= 1;
	    
	    	char key[256];
	    	memset(&key, '\0', strlen(key));
	    	
	    	int i = 0; 
	    	while(*key_start != *key_end)
	    		key[i++] = *key_start++;
		key[i] = '\0';
	    	key_start = key;
	    	cout<<key<<endl;
	    	
	    	//now we have to perform hashing and encoding 
		cout<<"Initialising Websocket Handshake...\n";
		
	    	// Calculate Sec-WebSocket-Accept header
	    	char accept_key[1024];
	    	hashing_and_encoding(key_start, accept_key); // this is done to ensure the security, integrity, and authenticity of the handshake request and response
		cout<<accept_key<<"\n";
	    	// Send WebSocket handshake response to the client
	    	char response [2048];
	    	sprintf(response, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_key);
	    	//cout<<client_socket<<endl;
	    	send(client_socket, response, strlen (response), 0); 
	    	 // send the response to the client, hence handshake is complete
		cout<<"Websocket handshake completed\n\n";
	}
	
	
	int getServerSocketfromtcp()
	{
		return tcp.getServerSocket();
	}
	
	int webSocketCreate()
	{
		char buffer[2048];
		int client_socket = tcp.connection_accepting();
        	if (client_socket == -1)
            		return -1;
            	int len = tcp.getResponse(client_socket, buffer, 2048);
        	buffer[len] = '\0';  // this is now storing the upgrade request from the client
		
        	handle_websocket_upgrade(client_socket, buffer); // perform handshake process
        	return client_socket;
	}
};
	
class ChatServer
{
	WebSocketServer websocket;  // creating object
	public:
	void start_server()
	{	
		int server_socket = websocket.getServerSocketfromtcp();
		while(1)
		{
			int connfd = websocket.webSocketCreate();
			if (connfd < 0)
				continue;

			thread clientThread(&ChatServer::handle_client, this, connfd); //here we call handle_clients function passing new_conndf as argument
			clientThread.detach();
			
			
		}
		close(server_socket);
	}
	
	int send_websocket_frame(int client_socket, char *username, uint8_t fin, uint8_t opcode, char *payload) 
	{
	    uint8_t encoded_data[1024];
	    // Encode the WebSocket frame before sending 
	    int encoded_size = websocket.encode_websocket_frame(fin, opcode, 0, strlen (payload), (uint8_t *)payload, encoded_data);  // encode the data before sending
	    // Send the encoded message to the client
	    ssize_t bytes_sent = send(client_socket, encoded_data, encoded_size, 0);
	    if (bytes_sent == -1) 
	    {
		cout<<"Failed to send\n";
		return -1;
	    }
	    return 0;
	}
	
	void broadcast_message(char* message, int sender_connfd) 
	{
	    	for (int i = 0; i < MAX_CLIENTS; i++)
		{
			if (clients[i] && clients[i] -> connfd != sender_connfd) // we make sure we don't send the broadcast message to the user who has joined
				send_websocket_frame(clients[i] -> connfd, clients[i] -> name, 1, 1, message);  // we have set the opcode as 1 (to send text message)
		}
	}
	
	void handle_client (int arg) 
	{
		int connfd = arg;
		int status; // connfd is the client socket
    		uint8_t name[35];
    		char *decoded_name = NULL;
	
    		if (recv(connfd, name, sizeof(name), 0) <= 0)  // we receive the name of the user firstly, this name has to be unique
    		{
        		cout<<"error in receiving username\n";
        		close(connfd);
        		return;
    		}
    		
    		status = websocket.process_websocket_frame(name, sizeof(name), &decoded_name, connfd);  // we have to process the web socket frame after receiving the username
	    	if (status == -1)  // some error has occured
	    	{
	    		//printf("Error processing WebSocket frame\n");
			return;
	    	}
	    	
	    	client_t* new_client = (client_t*)malloc(sizeof(client_t));  // we are creating a structure for each clients to store their details
    	
	    	new_client -> connfd = connfd;
	    	strcpy(new_client -> name, decoded_name);  // copy the decoded name to the name member of new_client
	    	
	    	websocket.queue_add(new_client);  // we add the new_client in a queue

	    	// Notify all clients about the new user
	    	char message [100];
	    	sprintf(message, "%s has joined the chat.\n", new_client -> name); 
	    	cout<<message;
	    	broadcast_message(message, connfd);  // we broadcast the above message to all existing clients except the sender

	    	// Receive and broadcast messages
	    	while (1) 
	    	{
			uint8_t buffer[1024];
			char receiver_name[35];
			char *decoded_data = NULL;  // this stores the decoded message 
			memset(&buffer, '\0', strlen((char*)buffer));
			ssize_t bytes_received = recv(connfd, buffer, sizeof (buffer), 0); // receive the message from the client
			if (bytes_received <= 0)
			{
		    		break;
		    	}
		    	
			buffer[bytes_received] = '\0';
			//cout<<"\nMessage got: "<<buffer<<"\n";
			int status = websocket.process_websocket_frame(buffer, bytes_received, &decoded_data, connfd); 
			if (status == -1)
			{	
		    		break;
		    	}
			else if (status != 0) 
			{
		    		cout<<"Error processing WebSocket frame\n";
		    		close(connfd);
		    		continue;
			} 

			char full_message [65535];
			//cout<<"Decoded data: "<<decoded_data<<"\n";
			
			if (strstr(decoded_data, "send_to:") && (strlen(decoded_data) != strlen("send_to:")))
			{
				char *privates = strstr(decoded_data, "send_to:");  // send_to:ahamed:your message to be sent
				privates += 8;
				char *end = strstr(privates, ":");
				int i = 0;
				while(privates != end)
				{
					receiver_name[i++] = *privates++;
				}
				receiver_name[i] = '\0';
				cout<<"receiver's name: "<<receiver_name<<"\n";
				end += 1;  // this is the message
				cout<<"message: "<<end<<"\n";
				
				char sender_name[35];
				char *p = sender_name;
				for(int k = 0; k < MAX_CLIENTS; k++)
				{
					if(clients[k] -> connfd == connfd)
					{
						 p = (clients[k] -> name);
						break;
					}
				}
				
				char new_msg[24000] = "Received private message from ";
				strcat(new_msg, p);
				strcat(new_msg, " : ");
				strcat(new_msg, end);
				cout<<"new message: "<<new_msg<<"\n";
			
		   		int j;
	    			for (j = 0; j < MAX_CLIENTS; j++)
				{
					if (clients[j] && strcmp(clients[j] -> name, receiver_name) == 0)   // to find the connfd of that individual client
					{
						uint8_t encoded_data[24000]; // Encode the WebSocket frame before sending 
	    					int encoded_size = websocket.encode_websocket_frame(1, 1, 0, strlen(new_msg), (uint8_t *)new_msg, encoded_data);  
	   					 // Send the encoded message to the client
	    					send(clients[j] -> connfd, encoded_data, encoded_size, 0);
	    					break;
	    					
					}
				}
				if (j == MAX_CLIENTS)
				{
					char c[] = "User not found!!!\n";
					send_websocket_frame(connfd, receiver_name, 1, 1, c);
				}
			}
			else  // normal message, send the message to all the user
			{
		    		sprintf(full_message, "%s: %s", new_client -> name, decoded_data);
		    		// Broadcast the message to all clients
		    		broadcast_message (full_message, connfd);
	       	}
		}    
		
		// Notify all clients about the user leaving
	    	sprintf (message, "%s has left the chat.\n", new_client -> name);
	    	cout<<message;
	    	broadcast_message (message, connfd);

	    	// Remove the disconnected client from the list
	    	websocket.queue_remove(connfd);
	    	// Close the connection
	    	close(connfd);	    
	}		
};	

int main ()
{
    	ChatServer chat;
    	chat.start_server();
    	return 0;
}
