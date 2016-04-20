#include "common.h"

/* IPC ключ */
key_t IPC_key;

/* IPC дескриптор для массива IPC семафоров
 * в массиве 2 семафора
 * 0 - для контроля количества экземпляров на одном объекте
 * 1 - для контроля доступа к разделяемой памяти
 */

int semid;
int semSharedMemoryCtrl;
#define semCnt 2
#define semSharedMemoryNum 1

/* Структура для задания операции над семафором */
struct sembuf my_sembuf;
//идентификатор очереди команд
int qid;

#define maxClients 40
#define sharedMemSize sizeof(clients)
int shmid; //айдишник для доступа к разделяемой памяти
client_t *clients; //адрес начала блока разделяемой памяти

void clientsListInit()
{
	for(int i=0;i<maxClients;i++)
	{
		clients[i].pid = -1; //клиент с pid -1 считается несуществующим
		clients[i].qid = -1;
		clients[i].msgInCnt = 0;
		clients[i].msgOutCnt = 0;
		clients[i].msgErrCnt = 0;
	}
}

mess_data_t inputData;
mess_data_t outputData;

#define buffer_size 2048 //размер буффера для чтения из потока вывода ребенка
char reading_buf[buffer_size]; //буффер
char writing_buf[buffer_size]; //буффер
int bytes = 0; //смещение в буфере чтения
int RSTDOUT[2]; //1-запись, 0 - чтение
int RSTDIN[2]; //1-запись, 0 - чтение
int RSTDERR[2]; //1-запись, 0 - чтение
time_t lastio; //таймауты для select()
int logfileno = 2;
pid_t child;


struct sembuf enterBuf;
void sharedEnter(int semid)
{
	if(semop(semid, &enterBuf, 1) == -1)
	{
		perror("Can not enter shared memory sem");
		exit(EXIT_FAILURE);
	}
}

struct sembuf leaveBuf;
void sharedLeave(int semid)
{
	if(semop(semid, &leaveBuf, 1) == -1)
	{
		perror("Can not leave shared memory sem");
		exit(EXIT_FAILURE);
	}
}

/*
 * Проверяет живы ли клиенты один раз в пять секунд
 * отменяет регистрацию мертвых клиентов
 */
void *isClientAlive_fnc(void *ptr)
{
	while(1)
	{
		sharedEnter(semid);
		for(int i=0;i<maxClients;i++)
		{
			if(clients[i].pid != -1 && kill(clients[i].pid, 0)==-1)
			{
				clientUnregister(clients[i].pid);
			}
		}
		sharedLeave(semid);
		sleep(5);
	}
}

void clientRegister(pid_t c_pid,int c_qid)
{
	mess_srvAnswer_t answer;
	int i=0;
	
	sharedEnter(semid);
	for(;i<maxClients;i++)
	{
		if(clients[i].pid == -1)
		{
			clients[i].pid = c_pid;
			clients[i].qid = c_qid;
			clients[i].msgInCnt = 0;
			clients[i].msgOutCnt = 0;
			clients[i].msgErrCnt = 0;
			break;
		}
	}
	sharedLeave(semid);
	
	if(i==maxClients)
	{
		answer.status = statusRefused;
	}
	else
	{
		answer.status = statusOK;
	}	
	answer.mtype = msgSrvAnswerType;
	answer.pid = getpid();
	msgsnd(c_qid, &answer, msgAnswerLength, 0);
}

void clientUnregister(pid_t clientPid)
{	
	for(int i=0;i<maxClients;i++)
	{
		if(clients[i].pid == clientPid)
		{
			clients[i].pid = -1;
		}
	}
}

void *listner_fnc(void *ptr)
{
	mess_command_t received;
	while(1)
	{
		/* Receives the message */
		if(msgrcv(qid, &received, msgCmdLength, msgCommandType, 0) == -1)
		{
			perror("Error receiving data");
			exit(EXIT_SUCCESS);
		}
		
		switch(received.cmd)
		{
			case registerCmd:
			{
				clientRegister(received.pid,received.qid);
				break;
			};
			case statusCmd:
			{
				
				break;
			};
			case unregisterCmd:
			{
				sharedEnter(semid);
				clientUnregister(received.pid);
				sharedLeave(semid);
				break;
			};
			default:
			{
				break;
			}			
		}
		
		usleep(5000);
	}
}

