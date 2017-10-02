CFLAGS=-g -Wall 
CC=gcc 
LIBS=-lm -lpthread 
INCLUDES= 
OBJS_FORMAT=h4h_format.o 
SRCS_FORMAT=h4h_format.c 

#The variable $@ has the value of the target. In this case $@ = psort 
h4h_format: ${OBJS_FORMAT} 
		${CC} ${CFLAGS} ${INCLUDES} -o $@ ${OBJS_FORMAT} ${LIBS} 

.c.o: 
		${CC} ${CFLAGS} ${INCLUDES} -c $< 

clean: 
		rm *.o h4h_format 
