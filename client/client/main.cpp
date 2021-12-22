#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>      // gethostbyname()
#include <sys/socket.h>
#include <netinet/in.h>
#include <fstream>
#include <vector>
#define FILE_FIN 150
using namespace std;
string serverIP="";
int serverPort;
int clientPort;
struct sockaddr_in clientAddr;
struct sockaddr_in serverAddr;
socklen_t addrlen;
ofstream fp;
ofstream fout;
char filename[100];
int protocol = 1;
int clientsocket;
struct packet {
    /* Header */
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data[500]; /* Not always 500 bytes, can be less */
};
struct ack_packet {
    uint16_t len;
    uint32_t ackno;

};
vector<packet*> *buffer = new vector<packet*>;
void extractPacket(packet* packet, char buffer[], int buffLength) {

    unsigned char b0 = buffer[0];
    unsigned char b1 = buffer[1];
    unsigned char b2 = buffer[2];
    unsigned char b3 = buffer[3];
    unsigned char b4 = buffer[4];
    unsigned char b5 = buffer[5];

    //len combine first two bytes
    packet->len = (b0 << 8) | b1;
    //seq_no combine second four bytes
    packet->seqno = (b2 << 24) | (b3 << 16) | (b4 << 8) | (b5);
    for (int i = 6; i < buffLength; ++i) {
        packet->data[i - 6] = buffer[i];
    }

}// convert buffer to packets
void convertAckPacketToByte(ack_packet *packet, char* buffer) {

    //len field
    buffer[0] = packet->len >> 8;
    buffer[1] = (packet->len) & 255;
    //seqnumber
    buffer[2] = packet->ackno >> 24;
    buffer[3] = (packet->ackno >> 16) & 255;
    buffer[4] = (packet->ackno >> 8) & 255;
    buffer[5] = (packet->ackno) & 255;
} //convert ack to bytes

void sendAck(int seqno) {
    char ackData[8];
    ack_packet Ack;
    Ack.ackno = seqno;
    Ack.len = 8;
    convertAckPacketToByte(&Ack, ackData);
    int Flag = sendto(clientsocket, ackData, 8, 0, (struct sockaddr *) &serverAddr,
                          sizeof(serverAddr));
    if (Flag == -1)
        perror("ERROR");
} // send ack to server

int getPacketLength(char data[]) {

    unsigned char b2 = data[0];
    unsigned char b3 = data[1];
    int len = (b2 << 8) | b3;
    return len;

} // calculate packet length
int movepktsToFile(ofstream *fp, packet* packet) {
    if (packet->len == FILE_FIN)
        return 3;
    if (fp == NULL) {
        return -1;
    }
    if (fp->is_open()) {
        for (int i = 0; i < packet->len; ++i) {
            *fp << packet->data[i];
        }
    } else {
        //file is not opened yet
        return -2;
    }
    //success
    return 1;
} // take data from packet and put it to the file
void receiveStopAndWait() {
    int dublicateCount = 0;
    bool isfirstRecieve = true;
    char data[512];
    int recvLen = 1;
    int currentlyReceivedBytes = 0;
    int packetLength = 0;
    int packetCount = 0;
    cout << "Receiving  pkts  now" << endl;
    while (true) {

        currentlyReceivedBytes = 0;
        isfirstRecieve = true;
        cout << "Receiving pkt  " << packetCount << endl;
        fout << "EXPECTED pkt   " << packetCount << endl;
        while (true) {
            recvLen = recvfrom(clientsocket, data, 512, 0, (struct sockaddr *) &serverAddr,
                               &addrlen);
            if (isfirstRecieve) {
                packetLength = getPacketLength(data) + 8;
                isfirstRecieve = false;
            }
            if (packetLength == FILE_FIN + 8) {

                break;
            }

            currentlyReceivedBytes += recvLen;
            if (currentlyReceivedBytes == packetLength || recvLen == 0) {
                break;
            }
        }

        packet recvdPacket;
        extractPacket(&recvdPacket, data, recvLen);
        string st = (string)recvdPacket.data;
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        cout << "pkt  " << recvdPacket.seqno << " Received" << endl;
        fout << "pkt  " << recvdPacket.seqno << " Received" << endl;
        //send acks here

        if (recvdPacket.seqno == packetCount) {
            sendAck(recvdPacket.seqno);
            movepktsToFile(&fp, &recvdPacket);
            packetCount++;
            packetCount = packetCount % 2;
        } else {
            dublicateCount++;
            sendAck(recvdPacket.seqno);
            cout << "DUBLICATION   pkt " << packetCount
                 << " Acked" << endl;
            fout << "DUBLICATION   pkt  " << packetCount
                 << " Acked" << endl;
        }

        if (packetLength == FILE_FIN + 8) {

            break;
        }

    }

} //  receive packets using  stop and wait protocol

