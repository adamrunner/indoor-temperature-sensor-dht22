struct message_data
{
   char* hostname;
   char temperature[8];
   char battery[8];
   // char humidity[4];
};

typedef struct message_data MessageData;