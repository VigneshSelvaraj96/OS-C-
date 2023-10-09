#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

#include "cs402.h"
#include "my402list.h"

// create a struct for each packet
typedef struct packet
{
    int packet_num;
    int tokens_required;
    double service_time;
    struct timeval arrival_time;
    struct timeval enter_q1_time;
    struct timeval leave_q1_time;
    struct timeval enter_q2_time;
    struct timeval leave_q2_time;
} packet;

// global variables
My402List *Q1, *Q2;
int Total_packets = 0;
int Total_tokens = 0;
int Total_tokens_dropped = 0;
int Total_packets_dropped = 0;
int Total_packets_served = 0;
int tokens_in_bucket = 0;

// mutex and condition variables
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t Q2_cond = PTHREAD_COND_INITIALIZER;

// simulation parameters
double lambda = 1;     // packets per second
double mu = 0.35;      // packets per second servicing time
double r = 1.5;        // tokens per second
int bucket_depth = 10; // bucket depth
int num = 20;          // number of packets to arrive
int p = 3;             // number of tokens required per packet
struct timeval simulation_start_time;
struct timeval simulation_end_time;

// declare the 4 threads to be used for simulation here
pthread_t packet_arrival_thread;
pthread_t token_arrival_thread;
pthread_t server1_thread;
pthread_t server2_thread;

// declare control c catching thread
pthread_t control_c_catch_thread;

// statistics variables
double avg_interarrival_time = 0;
double total_time_taken_by_packets_in_S1 = 0;
double total_time_taken_by_packets_in_S2 = 0;
double total_time_taken_by_packets_in_Q1 = 0;
double total_time_taken_by_packets_in_Q2 = 0;
double running_avg_service_time = 0;
double running_avg_time_packet_spent_in_system_squared = 0;
double running_avg_time_packet_spent_in_system = 0;

// file pointer to read from the tsfile
char *tsfile = NULL;
FILE *fp = NULL;

// signal handling variables
sigset_t set;
int control_c = 0;
int kill_token_thread = 0;
int kill_packet_thread = 0;

// print all the things in the my402list
void print_list(My402List *list)
{
    My402ListElem *elem = NULL;
    for (elem = My402ListFirst(list); elem != NULL; elem = My402ListNext(list, elem))
    {
        packet *pa = (packet *)elem->obj;
        printf("%d\n", pa->packet_num);
    }
}

// helper method to convert timval to double
double timeval_to_microsec(struct timeval t)
{
    double time_in_microseconds = (double)t.tv_sec * 1000 * 1000 + (double)t.tv_usec;
    return time_in_microseconds;
}

double timeval_to_microsec_wrtsystime(struct timeval t)
{
    double time_in_microseconds = (double)t.tv_sec * 1000 * 1000 + (double)t.tv_usec;
    return time_in_microseconds - timeval_to_microsec(simulation_start_time);
}

// helper method to calculate the difference between simulation startime and the second timeval
double timeval_diff(double t)
{
    double time_in_microseconds = t - timeval_to_microsec(simulation_start_time);
    return time_in_microseconds;
}