/*берет данные из буфера reading_buf и раздает всем клиентам */
void broadcastMsgData(int stream)
{
	mess_data_t sdata;
	sdata.mtype = msgOutDataType;
	sdata.pid = getpid();
	sdata.stream = stream;
	memcpy(sdata.data,reading_buf,buffer_size);	
	
	sharedEnter(semid);
	for(int i=0;i<maxClients;i++)
	{
		if(clients[i].pid != -1)
		{
			msgsnd(clients[i].qid, &sdata, msgDataLength, 0);
			if(stream==2)
			{
				clients[i].msgErrCnt++;
			}
			else
			{
				clients[i].msgOutCnt++;
			}
		}
	}
	sharedLeave(semid);
}

void *clientsHandler_fnc(void *ptr)
{	
	while(1)
	{
		sharedEnter(semid);
		for(int i=0;i<maxClients;i++)
		{
			if(clients[i].pid != -1)
			{
				int status = msgrcv(clients[i].qid, &inputData, msgDataLength, msgInDataType, IPC_NOWAIT);
				if(status>0)
				{
					memcpy(writing_buf,inputData.data,buffer_size);
					clients[i].msgInCnt++;
					handle_input(RSTDIN[PIPE_WRITE]);
				}
			}
		}
		sharedLeave(semid);
		usleep(10000);
	}
}

void sign_handler(int signo, siginfo_t *siginf, void *ptr)
{
	switch(signo)
	{
		case SIGCHLD:
		{
			printf("%d TERMINATED WITH EXIT CODE: %d\n", siginf->si_pid, siginf->si_status);
			exit(EXIT_SUCCESS);
			break;
		};
		case SIGPIPE:
		{
			printf("Listner is dead..\n");
			exit(EXIT_SUCCESS);
			break;
		};
		case SIGINT:
		{
			printf("SIGINT\n");
			/*Удаляет семафор*/
			if(semctl( semid, 0, IPC_RMID, 0 )==-1)
			{
				perror("Error deleting sem!");
				exit(EXIT_FAILURE);
			}
			
			/* Destroys the queue */
			if(msgctl(qid, IPC_RMID, 0) == -1)
			{
				perror("Error deleting queue!");
				exit(EXIT_FAILURE);
			}
			
			/*отцепить область разделяемой памяти*/
			if(shmdt(clients) == -1)
			{ 
				perror("Can't detach shared memory");
				exit(EXIT_FAILURE);
			}
			
			exit(EXIT_SUCCESS);
			break;
		};
		default:
		{
			break;
		};
	}
	return;
}

void SIGNIO_handler(int signo, siginfo_t *siginf, void *ptr)
{
	lastio = time(NULL);
	if(siginf->si_fd == RSTDOUT[PIPE_READ])
	{
		handle_output(RSTDOUT[PIPE_READ]);
	}
	if(siginf->si_fd == RSTDERR[PIPE_READ])
	{
		handle_output(RSTDERR[PIPE_READ]);
	}
	return;
}

int isSingleOnFile(int semid)
{
	/*
	Получает значение семафора
	*/
	int sem_value = semctl(semid, 0, GETVAL, 0);
    if(sem_value<0)
    {
		perror("semctl");
		exit(EXIT_FAILURE);
    }
 
	/*
	Если обнаруживаем, что значение семафора больше 0,
	т.е. уже запущена другая копия программы
	*/
	if(sem_value > 0)
	{
		return 0;
	}
	else
	{
		return 1;
	}
}

int handle_output(int fd)
{
	while((read(fd, reading_buf+bytes, 1)) > 0)
	{
		bytes++;
		if(reading_buf[bytes-1]=='\n' || bytes==buffer_size)
		{
			int stream = 1;
			if(fd==RSTDERR[PIPE_READ]) stream = 2;
			broadcastMsgData(stream);
			LOG(reading_buf,bytes,stream);
			memset(reading_buf,0,buffer_size);
			bytes=0;
			return 0;
		}
	}
	
	return 0;
}

void LOG(char *buf, int size,int stream)
{
	char *dtm = get_datetime();
	if(stream>=0)
	{
		char buffer[80];
		if(stream==0)
			sprintf(buffer," >0 ");
		else
			sprintf(buffer," <%d ",stream);
		write(logfileno,dtm,strlen(dtm));
		write(logfileno,buffer,strlen(buffer));
		write(logfileno,buf,size);
	}
	else
	{
		char buffer[70];
		strcpy(buffer,dtm);
		strcat(buffer," NOIO\n");
		write(logfileno,buffer,strlen(buffer));
	}
	free(dtm);
}

