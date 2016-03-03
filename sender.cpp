#include <sys/socket.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

const short PAYLOAD_SIZE = 512;

struct Packet
{
    unsigned short sourcePort;
    unsigned short destinationPort;
    unsigned int   sequenceNumber;
    unsigned int   ackNumber;

    // message types
    unsigned char  synSynAck; // 0 for neither, 1 for SYN, 255 for SYNACK
    unsigned char  ackFin; // 0 for neither, 1 for ACK, 255 for FIN

    unsigned short windowSize; 
    unsigned int   payloadLength;
    char           payload[PAYLOAD_SIZE+1];
};

// check to see if the packet is of the type SYN
bool isSynPacket(struct Packet *pkt)
{
    if (pkt->synSynAck == 1 && pkt->ackFin == 0)
        return true;
    else
        return false;
}

// check to see if the packet is of the type ACK
bool isAckPacket(struct Packet *pkt)
{
    if (pkt->ackFin == 1 && pkt->synSynAck == 0)
        return true;
    else
        return false;
}

// check to see if the packet is not SYN or ACK, so it must be a data packet
bool isDataPacket(struct Packet *pkt)
{
    if (pkt->synSynAck == 0 && pkt->ackFin == 0)
        return true;
    else
        return false;
}

// set the payload to 0
void clearPayload(struct Packet *pkt)
{
    for (int i = 0; i < PAYLOAD_SIZE; i++)
    {
        pkt->payload[i] = '\0';
    }
}

void setPayload(struct Packet *pkt, const char *str, int len)
{
    memcpy(pkt->payload, str, len);
    pkt->payloadLength = len;
}

unsigned int getCumAck(struct Packet *pkt)
{
    return pkt->ackNumber;
}

// param pkt is the packet you would like to modify into a SYNACK type
void createSynAckPacket(struct Packet *pkt, unsigned short sourcePort, unsigned short destinationPort)
{
    // set the type to SYNACK
    pkt->synSynAck = 255;
    pkt->ackFin = 0;
    pkt->sequenceNumber = 0;
    pkt->ackNumber = 0;
    pkt->sourcePort = sourcePort;
    pkt->destinationPort = destinationPort;
    pkt->payloadLength = 0;
    clearPayload(pkt);
}

// param pkt is the packet you would like to modify into a FIN type
void createFinPacket(struct Packet *pkt, unsigned short sourcePort, unsigned short destinationPort)
{
    // set the type to FIN
    pkt->synSynAck = 0;
    pkt->ackFin = 255;
    pkt->sequenceNumber = 0;
    pkt->ackNumber = 0;
    pkt->sourcePort = sourcePort;
    pkt->destinationPort = destinationPort;
    pkt->payloadLength = 0;
    clearPayload(pkt);
}

void printPacket(struct Packet *pkt)
{
    bool condensed = true;

    if (!condensed)
    {
        printf("Header:\n");
        printf("Source Port: %hu | Destination Port: %hu\n", pkt->sourcePort, pkt->destinationPort);
        printf("Sequence Number: %d\n", pkt->sequenceNumber);
        printf("Ack Number: %d\n", pkt->ackNumber);
        printf("SYN/SYNACK: %d | ACK/FIN: %d | Window Size: %hu\n", pkt->synSynAck, pkt->ackFin, pkt->windowSize);
        printf("Payload Length: %d\n", pkt->payloadLength);
        printf("Data:\n");
        pkt->payload[PAYLOAD_SIZE] = '\0';
        printf("%s\n", pkt->payload);
        printf("\n**************************************\n\n");
    }
    else
    {
        if (pkt->synSynAck != 0 || pkt->ackFin != 0)
            printf("SYN/SYNACK: %d | ACK/FIN: %d | Window Size: %hu\n", pkt->synSynAck, pkt->ackFin, pkt->windowSize);
        else
        {
            printf("Sequence Number: %d | Ack Number: %d\n", pkt->sequenceNumber, pkt->ackNumber);
        }
    }
}

// param filename, this function will return the filename in the parameter
void getFileName(struct Packet *pkt, char *filename, int filenameLength)
{
    for (int i = 0; i < filenameLength; i++)
    {
        filename[i] = pkt->payload[i];
    }
    filename[filenameLength-1] = '\0';
}

unsigned short getPacketWindowSize(struct Packet *pkt)
{
    return pkt->windowSize;
}


class Connection
{
public:
    Connection(unsigned short sourcePort, unsigned short destinationPort, unsigned short windowSize);
    void closeConnection();