void *tokenizer_startroutine()
{
    double inter_token_time = 1.0 / r;
    double time_to_sleep = 0;
    // if the inter_token_time is >10 s, then set it to 10s, else round it to the nearest ms
    if (inter_token_time > 10)
    {
        inter_token_time = 10 * 1000 * 1000;
    }
    else
    {
        inter_token_time = round(inter_token_time * 1000) / 1000.0;
        inter_token_time = inter_token_time * 1000 * 1000;
    }
    // at this point, inter_token_time is in microseconds

    struct timeval this_token_arrival_time = simulation_start_time;
    struct timeval last_token_arrival_time = this_token_arrival_time;

    while (TRUE)
    {
        time_to_sleep = inter_token_time - (timeval_to_microsec(this_token_arrival_time) - timeval_to_microsec(last_token_arrival_time));
        // print last token arrival time and this token arrival time
        // printf("last token arrival time: %08.3f\n", timeval_to_microsec(last_token_arrival_time) / 1000.0);
        // printf("this token arrival time: %08.3f\n", timeval_to_microsec(this_token_arrival_time) / 1000.0);
        if (time_to_sleep < 0)
        {
            time_to_sleep = 0;
        }
        usleep(time_to_sleep);
        // print time to sleep
        // printf("time to sleep: %f\n", time_to_sleep);
        // printf("inter_token_time: %f\n", inter_token_time);
        //  printf("this_token_arrival_time: %f\n", timeval_to_microsec(this_token_arrival_time));
        // printf("last_token_arrival_time: %f\n", timeval_to_microsec(last_token_arrival_time));
        //  printf("simulation start time: %f\n", timeval_to_microsec(simulation_start_time) / 1000000.0);

        // lock mutex to access shared variables
        pthread_mutex_lock(&mutex);

        // check if all packets have arrived and q1 is empty, if so, self terminate
        if (kill_packet_thread && My402ListEmpty(Q1))
        {
            kill_token_thread = 1;
            pthread_cond_broadcast(&Q2_cond);
            pthread_mutex_unlock(&mutex);
            pthread_exit(0);
        }

        // check for ctrl c
        if (control_c)
        {
            kill_token_thread = 1;
            pthread_cond_broadcast(&Q2_cond);
            pthread_mutex_unlock(&mutex);
            pthread_exit(0);
        }

        // token arrives here
        gettimeofday(&this_token_arrival_time, 0);
        Total_tokens++;
        tokens_in_bucket++;

        // check if there more tokens than bucket depth and drop if there is
        if (tokens_in_bucket > bucket_depth)
        {
            printf("%08.3fms: token t%d arrives, dropped\n", timeval_to_microsec_wrtsystime(this_token_arrival_time) / 1000.0, Total_tokens);
            Total_tokens_dropped++;
            tokens_in_bucket--;
        }

        else
        {
            printf("%08.3fms: token t%d arrives, token bucket now has %d token\n", timeval_to_microsec_wrtsystime(this_token_arrival_time) / 1000.0, Total_tokens, tokens_in_bucket);
        }
        // check if first packet in q1 can be moved into q2
        if (!My402ListEmpty(Q1))
        {
            My402ListElem *first_elem = My402ListFirst(Q1);
            packet *pa = (packet *)first_elem->obj;
            if (pa->tokens_required <= tokens_in_bucket)
            {
                My402ListUnlink(Q1, first_elem);
                gettimeofday(&pa->leave_q1_time, 0);
                tokens_in_bucket = 0;
                printf("%08.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has %d token\n", timeval_to_microsec_wrtsystime(pa->leave_q1_time) / 1000.0, pa->packet_num, (timeval_to_microsec(pa->leave_q1_time) - timeval_to_microsec(pa->enter_q1_time)) / 1000.0, tokens_in_bucket);
                My402ListAppend(Q2, pa);
                gettimeofday(&pa->enter_q2_time, 0);
                printf("%08.3fms: p%d enters Q2\n", timeval_to_microsec_wrtsystime(pa->enter_q2_time) / 1000.0, pa->packet_num);
                // wake up all the server threads
                pthread_cond_broadcast(&Q2_cond);
            }
        }
        // unlock mutex
        last_token_arrival_time = this_token_arrival_time;
        pthread_mutex_unlock(&mutex);
    }
}

