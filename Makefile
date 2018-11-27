CFLAGS:=-O3 -Wall `pkg-config libdrm --cflags`
LDFLAGS:=`pkg-config libdrm libjpeg --libs`
BIN:=kmsfbshot
SRC:=kmsfbshot.c
OBJ:=kmsfbshot.o
CC:=gcc

all: ${OBJ}
	${CC} ${CFLAGS} ${OBJ} ${LDFLAGS} -o ${BIN}
clean:
	rm -rf $(OBJ) $(BIN)
.c.o: ${SRC}
	${CC} ${CFLAGS} -c $<