    // open the file requested by the client
    bool openFile(char *filename);

    // get the next packet that should be sent based on most recent seq number
    // return seq number of this pkt
    unsigned int getNextPacket(struct Packet *pkt);

    // return false if we can send another pkt based on window size and packets in flight
    // return true if packets in flight is equal to window size or file is small enough
    // to where it does not need the entire window to transmit
    bool isWindowFull();

    // tell the connection that we received an ack, so check if we can move the window size by 1
    void receivedAck(unsigned int cumAck);

    // check to see if there has been a timeout
    bool hasTimedout();
    void setPacketSendTime();

    // have all of the packets for the file been acked?
    // return true if the file has been transfered reliably
    bool isTransferComplete();

private:
    unsigned short     m_sourcePort;
    unsigned short     m_destinationPort;

    unsigned int       m_lastSequenceNumber;
    unsigned short     m_windowSize;
    unsigned short     m_windowStart;
    unsigned short     m_windowEndOfFile;

    FILE               *m_f;
    char               *m_fileContents;
    unsigned int       m_fileSize;

    unsigned long long timeout;
};

Connection::Connection(unsigned short sourcePort, unsigned short destinationPort, unsigned short windowSize)
{
    // do something
    m_lastSequenceNumber = 0;
    m_windowStart = 0;
    m_windowSize = windowSize;
    m_windowEndOfFile = 0;

    m_sourcePort = sourcePort;
    m_destinationPort = destinationPort;
}

void Connection::setPacketSendTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);

    timeout = (unsigned long long)(tv.tv_sec) * 1000 +
              (unsigned long long)(tv.tv_usec) / 1000;
}

bool Connection::hasTimedout()
{
    //printf("Check timeout\n");
    const unsigned long long TIMEOUT_LENGTH = 500;
    struct timeval tv;

    gettimeofday(&tv, NULL);

    unsigned long long millisecondsSinceEpoch = 
        (unsigned long long)(tv.tv_sec) * 1000 +
        (unsigned long long)(tv.tv_usec) / 1000;

    if (millisecondsSinceEpoch - timeout >= TIMEOUT_LENGTH)
    {
        // reset the window to prepare for retransmit of window (Go Back N)
        m_lastSequenceNumber = m_windowStart;
        return true;
    }
    else
    {
        return false;
    }
}

bool Connection::openFile(char *filename)
{
    struct stat buf;
    if (stat(filename, &buf) != 0)
    {
        // file was not found
        // TODO: close the connection
        //closeConnection();
        return false;
    }
    else
    {
        // file was found
        m_f = fopen(filename, "r");

        // get file size
        m_fileSize = (int) buf.st_size;

        m_windowEndOfFile = m_fileSize / PAYLOAD_SIZE + 1;

        // debug
        printf("DEBUG: filesize: %d bytes, num pkts: %d\n", m_fileSize, m_windowEndOfFile);

        // allocate memory, read file and copy file into memory
        m_fileContents = (char *) malloc(sizeof(char) * m_fileSize + 1);
        if (m_fileContents == NULL)
        { 
            printf("Could not allocate memory for file!\n");
        }
        bzero(m_fileContents, sizeof(char) * m_fileSize + 1);

        // read file
        fread(m_fileContents, sizeof(char), m_fileSize, m_f);
        if (ferror(m_f))
        {
            printf("Could not read file!\n");
        }
        m_fileContents[m_fileSize] = '\0';

        fclose(m_f);
        return true;
    }
}

bool Connection::isWindowFull()
{
    // the window is not full if the sequence number is inside the window size
    // and if the file's data has not been fully sent yet
    unsigned short trueWindowEnd = 0;

    if (m_windowStart + m_windowSize <= m_windowEndOfFile)
        trueWindowEnd = m_windowStart + m_windowSize;
    else if (m_windowStart + m_windowSize > m_windowEndOfFile)
        trueWindowEnd = m_windowEndOfFile;

    if (m_lastSequenceNumber < trueWindowEnd)
        return false;
    else
        return true;
}

