/*
 * Author:      William Chia-Wei Cheng (bill.cheng@usc.edu)
 *
 * @(#)$Id: listtest.c,v 1.2 2020/05/18 05:09:12 william Exp $
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <stdbool.h>
#include <time.h>
#include <locale.h>
#include <errno.h>

#include "cs402.h"
#include "my402list.h"

#define CHARBUFFERLEN 2000

typedef struct linefields
{
    char type;
    time_t time;
    int amount;
    char *description;
} LineFields;

static void
printtablestart()
{
    printf("+-----------------+--------------------------+----------------+----------------+\n");
    printf("|       Date      | Description              |         Amount |        Balance |\n");
    printf("+-----------------+--------------------------+----------------+----------------+\n");
}

static void printtableend()
{
    printf("+-----------------+--------------------------+----------------+----------------+\n");
}

// function that checks if the amount is valid. The number of digits to the left of the decimal point can be at most 7 digits (i.e., < 10,000,000).The number of digits
// to the right of the decimal should be exactly 2.
static bool validateamount(char *amount)
{
    int leftdigitcount = 0;
    int digitindex = 0;
    int rightdigitcount = 0;
    while (amount[digitindex] != '.')
    {
        leftdigitcount++;
        digitindex++;
    }
    rightdigitcount = strlen(amount) - leftdigitcount - 1;
    if (leftdigitcount > 7)
    {
        fprintf(stderr, "The number of digits to the left of the decimal point can be at most 7 digits (i.e., < 10,000,000)\n");
        return FALSE;
    }
    if (rightdigitcount != 2)
    {
        fprintf(stderr, "The number of digits to the right of the decimal should be exactly 2\n");
        return FALSE;
    }
    if (amount[0] == '0' && leftdigitcount > 1)
    {
        fprintf(stderr, "If the number is zero, its length must be 1\n");
        return FALSE;
    }
    return TRUE;
}

// function that goes through the my402list and prints the information in the list in the table format
// date, description, amount and balance. If the transaction is a debit, the amount is negative and if the transaction is a credit, the amount is positive
// the amount must have a paranthesis around it if its a credit and no paranthesis if its a debit
void printlist(My402List *pList)
{
    My402ListElem *elem = NULL;
    printtablestart();
    int balance = 0;
    for (elem = My402ListFirst(pList); elem != NULL; elem = My402ListNext(pList, elem))
    {
        LineFields *line = (LineFields *)(elem->obj);
        char *thisTime = ctime(&(line->time));
        if (line->type == '+')
        {
            balance += line->amount;
        }
        else
        {
            balance -= line->amount;
        }
        printf("%s", "| ");
        fprintf(stdout, "%.*s", 11, thisTime);
        fprintf(stdout, "%.*s", 4, (thisTime + 20));
        printf("%s", " | ");
        printf("%-24s", line->description);
        printf("%s", " | ");
        // print amount
        if (line->amount >= 1000000000)
        {
            printf("%13s", "?,???,???.??");
            printf("%s", " | \n");
        }
        int dollars = line->amount / 100;
        int cents = line->amount % 100;
        if (line->type == '+')
        {
            printf("%'10d", dollars);
            printf("%s", ".");
            printf("%02d", cents);
            printf("%s", "  | ");
        }
        else
        {
            printf("%s", "(");
            printf("%'9d", dollars);
            printf("%s", ".");
            printf("%02d", cents);
            printf("%s", ")");
            printf("%s", " | ");
        }
        // print balance
        if (balance >= 1000000000)
        {
            printf("%13s", "?,???,???.??");
            printf("%s", "  |\n");
        }
        else
        {

            if (balance >= 0)
            {
                int balance_dollars = balance / 100;
                int balance_cents = balance % 100;
                printf("%'10d", balance_dollars);
                printf("%s", ".");
                printf("%02d", balance_cents);
                printf("%s", "  |");
                printf("\n");
            }
            else
            {
                int absbalance = abs(balance);
                int balance_dollars = absbalance / 100;
                int balance_cents = absbalance % 100;
                printf("%s", "(");
                printf("%'9d", balance_dollars);
                printf("%s", ".");
                printf("%02d", balance_cents);
                printf("%s", ")");
                printf("%s", " |");
                printf("\n");
            }
        }
    }
    printtableend();
}

// read every line in the tfile and store the information in the LineFields structure and if valid, push it into the my402list
static void process(FILE *fp, My402List *pList, My402List *pList2)
{

    int linecount = 1;
    char buffer[CHARBUFFERLEN] = "";
    // read every line in the tfile, line by line, and store it in the buffer
    while (fgets(buffer, sizeof(buffer), fp) != NULL)
    {

        // checking if length of line is >1024
        if (strlen(buffer) > 1024)
        {
            fprintf(stderr, "The line is too long\n");
            fprintf(stderr, "strlen(buffer): %u\n", strlen(buffer));
            fprintf(stderr, "linecount: %d\n", linecount);
            exit(1);
        }
        // Then I would count the number of tabs (i.e., '\t' characters)in a line. If it's not equal to 3, then it's an error.
        int tabcount = 0;
        for (int i = 0; i < strlen(buffer); i++)
        {
            if (buffer[i] == '\t')
            {
                tabcount++;
            }
        }
        if (tabcount != 3)
        {
            fprintf(stderr, "The line does not have exactly 4 fields\n");
            fprintf(stderr, "tabcount: %d\n", tabcount);
            fprintf(stderr, "buffer: %s\n", buffer);
            fprintf(stderr, "linecount: %d\n", linecount);
            exit(1);
        }
        else
        {
            LineFields *line = (LineFields *)malloc(sizeof(LineFields));
            // Otherwise, I would set the char* pointers to each of the strings in the line ending with \0 and store it in the LineFields structure
            char str1[CHARBUFFERLEN];
            char str2[CHARBUFFERLEN];
            char str3[CHARBUFFERLEN];
            char str4[CHARBUFFERLEN];
            char *ptr = NULL;
            char buffercpy[CHARBUFFERLEN];
            strcpy(buffercpy, buffer);
            ptr = buffercpy;
            // loop through the buffer and set the pointers to each of the strings in the line ending with \0
            char *wordPtr = str1; // Initialize a pointer to the current word
            while (*ptr != '\0' && *ptr != '\t')
            {
                *wordPtr = *ptr;
                ptr++;
                wordPtr++;
            }
            *wordPtr = '\0';
            // Skip the tabs and any spaces
            while (*ptr == '\t' || *ptr == ' ')
            {
                ptr++;
            }
            wordPtr = str2; // Initialize a pointer to the current word
            // Extract the second word (similar to the first)
            while (*ptr != '\t' && *ptr != ' ')
            {
                *wordPtr = *ptr;
                wordPtr++;
                ptr++;
            }
            *wordPtr = '\0';
            // Skip the tabs and any spaces
            while (*ptr == '\t' || *ptr == ' ')
            {
                ptr++;
            }
            wordPtr = str3; // Initialize a pointer to the current word
            // Extract the third word
            while (*ptr != '\t' && *ptr != ' ')
            {
                *wordPtr = *ptr;
                wordPtr++;
                ptr++;
            }
            *wordPtr = '\0';
            // Skip the tabs and any spaces
            while (*ptr == '\t' || *ptr == ' ')
            {
                ptr++;
            }
            wordPtr = str4; // Initialize a pointer to the current word
            // Extract the fourth word
            int trunccount = 0;
            while (*ptr != '\t' && *ptr != '\n')
            {
                if (trunccount >= 24)
                {
                    break;
                }
                *wordPtr = *ptr;
                wordPtr++;
                ptr++;
                trunccount++;
            }
            *wordPtr = '\0';
            // check if the first field is a + or -
            if (str1[0] != '+' && str1[0] != '-')
            {
                fprintf(stderr, "The first field is not a + or -\n");
                // print the invalid field
                fprintf(stderr, "str1: %s\n", str1);
                fprintf(stderr, "linecount: %d\n", linecount);
                exit(1);
            }
            else
            {
                // assuming its a valid type, I would store it in the LineFields structure
                line->type = str1[0];
            }

            // check if the second field is a valid time if it has length <11 and value of field must be >0. First digit must not be 0 too
            if (strlen(str2) >= 11 || strlen(str2) < 1 || str2[0] == '0')
            {
                fprintf(stderr, "The time field is not valid\n");
                fprintf(stderr, "it either has length >=11 or value of field is <0. First digit must not be 0 too");
                fprintf(stderr, "Timestamp read incorrectly was: %s\n", str2);
                fprintf(stderr, "linecount: %d\n", linecount);
                exit(1);
            }
            else
            {
                // assuming its a valid timestamp, I would convert it to time_t and store it in the LineFields structure
                int temptime = atoi(str2);
                time_t temptime_t = (time_t)temptime;
                time_t currtime = time(NULL);
                if (temptime_t > currtime)
                {
                    fprintf(stderr, "The time field is not valid as it cannot be in the future\n");
                    fprintf(stderr, "linecount: %d\n", linecount);
                    exit(1);
                }
                else
                {
                    // valid timestamp
                    line->time = temptime_t;
                    // add the current timestamp to plist2 and check if there are any duplicates
                    int *timeptr = (int *)malloc(sizeof(int));
                    *timeptr = temptime_t;
                    if (My402ListFind(pList2, timeptr) != NULL)
                    {
                        fprintf(stderr, "There exists another entry with the same exact timestamp\n");
                        fprintf(stderr, "linecount: %d\n", linecount);
                        fprintf(stderr, "time: %d\n", *timeptr);
                        exit(1);
                    }
                    else
                    {
                        My402ListAppend(pList2, timeptr);
                    }
                }
            }
            // check if the third field is a valid amount Transaction amount (a number followed by a period followed by two digits;
            // if the number is not zero, its first digit must not be zero; if the number is zero, its length must be 1).
            // The number to the left of the decimal point can be at most 7 digits (i.e., < 10,000,000). The transaction amount must have a positive value
            if (!validateamount(str3))
            {
                fprintf(stderr, "The amount field is not valid\n");
                fprintf(stderr, "Amount incorrectly read was: %s\n", str3);
                fprintf(stderr, "linecount: %d\n", linecount);
                exit(1);
            }
            else
            {
                // assuming its a valid amount, I would convert it to int and store it in the LineFields structure
                char *token = strtok((char *)str3, ".");
                int dollars = atoi(token);
                int cents = 0;

                token = strtok(NULL, ".");

                if (token != NULL)
                {
                    cents = atoi(token);
                }
                int totalamt = dollars * 100 + cents;
                line->amount = totalamt;
            }
            // check if the fourth field is a valid description
            if (strlen(str4) <= 0)
            {
                fprintf(stderr, "The description field cannot be empty\n");
                fprintf(stderr, "linecount: %d\n", linecount);
                exit(1);
            }
            else
            {
                // assuming its a valid description, I would store it in the LineFields structure
                line->description = (char *)malloc(sizeof(char) * strlen(str4) + 1);
                strcpy(line->description, str4);
                // add null terminator
                line->description[strlen(str4)] = '\0';
                // update linecount
                linecount++;
                // add the temp line to the my402list
                My402ListAppend(pList, line);
            }
        }
    }
}

static void BubbleForward(My402List *pList, My402ListElem **pp_elem1, My402ListElem **pp_elem2)
/* (*pp_elem1) must be closer to First() than (*pp_elem2) */
{
    My402ListElem *elem1 = (*pp_elem1), *elem2 = (*pp_elem2);
    void *obj1 = elem1->obj, *obj2 = elem2->obj;
    My402ListElem *elem1prev = My402ListPrev(pList, elem1);
    /*  My402ListElem *elem1next=My402ListNext(pList, elem1); */
    /*  My402ListElem *elem2prev=My402ListPrev(pList, elem2); */
    My402ListElem *elem2next = My402ListNext(pList, elem2);

    My402ListUnlink(pList, elem1);
    My402ListUnlink(pList, elem2);
    if (elem1prev == NULL)
    {
        (void)My402ListPrepend(pList, obj2);
        *pp_elem1 = My402ListFirst(pList);
    }
    else
    {
        (void)My402ListInsertAfter(pList, obj2, elem1prev);
        *pp_elem1 = My402ListNext(pList, elem1prev);
    }
    if (elem2next == NULL)
    {
        (void)My402ListAppend(pList, obj1);
        *pp_elem2 = My402ListLast(pList);
    }
    else
    {
        (void)My402ListInsertBefore(pList, obj1, elem2next);
        *pp_elem2 = My402ListPrev(pList, elem2next);
    }
}

