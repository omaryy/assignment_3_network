#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/types.h>
#include <vector>
#include <pthread.h>
#include <sys/time.h>
#include <fstream>
typedef unsigned char uchar;
#ifndef ERESTART
#endif
int TIMEOUT = 300000;
int TIMERSTOP = -100;
using namespace std;
extern int errno;
ofstream logFile;
ofstream logFile2;
ofstream logFile3;

timeval time1;
timeval time2;

int timeCount = 0;
float  plp;
struct ack_packet
{

    uint16_t len;
    uint32_t ackno;
};
struct packet
{
    /* Header */
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data[500]; /* Not always 500 bytes, can be less */
};
int serverPort = 0;
int maxWindowSize;
int curWindowSize=2;
FILE* fp;
int threshold=50;
vector<char*> *frames = new vector<char*>;
vector<bool> *ackedFrames = new vector<bool>;
vector<int> *times = new vector<int>;
vector<int> *lens = new vector<int>;
int base = 0;
int curr = 0;
int childSocket;
int protocol = 1;//0 for stop and wait and   1 for Go Back N
int outOfOrder = 0;
pthread_t recThread;
pthread_t stopAndWaitTimerThread;
pthread_t SelectiveThread;
bool stopAndWaitAcked = false;
pthread_mutex_t Selectivemutex;
pthread_mutex_t StopAndWaitmutex;
pthread_mutex_t StopAndWaitmutexTimer;
pthread_mutex_t StopAndWaitRecThreadCreationMutex;
/* serve: set up the service */
struct sockaddr_in my_addr; /* address of this service */
struct sockaddr_in child_addr; /* address of this service */
struct sockaddr_in client_addr; /* client's address */
int svc; /* listening socket providing service */
int request; /* socket accepting the request */
int dropCount = 0;
int droppedpackets=0;
bool isdropped()
{
    float drop = dropCount * plp;

    if (drop >= 0.9)
    {
        dropCount = 0;
        droppedpackets++;
        return true;
    }
    else
    {
        return false;
    }

} // check if packet dropped or not and if dropped increase num of dropped packets

void makepackets(packet* pkt, char buffer[], int bufferLength)
{

    uchar b0 = buffer[0];
    uchar b1 = buffer[1];
    uchar b2 = buffer[2];
    uchar b3 = buffer[3];
    uchar b4 = buffer[4];
    uchar b5 = buffer[5];

    pkt->len = (b0 << 8) | b1;
    //seq_no combine third four bytes
    pkt->seqno = (b2 << 24) | (b3 << 16) | (b4 << 8) | (b5);
    for (int i = 6; i < bufferLength; ++i)
    {
        pkt->data[i - 6] = buffer[i];
    }

} // put data into the packet from buffer
void resend(int i)
{
    sendto(childSocket, frames->at(i), lens->at(i) + 8, 0,
                      (struct sockaddr *) &client_addr, sizeof(client_addr));

    packet p;
    makepackets(&p, frames->at(i), lens->at(i));
    logFile << "Resend pkt  = " << p.seqno << endl;

} //if packet is dropped resend it&max_window_size &plp

