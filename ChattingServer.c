#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include<errno.h>
#include<arpa/inet.h>
#include<sys/socket.h>
#include<sys/epoll.h>

#define BUF_SIZE 100
#define MAX_CLIENT 256
#define EPOLL_SIZE 50

void setnonblockingmode(int fd);
void error_handling(char *buf);
void send_msg(char *msg, int length);

int client_count = 0;
int client_socket_list[MAX_CLIENT];

int main(int argc, char *argv[]){
	int server_socket, client_socket; 			//서버소켓과 클라이언트 소켓의 파일디스크립터를 담을 변수
	struct sockaddr_in server_addr, client_addr; //서버와 클라이언트의 주소정보를 담을 변수
	socklen_t addr_size; 	// IPv4에 해당하는 주소정보의 크기정보
	int str_len, i; 	// read함수 호출 시 읽어온 바이트수를 저장할 변수와 for-loop때 사용할 index변수
	char buf[BUF_SIZE];  // 읽어온 데이터를 저장해두기 위한 buf

	struct epoll_event *epoll_events;  //epoll_wait 호출 후 변경된 파일디스크립터 정보를 담기 위한 변수
	struct epoll_event event;  //epoll_wait 함수 호출 전 관찰할 파일디스크립터와 이벤트를 등록하기 위한 변수
	int epollfd, event_count;  //epoll을 이용하기 위한 파일디스크립터를 저장하는 변수와 epoll_wait함수가 반환하는 이벤트 갯수를 저장하는 변수

	if(argc!=2){
			printf("Usage : %s <port>\n", argv[0]);
			exit(1);
	}

	server_socket = socket(PF_INET, SOCK_STREAM, 0); // TCP로 설정
	memset(&server_addr, 0, sizeof(server_addr));  //server_addr의 모든 바이트를0으로 초기화
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  //현재 컴퓨터의 IP주소로 설정
	server_addr.sin_port = htons(atoi(argv[1]));  //입력받은 포트번호로 포트 설정

	if(bind(server_socket, (struct sockaddr*) &server_addr, sizeof(server_addr))==-1)
			error_handling("bind() ERROR");  // 소켓에 주소정보 할당
	if(listen(server_socket, 5) == -1)   // 클라이언트 연결 대기
			error_handling("listen() ERROR");
	
	epollfd = epoll_create(EPOLL_SIZE);  // epoll 파일디스크립터 생성
	epoll_events = malloc(sizeof(struct epoll_event) * EPOLL_SIZE);

	setnonblockingmode(server_socket);
	event.events = EPOLLIN;  //수신할 데이터가 존재하는 상황에 대한 이벤트 등록
	event.data.fd = server_socket;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, server_socket, &event); // epoll인스턴스에 server_socket을 등록

	while(1){
		event_count = epoll_wait(epollfd, epoll_events, EPOLL_SIZE, -1); // 이벤트가 발생할 때 까지 대기
																													// 이벤트가 발생한 파일디스크립터 갯수 반환
		if(event_count == -1){
			puts("epoll_wait() ERROR");
			break;
		}
		
		puts("return epoll_wait");
		for(i = 0 ; i < event_count ; i++){
			if(epoll_events[i].data.fd == server_socket){ // server_socket의 변화가 감지 되었을 경우
				addr_size = sizeof(client_addr);
				client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_size);
				
				setnonblockingmode(client_socket);  //non-blocking mode
				event.events = EPOLLIN|EPOLLET;  // Edge-trigger mode
				event.data.fd = client_socket;
				epoll_ctl(epollfd, EPOLL_CTL_ADD, client_socket, &event); //epoll 인스턴스에 방금 생성한 client_socket을 추가함
				client_socket_list[client_count++] = client_socket;  //전역 변수에 client_socket의 fd값 저장

				printf("connected client : %d\n", client_socket);
			}  
			else{  // client_socket에 변화가 감지 되었을 경우
					
				while(1){
						str_len = read(epoll_events[i].data.fd, buf, BUF_SIZE); // buf에 BUF_SIZE만큼 읽어옴

						if(str_len == 0){  // EOF가 전달 되었을 경우
							epoll_ctl(epollfd, EPOLL_CTL_DEL, epoll_events[i].data.fd, NULL);
							close(epoll_events[i].data.fd); //epoll인스턴스에서 해당 client_socket을 제거
										
								for(i = 0 ; i < client_count ; i++){
									if(epoll_events[i].data.fd == client_socket_list[i]){ 
										while(i++ < client_count-1)  // client_socket_list에 빈자리가 없도록 하기 위함
											client_socket_list[i] = client_socket_list[i+1]; 
										break;
									}
								}
										
								client_count--;  // 클라이언트 하나가 종료되었으므로 갯수를 하나 줄임
								printf("closed client : %d\n", epoll_events[i].data.fd);
								break;
						}
						else if(str_len < 0){
							if(errno==EAGAIN) //str_len이 -1이고 errno가 EAGAIN이면 더 이상 읽어올 정보가 없음
								break;
						}
						else{
								send_msg(buf, str_len);
						}
				}
			}
		}
	}

	close(server_socket); //server_socket 종료
	close(epollfd); //epoll 종료
	return 0;

}


void send_msg(char *msg, int length){
	int i;
	for(i = 0 ; i < length ; i++)
			write(client_socket_list[i], msg, length); 
}

void setnonblockingmode(int fd){
	int flag = fcntl(fd, F_GETFL, 0);  //해당 파일디스크립터의 파일상태 flag를 가져옴
	fcntl(fd, F_SETFL, flag|O_NONBLOCK); // 해당 파일디스크립터에 flag에서 가져온 파일상태 + nonblock 설정
}

void error_handling(char *buf){
	fputs(buf, stderr);
	fputc('\n', stderr);
	exit(1);
}