static void BubbleSortForwardList(My402List *pList, int num_items)
{
    My402ListElem *elem = NULL;
    int i = 0;

    if (My402ListLength(pList) != num_items)
    {
        fprintf(stderr, "List length is not %1d in BubbleSortForwardList().\n", num_items);
        exit(1);
    }
    for (i = 0; i < num_items; i++)
    {
        int j = 0, something_swapped = FALSE;
        My402ListElem *next_elem = NULL;

        for (elem = My402ListFirst(pList), j = 0; j < num_items - i - 1; elem = next_elem, j++)
        {

            time_t cur_time = ((LineFields *)(elem->obj))->time, next_time = 0;
            // if there exists another entry with the same exact timestamp, then print error message and exit
            next_elem = My402ListNext(pList, elem);
            next_time = ((LineFields *)(next_elem->obj))->time;
            if (cur_time == next_time)
            {
                fprintf(stderr, "There exists another entry with the same exact timestamp\n");
                exit(1);
            }

            if (cur_time > next_time)
            {
                BubbleForward(pList, &elem, &next_elem);
                something_swapped = TRUE;
            }
        }
        if (!something_swapped)
            break;
    }
}

/* ----------------------- main() ----------------------- */

int main(int argc, char *argv[])
{

    setlocale(LC_NUMERIC, "");
    FILE *fp = NULL;
    // command line arguments should be of the form: warmup1 sort [tfile] where tfile is optional. If tfile is not specified, then the program should read from stdin.
    if (argc > 3 || argc < 2)
    {
        fprintf(stderr, "Command line is malformed. The number of arguements must be exactly 3\n");
        fprintf(stderr, "argc: %d\n", argc);
        fprintf(stderr, "usage: warmup1 sort [tfile]\n");
        exit(1);
    }
    // if the second argument is not sort, then print error message and exit
    if (strcmp(argv[1], "sort") != 0)
    {
        fprintf(stderr, "usage: need to type sort command as the second argument\n");
        fprintf(stderr, "%s is not a valid command line option\n", argv[1]);
        exit(1);
    }
    // if the third argument is not null, then open the file and read from it
    if (argc == 3)
    {
        fp = fopen(argv[2], "r");
        if (fp == NULL)
        {
            fprintf(stderr, "Cannot open file\n");
            perror("fopen error: ");
            exit(1);
        }
    }
    // if the third argument is null, then read from stdin
    else
    {
        fp = stdin;
    }
    My402List *mylist = (My402List *)malloc(sizeof(My402List));
    My402ListInit(mylist);
    // make another my402list to store the timestamp of each entry and check if there are any duplicates
    My402List *mylist2 = (My402List *)malloc(sizeof(My402List));
    My402ListInit(mylist2);

    process(fp, mylist, mylist2);
    fclose(fp);
    BubbleSortForwardList(mylist, My402ListLength(mylist));
    printlist(mylist);
    free(mylist);
    free(mylist2);
    return (0);
}