/* берет данные из writing_buf и отдает дочернему приложению через pipe */
int handle_input(int fd)
{
	int len = 0;
	len = strlen(writing_buf);
	writing_buf[len] = '\n';
	len++;
	write(fd,writing_buf,len);
	LOG(writing_buf,len,0);
	return 0;
}

void proc_manager(params *cmd)
{	
	
	enterBuf.sem_op = -1;
	enterBuf.sem_flg = 0;
	enterBuf.sem_num = semSharedMemoryNum;
	leaveBuf.sem_op = 1;
	leaveBuf.sem_flg = 0;
	leaveBuf.sem_num = semSharedMemoryNum;
	
	/* IPC ключ */
	IPC_key = ftok(cmd->ftokFilePath, ProjId);
	if(IPC_key<0)
	{
		perror("ftok error");
		exit(EXIT_FAILURE);
	}
	
	/*
	Пытаемся получить доступ по ключу к массиву семафоров,
	если он существует, или создать его из одного семафора,
	если его еще не существует, с правами доступа
	read & write для всех пользователей
    */
	semid = semget( IPC_key, semCnt, 0666 | IPC_CREAT );
	if(semid == -1)
	{
		perror("Can\'t get semaphore id");
		exit(EXIT_FAILURE);
	}
	
	if(!isSingleOnFile(semid))
	{
		printf("That ftok object already in use!\n");
		exit(EXIT_SUCCESS);
	}
	
	/* увеличение значения семафора на 1 */
	my_sembuf.sem_op = 1;
	my_sembuf.sem_flg = 0;
	my_sembuf.sem_num = 0;
	if(semop(semid, &my_sembuf, 1))
	{
		perror("Error increment semaphore!\n");
		exit(EXIT_FAILURE);
	}
	if(semctl(semid, 1, SETVAL, 1) == -1)
	{
		perror("semctl");
		exit(-1);
	}
	
	/* создать общедоступную очередь */
	qid = msgget(IPC_key, IPC_CREAT | 0666);
	
	/* создать общедоступную память */
	shmid = shmget(IPC_key, sharedMemSize, 0666 | IPC_CREAT);
	if (shmid == -1)
	{
		perror("shmget error");
		exit(EXIT_FAILURE);
	}
	
	/* получить указатель на начало области разделяемой памяти */
	clients = (client_t*)shmat(shmid, NULL, 0);
	if(clients==NULL)
	{
		perror("shmat error");
		exit(EXIT_FAILURE);
	}
	
	clientsListInit();
	
	pthread_t listner_thr;
	pthread_create(&listner_thr, NULL, listner_fnc, NULL);
	pthread_detach(listner_thr);
	
	pthread_t isClientAlive_thr;
	pthread_create(&isClientAlive_thr, NULL, isClientAlive_fnc, NULL);
	pthread_detach(isClientAlive_thr);
	
	pthread_t clientsHandler_thr;
	pthread_create(&clientsHandler_thr, NULL, clientsHandler_fnc, NULL);
	pthread_detach(clientsHandler_thr);
	
	//////////////////////////////
	
	logfileno = cmd->logfile_descr;
	lastio = time(NULL);
	struct sigaction sa;
	sigemptyset(&(sa.sa_mask));
	sigfillset(&(sa.sa_mask));
	
	sa.sa_flags = SA_SIGINFO|SA_NOCLDWAIT|SA_RESTART; //use siginfo
	sa.sa_sigaction = sign_handler; //set signals handler
	if(sigaction(SIGPIPE, &sa, 0)==-1 || sigaction(SIGCHLD, &sa, 0)==-1 || sigaction(SIGINT, &sa, 0))
	{
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	
	if(pipe(RSTDOUT)==-1 || pipe(RSTDIN)==-1 || pipe(RSTDERR)==-1)
	{
		perror(NULL);
		exit(EXIT_FAILURE);
	}
	
	add_flags(RSTDOUT[PIPE_READ],O_NONBLOCK);
	add_flags(RSTDOUT[PIPE_WRITE],O_NONBLOCK);
	add_flags(RSTDERR[PIPE_READ],O_NONBLOCK);
	add_flags(RSTDERR[PIPE_WRITE],O_NONBLOCK);
	
	child = fork();
	switch(child)
	{
		case -1:
		{
			perror(NULL);
			exit(EXIT_FAILURE);
			break;
		};
		case 0: //fork
		{			
			//перенаправить stdin
			if (dup2(RSTDIN[PIPE_READ], STDIN_FILENO) == -1)
			{
				perror("redirecting stdin");
				exit(EXIT_FAILURE);
			}
			// перенаправить stdout
			if (dup2(RSTDOUT[PIPE_WRITE], STDOUT_FILENO) == -1)
			{
				perror("redirecting stdout");
				exit(EXIT_FAILURE);
			}
			// перенаправить stderr
			if (dup2(RSTDERR[PIPE_WRITE], STDERR_FILENO) == -1)
			{
				perror("redirecting stderr");
				exit(EXIT_FAILURE);
			}
			
			close(RSTDIN[PIPE_READ]);
			close(RSTDIN[PIPE_WRITE]);
			close(RSTDOUT[PIPE_READ]);
			close(RSTDOUT[PIPE_WRITE]);
			close(RSTDERR[PIPE_READ]);
			close(RSTDERR[PIPE_WRITE]);
			
			char **argv;
			int argc=0;
			argv = string_to_argv(cmd->execute,&argc);
			
			execvp(argv[0], argv);
			perror("Call exec() failed!");
			exit(EXIT_FAILURE);
		};
		default: //батька
		{
			close(RSTDIN[PIPE_READ]);
			close(RSTDOUT[PIPE_WRITE]);
			close(RSTDERR[PIPE_WRITE]);
			
			if(cmd->multiplex==1) multiplexer_select(cmd);
			else if(cmd->multiplex==0) multiplexer_signal(cmd);
			
			break;
		};
	}
}

void multiplexer_select()
{
	fd_set rfds,wfds; //набор дескрипторов потоков с которых нужно ожидать чтение
	fd_set rfdscopy = rfds,wfdscopy = wfds; //select портит fd_set и таймауты
	struct timeval tv; //таймауты для select()
	struct timeval tvcopy = tv;
	int retval;
	FD_ZERO(&rfds); //очистить набор
	FD_SET(RSTDOUT[PIPE_READ], &rfds); //добавить дескриптор в набор
	FD_SET(RSTDERR[PIPE_READ], &rfds); //добавить дескриптор в набор
	tv.tv_sec = 1; //ждать максимум n секунд(после выхода из select()
	tv.tv_usec = 0; //значение таймаута будет сброшено)
	int maxfd = max(RSTDOUT[PIPE_READ],RSTDERR[PIPE_READ]);
	
	while(1)
	{
		tvcopy = tv;
		rfdscopy = rfds; wfdscopy = wfds;
		retval = select(maxfd+1, &rfdscopy, NULL, NULL, &tvcopy);
		
		if(retval<0)
		{
			if(errno==EINTR) continue; //если выход из селекта из-за сигнала
			perror("Call select() failed");
			exit(EXIT_FAILURE);
		}
		else if(retval>0)
		{
			if(FD_ISSET(RSTDOUT[PIPE_READ], &rfdscopy))
			{
				handle_output(RSTDOUT[PIPE_READ]);
			}
			if(FD_ISSET(RSTDERR[PIPE_READ], &rfdscopy))
			{
				handle_output(RSTDERR[PIPE_READ]);
			}
		}
		else
		{
			LOG(NULL,0,-1);
		}
	}
	close(RSTDIN[1]);
	exit(EXIT_SUCCESS);	
}

void multiplexer_signal()
{
	struct sigaction sa2;
	sigemptyset(&(sa2.sa_mask));
	sigfillset(&(sa2.sa_mask));	
	sa2.sa_flags = SA_SIGINFO|SA_RESTART;
	sa2.sa_sigaction = SIGNIO_handler;
	
	if(sigaction(SIGIO, &sa2, 0)==-1)
	{
		perror(NULL);
		exit(EXIT_FAILURE);		
	}	
	
	add_flags(RSTDOUT[PIPE_READ],O_NONBLOCK|FASYNC);
	add_flags(RSTDERR[PIPE_READ],O_NONBLOCK|FASYNC);
	add_flags(STDOUT_FILENO,O_NONBLOCK);
	
	fcntl(RSTDOUT[PIPE_READ], F_SETSIG, SIGIO);
	fcntl(RSTDOUT[PIPE_READ], F_SETOWN, getpid());
	fcntl(RSTDERR[PIPE_READ], F_SETSIG, SIGIO);
	fcntl(RSTDERR[PIPE_READ], F_SETOWN, getpid());
	
	while(1) 
	{		
		sleep(1);
		if(time(NULL)>lastio)
		{
			LOG(NULL,0,-1);
			lastio = time(NULL);
		}
	}
}
