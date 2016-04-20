#include "common.h"

/* IPC ключ */
key_t IPC_key;
// IPC дескриптор для массива IPC семафоров
int semid;
/* Структура для задания операции над семафором */
struct sembuf my_sembuf;
//идентификатор очереди
int qid;

#define maxClients 40
client_t clients[maxClients];

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

list_t *clientFirst;
list_t *clientLast;
mess_data_t inputData;
mess_data_t outputData;

int clientsCount = 0; //текущее количество зарег. клиентов

#define buffer_size 2048 //размер буффера для чтения из потока вывода ребенка
pthread_mutex_t child_dead = PTHREAD_MUTEX_INITIALIZER;
volatile int child_dead_var = 0;
char reading_buf[buffer_size]; //буффер
char writing_buf[buffer_size]; //буффер
int bytes = 0; //смещение в буфере чтения
int byteswr = 0; //смещение в буфере записи
int RSTDOUT[2]; //1-запись, 0 - чтение
int RSTDIN[2]; //1-запись, 0 - чтение
int RSTDERR[2]; //1-запись, 0 - чтение
time_t lastio; //таймауты для select()
int logfileno = 2;
pid_t child;

/*
 * Проверяет живы ли клиенты один раз в пять секунд
 * отменяет регистрацию мертвых клиентов
 */
void *isClientAlive_fnc(void *ptr)
{
	while(1)
	{
		list_t *cur = clientFirst;
		while(cur!=NULL)
		{
			if(kill(cur->RClient.pid, 0)==-1)
			{
				printf("client dead. pid %d\n",cur->RClient.pid);
				clientUnregister(cur->RClient.pid);
			}
			cur = cur->next;
		}
		sleep(5);
	}
}

void clientRegister(client_t *cl)
{
	list_t *cur = (list_t*)malloc(sizeof(list_t));
	if(cur==NULL)
	{
		perror("error memory alloc");
		exit(EXIT_FAILURE);
	}
	memcpy(&(cur->RClient),cl,sizeof(client_t));
	
	
	if(clientFirst==NULL)
	{		
		cur->next = NULL;
		clientFirst = cur;
		clientLast = cur;
	}
	else
	{
		cur->next = NULL;
		clientLast->next=cur;
		clientLast = cur;
	}
	
	mess_srvAnswer_t answer;
	answer.mtype = msgSrvAnswerType;
	answer.pid = getpid();
	answer.status = statusOK;
	msgsnd(clientLast->RClient.qid, &answer, msgAnswerLength, 0);
}
void clientUnregister(pid_t clientPid)
{
	list_t *cur = clientFirst;
	list_t *prev = clientFirst;
	while(cur!=NULL && cur->RClient.pid!=clientPid)
	{
		prev = cur;
		cur = cur->next;
	}
	if(cur!=NULL)
	{
		if(cur == clientFirst)
		{
			clientFirst = NULL;			
		}
		else
		{
			prev->next = cur->next;
		}
		free(cur);
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
				client_t clientNew;
				clientNew.qid = received.qid;
				clientNew.pid = received.pid;
				clientNew.msgInCnt = 0;
				clientNew.msgOutCnt = 0;
				clientNew.msgErrCnt = 0;
				clientsCount++;
				clientRegister(&clientNew);					
				printf("+client pid %d\n",clientLast->RClient.pid);
				break;
			};
			case statusCmd:
			{
				
				break;
			};
			case unregisterCmd:
			{
				clientUnregister(received.pid);
				clientsCount--;
				printf("-client pid %d\n",received.pid);
				break;
			};
			default:
			{
				break;
			}
			
		}
		printf("MESSAGE RECEIVED...\n");
		
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
	list_t *current = clientFirst;
	
	while(current!=NULL)
	{
		msgsnd(clientLast->RClient.qid, &sdata, msgDataLength, 0);
		if(stream==2)
		{
			current->RClient.msgErrCnt++;
		}
		else
		{
			current->RClient.msgOutCnt++;
		}
		current = current->next;
	}	
}

void *clientsHandler_fnc(void *ptr)
{	
	while(1)
	{
		list_t *current = clientFirst;
		
		while(current!=NULL)
		{
			int status = msgrcv(current->RClient.qid, &inputData, msgDataLength, msgInDataType, IPC_NOWAIT);
			if(status>0)
			{
				memcpy(writing_buf,inputData.data,buffer_size);
				current->RClient.msgInCnt++;
				handle_input(RSTDIN[PIPE_WRITE]);
			}
			current = current->next;
		}
		usleep(1000);
	}
}

