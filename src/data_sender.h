#ifndef DATA_SENDER_H
#define DATA_SENDER_H

enum SendResult { SEND_OK, SEND_NO_INTERNET, SEND_SERVER_DOWN };

SendResult dataSenderSend();

#endif