unsigned int Connection::getNextPacket(struct Packet *pkt)
{
    // set the header correctly
    pkt->sourcePort = m_sourcePort;
    pkt->destinationPort = m_destinationPort;

    pkt->sequenceNumber = m_lastSequenceNumber;
    pkt->ackNumber = 0;

    pkt->synSynAck = 0;
    pkt->ackFin = 0;

    // compute number of bytes to copy
    int payloadSize = 0;
    if (m_lastSequenceNumber * PAYLOAD_SIZE + PAYLOAD_SIZE < m_fileSize)
        payloadSize = PAYLOAD_SIZE;
    else
    {
        // we have reached the end of the file
        // compute a custom number of bytes to copy so we don't overrun the end
        payloadSize = m_fileSize - (m_lastSequenceNumber * PAYLOAD_SIZE);
    }

    // copy the file data into the payload
    memcpy(pkt->payload, &m_fileContents[m_lastSequenceNumber * PAYLOAD_SIZE], payloadSize);

    pkt->payloadLength = payloadSize;

    // only increment the last sequence number if we are still inside the window
    if (!isWindowFull())
        m_lastSequenceNumber++;

    return m_lastSequenceNumber - 1;
}

void Connection::receivedAck(unsigned int cumAck)
{
    m_windowStart = cumAck;
}

bool Connection::isTransferComplete()
{
    if (m_windowStart >= m_windowEndOfFile)
        return true;
    else
        return false;
}

void Connection::closeConnection()
{
    free(m_fileContents);
}

void sendPacketsHelper(Connection *connection, struct Packet *pkt, int socketFd, struct sockaddr_in remoteAddress)
{
    int sentLen = 0;
    while (!connection->isWindowFull())
    {
        // set the time of the most recently set packet for the connection
        connection->setPacketSendTime();

        // add the file contents to the payload of the pkt
        connection->getNextPacket(pkt);
        
        if ((sentLen = sendto(socketFd, pkt, sizeof(struct Packet), 0, (struct sockaddr *) &remoteAddress, sizeof(remoteAddress))) > 0)
        {
            // debug info
            printf("Sent %d bytes\n", sentLen);
            printPacket(pkt);
        }
        else
        {
            printf("Error sending msg: %s\n", strerror(errno));
        }
    }
}