void *packet_start_routine()
{
    double inter_packet_time = 1.0 / lambda;
    double time_to_sleep = 0;
    double service_time = 0;

    // if the inter_packet_time is >10 s, then set it to 10s, else round it to the nearest ms
    if (inter_packet_time > 10)
    {
        inter_packet_time = 10 * 1000 * 1000;
    }
    else
    {
        inter_packet_time = round(inter_packet_time * 1000) / 1000.0;
        inter_packet_time = inter_packet_time * 1000 * 1000;
    }
    // at this point, inter_packet_time is in microseconds

    struct timeval this_packet_arrival_time = simulation_start_time;
    struct timeval last_packet_arrival_time = this_packet_arrival_time;

    for (int i = 0; i < num; i++)
    {
        // check for ctrl c
        if (control_c)
        {
            pthread_mutex_lock(&mutex);
            kill_packet_thread = 1;
            pthread_cond_broadcast(&Q2_cond);
            pthread_mutex_unlock(&mutex);
            pthread_exit(0);
        }
        time_to_sleep = inter_packet_time - (timeval_to_microsec(this_packet_arrival_time) - timeval_to_microsec(last_packet_arrival_time));
        if (time_to_sleep < 0)
        {
            time_to_sleep = 0;
        }
        usleep(time_to_sleep);

        // check to see if service time is greater than 10s
        if ((1.0 / mu) > 10)
        {
            service_time = 10 * 1000 * 1000;
        }
        else
        {
            service_time = round((1.0 / mu) * 1000) / 1000.0;
            service_time = service_time * 1000 * 1000;
        }

        // create the packet and initialize all the values
        packet *pa = (packet *)malloc(sizeof(packet));
        pa->packet_num = i + 1;
        pa->tokens_required = p;
        pa->service_time = service_time;

        // packet arrives here, lock mutex to access shared variables
        pthread_mutex_lock(&mutex);
        gettimeofday(&this_packet_arrival_time, 0);
        Total_packets++;
        gettimeofday(&pa->arrival_time, 0);
        // check if the packet needs more than 10 tokens and drop if it does
        if (pa->tokens_required > bucket_depth)
        {
            printf("%08.3fms: p%d arrives, needs %d tokens, dropped\n", timeval_to_microsec_wrtsystime(pa->arrival_time) / 1000.0, pa->packet_num, pa->tokens_required);
            Total_packets_dropped++;
            free(pa);
        }
        else
        {
            printf("%08.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms\n", timeval_to_microsec_wrtsystime(pa->arrival_time) / 1000.0, pa->packet_num, pa->tokens_required, (timeval_to_microsec(pa->arrival_time) - timeval_to_microsec(last_packet_arrival_time)) / 1000.0);
            // check if q1 is empty and if it is, then add the packet to q1, checking if it can be moved to q2
            if (My402ListEmpty(Q1))
            {
                // check if the packet can be moved to q2
                if (pa->tokens_required <= tokens_in_bucket)
                {
                    My402ListAppend(Q1, (void *)pa);
                    gettimeofday(&pa->enter_q1_time, 0);
                    printf("%08.3fms: p%d enters Q1\n", timeval_to_microsec_wrtsystime(pa->enter_q1_time) / 1000.0, pa->packet_num);
                    My402ListUnlink(Q1, My402ListFirst(Q1));
                    tokens_in_bucket -= pa->tokens_required;
                    gettimeofday(&pa->leave_q1_time, 0);
                    printf("%08.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has %d token\n", timeval_to_microsec_wrtsystime(pa->leave_q1_time) / 1000.0, pa->packet_num, (timeval_to_microsec(pa->leave_q1_time) - timeval_to_microsec(pa->enter_q1_time)) / 1000.0, tokens_in_bucket);
                    My402ListAppend(Q2, (void *)pa);
                    gettimeofday(&pa->enter_q2_time, 0);
                    printf("%08.3fms: p%d enters Q2\n", timeval_to_microsec_wrtsystime(pa->enter_q2_time) / 1000.0, pa->packet_num);
                    // wake up all the server threads
                    pthread_cond_broadcast(&Q2_cond);
                }
                else
                {
                    // append to q1 and wait for the token thread to accumulate tokens
                    My402ListAppend(Q1, (void *)pa);
                    gettimeofday(&pa->enter_q1_time, 0);
                    printf("%08.3fms: p%d enters Q1\n", timeval_to_microsec_wrtsystime(pa->enter_q1_time) / 1000.0, pa->packet_num);
                }
            }
            else
            {
                // add the packet to q1 since its not the first packet to arrive at q1
                My402ListAppend(Q1, (void *)pa);
                gettimeofday(&pa->enter_q1_time, 0);
                printf("%08.3fms: p%d enters Q1\n", timeval_to_microsec_wrtsystime(pa->enter_q1_time) / 1000.0, pa->packet_num);
            }
        }

        // statistics computing
        // update avg interarrival time
        avg_interarrival_time = (avg_interarrival_time * (Total_packets - 1) + (timeval_to_microsec(pa->arrival_time) - timeval_to_microsec(last_packet_arrival_time))) / Total_packets;
        // unlock mutex
        last_packet_arrival_time = this_packet_arrival_time;
        pthread_cond_broadcast(&Q2_cond);
        pthread_mutex_unlock(&mutex);
    }
    // update all packets arrived signal
    pthread_mutex_lock(&mutex);
    kill_packet_thread = 1;
    pthread_mutex_unlock(&mutex);
    pthread_exit(0);
}