void receiveGBN() {
    bool firstRecieve = true;
    char data[512];
    int recvLen = 1;
    int currentlyReceivedBytes = 0;
    int packetLength = 0;
    int packetCount = 0;

    cout << "Receiving  pkts now" << endl;
    fout << "Receiving  pkts now" << endl;
    while (1) {
        currentlyReceivedBytes = 0;
        firstRecieve = true;
        cout << "expected pkt " << packetCount << endl;
        fout << "expected pkt " << packetCount << endl;
        while (1) {
            recvLen = recvfrom(clientsocket, data, 512, 0, (struct sockaddr *) &serverAddr,
                               &addrlen);
            if (firstRecieve) {
                packetLength = getPacketLength(data) + 8;
                firstRecieve = false;
            }
            if (packetLength == FILE_FIN + 8) {

                break;
            }

            currentlyReceivedBytes += recvLen;
            if (currentlyReceivedBytes == packetLength || recvLen == 0) {
                break;
            }
        }
        packet *recvdPacket = new packet;

        extractPacket(recvdPacket, data, recvLen);
        string st(recvdPacket->data, strlen(recvdPacket->data));
        vector<char> bytes(st.begin(), st.end());
        bytes.push_back('\0');
        char *c = &bytes[0];

        cout << "pkt   " << recvdPacket->seqno <<"received"<<endl;
        fout << "pkt   " << recvdPacket->seqno <<"received"<<endl;
        if (recvdPacket->seqno < packetCount) {
            cout << "duplicate pkt ... drop it ..."<< endl;
            fout << "duplicate pkt ... drop it ..."<< endl;
            sendAck(recvdPacket->seqno);

        } else if (recvdPacket->seqno == packetCount) {
            sendAck(recvdPacket->seqno);
            movepktsToFile(&fp, recvdPacket);
            ++packetCount;
        }

        if (packetLength == FILE_FIN + 8) {
            break;
        }

    }
} // recieve packets by GBN protocol
void connectToServer() {
    struct hostent *hp;
    addrlen = sizeof(serverAddr);

    if ((clientsocket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("ERROR");
        exit(0);
    }

    memset((char *) &clientAddr, 0, sizeof(clientAddr));
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = htonl(INADDR_ANY );
    clientAddr.sin_port = htons(clientPort);
    unsigned int alen = sizeof(clientAddr);

    memset((char*) &serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(serverPort);
    hp = gethostbyname(serverIP.c_str());
    if (!hp) {
        cout<<"server not found"<<endl;
        exit(0);
    }

    memcpy((void *) &serverAddr.sin_addr, hp->h_addr_list[0], hp->h_length);
    char *dataSend;
    dataSend = filename;
    int sendFlag = sendto(clientsocket, dataSend, strlen(dataSend), 0,
                          (struct sockaddr *) &serverAddr, sizeof(serverAddr));


    fp.open(filename);
    if (protocol==0) {
        fout << "stop and wait " << endl;
        receiveStopAndWait();
    }
    else if (protocol==1) {
        fout << "GBN " << endl;
        receiveGBN();
    }
    fp.close();
    fout.close();
    printf("close()\n");
    shutdown(clientsocket, 2);
} //connect to server acn check which protocol is used
int main(int argc, char **argv) {

    string line;
    ifstream myfile ("input.txt");
    if (myfile.is_open())
    {
        if( getline (myfile,line) )
        {
            serverIP=line;
        }
        if( getline (myfile,line) )
        {
            serverPort=atoi(line.c_str());
        }

        if( getline (myfile,line) )
        {
            clientPort=atoi(line.c_str());
        }
        if( getline (myfile,line) )
        {
            strcpy(filename,line.c_str());
        }
        myfile.close();
    }
    fout.open("tracing.txt");
    connectToServer();
    return 0;
}