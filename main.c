#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <linux/kernel.h>

#define one_unit() { volatile unsigned long i; for(i = 0; i < 1000000UL; i++); }
#define PARENT_CPU 0
#define CHILD_CPU 1
#define FIFO 1
#define RR 2
#define SJF 3
#define PSJF 4
#define GET_TIME 333
#define PRINT 334
#define TIME_Q 500

typedef struct{
	char name[32];
	int ready_time;
	int exec_time;
	pid_t pid;
}process;

typedef struct{
	int data[256];
	int head, tail;
}queue;

process *proc;
char S[5];
int N, policy;
int running = -1, time = 0, finished = 0, last_time;
queue Q;

int cmp(const void *a, const void *b)
{
	return ((process*)a)->ready_time - ((process*)b)->ready_time;
}

void initqueue(queue *q)
{
	q->head = 0;
	q->tail = 0;
	return;
}

void enqueue(queue *q, int x)
{
	q->data[q->tail] = x;
	q->tail = (q->tail + 1) % 256;
	return;
}

int dequeue(queue *q)
{
	if(q->head == q->tail){
		return -1;
	}
	int ret = q->data[q->head];
	q->head = (q->head + 1) % 256;
	return ret;
}

void assign_cpu(pid_t pid, int cpu)
{
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	
	if(sched_setaffinity(pid, sizeof(cpu_set_t), &mask)){
		printf("sched_setaffinity error: %s\n", strerror(errno));
		exit(1);
	}
}

int runnable(int index)
{
	return (proc[index].pid > 0) && (proc[index].exec_time > 0);
}

int select_next()
{
	//non-preemptive
	if(running != -1 && (policy == FIFO || policy == SJF))
		return running;
	
	if(policy == FIFO){
		return dequeue(&Q);
	}
	else if(policy == RR){
		if(running == -1)
			return dequeue(&Q);
		else{
			if((time - last_time) % TIME_Q == 0){
				enqueue(&Q, running);
				return dequeue(&Q);
			}
			else
				return running;
		}
	}
	else{
		int next = -1, min = INT_MAX;
		for(int i = 0; i < N; i++){
			if(runnable(i) && proc[i].exec_time < min){
				next = i;
				min = proc[i].exec_time;
			}
		}
		return next;
	}
	return -1;
}

void block_proc(pid_t pid)
{
	struct sched_param p;
	p.sched_priority = 0;
	
	if(sched_setscheduler(pid, SCHED_IDLE, &p) < 0)
		perror("sched_setscheduler");
	return;
}

void wake_proc(pid_t pid)
{
	struct sched_param p;
	p.sched_priority = 99;
	
	if(sched_setscheduler(pid, SCHED_FIFO, &p) < 0)
		perror("sched_setscheduler");
	//fprintf(stderr, "wake %d\n", pid);
	return;
}

int exec_proc(int index)
{	
	struct timespec start, end;
	syscall(GET_TIME, &start);
	
	pid_t pid = fork();
	
	if(pid < 0){
		fprintf(stderr, "fork error!\n");
		exit(1);
	}
	else if(pid == 0){
		
		int n = proc[index].exec_time;
		for(int j = 0; j < n; j++){
			/*if(j % 100 == 0)
				printf("%s: %d/%d\n", proc[index].name, j, n);*/
			one_unit();
		}
		
		syscall(GET_TIME, &end);
		/*if(end.tv_nsec < start.tv_nsec){
			end.tv_nsec += 1000000000;
			end.tv_sec--;
		}
		end.tv_sec -= start.tv_sec;
		end.tv_nsec -= start.tv_nsec;*/
		syscall(PRINT, getpid(), start, end);
		//fprintf(stderr, "%s finished!\n", proc[index].name);
		exit(0);
	}
	
	assign_cpu(pid, CHILD_CPU);
	return pid;
}
int create_med_prio_proc()
{
	pid_t pid = fork();
	if(pid == 0){
		struct sched_param p;
		p.sched_priority = 50;
		if(sched_setscheduler(pid, SCHED_FIFO, &p) < 0)
			perror("sched_setscheduler");
			
		while(1);
		exit(0);
	}
	
	assign_cpu(pid, CHILD_CPU);
	return pid;
}
void scheduler()
{
	for(int i = 0; i < N; i++)
		proc[i].pid = -1;	//not ready
	
	qsort(proc, N, sizeof(process), cmp);
	/*for(int i = 0; i < N; i++)
		printf("%s %d %d\n", proc[i].name, proc[i].ready_time, proc[i].exec_time);*/

	
	assign_cpu(0, PARENT_CPU);
	
	int med_proc = create_med_prio_proc();
	
	if(policy == FIFO || policy == RR)
		initqueue(&Q);
	
	int next;
	while(1){
		if(running != -1 && proc[running].exec_time == 0){
			waitpid(proc[running].pid, NULL, 0);
			//fprintf(stderr, "%s finished at time %d\n", proc[running].name, time);
			finished++;
			running = -1;
			if(finished == N)
				break;
		}
		
		for(int i = 0; i < N; i++){
			if(proc[i].ready_time == time){
				proc[i].pid = exec_proc(i);
				
				fprintf(stdout, "%s %d\n", proc[i].name, proc[i].pid);
				fflush(stdout);
				if(policy == FIFO || policy == RR){
					enqueue(&Q, i);
				}
				//fprintf(stderr, "%s ready at time %d\n", proc[i].name, time);
			}
		}
		
		next = select_next();
		//printf("time %d: running = %d	next = %d	last time = %d\n", time, running, next, last_time);
		if(next != -1){
			if(next != running){
				if(running != -1)
					block_proc(proc[running].pid);
				wake_proc(proc[next].pid);
				running = next;
				last_time = time;
				//printf("last time = %d\n", time);
			}
		}
		
		one_unit();
		if(running != -1)
			proc[running].exec_time--;
		time++;
	}
	
	kill(med_proc, SIGKILL);
	return;
}

int main()
{
	scanf("%s%d", S, &N);
	if(strcmp(S, "FIFO") == 0){
		policy = FIFO;
	}
	else if(strcmp(S, "RR") == 0){
		policy = RR;
	}
	else if(strcmp(S, "SJF") == 0){
		policy = SJF;
	}
	else if(strcmp(S, "PSJF") == 0){
		policy = PSJF;
	}
	else{
		fprintf(stderr, "Invalid scheduling policy!\n");
		exit(1);
	}
	
	proc = (process*)malloc(N * sizeof(process));
	for(int i = 0; i < N; i++)
		scanf("%s%d%d", proc[i].name, &proc[i].ready_time, &proc[i].exec_time);
	
	scheduler();
	
	/*for(int i = 0; i < N; i++)
		printf("%s %d %d\n", proc[i].name, proc[i].ready_time, proc[i].exec_time);*/
	exit(0);
}