void sign_handler(int signo, siginfo_t *siginf, void *ptr)
{
	switch(signo)
	{
		case SIGCHLD:
		{
			printf("%d TERMINATED WITH EXIT CODE: %d\n", siginf->si_pid, siginf->si_status);
			//exit(EXIT_SUCCESS);
			child_dead_var =1;
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
		//gettimeofday(&lastio,NULL);
		handle_output(RSTDOUT[PIPE_READ]);
	}
	if(siginf->si_fd == RSTDERR[PIPE_READ])
	{
		//gettimeofday(&lastio,NULL);
		handle_output(RSTDOUT[PIPE_READ]);
	}
	if(siginf->si_fd == STDIN_FILENO)
	{
		//gettimeofday(&lastio,NULL);
		handle_input(RSTDIN[PIPE_WRITE]);
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

/*
 * Возвращает дату и время в формате DD.MM.YYYY/HH:MM:SS
 */
char *get_datetime()
{
	char *datetime = (char*)malloc(30);
	time_t rawtime;
	struct tm *timeinfo;
	time (&rawtime);
	timeinfo = localtime(&rawtime);
	sprintf (datetime,"%d.%d.%d/%d:%d:%d",timeinfo->tm_mday,timeinfo->tm_mon+1, timeinfo->tm_year+1900,
												timeinfo->tm_hour,timeinfo->tm_min, timeinfo->tm_sec);
	return datetime;
}

const char delims[] = {'\t','\n',' '};
int delims_count = sizeof(delims)/sizeof(delims[0]);
int is_delimetr(char symbol) //return 1 if symbol is delimeter or 0
{
	for(int i=0;i<delims_count;i++)
	{
		if(delims[i]==symbol) return 1;
	}
	return 0;
}


char **string_to_argv(char *string, int *argc)
{
	int exec_len = strlen(string);
	int begin =0;
	int end =0;
	int argc_local = 0;
	char **argv_local;
	
	while(begin<exec_len)
	{
		while(begin<exec_len && is_delimetr(string[begin])) begin++;
		end = begin;
		while(end<exec_len && !is_delimetr(string[end])) end++;
		begin = end;
		argc_local++;
	}
	
	argv_local = (char**)malloc(argc_local+1);
	int i = 0;
	end = 0;
	begin = 0;
	
	while(begin<exec_len)
	{
		while(begin<exec_len && is_delimetr(string[begin])) begin++;
		end = begin;
		while(end<exec_len && !is_delimetr(string[end])) end++;
		
		int arr_size = end-begin;
		argv_local[i] = malloc(arr_size+2);
		memcpy(argv_local[i],string+begin,arr_size);
		argv_local[i][arr_size] = '\0';
		
		begin = end;
		i++;
	}
	argv_local[argc_local] = NULL;
	
	*argc = argc_local;
	return argv_local;
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

void add_flags(int fd, int flags)
{
	int old_flags = fcntl(fd, F_GETFL, 0);
	fcntl(fd,F_SETFL,old_flags|flags);
	return;
}

void proc_manager(params *cmd)
{
	
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
	semid = semget( IPC_key, 1, 0666 | IPC_CREAT );
	if(semid<0)
	{
		perror("Can\'t get semaphore id\n");
	}
	
	if(!isSingleOnFile(semid))
	{
		printf("That ftok object already in use!\n");
		exit(EXIT_SUCCESS);
	}
	
	/*
	Выполним операцию A(semid1,1) (увеличение значения семафора на 1 )
	для нашего массива семафоров.
	Для этого сначала заполним нашу структуру.
	Флаг, полагаем равным 0. Массив семафоров состоит
	из одного семафора с номером 0. Код операции 1.
    */
	my_sembuf.sem_op = 1;
	my_sembuf.sem_flg = 0;
	my_sembuf.sem_num = 0;
	if(semop(semid, &my_sembuf, 1))
	{
		perror("Error increment semaphore!\n");
	}
	
	/* создать общедоступную очередь */
	qid = msgget(IPC_key, IPC_CREAT | 0666);
	
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
	
	sa.sa_flags = SA_SIGINFO|SA_NOCLDWAIT; //use siginfo
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
	sa2.sa_flags = SA_SIGINFO;
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
		if(child_dead_var==1)
		{
			exit(EXIT_SUCCESS);
		}
	}
}
