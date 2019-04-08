#include <stdio.h>
#include <u7186EX\7186e.h>
#include <u7186EX\Tcpip32.h>

/* Address to accept any incoming messages */
#define	INADDR_ANY        0x00000000
/* Address to send to all hosts */
#define	INADDR_BROADCAST  0xffffffff
/* The port used to listen the broadcast messages from palert */
#define	LISTEN_PORT   54321
/* The port used to send the control messages */
#define	CONTROL_PORT  23
/* The size of the buffer used to recieved the data from broadcast */
#define BUFSIZE         128
#define MAC_STR_LENGTH  17  /* Include the null-terminator */
#define IPV4_STR_LENGTH 16  /* Include the null-terminator */

/* Main socket */
static volatile int SockRecv;
static volatile int SockSend;

/* Global address info, expecially for sending command */
static struct sockaddr_in Laddr;

/* Input buffer */
static char InBuffer[BUFSIZE];

/* Mark for seq. */
static const unsigned char Mark[3] = { 0x01, 0x48, 0x49 };

/* Internal functions' prototype */
static int NetworkInit( const int, const int );
static int SendCommand( const char * );
static int WaitNetworkConnect( void );
static char *ExtractResponse( char *, const int );

/* Main function, entry */
void main( void )
{
	int i, ret;
	int fromlen = sizeof(struct sockaddr);

	char  mac[6];
	char  ip[IPV4_STR_LENGTH];
	char *pos;

	struct sockaddr_in sfrom;

/* Initialization for u7186EX's general library */
	InitLib();

/* Wait for the network interface ready, it might be shoter */
	Delay(1500);
/* Initialization for network interface library */
	if ( NetworkInit( LISTEN_PORT, CONTROL_PORT ) < 0 ) return;

/* Initialization for 7-segment led, it might be unneccary */
	Init5DigitLed();

/* Show the "-H-" message on the 7-seg led */
	Show5DigitLedSeg(1, 0);
	Show5DigitLedSeg(2, 1);
	Show5DigitLedSeg(3, 0x37);
	Show5DigitLedSeg(4, 1);
	Show5DigitLedSeg(5, 0);

/* Wait until the network connection is on */
	WaitNetworkConnect();

/* Start to send the control command */
	while ( 1 ) {
	/* Show the "-S-" message on the 7-seg led */
		Show5DigitLed(3, 5);
		Delay(500);
	/* Send out the MAC address request command */
		if ( SendCommand( "mac" ) <= 0 ) {
			Nterm();
			return;
		}

	/* Flash the input buffer */
		memset(InBuffer, 0, BUFSIZE);
	/* Show the "-L-" message on the 7-seg led */
		Show5DigitLedSeg(3, 0x0E);
		Delay(500);
	/* Recieving the response from the palert */
		ret = recvfrom(SockRecv, InBuffer, BUFSIZE, 0, (struct sockaddr *)&sfrom, &fromlen);

	/* Once it recieved the response jump out this loop */
		if ( ret > 0 ) break;
	}

/* Show the "-S-" message on the 7-seg led */
	Show5DigitLed(3, 5);
	Delay(500);
/* Send out the program execute command */
	if ( SendCommand( "runexe" ) <= 0 ) {
		Nterm();
		return;
	}

/* Start to parse the response */
	InBuffer[ret] = '\0';
/* Extract the MAC address from the raw response */
	pos = ExtractResponse( InBuffer, MAC_STR_LENGTH );
	if ( pos == NULL ) {
		Nterm();
		return;
	}

/* Parsing the MAC address with hex format into six chars */
	sscanf(pos, "%x:%x:%x:%x:%x:%x",
		&mac[0], &mac[1], &mac[2],
		&mac[3], &mac[4], &mac[5]);

/* Close the sockets */
	closesocket(SockRecv);
	closesocket(SockSend);

	while ( 1 ) {
	/* Try to display the MAC address on the 7-seg led */
		pos = mac;
	/* Every 2 chars will be display on the same page */
		for ( i = 0; i < 3; i++ ) {
		/* Display the seq. mark */
			Show5DigitLedSeg(1, Mark[i]);
		/* Seperate one char into two digits */
			ret = (*pos >> 4) & 0xf;
			Show5DigitLed(2, ret);
			ret = *pos++ & 0xf;
			Show5DigitLedWithDot(3, ret);
		/* The next char */
			ret = (*pos >> 4) & 0xf;
			Show5DigitLed(4, ret);
			ret = *pos++ & 0xf;
			Show5DigitLedWithDot(5, ret);
		/* Display for 4 sec. */
			Delay(4000);
		}
	}

/* Terminate the network interface */
	Nterm();
	return;
}

