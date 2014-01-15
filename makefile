CFLAGS += -O0 -g -Wall

all: expr

expr: main.o express.o
	$(CC) $(FLAGS) -o $@ $^ -lm

clean:
	rm -rf *.o expr