void *packet_start_routine_tsfileinput()
{

    // fp already has file open in main
    char buf[2000];
    double time_to_sleep = 0;
    double service_time = 0;
    double inter_packet_time = 0;

    // read first line from file to get number of packets
    if (fgets(buf, sizeof(buf), fp) != NULL)
    {
        if (strlen(buf) > 1024)
        {
            fprintf(stderr, "Error: tsfile line is too long, more than 1024 characters\n");
            exit(1);
        }

        // number of packets is on the first line
        // remove leading and trailing spaces
        char *temp = malloc(sizeof(buf));
        int writer_index = 0;
        for (int i = 0; i < strlen(buf); i++)
        {
            if (buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\n')
            {
                temp[writer_index] = buf[i];
                writer_index++;
            }
        }
        // add null terminator
        temp[writer_index] = '\0';
        num = atoi(temp);
        // check if line 1 is just a number, if it is not, print error and exit
        if (num == 0)
        {
            fprintf(stderr, "Error: tsfile line 1 is not a number\n");
            exit(1);
        }
        // printf("num: %d\n", num);
        free(temp);
    }
    else
    {
        fprintf(stderr, "Error: tsfile is empty\n");
        exit(1);
    }

    struct timeval this_packet_arrival_time = simulation_start_time;
    struct timeval last_packet_arrival_time = this_packet_arrival_time;

    for (int i = 0; i < num; i++)
    {
        // check for ctrl c
        if (control_c)
        {
            pthread_mutex_lock(&mutex);
            kill_packet_thread = 1;
            pthread_cond_broadcast(&Q2_cond);
            pthread_mutex_unlock(&mutex);
            pthread_exit(0);
        }

        // read the line from the file to get the inter packet time, number of tokens required and service time respectively
        if (fgets(buf, sizeof(buf), fp) != NULL)
        {
            char inter_packet_time_str[50];
            char tokens_required_str[50];
            char service_time_str[50];
            char buffercpy[2000];
            strcpy(buffercpy, buf);
            char *ptr = buffercpy;
            char *wordptr = inter_packet_time_str;
            while (*ptr != '\t' && *ptr != ' ')
            {
                *wordptr = *ptr;
                wordptr++;
                ptr++;
            }
            *wordptr = '\0';
            wordptr = tokens_required_str;
            // skip tabs and spaces
            while (*ptr == '\t' || *ptr == ' ')
            {
                ptr++;
            }
            while (*ptr != '\t' && *ptr != ' ')
            {
                *wordptr = *ptr;
                wordptr++;
                ptr++;
            }
            *wordptr = '\0';
            ptr++;
            wordptr = service_time_str;
            // skip tabs and spaces
            while (*ptr == '\t' || *ptr == ' ')
            {
                ptr++;
            }
            while (*ptr != '\t' && *ptr != ' ')
            {
                *wordptr = *ptr;
                wordptr++;
                ptr++;
            }
            *wordptr = '\0';
            ptr++;
            // atoi the strings to get the values
            inter_packet_time = atof(inter_packet_time_str) * 1000;
            p = atoi(tokens_required_str);
            service_time = atof(service_time_str) * 1000;
            // print all these values
            //   printf("inter_packet_time: %f\n", inter_packet_time);
            //  printf("p: %d\n", p);
            //  printf("service_time: %f\n", service_time);
        }
        else
        {
            fprintf(stderr, "Error: tsfile does not have enough lines\n");
            exit(1);
        }

        time_to_sleep = inter_packet_time - (timeval_to_microsec(this_packet_arrival_time) - timeval_to_microsec(last_packet_arrival_time));
        if (time_to_sleep < 0)
        {
            time_to_sleep = 0;
        }
        usleep(time_to_sleep);

        // create the packet and initialize all the values
        packet *pa = (packet *)malloc(sizeof(packet));
        pa->packet_num = i + 1;
        pa->tokens_required = p;
        pa->service_time = service_time;

        // packet arrives here, lock mutex to access shared variables
        pthread_mutex_lock(&mutex);
        gettimeofday(&this_packet_arrival_time, 0);
        Total_packets++;
        gettimeofday(&pa->arrival_time, 0);
        // printf("This packet arrival time: %f\n", timeval_to_microsec_wrtsystime(this_packet_arrival_time) / 1000.0);
        //  check if the packet needs more than 10 tokens and drop if it does
        if (pa->tokens_required > bucket_depth)
        {
            printf("%08.3fms: p%d arrives, needs %d tokens, dropped\n", timeval_to_microsec_wrtsystime(pa->arrival_time) / 1000.0, pa->packet_num, pa->tokens_required);
            Total_packets_dropped++;
            free(pa);
        }
        else
        {
            printf("%08.3fms: p%d arrives, needs %d tokens, inter-arrival time = %.3fms\n", timeval_to_microsec_wrtsystime(pa->arrival_time) / 1000.0, pa->packet_num, pa->tokens_required, (timeval_to_microsec(pa->arrival_time) - timeval_to_microsec(last_packet_arrival_time)) / 1000.0);
            // check if q1 is empty and if it is, then add the packet to q1, checking if it can be moved to q2
            if (My402ListEmpty(Q1))
            {
                // check if the packet can be moved to q2
                if (pa->tokens_required <= tokens_in_bucket)
                {
                    My402ListAppend(Q1, (void *)pa);
                    gettimeofday(&pa->enter_q1_time, 0);
                    printf("%08.3fms: p%d enters Q1\n", timeval_to_microsec_wrtsystime(pa->enter_q1_time) / 1000.0, pa->packet_num);
                    My402ListUnlink(Q1, My402ListFirst(Q1));
                    tokens_in_bucket -= pa->tokens_required;
                    gettimeofday(&pa->leave_q1_time, 0);
                    printf("%08.3fms: p%d leaves Q1, time in Q1 = %.3fms, token bucket now has %d token\n", timeval_to_microsec_wrtsystime(pa->leave_q1_time) / 1000.0, pa->packet_num, (timeval_to_microsec(pa->leave_q1_time) - timeval_to_microsec(pa->enter_q1_time)) / 1000.0, tokens_in_bucket);
                    My402ListAppend(Q2, (void *)pa);
                    gettimeofday(&pa->enter_q2_time, 0);
                    printf("%08.3fms: p%d enters Q2\n", timeval_to_microsec_wrtsystime(pa->enter_q2_time) / 1000.0, pa->packet_num);
                    // wake up all the server threads
                    pthread_cond_broadcast(&Q2_cond);
                }
                else
                {
                    // append to q1 and wait for the token thread to accumulate tokens
                    My402ListAppend(Q1, (void *)pa);
                    gettimeofday(&pa->enter_q1_time, 0);
                    printf("%08.3fms: p%d enters Q1\n", timeval_to_microsec_wrtsystime(pa->enter_q1_time) / 1000.0, pa->packet_num);
                }
            }
            else
            {
                // add the packet to q1 since its not the first packet to arrive at q1
                My402ListAppend(Q1, (void *)pa);
                gettimeofday(&pa->enter_q1_time, 0);
                printf("%08.3fms: p%d enters Q1\n", timeval_to_microsec_wrtsystime(pa->enter_q1_time) / 1000.0, pa->packet_num);
            }
        }

        // statistics computing
        // update avg interarrival time
        avg_interarrival_time = (avg_interarrival_time * (Total_packets - 1) + (timeval_to_microsec(pa->arrival_time) - timeval_to_microsec(last_packet_arrival_time))) / Total_packets;
        // unlock mutex
        last_packet_arrival_time = this_packet_arrival_time;
        pthread_cond_broadcast(&Q2_cond);
        pthread_mutex_unlock(&mutex);
    }
    // update all packets arrived signal
    pthread_mutex_lock(&mutex);
    kill_packet_thread = 1;
    pthread_mutex_unlock(&mutex);
    pthread_exit(0);
}

// server thread
void *server(void *server_num)
{
    while (TRUE)
    {
        struct timeval exitq2_time;
        struct timeval startservicing_time;
        struct timeval endservicing_time;
        pthread_mutex_lock(&mutex);
        // wait till q2 is not empty
        while (My402ListEmpty(Q2))
        {
            // check if q1 is also empty and all packets have arrived, if so, self terminate
            if (kill_packet_thread && My402ListEmpty(Q1) && kill_token_thread)
            {
                pthread_cond_broadcast(&Q2_cond);
                pthread_mutex_unlock(&mutex);
                pthread_exit(0);
            }
            pthread_cond_wait(&Q2_cond, &mutex);
        }
        // if q2 is not empty and there are packets to be served
        if (!My402ListEmpty(Q2) && Total_packets_served < num)
        {
            My402ListElem *first_elem = My402ListFirst(Q2);
            packet *pa = (packet *)first_elem->obj;
            gettimeofday(&exitq2_time, 0);
            pa->leave_q2_time = exitq2_time;
            printf("%08.3fms: p%d leaves Q2, time in Q2 = %.3fms\n", timeval_to_microsec_wrtsystime(exitq2_time) / 1000.0, pa->packet_num, (timeval_to_microsec(exitq2_time) - timeval_to_microsec(pa->enter_q2_time)) / 1000.0);
            My402ListUnlink(Q2, first_elem);
            gettimeofday(&startservicing_time, 0);
            printf("%08.3fms: p%d begins service at S%d, requesting %.3fms of service\n", timeval_to_microsec_wrtsystime(startservicing_time) / 1000.0, pa->packet_num, (int)server_num, pa->service_time / 1000.0);
            pthread_mutex_unlock(&mutex);
            usleep(pa->service_time);
            pthread_mutex_lock(&mutex);
            gettimeofday(&endservicing_time, 0);
            printf("%08.3fms: p%d departs from S%d, service time = %.3fms, time in system = %.3fms\n", timeval_to_microsec_wrtsystime(endservicing_time) / 1000.0, pa->packet_num, (int)server_num, (timeval_to_microsec(endservicing_time) - timeval_to_microsec(startservicing_time)) / 1000.0, (timeval_to_microsec(endservicing_time) - timeval_to_microsec(pa->arrival_time)) / 1000.0);
            // pa->leave_q2_time = endservicing_time;
            //  statistics computing
            //  update the total time taken by packets in Q1 and Q2
            total_time_taken_by_packets_in_Q1 += (timeval_to_microsec(pa->leave_q1_time) - timeval_to_microsec(pa->enter_q1_time));
            total_time_taken_by_packets_in_Q2 += (timeval_to_microsec(pa->leave_q2_time) - timeval_to_microsec(pa->enter_q2_time));
            // update the total time taken by packets in S1 and S2
            if ((int)server_num == 1)
            {
                total_time_taken_by_packets_in_S1 += (timeval_to_microsec(endservicing_time) - timeval_to_microsec(startservicing_time));
            }
            else
            {
                total_time_taken_by_packets_in_S2 += (timeval_to_microsec(endservicing_time) - timeval_to_microsec(startservicing_time));
            }
            // update avg time a packet spent in service and system
            running_avg_service_time = (running_avg_service_time * (Total_packets_served) + (timeval_to_microsec(endservicing_time) - timeval_to_microsec(startservicing_time))) / (Total_packets_served + 1);
            // running_avg_service_time_squared = (running_avg_service_time_squared * (Total_packets_served) + pow((timeval_to_microsec(endservicing_time) - timeval_to_microsec(startservicing_time)), 2)) / (Total_packets_served + 1);
            running_avg_time_packet_spent_in_system = (running_avg_time_packet_spent_in_system * (Total_packets_served) + (timeval_to_microsec(endservicing_time) - timeval_to_microsec(pa->arrival_time))) / (Total_packets_served + 1);
            running_avg_time_packet_spent_in_system_squared = (running_avg_time_packet_spent_in_system_squared * (Total_packets_served) + pow((timeval_to_microsec(endservicing_time) - timeval_to_microsec(pa->arrival_time)), 2)) / (Total_packets_served + 1);
            Total_packets_served++;
            pthread_cond_broadcast(&Q2_cond);
            pthread_mutex_unlock(&mutex);
        }
    }
}

// separate ctrl c catching thread
void *ctrl_c_catch()
{
    struct timeval ctrl_c_time;
    struct timeval packet_removed_time;
    int returnsig = 0;
    sigwait(&set, &returnsig);
    My402ListElem *q1elem = NULL;
    My402ListElem *q2elem = NULL;

    // lock mutex to access shared variables
    pthread_mutex_lock(&mutex);
    gettimeofday(&ctrl_c_time, 0);
    printf("\n%08.3fms: SIGINT caught, no new packets or tokens will be allowed\n", timeval_to_microsec_wrtsystime(ctrl_c_time) / 1000.0);
    control_c = 1;
    kill_token_thread = 1;
    kill_packet_thread = 1;

    pthread_cond_broadcast(&Q2_cond);
    pthread_cancel(token_arrival_thread);
    pthread_cancel(packet_arrival_thread);

    // print and remove all packets in q1
    while (!My402ListEmpty(Q1))
    {
        q1elem = My402ListFirst(Q1);
        packet *pa = (packet *)q1elem->obj;
        gettimeofday(&packet_removed_time, 0);
        printf("%08.3fms: p%d removed from Q1\n", timeval_to_microsec_wrtsystime(packet_removed_time) / 1000.0, pa->packet_num);
        My402ListUnlink(Q1, q1elem);
        Total_packets_dropped++;
        free(pa);
    }

    // print and remove all packets in q2
    while (!My402ListEmpty(Q2))
    {
        q2elem = My402ListFirst(Q2);
        packet *pa = (packet *)q2elem->obj;
        gettimeofday(&packet_removed_time, 0);
        printf("%08.3fms: p%d removed from Q2\n", timeval_to_microsec_wrtsystime(packet_removed_time) / 1000.0, pa->packet_num);
        My402ListUnlink(Q2, q2elem);
        Total_packets_dropped++;
        free(pa);
    }

    // close the file pointer
    if (tsfile != NULL)
    {
        fclose(fp);
    }
    pthread_mutex_unlock(&mutex);
    pthread_exit(0);
}

void verify_command_line_arguments(int argc, char *argv[])
{
    // go through each argument and check if its valid while setting it if it is
    char *inputcommand;
    for (int i = 1; i < argc; i++)
    {
        inputcommand = argv[i];
        if (strcmp(inputcommand, "-lambda") == 0)
        {
            // check if the value provided for lambda is valid
            if (i + 1 < argc)
            {
                lambda = atof(argv[i + 1]);
                if (lambda <= 0)
                {
                    fprintf(stderr, "Error: lambda value must be greater than 0\n");
                    exit(1);
                }
                i++;
            }
            else
            {
                fprintf(stderr, "Error: lambda value not provided\n");
                exit(1);
            }
        }
        // check if value provided was for mu and if its valid
        else if (strcmp(inputcommand, "-mu") == 0)
        {
            if (i + 1 < argc)
            {
                mu = atof(argv[i + 1]);
                if (mu <= 0)
                {
                    fprintf(stderr, "Error: mu value must be greater than 0\n");
                    exit(1);
                }
                i++;
            }
            else
            {
                fprintf(stderr, "Error: mu value not provided\n");
                exit(1);
            }
        }
        // check if value provided was for r and if its valid
        else if (strcmp(inputcommand, "-r") == 0)
        {
            if (i + 1 < argc)
            {
                r = atof(argv[i + 1]);
                if (r <= 0)
                {
                    fprintf(stderr, "Error: r value must be greater than 0\n");
                    exit(1);
                }
                i++;
            }
            else
            {
                fprintf(stderr, "Error: r value not provided\n");
                exit(1);
            }
        }
        // check if value provided was for B and if its valid
        else if (strcmp(inputcommand, "-B") == 0)
        {
            if (i + 1 < argc)
            {
                bucket_depth = atoi(argv[i + 1]);
                if (bucket_depth <= 0)
                {
                    fprintf(stderr, "Error: B value must be greater than 0\n");
                    exit(1);
                }
                i++;
            }
            else
            {
                fprintf(stderr, "Error: B value not provided\n");
                exit(1);
            }
        }
        // check if value provided was for P and if its valid
        else if (strcmp(inputcommand, "-P") == 0)
        {
            if (i + 1 < argc)
            {
                p = atoi(argv[i + 1]);
                if (p <= 0)
                {
                    fprintf(stderr, "Error: P value must be greater than 0\n");
                    exit(1);
                }
                i++;
            }
            else
            {
                fprintf(stderr, "Error: P value not provided\n");
                exit(1);
            }
        }
        // check if value provided was for num and if its valid
        else if (strcmp(inputcommand, "-n") == 0)
        {
            if (i + 1 < argc)
            {
                num = atoi(argv[i + 1]);
                if (num <= 0)
                {
                    fprintf(stderr, "Error: n value must be greater than 0\n");
                    exit(1);
                }
                i++;
            }
            else
            {
                fprintf(stderr, "Error: n value not provided\n");
                exit(1);
            }
        }
        // check if the value provided was for a tsfile and if its valid
        else if (strcmp(inputcommand, "-t") == 0)
        {
            if (i + 1 < argc)
            {
                tsfile = argv[i + 1];
                i++;
                // check if the file exists and exit immediately if it doesnt
                fp = fopen(tsfile, "r");
                if (fp == NULL)
                {
                    fprintf(stderr, "Error: tsfile not found\n");
                    exit(1);
                }
            }
            else
            {
                fprintf(stderr, "Error: tsfile not provided\n");
                exit(1);
            }
        }
        else
        {
            fprintf(stderr, "Malformed command.\nWrong command: %s\nusage: ./warmup2 [-lambda lambda] [-mu mu] [-r r] [-B B] [-P P] [-n num] [-t tsfile]\n", inputcommand);
            exit(1);
        }
    }
}

int main(int argc, char *argv[])
{

    // set the signal set to only catch ctrl c
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    pthread_sigmask(SIG_BLOCK, &set, 0);

    // read the command line arguments here and set the simulation parameters
    verify_command_line_arguments(argc, argv);

    // // check if needs to run from tsfile
    // if (tsfile != NULL)
    // {
    //     fp = fopen(tsfile, "r");
    //     if (fp == NULL)
    //     {
    //         fprintf(stderr, "Error: tsfile not found\n");
    //         exit(1);
    //     }
    // }

    // print all the simulation parameters
    printf("Emulation Parameters:\n");
    printf("\tnumber to arrive = %d\n", num);
    if (tsfile == NULL)
    {
        printf("\tlambda = %.6g\n", lambda);
        printf("\tmu = %.6g\n", mu);
    }
    printf("\tr = %.6g\n", r);
    printf("\tB = %d\n", bucket_depth);
    printf("\tP = %d\n", p);
    if (tsfile != NULL)
    {
        printf("\ttsfile = %s\n", tsfile);
    }

    // set simulation start time to current time
    gettimeofday(&simulation_start_time, 0);
    Q1 = (My402List *)malloc(sizeof(My402List));
    Q2 = (My402List *)malloc(sizeof(My402List));
    My402ListInit(Q1);
    My402ListInit(Q2);
    printf("%08.3fms: emulation begins\n", timeval_to_microsec_wrtsystime(simulation_start_time) / 1000.0);
    // create ctrl c catching thread
    pthread_create(&control_c_catch_thread, 0, ctrl_c_catch, NULL);
    // create the 4 threads here
    pthread_create(&token_arrival_thread, 0, tokenizer_startroutine, NULL);
    if (tsfile != NULL)
    {
        pthread_create(&packet_arrival_thread, 0, packet_start_routine_tsfileinput, NULL);
    }
    else
    {
        pthread_create(&packet_arrival_thread, 0, packet_start_routine, NULL);
    }

    // pthread_create(&packet_arrival_thread, 0, packet_start_routine, NULL);
    pthread_create(&server1_thread, 0, server, (void *)1);
    pthread_create(&server2_thread, 0, server, (void *)2);

    // join the 4 threads here
    pthread_join(packet_arrival_thread, NULL);
    pthread_join(token_arrival_thread, NULL);
    pthread_join(server1_thread, NULL);
    pthread_join(server2_thread, NULL);

    // set simulation end time to current time
    gettimeofday(&simulation_end_time, 0);
    printf("%08.3fms: emulation ends\n", timeval_to_microsec_wrtsystime(simulation_end_time) / 1000.0);

    // calculation for standard deviation
    double variance = (running_avg_time_packet_spent_in_system_squared / 1000000.0) - ((running_avg_time_packet_spent_in_system * running_avg_time_packet_spent_in_system) / 1000000.0);
    double standard_deviation = sqrt(variance);

    // print statistics here, in seconds
    printf("\nStatistics:\n");
    printf("\taverage packet inter-arrival time = %.6gs\n", avg_interarrival_time / 1000000.0);
    if (Total_packets_served == 0)
    {
        printf("\n\taverage packet service time = (N/A, no packet served)");
    }
    else
        printf("\taverage packet service time = %.6gs\n", running_avg_service_time / 1000000.0);
    printf("\n");
    printf("\taverage number of packets in Q1 = %.6g\n", total_time_taken_by_packets_in_Q1 / (timeval_to_microsec(simulation_end_time) - timeval_to_microsec(simulation_start_time)));
    printf("\taverage number of packets in Q2 = %.6g\n", total_time_taken_by_packets_in_Q2 / (timeval_to_microsec(simulation_end_time) - timeval_to_microsec(simulation_start_time)));
    printf("\taverage number of packets at S1 = %.6g\n", total_time_taken_by_packets_in_S1 / (timeval_to_microsec(simulation_end_time) - timeval_to_microsec(simulation_start_time)));
    printf("\taverage number of packets at S2 = %.6g\n", total_time_taken_by_packets_in_S2 / (timeval_to_microsec(simulation_end_time) - timeval_to_microsec(simulation_start_time)));
    printf("\n");
    if (Total_packets_served == 0)
    {
        printf("\taverage time a packet spent in system = (N/A, no packet served)\n");
        printf("\tstandard deviation for time spent in system = (N/A, no packet served)\n");
    }
    else
    {
        printf("\taverage time a packet spent in system = %.6gs\n", running_avg_time_packet_spent_in_system / 1000000.0);
        printf("\tstandard deviation for time spent in system = %.6gs\n", standard_deviation / 1000.0);
    }

    // printf("\taverage time a packet spent in system = %.6gs\n", running_avg_time_packet_spent_in_system / 1000000.0);
    // printf("\tstandard deviation for time spent in system = %.6gs\n", standard_deviation / 1000.0);
    printf("\ttoken drop probability = %.6g\n", (double)Total_tokens_dropped / Total_tokens);
    printf("\tpacket drop probability = %.6g\n", (double)Total_packets_dropped / Total_packets);
}