/* External variables for broadcast setting */
extern int bAcceptBroadcast;

/*
*  NetworkInit() - The initialization process of network interface.
*  argument:
*    rport - The port number for recieving response.
*    sport - The port number for sending the command.
*  return:
*    0   - All of the socket we need are created.
*    < 0 - Something wrong when creating socket or setting up the operation mode.
*/
static int NetworkInit( const int rport, const int sport )
{
	int  ret = 0;
	char optval = 1;

/* Initialization for network interface library */
	ret = NetStart();
	if ( ret < 0 ) goto err_return;

/* Setup for accepting broadcast packet */
	bAcceptBroadcast = 1;

/* Create the recieving socket */
	SockRecv = socket(PF_INET, SOCK_DGRAM, 0);
	if ( SockRecv < 0 ) goto err_return;

/* Set the socket to reuse the address */
	ret = setsockopt(SockRecv, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	if ( ret < 0 ) goto err_return;

/* Bind the recieving socket to the broadcast port */
	memset(&Laddr, 0, sizeof(struct sockaddr));
	Laddr.sin_family = PF_INET;
	Laddr.sin_addr.s_addr = htonl(INADDR_ANY);
	Laddr.sin_port = htons(rport);
	ret = bind(SockRecv, (struct sockaddr *)&Laddr, sizeof(struct sockaddr));
	if ( ret < 0 ) goto err_return;

/* Set the timeout of recieving socket to 1 sec. */
	SOCKET_RXTOUT(SockRecv, 1000);

/* Create the sending socket */
	ret = SockSend = socket(PF_INET, SOCK_DGRAM, 0);
	if ( SockSend < 0 ) goto err_return;

/* Set the socket to be able to broadcast */
	ret = setsockopt(SockSend, SOL_SOCKET, SO_BROADCAST, &optval, sizeof(optval));
	if ( ret < 0 ) goto err_return;

/* Set the sending address info */
	memset(&Laddr, 0, sizeof(struct sockaddr));
	Laddr.sin_family = PF_INET;
	Laddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
	Laddr.sin_port = htons(sport);

	return 0;

/* Return for error */
err_return:
	Nterm();
	return ret;
}

/*
*  SendCommand() - Sending control command to the address(Laddr).
*  argument:
*    comm - The pointer to the string of command.
*  return:
*    <= 0 - It didn't send the command properly.
*    > 0  - It has been sent properly.
*/
static int SendCommand( const char *comm )
{
	int  commlen;
	char buffer[16];

	memset(buffer, 0, 16);
	sprintf(buffer, "%s\r", comm);
	commlen = strlen(buffer);

	return sendto(SockSend, buffer, commlen, 0, (struct sockaddr *)&Laddr, sizeof(Laddr));
}

/* External variables for network linking status */
extern volatile unsigned bEthernetLinkOk;

/*
*  WaitNetworkConnect() - Waiting for the network connection is ready.
*  argument:
*    None.
*  return:
*    0x40 - The connection is ready.
*/
static int WaitNetworkConnect( void )
{
	int ret = bEthernetLinkOk;

/* After testing, when network is real connected, this number should be 0x40(64). */
	while ( ret != 0x40 ) {
		ret = bEthernetLinkOk;
		Delay(500);
	}

	return ret;
}

/*
*  ExtractResponse() - Extract the real data from the whole response.
*  argument:
*    buffer - The pointer to the whole raw response.
*    length - The length of the real data in string.
*  return:
*    NULL  - It didn't find out the data.
*    !NULL - The pointer to the real data string.
*/
static char *ExtractResponse( char *buffer, const int length )
{
	char *pos = NULL;

	if ( buffer != NULL ) {
	/* Find out where is the '=', and skip all the following space or tab. */
		pos = strchr(buffer, '=') + 1;
		if ( pos != NULL ) {
			for( ; *pos == ' ' || *pos == '\t'; pos++ );
		/* Appending a null-terminator after the data. */
			*(pos + length) = '\0';
		}
	}

	return pos;
}