int main(int argc, char **argv)
{
    // check that a port number was passed as an arguement
    if (argc < 2)
    {
        printf("Usage: ./sender <portnum> <packet loss> <packet corruption>\n");
        return 0;
    }

    const int BUFFER_SIZE = 1024;

    // address information for self and remote host to reply to
    struct sockaddr_in myAddress;
    struct sockaddr_in remoteAddress;

    // socket file descriptor and buffers for sending and receiving
    int socketFd;
    //char sendBuffer[BUFFER_SIZE];
    char recvBuffer[BUFFER_SIZE];

    // number of bytes received from remote host
    int receivedLen = 0;
    int sentLen = 0;
    socklen_t remoteAddressSize = sizeof(remoteAddress);

    // grab port number from args, as well as probabilties
    int portNumber = atoi(argv[1]);
    int packetLossPercentage = 0;
    int packetCorruptionPercentage = 0; 

    // assign the probabilties from the command line if they were passed
    if (argc == 4)
    {
        packetLossPercentage = atoi(argv[2]);
        packetCorruptionPercentage = atoi(argv[3]);
    }

    printf("Starting server on port %d...\n\n\n", portNumber);

    // attempt to create a UDP socket
    if ((socketFd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        printf("Unable to create socket!\n");
        return 0;
    }

    // set the socket as non blocking for timeout implementation
    int flags = fcntl(socketFd, F_GETFL);
    flags |= O_NONBLOCK;
    fcntl(socketFd, F_SETFL, flags);

    memset((char *) &myAddress, 0, sizeof(myAddress));
    memset((char *) &remoteAddress, 0, sizeof(remoteAddress));
    myAddress.sin_family = AF_INET;
    myAddress.sin_addr.s_addr = htonl(INADDR_ANY); 
    myAddress.sin_port = htons(portNumber);

    // attempt to bind the address and port to the socket
    if (bind(socketFd, (struct sockaddr *) &myAddress, sizeof(myAddress)) < 0)
    {
        printf("Unable to bind!\n");
        return 0;
    }

    // create a connection in the case that the server receives a connection from the client
    Connection *connection = NULL;
    int handshakeSeq = false;
    srand(time(NULL));

    while (true)
    {
        // check to see if there is there has been a timeout
        if (connection != NULL && connection->hasTimedout())
        {  
            // debug
            printf("\n********TIMEOUT: RESENDING WINDOW!********\n\n");
            struct Packet pkt;
            sendPacketsHelper(connection, &pkt, socketFd, remoteAddress);
        }
        else if (connection != NULL && connection->isTransferComplete())
        {
            struct Packet pkt;
            // the transfer is complete, so close the connection with a FIN packet
            createFinPacket(&pkt, ntohs(myAddress.sin_port), ntohs(remoteAddress.sin_port));
            if ((sentLen = sendto(socketFd, &pkt, sizeof(struct Packet), 0, (struct sockaddr *) &remoteAddress, remoteAddressSize)) > 0)
            {
                // debug info
                printf("Sent %d bytes for FIN\n", sentLen);
                printPacket(&pkt);

                connection->closeConnection();
                free(connection);
                connection = NULL;
            }
            else
            {
                printf("Error sending msg: %s\n", strerror(errno));
            }
            continue;
        }
        else if ((receivedLen = recvfrom(socketFd, recvBuffer, BUFFER_SIZE, 0, (struct sockaddr *) &remoteAddress, &remoteAddressSize)) > 0)
        {
            // try to interpret the incoming message as a packet, its a packet if it is the correct size
            // TODO change this to be correct (remote true OR)
            if (true || receivedLen == sizeof(struct Packet))
            {
                // cast the buffer as a packet
                Packet *pkt = reinterpret_cast<struct Packet *>(recvBuffer);
                printf("This is the packet we just received (%d bytes):\n", receivedLen);
                printPacket(pkt);

                // determine the type of message with the following if statement

                // check to see if it is a SYN
                if (isSynPacket(pkt))
                {
                    // set the state of the handshake, that it has begun
                    handshakeSeq = true;

                    // this is a SYN packet from the client requesting a new connection
                    // send back a SYNACK packet, telling the client we accept their new connection request
                    createSynAckPacket(pkt, ntohs(myAddress.sin_port), ntohs(remoteAddress.sin_port));

                    if ((sentLen = sendto(socketFd, pkt, sizeof(struct Packet), 0, (struct sockaddr *) &remoteAddress, remoteAddressSize)) > 0)
                    {
                        // debug info
                        printf("Sent %d bytes\n", sentLen);
                        printPacket(pkt);
                    }
                    else
                    {
                        printf("Error sending msg: %s\n", strerror(errno));
                    }
                    continue;
                }
                // check to see if it is an ACK type and that we have already received a SYN
                else if (isAckPacket(pkt) && handshakeSeq)
                {
                    // set the handshake state that the handshake is over
                    handshakeSeq = false;

                    // create a new reliable data transfer connection
                    connection = new Connection(ntohs(myAddress.sin_port), ntohs(remoteAddress.sin_port), getPacketWindowSize(pkt));

                    // this is an ACK packet from the client saying it received our SYNACK
                    // the client should have sent along the filename is is requesting in the payload

                    // grab the filename from the payload
                    const int filenameLength = 64;
                    char filename[filenameLength];
                    getFileName(pkt, filename, filenameLength);

                    // debug
                    printf("Requested filename: %s\n", filename);

                    // attempt to open the file if it exists
                    if(connection->openFile(filename))
                    {
                        // if we opened the file OK, reply with the contents of the file
                        // send packets using pipelining based on the receiver's window size

                        // check to see if we are allowed to send another packet based on window size
                        sendPacketsHelper(connection, pkt, socketFd, remoteAddress);
                    }
                    else
                    {
                        // if we could not open the file, close the connection with a FIN packet
                        createFinPacket(pkt, ntohs(myAddress.sin_port), ntohs(remoteAddress.sin_port));
                        if ((sentLen = sendto(socketFd, pkt, sizeof(struct Packet), 0, (struct sockaddr *) &remoteAddress, remoteAddressSize)) > 0)
                        {
                            // set payload as file not found message
                            const char *errmsg = "File not found: closing connection with FIN";
                            setPayload(pkt, errmsg, strlen(errmsg));
                            // debug info
                            printf("Sent %d bytes\n", sentLen);
                            printPacket(pkt);

                            // no need to free memory for file since it was never allocated
                            //connection->closeConnection();
                            free(connection);
                            connection = NULL;
                            continue;
                        }
                        else
                        {
                            printf("Error sending msg: %s\n", strerror(errno));
                        }
                    }
                }
                // check to see if it is neither SYN or ACK, this should just be an ack for data
                else if (connection != NULL && isDataPacket(pkt))
                {
                    // determine if we should drop the packet based on the probabilities
                    if ((rand() % 100) < packetLossPercentage || (rand() % 100) < packetCorruptionPercentage)
                    {
                        printf("\n********PACKET LOST OR CORRUPTED!********\n\n");
                        continue;
                    }

                    // give the connection the latest cum ack
                    connection->receivedAck(getCumAck(pkt));

                    sendPacketsHelper(connection, pkt, socketFd, remoteAddress);
                }
            }
        }
    }

    return 0;
}