void *GBNTimer(void *time)
{

    while (true)
    {
        pthread_mutex_lock(&Selectivemutex);
        for (int i = 0; i < times->size(); ++i)
        {
            times->at(i)--;
            if(times->at(i)==0)
            {

                logFile<<"TIME OUT"<<endl;
                times->at(i)= TIMEOUT;
                resend(i);
            }
        }

        pthread_mutex_unlock(&Selectivemutex);
    }

} // calculate time  packet  transfer by  GBN
void convertDataPacketToByte(packet *packet, char* buffer)
{

    //len field

    buffer[0] = packet->len >> 8;
    buffer[1] = (packet->len) & 255;

    //seqnumber

    buffer[2] = packet->seqno >> 24;
    buffer[3] = (packet->seqno >> 16) & 255;
    buffer[4] = (packet->seqno >> 8) & 255;
    buffer[5] = (packet->seqno) & 255;

    //data

    for (int i = 6; i < packet->len + 8; ++i)
    {
        buffer[i] = packet->data[i - 6];
    }

} // convert packets to bytes
void sendByGBN(char* fileName)
{
    fp = fopen(fileName, "r");
    if (fp == NULL)
    {
        perror("Error Not found  404");

    }
    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);

    fseek(fp, 0L, SEEK_SET);

    packet currPacket;
    int packetCount = 0;

    for (int i = 0; i < size;)
    {

        dropCount++;
        dropCount = dropCount % 101;


        currPacket.len = fread(currPacket.data, 1, 500, fp);
        string st(currPacket.data, strlen(currPacket.data));
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];
        currPacket.seqno = packetCount;
        i += currPacket.len;
        char *buffer = new char[512];
        unsigned int sendFlag = 1;
        convertDataPacketToByte(&currPacket, buffer);

        while (1)
        {
            pthread_mutex_lock(&Selectivemutex);
            if (curr - base < curWindowSize - 1)
            {
                if (!isdropped())
                {
                    cout << "send pkt"<<"  "<< curr
                         << endl;
                    sendFlag = sendto(childSocket, buffer, currPacket.len + 8,
                                      0, (struct sockaddr *) &client_addr,
                                      sizeof(client_addr));
                    if (sendFlag < -1)
                        break;
                    else if (sendFlag == -1)
                        perror("ERROR");
                }
                else
                {
                    // return to slow start
                    threshold= curWindowSize/2;
                    curWindowSize = 1;
                    logFile2<<threshold<<endl;
                    time_t now=time(0);
                    tm *ltm=localtime(&now);
                    logFile3<<30+ltm->tm_min<<"."<<ltm->tm_sec<<"     "<<curWindowSize<< endl;


                    logFile<<"there is a pkt loss "<<endl;
                    logFile << endl;
                    logFile << "pkt :" << packetCount<< " is lost " << endl;
                    logFile << endl;
                    break;
                }
            }
            else
            {
                pthread_mutex_unlock(&Selectivemutex);
            }
        }
        logFile << "pkt " << currPacket.seqno << " is sent" << endl;

        curr = currPacket.seqno;
        frames->push_back(buffer);
        ackedFrames->push_back(false);
        times->push_back(TIMEOUT);
        lens->push_back(currPacket.len);

        pthread_mutex_unlock(&Selectivemutex);
        packetCount++;
    }
    currPacket.len = 150;
    currPacket.seqno = packetCount;
    gettimeofday(&time2, NULL);
    long t2 = time2.tv_sec;
    t2 = t2 - time1.tv_sec;
    logFile << "Time Taken is " << t2 << " secs" << endl;
    logFile.close();
    char *dataFinish = new char[8];
    int sendFlag = 1;
    convertDataPacketToByte(&currPacket, dataFinish);
    while (1)
    {
        if (curr - base < curWindowSize - 1)
        {
            sendFlag = sendto(childSocket, dataFinish, sizeof(dataFinish), 0,
                              (struct sockaddr *) &client_addr, sizeof(client_addr));
            break;
        }
    }
    pthread_mutex_lock(&Selectivemutex);

    curr = currPacket.seqno;
    frames->push_back(dataFinish);
    ackedFrames->push_back(false);
    lens->push_back(currPacket.len);
    times->push_back(TIMEOUT);
    pthread_mutex_lock(&Selectivemutex);

    if (sendFlag == -1)
        perror("sendto");

} // send packets to client using GBN
void parseAckPacket(char *buffer, int buffLength, ack_packet* packet)
{

    uchar b0 = buffer[0];
    uchar b1 = buffer[1];
    uchar b2 = buffer[2];
    uchar b3 = buffer[3];
    uchar b4 = buffer[4];
    uchar b5 = buffer[5];

    //len combine first two bytes
    packet->len = (b0 << 8) | b1;
    //seq_no combine second four bytes
    packet->ackno = (b2 << 24) | (b3 << 16) | (b4 << 8) | (b5);
}
void sig_func(int sig)
{
    cout << "receiving thread is dead" << endl;
    pthread_exit(NULL);
} //kill
void *receiveAckGBN(void *threadid)
{

    while (true)
    {
        socklen_t alen = sizeof(client_addr); /* length of address */
        int reclen;
        char Buffer[8];
        if (reclen = (recvfrom(childSocket, Buffer, 8, 0,
                               (struct sockaddr *) &client_addr, &alen)) < 0)
            perror("ERROR");
        ack_packet packet;
        parseAckPacket(Buffer, 8, &packet);
        int frameIndex = packet.ackno - base;
        pthread_mutex_lock(&Selectivemutex);
        cout << "ack " << packet.ackno << " received" << endl;
        logFile << "ack " << packet.ackno << " received" << endl;


        if (frameIndex < 0 || ackedFrames->size() == 0)
        {
            cout << "dublicate ack  ignore" << endl;
            logFile << "dublicate ack ignore" << endl;

        }
        else if (ackedFrames->size() != 0
                 && ackedFrames->at(frameIndex) == true)
        {
            cout << "dublicate ack is detected " << endl;
            logFile << "dublicate ACK is detected " << endl;
        }
        else
        {
            if (curWindowSize*2 < threshold)
            {

                curWindowSize*=2;
                logFile << "increase  window size  from  " << curWindowSize/2<< " TO " << curWindowSize << endl;
                logFile2<<threshold<<endl;
                time_t now=time(0);
                tm *ltm=localtime(&now);
                logFile3<<30+ltm->tm_min<<"."<<ltm->tm_sec<<"     "<<curWindowSize<< endl;





            }else if(curWindowSize<maxWindowSize&&curWindowSize>=threshold){
                curWindowSize++;
                logFile << "increase  window size  from  " << (curWindowSize - 1)<< " TO " << curWindowSize << endl;
                logFile2<<threshold<<endl;
                time_t now=time(0);
                tm *ltm=localtime(&now);
                logFile3<<30+ltm->tm_min<<"."<<ltm->tm_sec<<"     "<<curWindowSize<< endl;


            }else if(curWindowSize*2>threshold&&curWindowSize<threshold){
                curWindowSize+= (threshold-curWindowSize);
                logFile << "increase window size  from  " << (threshold-curWindowSize)<< " TO " << curWindowSize << endl;
                logFile2<<threshold<<endl;
                time_t now=time(0);
                tm *ltm=localtime(&now);
                logFile3<<30+ltm->tm_min<<"."<<ltm->tm_sec<<"     "<<curWindowSize<< endl;


            }
            ackedFrames->at(frameIndex) = true;
            //TODO stop timer
            times->at(frameIndex) = TIMERSTOP;
            if (ackedFrames->at(0) == true)
            {
                cout<< "pkt  "<<base<<"  is acked..move the window"<< endl;
                logFile << "pkt is acked...move the window"<< endl;
                for (int i = 0; i <= curr - base; ++i)
                {
                    if (ackedFrames->size() == 0)
                        break;
                    if (ackedFrames->at(0) == true)
                    {
                        frames->erase(frames->begin());
                        ackedFrames->erase(ackedFrames->begin());
                        times->erase(times->begin());
                        lens->erase(lens->begin());
                        ++base;
                    }
                    else
                    {
                        break;
                    }
                }
                cout << "base pkt  is  = " << base << endl;
                logFile << "base  pkt  is = " << base << endl;
            }

        }
        pthread_mutex_unlock(&Selectivemutex);
    }

} //recieve ack from client by GBN
void *receiveAckStopAndWait(void *threadid)
{
    int packetCount = ((intptr_t) threadid);
    socklen_t alen = sizeof(client_addr); /* length of address */
    int reclen;
    char Buffer[8];
    cout << "waiting for ack " << packetCount << endl;
    logFile << "waiting for ack " << packetCount << endl;
    bool waitEnd = false;
    while (!waitEnd)
    {
        stopAndWaitAcked = false;
        if (reclen = (recvfrom(childSocket, Buffer, 8, 0,
                               (struct sockaddr *) &client_addr, &alen)) < 0)
            perror("recvfrom() failed");

        pthread_mutex_lock(&StopAndWaitmutexTimer);
        ack_packet packet;
        parseAckPacket(Buffer, 8, &packet);
        cout << "ack #" << packet.ackno << " received" << endl;
        logFile << "ack #" << packet.ackno << " received" << endl;
        uint16_t temp;
        if (packetCount == 0)
            temp = 0;
        else
            temp = 1;
        if (packet.ackno != temp)
        {
            cout << "Received is " << packet.ackno
                 << "...out of order ack...wait for right Packet # "
                 << packetCount << endl;

            logFile << "Received is " << packet.ackno
                    << "...out of order ack...wait for right Packet # "
                    << packetCount << endl;
            outOfOrder++;
            pthread_mutex_unlock(&StopAndWaitmutexTimer);
        }
        else
        {
            cout << "killing Timer" << endl;
            waitEnd = true;
            stopAndWaitAcked = true;
        }
    }
    pthread_mutex_unlock(&StopAndWaitmutexTimer);
    pthread_mutex_unlock(&StopAndWaitmutex);
    pthread_exit(NULL);
} // recieve ack from client by stop and wait
void *StopAndWaitTimer(void *time)
{

    int sleeptime = 3 * 100000;
    int timeHolder = sleeptime;
    while (true)
    {
        pthread_mutex_lock(&StopAndWaitRecThreadCreationMutex);
        pthread_mutex_lock(&StopAndWaitmutexTimer);
        sleeptime--;
        if (stopAndWaitAcked == true)
        {
            sleeptime = timeHolder;
        }
        else if (sleeptime <= 0)
        {
            sleeptime = timeHolder;
            timeCount++;
            cout << "-----------------TIMEOUT-------------------------" << endl;
            logFile << "-----------------TIME OUT-------------------------"
                    << endl;
            pthread_cancel(recThread);
            stopAndWaitAcked = false;
            pthread_mutex_unlock(&StopAndWaitmutex);
        }
        pthread_mutex_unlock(&StopAndWaitmutexTimer);
        pthread_mutex_unlock(&StopAndWaitRecThreadCreationMutex);
    }

} //time take stop and  wait
void sendByStopAndWait(char* fileName)
{
    bool recThreadIsCreated = false;
    bool timerStarted = false;
    fp = fopen(fileName, "r");
    if (fp == NULL)
    {
        perror("Error Not found  404");

    }
    fseek(fp, 0L, SEEK_END);
    long size = ftell(fp);

    fseek(fp, 0L, SEEK_SET);

    packet currPacket;

    cout << "File Size : " << size << endl;
    logFile << "File Size : " << size << endl;
    int packetCount = 0;

    int couny = 0;
    dropCount = 0;
    for (int i = 0; i < size;)
    {
        dropCount++;
        dropCount = dropCount % 101;
        currPacket.len = fread(currPacket.data, 1, 500, fp);
        string st = (string)currPacket.data;
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];
        currPacket.seqno = packetCount;
        char buffer[512];
        i += currPacket.len;
        unsigned int sendFlag = 1;
        convertDataPacketToByte(&currPacket, buffer);
        bool acked = false;
        stopAndWaitAcked = false;
        while (!acked)
        {
            if (!isdropped())
            {
                while (1)
                {
                    sendFlag = sendto(childSocket, buffer, currPacket.len + 8,
                                      0, (struct sockaddr *) &client_addr,
                                      sizeof(client_addr));
                    if (sendFlag == 0)
                    {
                        break;
                    }
                    else if (sendFlag < -1)
                        break;
                    else if (sendFlag == -1)
                        cout<<"error sendto"<<endl;
                }
                cout << "******		" << packetCount << " Packet Sent" << endl;
                logFile << "PACKET: " << packetCount << " Sent" << endl;
            }
            else
            {
                cout << "Package Dropped" << endl;
                logFile << "----------------Package Dropped---------------"<< endl;
            }
            pthread_mutex_lock(&StopAndWaitmutex);
            pthread_mutex_lock(&StopAndWaitRecThreadCreationMutex);
            if (recThreadIsCreated == true)
            {
                pthread_kill(recThread, 0);
                recThreadIsCreated = false;
            }
            while (1)
            {
                int rc = pthread_create(&recThread, NULL, receiveAckStopAndWait,
                                        (void *) packetCount);
                recThreadIsCreated = true;
                pthread_detach(recThread);
                if (rc)
                {
                    cout << "error in starting the Stop and wait ack thread"
                         << endl;
                    sleep(1);
                }
                else
                {
                    break;
                }
            }
            pthread_mutex_unlock(&StopAndWaitRecThreadCreationMutex);
            if (!timerStarted)
            {
                timerStarted = true;
                int timeout = 3;
                while (1)
                {
                    int rc2 = pthread_create(&stopAndWaitTimerThread, NULL,
                                             StopAndWaitTimer, (void *) timeout);
                    if (rc2)
                    {
                    }
                    else
                    {
                        pthread_detach(stopAndWaitTimerThread);
                        break;
                    }
                }
            }

            pthread_mutex_lock(&StopAndWaitmutex);
            pthread_mutex_lock(&StopAndWaitmutexTimer);
            if (stopAndWaitAcked == true)
            {
                stopAndWaitAcked = false;
                acked = true;
            }
            else
            {
                acked = false;
            }
            pthread_mutex_unlock(&StopAndWaitmutex);
            pthread_mutex_unlock(&StopAndWaitmutexTimer);

        }

        packetCount++;
        packetCount = packetCount % 2;

    }
    currPacket.len = 150;
    currPacket.seqno = packetCount;
    cout << "******		Sending finish packet." << endl;
    char dataFinish[8];
    int sendFlag = 1;
    convertDataPacketToByte(&currPacket, dataFinish);
    sendFlag = sendto(childSocket, dataFinish, sizeof(dataFinish), 0,
                      (struct sockaddr *) &client_addr, sizeof(client_addr));

    if (sendFlag == -1)
        perror("sendto");
    pthread_kill(stopAndWaitTimerThread, 0);
    cout << "******		Sending finish packet DONE." << endl;
    cout << "Out Of order Count = " << outOfOrder << endl;
} //send packet by stop and wait
void start()
{
    signal(SIGSEGV, sig_func);
    socklen_t alen;
    int sockoptval = 1;

    char hostname[128] = "localhost";
    if ((svc = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
    {
        perror("cannot create socket");
        exit(1);
    }

    memset((char*) &my_addr, 0, sizeof(my_addr));
    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(serverPort);
    my_addr.sin_addr.s_addr = htonl(INADDR_ANY );

    if (bind(svc, (struct sockaddr *) &my_addr, sizeof(my_addr)) < 0)
    {
        perror("bind failed");
        exit(1);
    }
    while(1)
    {

        alen = sizeof(client_addr);
        cout << "listening  :) " << endl;
        char requestedfile[100];
        if ((recvfrom(svc, requestedfile, 100, 0, (struct sockaddr *) &client_addr,
                      &alen)) < 0)
            cout<<"Error requested file from  client"<<endl;
        cout << "requested file from server is   : " << requestedfile << endl;
        logFile << "A request is received for file : " << requestedfile << endl;
        logFile << "FORKING NEW PROCESS TO HANDLE THE SENDING" << endl;
        int pid = fork();
        //child process will enter
        if (pid == 0)
        {
            logFile << "child process created " << endl;
            pthread_t ackThread;
            int rc;
            long t = 0;
            logFile << "creating new socket to handel the connection" << endl;
            if ((childSocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
            {
                cout<<"cannot create socket"<<endl;
                exit(1);
            }

            memset((char*) &child_addr, 0, sizeof(child_addr));
            child_addr.sin_family = AF_INET;
            child_addr.sin_addr.s_addr = htonl(INADDR_ANY );
            child_addr.sin_port = htons(serverPort);
            cout << "Child Socket Created" << endl;
            logFile << "Child Socket Created" << endl;


            if(protocol==0)
            {
                logFile << "stop and wait" << endl;
                gettimeofday(&time1, NULL);
                long t1 = time1.tv_sec;
                sendByStopAndWait(requestedfile);
                gettimeofday(&time2, NULL);
                long t2 = time2.tv_sec;
                t2 = t2 - t1;
                logFile << "Time Taken is " << t2 << " secs" << endl;
                logFile.close();
                exit(0);

            }
            else if(protocol==1)
            {

                logFile << "GBN" << endl;

                rc = pthread_create(&ackThread, NULL, receiveAckGBN, (void *) t);

                int rc1 = pthread_create(&SelectiveThread, NULL,
                                         GBNTimer, (void *) t);

                if (rc || rc1)
                {
                    cout<< "error in  the ack thread or timer "<< endl;
                }
                else
                {
                    pthread_detach(ackThread); //resources release back to the system
                    gettimeofday(&time1, NULL);
                    long t1 = time1.tv_sec;

                    sendByGBN(requestedfile);
                    gettimeofday(&time2, NULL);
                    long t2 = time2.tv_sec;
                    t2 = t2 - t1;
                    logFile << "Time Taken is " << t2 << " secs" << endl;
                    logFile.close();
                    exit(0);
                    shutdown(request, 2); /* 3,590,313 close the connection */
                }

            }

            close(childSocket);
        }
        else if (pid == -1)
        {
            cout << "error forking  process to handle the client" << endl;
        }
    }

    close(svc);
} // start the code by reading from input file port num and plp and indicate which protocol will be used
int main(int argc, char** argv)
{

    logFile.open("tracing.txt");
    logFile2.open("Threshold.txt");
    logFile3.open("current_Window_size.txt");
    string line;
    ifstream input ("input.txt");
    if (input.is_open())
    {

        if( getline (input,line) )
        {
            serverPort=atoi(line.c_str());
        }
        if( getline (input,line) )
        {
            maxWindowSize=atoi(line.c_str());
        }
        if( getline (input,line) )
        {
            plp=atof(line.c_str());
        }
    }
    input.close();
    start();
    return 0;
}